//Renderer phase 3 - World Rendering Routines
#include "doomdef.h"
#include "r_local.h"

#include <dc/matrix.h>
#include <dc/pvr.h>
#include <math.h>

d64Poly_t next_poly;

int xform_verts = 0;
int subd_verts = 0;
int xform_polys = 0;
int context_change;
int in_things = 0;

extern int brightness;
extern short SwapShort(short dat);
extern int VideoFilter;

extern pvr_poly_cxt_t **txr_cxt_bump;
extern pvr_poly_cxt_t **txr_cxt_nobump;

extern pvr_poly_hdr_t pvr_sprite_hdr;
extern pvr_poly_hdr_t pvr_sprite_hdr_nofilter;

extern float *all_u;
extern float *all_v;
extern float *all_u2;
extern float *all_v2;

int has_bump = 0;
int in_floor = 0;

pvr_vertex_t __attribute__((aligned(32))) quad2[4];
pvr_vertex_t __attribute__((aligned(32))) bump_verts[4];

pvr_poly_hdr_t hdr;
pvr_poly_cxt_t cxt;

pvr_poly_hdr_t thdr;

d64Vertex_t *dVTX[4];
d64Triangle_t dT1, dT2;

// when dynamic lighting was introduced, skies with clouds were getting lit
//   by high-flying projectiles
// now this is just used to keep from lighting the transparent liquid floor
extern int dont_color;
// the current number of lights - 1
extern int lightidx;
// array of lights generated in r_phase1.c
extern projectile_light_t __attribute__((aligned(32))) projectile_lights[NUM_DYNLIGHT];

// packed bumpmap parameters
uint32_t boargb;
uint32_t defboargb;

// unit normal vector for currently rendering primitive
float normx, normy, normz;

// bump-mapping parameters and variables
pvr_poly_hdr_t bumphdr;

float center_x, center_y, center_z;

void light_wall_hasbump(d64Poly_t *p);
void light_wall_nobump(d64Poly_t *p);
void light_plane_hasbump(d64Poly_t *p);
void light_plane_nobump(d64Poly_t *p);
void light_thing(d64Poly_t *p);

void (*poly_light_func[5]) (d64Poly_t *p) = {
	light_plane_nobump,
	light_plane_hasbump,
	light_wall_nobump,
	light_wall_hasbump,
	light_thing
};

extern pvr_poly_cxt_t flush_cxt;
extern pvr_poly_hdr_t flush_hdr;

extern pvr_dr_state_t dr_state;

extern void draw_pvr_line_hdr(d64Vertex_t *v1, d64Vertex_t *v2, int color);

// convenience macro for copying pvr_vertex_t
#define vertcpy(d,s) memcpy((d),(s),sizeof(pvr_vertex_t))


/* 
credit to Kazade / glDC code for my near-z clipping implementation
https://github.com/Kazade/GLdc/blob/572fa01b03b070e8911db43ca1fb55e3a4f8bdd5/GL/platforms/software.c#L140
*/


// check for vertices behind near-z plane
//
// q bit is set when input poly is a quad instead of triangle
//
// all bits 1 mean all vertices are visible
//
//  q v3 v2 v1 v0
//  -------------
//  0  0  0  0  0  triangle none visible
//  0  0  0  0  1  triangle vert 0 visible
//  ...
//  0  0  1  1  1  all verts of triangle visible
//  1  0  0  0  0  quad none visible
//  1  0  1  0  0  quad vert 2 visible
//  ...
//  1  1  1  1  1  all verts of a quad visible
static inline int nearz_vismask(d64Poly_t *poly)
{
	int nvert = poly->n_verts;
	int rvm = (nvert == 4) ? 16 : 0;
	
	for(int i=0;i<nvert;i++) {
		d64ListVert_t *vi = &poly->dVerts[i];
		rvm |= ((vi->v->z >= -vi->w) << i);
	}
	
	return rvm;
}

// must have 
// `float t` and `float invt = 1.0f - t;`
// defined local to calling function
// this is just for cleaner code
#define lerp(a,b) (invt * (a) + t * (b))

// lerp two 32-bit colors
static uint32_t color_lerp(float t, uint32_t v1c, uint32_t v2c)
{
	const float invt = 1.0f - t;

	// ARGB8888
	uint8_t c0 = lerp(((v1c >> 24)&0xff), ((v2c >> 24)&0xff));
	uint8_t c1 = lerp(((v1c >> 16)&0xff), ((v2c >> 16)&0xff));
	uint8_t c2 = lerp(((v1c >>  8)&0xff), ((v2c >>  8)&0xff));
	uint8_t c3 = lerp(((v1c      )&0xff), ((v2c      )&0xff));

	return D64_PVR_PACK_COLOR(c0, c1, c2, c3);
}


// lerp two d64ListVert_t
// called if one of the input verts is determined to be behind the near-z plane
static void nearz_clip(const d64ListVert_t *restrict v1,
						const d64ListVert_t *restrict v2,
						d64ListVert_t *out)
{
	const float d0 = v1->w + v1->v->z;
	const float d1 = v2->w + v2->v->z;
	// fastest way to get abs(d0 / (d1 - d0))
	const float t = (fabs(d0) * frsqrt((d1 - d0) * (d1 - d0))) + 0.000001f;
	const float invt = 1.0f - t;

	out->w = lerp(v1->w, v2->w); 

	out->v->x = lerp(v1->v->x, v2->v->x);
	out->v->y = lerp(v1->v->y, v2->v->y);
	out->v->z = lerp(v1->v->z, v2->v->z);

	out->v->u = lerp(v1->v->u, v2->v->u);
	out->v->v = lerp(v1->v->v, v2->v->v);

	out->v->argb = color_lerp(t, v1->v->argb, v2->v->argb);
	out->v->oargb = color_lerp(t, v1->v->oargb, v2->v->oargb);
}


// do the (z -> y, -y -> z) transform on the light positions
static void R_TransformProjectileLights(void)
{
	for (int i = 0; i < lightidx + 1; i++) {
		float tmp = projectile_lights[i].z;
		projectile_lights[i].z = -projectile_lights[i].y;
		projectile_lights[i].y = tmp;
		// this field is used during the BSP traversal for prioritizing/replacing lights
		// once the traversal is done, repurpose it to hold the inverse of light radius
		// calculate once instead of per poly and/or per vertex
		projectile_lights[i].distance = frapprox_inverse(projectile_lights[i].radius);
	}
}


// initialize a d64Poly_t * for rendering the next polygon
// n_verts 3 for triangle (planes)
//         4 for quad     (walls, switches, things)
// diffuse_hdr is pointer to header to submit if context change required
static void init_poly(d64Poly_t *poly, pvr_poly_hdr_t *diffuse_hdr, int n_verts)
{
	void *tr_tail = (void *)pvr_vertbuf_tail(PVR_LIST_TR_POLY);

	poly->n_verts = n_verts;
	// header always points to next usable position in TR DMA list
	poly->hdr = (pvr_poly_hdr_t *)tr_tail;

	// when header must be re-submitted
	if (context_change) {
		// copy the contents of the header into poly struct
		memcpy(poly->hdr, diffuse_hdr, sizeof(pvr_poly_hdr_t));
		// advance the TR list position
		tr_tail += sizeof(pvr_poly_hdr_t);
	}

	// set up 5 d64ListVert_t entries
	// each entry maintains a pointer into the TR DMA list for a vertex
	// near-z clipping is done in-place in the TR DMA list
	// some quad clipping cases require an extra vert added to triangle strip
	// this necessitates having contiguous space for 5 pvr_vertex_t available
	for (int i=0;i<5;i++) {
		// each d64ListVert_t gets a pointer to the corresponding pvr_vertex_t 
		poly->dVerts[i].v = (pvr_vertex_t *)tr_tail;
		// each vert also maintains float rgb for dynamic lighting
		poly->dVerts[i].r = 0.0f;
		poly->dVerts[i].g = 0.0f;
		poly->dVerts[i].b = 0.0f;
		// and a flag that gets set if the vertex is ever lit during TNL loop
		poly->dVerts[i].lit = 0;
		// advance the TR list position for next vert
		tr_tail += sizeof(pvr_vertex_t);
	}
}


static int lf_idx(void)
{
	if (!in_things) {
		// 0 -> plane nobump
		// 1 -> plane hasbump
		// 2 -> wall nobump
		// 3 -> wall hasbump
		return ((!in_floor)<<1) + has_bump;
	} else {
		// 4 -> thing
		return 4;
	}
}

pvr_vertex_t tmpv[4];
float tmpw[4];

// this is the main event
// given an unclipped, world-space polygon
// representing a Doom wall, plane or thing
//
// light the polygon
// 	per polygon: calculate light direction vector for normal mapping
//  per vertex: calculate dynamic lighting
//  blend dynamic lighting with sector lit color
// 
// transform the polygon vertices from world space into view space
//
// clip vertices in-place in the TR list against near-z plane
//
// perspective-divide the resultant vertices after clipping
//  so they are screen space and ready to give to PVR/TA
//
// if surface has an available normal map, hybrid rendering applies.
//  submit OP header to TA through store queue (if needed)
//  copy each post-clip vertex from the TR list into store queue
//    overwrite the ARGB with 0xff000000
//    overwrite the OARGB with packed bumpmap parameters
//    submit vertex directly to OP list by flushing store queue to TA
//
// advance TR DMA list position by sizeof(pvr_poly_hdr_t) (if needed)
//  plus number of post-clip vertices * sizeof(pvr_vertex_t)
//
// return to rendering code for next polygon
static void __attribute__((noinline)) tnl_poly(d64Poly_t *p)
{
	unsigned i;
	int p_vismask;
	int verts_to_process = p->n_verts;

	// set vert flags to defaults for poly type
	p->dVerts[0].v->flags = p->dVerts[1].v->flags = PVR_CMD_VERTEX;
	p->dVerts[3].v->flags = p->dVerts[4].v->flags = PVR_CMD_VERTEX_EOL;

	if (verts_to_process == 4) {
		p->dVerts[2].v->flags = PVR_CMD_VERTEX;
	} else {
		p->dVerts[2].v->flags = PVR_CMD_VERTEX_EOL;
	}

	/*********
	light cond truth table
						dont color == 1
						---					---
						|T|					|F|
						---					---
					---
(lightidx+1) > 0	|T|	 F					 T                 
					|F|	 F					 F
					---
	*********/

	boargb = defboargb;

	// the condition for doing lighting/normal stuff is:
	//  if any dynamic lights exist
	//   AND
	//  we aren't drawing the transparent layer of a liquid floor
	if ((lightidx + 1) && (!dont_color)) {
		(*poly_light_func[lf_idx()])(p);
	}

	// apply viewport/modelview/projection transform matrix to each vertex
	// all matrices are multiplied together once per frame in r_main.c
	// transform is a single `mat_trans_single3_nodivw` per vertex
	xform_verts += verts_to_process;
	xform_polys += 1;
	for (i = 0; i < verts_to_process; i++) {
		transform_d64ListVert(&p->dVerts[i]);
	}

	// determine which vertices in the polygon are not visible, if any
	p_vismask = nearz_vismask(p);

	// this is the most common case, handled before the switch
	// p_vismask of 31 or 7: quad or tri all vertices visible
	// requires 
	if (__builtin_expect(p_vismask != 31 && p_vismask != 7, 0)) {
 		switch (p_vismask) {
		// tri nothing visible, immediately return
		case 0:
			return;
		
		// tri only 0 visible
		case 1:
			nearz_clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[1]);
			nearz_clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[2]);

			break;

		// tri only 1 visible
		case 2:
			nearz_clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[0]);
			nearz_clip(&p->dVerts[1], &p->dVerts[2], &p->dVerts[2]);

			break;

		// tri 0 + 1 visible
		case 3:
			verts_to_process = 4;

			nearz_clip(&p->dVerts[1], &p->dVerts[2], &p->dVerts[3]);
			nearz_clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[2]);

			p->dVerts[2].v->flags = PVR_CMD_VERTEX;

			break;

		// tri only 2 visible
		case 4:
			nearz_clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[0]);
			nearz_clip(&p->dVerts[1], &p->dVerts[2], &p->dVerts[1]);

			break;

		// tri 0 + 2 visible
		case 5:
			verts_to_process = 4;

			nearz_clip(&p->dVerts[1], &p->dVerts[2], &p->dVerts[3]);
			nearz_clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[1]);

			p->dVerts[2].v->flags = PVR_CMD_VERTEX;

			break;

		// tri 1 + 2 visible
		case 6:
			verts_to_process = 4;

			vertcpy(p->dVerts[3].v, p->dVerts[2].v); 
			p->dVerts[3].w = p->dVerts[2].w;

			nearz_clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[2]);
			nearz_clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[0]);

			p->dVerts[2].v->flags = PVR_CMD_VERTEX;
			break;

		// quad nothing visible, immediately return
		case 16:
			return;

		// quad only 0 visible
		case 17:
			verts_to_process = 3;

			nearz_clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[1]);
			nearz_clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[2]);

			p->dVerts[2].v->flags = PVR_CMD_VERTEX_EOL;

			break;

		// quad only 1 visible
		case 18:
			verts_to_process = 3;

			nearz_clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[0]);
			nearz_clip(&p->dVerts[1], &p->dVerts[3], &p->dVerts[2]);

			p->dVerts[2].v->flags = PVR_CMD_VERTEX_EOL;

			break;

		// quad 0 + 1 visible
		case 19:
			nearz_clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[2]);
			nearz_clip(&p->dVerts[1], &p->dVerts[3], &p->dVerts[3]);

			break;

		// quad only 2 visible
		case 20:
			verts_to_process = 3;

			nearz_clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[0]);
			nearz_clip(&p->dVerts[2], &p->dVerts[3], &p->dVerts[1]);

			p->dVerts[2].v->flags = PVR_CMD_VERTEX_EOL;

			break;

		// quad 0 + 2 visible
		case 21:
			nearz_clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[1]);
			nearz_clip(&p->dVerts[2], &p->dVerts[3], &p->dVerts[3]);

			break;

		// quad 1 + 2 visible is not possible
		// it is a middle diagonal
		// case 22:

		// quad 0 + 1 + 2 visible
		case 23:
			verts_to_process = 5;

			nearz_clip(&p->dVerts[2], &p->dVerts[3], &p->dVerts[4]);
			nearz_clip(&p->dVerts[1], &p->dVerts[3], &p->dVerts[3]);

			p->dVerts[3].v->flags = PVR_CMD_VERTEX;

			break;

		// quad only 3 visible
		case 24:
			verts_to_process = 3;

			nearz_clip(&p->dVerts[1], &p->dVerts[3], &p->dVerts[0]);
			nearz_clip(&p->dVerts[2], &p->dVerts[3], &p->dVerts[2]);

			vertcpy(p->dVerts[1].v, p->dVerts[3].v); 
			p->dVerts[1].w = p->dVerts[3].w;

			p->dVerts[1].v->flags = PVR_CMD_VERTEX;
			p->dVerts[2].v->flags = PVR_CMD_VERTEX_EOL;
			break;

		// quad 0 + 3 visible is not possible
		// it is the other middle diagonal
		// case 25:
		
		// quad 1 + 3 visible
		case 26:
			nearz_clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[0]);
			nearz_clip(&p->dVerts[2], &p->dVerts[3], &p->dVerts[2]);

			break;

		// quad 0 + 1 + 3 visible
		case 27:
			verts_to_process = 5;

			nearz_clip(&p->dVerts[2], &p->dVerts[3], &p->dVerts[4]);
			nearz_clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[2]);

			p->dVerts[3].v->flags = PVR_CMD_VERTEX;

			break;

		// quad 2 + 3 visible
		case 28:
			nearz_clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[0]);
			nearz_clip(&p->dVerts[1], &p->dVerts[3], &p->dVerts[1]);

			break;

		// quad 0 + 2 + 3 visible
		case 29:
			verts_to_process = 5;

			vertcpy(p->dVerts[4].v, p->dVerts[3].v); 
			p->dVerts[4].w = p->dVerts[3].w;

			nearz_clip(&p->dVerts[1], &p->dVerts[3], &p->dVerts[3]);
			nearz_clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[1]);

			p->dVerts[3].v->flags = PVR_CMD_VERTEX;
			p->dVerts[4].v->flags = PVR_CMD_VERTEX_EOL;

			break;

		// quad 1 + 2 + 3 visible
		case 30:
			verts_to_process = 5;

			vertcpy(p->dVerts[4].v, p->dVerts[2].v); 		
			p->dVerts[4].w = p->dVerts[2].w;

			nearz_clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[2]);
			nearz_clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[0]);		

			p->dVerts[3].v->flags = PVR_CMD_VERTEX;
			p->dVerts[4].v->flags = PVR_CMD_VERTEX_EOL;
			break;
		
		default:
			I_Error("tnl_poly invalid vismask %d", p_vismask);
			break;
		}
	}

	for (i = 0; i < verts_to_process; i++) {
		float invw = frapprox_inverse(p->dVerts[i].w);
		p->dVerts[i].v->x *= invw;
		p->dVerts[i].v->y *= invw;
		p->dVerts[i].v->z = invw;
	}

// set this to 1 if you want wireframes
#if 0
	for (int i=0;i<v2pd-1;i++) {
		d64Vertex_t v1,v2;
		v1.v.x = p->dVerts[i].v->x;
		v1.v.y = p->dVerts[i].v->y;
		v1.v.z = 5;
		
		v2.v.x = p->dVerts[i+1].v->x;
		v2.v.y = p->dVerts[i+1].v->y;
		v2.v.z = 5;
	
		draw_pvr_line_hdr(&v1, &v2, p->dVerts[i].v->argb);
	}
	d64Vertex_t v1,v2;
	v1.v.x = p->dVerts[v2pd-1].v->x;
	v1.v.y = p->dVerts[v2pd-1].v->y;
	v1.v.z = 5;
	
	v2.v.x = p->dVerts[1].v->x;
	v2.v.y = p->dVerts[1].v->y;
	v2.v.z = 5;
	
	draw_pvr_line_hdr(&v1, &v2, p->dVerts[v2pd-1].v->argb);
	v1.v.x = p->dVerts[0].v->x;
	v1.v.y = p->dVerts[0].v->y;
	v1.v.z = 5;
	
	v2.v.x = p->dVerts[2].v->x;
	v2.v.y = p->dVerts[2].v->y;
	v2.v.z = 5;
	draw_pvr_line_hdr(&v1, &v2, p->dVerts[0].v->argb);
#endif

	uint32_t hdr_size = (context_change * sizeof(pvr_poly_hdr_t)); 
	uint32_t amount = hdr_size + (verts_to_process * sizeof(pvr_vertex_t));

	if (__builtin_expect(has_bump, 1)) {
		if (context_change) {
			/*void *hdr1 = pvr_dr_target(dr_state);
			memcpy(hdr1, &bumphdr, sizeof(pvr_poly_hdr_t));
			pvr_dr_commit(hdr1);*/

			// equivalent to above Direct Rendering API use
			sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), &bumphdr, context_change);
		}

		pvr_vertex_t *tr_vert = (pvr_vertex_t *)((uintptr_t)p->hdr + hdr_size);
		subd_verts += verts_to_process;
		for (int i=0;i<verts_to_process;i++) {
			pvr_vertex_t *vert = pvr_dr_target(dr_state);
			//vertcpy(vert, &tr_vert[i]);
			//memcpy(vert, &tr_vert[i], 24);
			*vert = tr_vert[i];
			vert->argb = 0xff000000;
			vert->oargb = boargb;
			pvr_dr_commit(vert);
		}
	}

	// update diffuse/TR list pointer
	subd_verts += verts_to_process;
	pvr_vertbuf_written(PVR_LIST_TR_POLY, amount);

	context_change = 0;
}

// unclipped triangles
// this is used to draw laser and nothing else
static inline void submit_triangle(pvr_vertex_t *v0, pvr_vertex_t *v1, 
	pvr_vertex_t *v2, pvr_poly_hdr_t *hdr, pvr_list_t list)
{
	v0->flags = PVR_CMD_VERTEX;
	v1->flags = PVR_CMD_VERTEX;
	v2->flags = PVR_CMD_VERTEX_EOL;
#if 0
	if (context_change) {
		pvr_vertex_t *hdr1 = pvr_dr_target(dr_state);
		memcpy(hdr1, hdr, sizeof(pvr_poly_hdr_t));
		pvr_dr_commit(hdr1);
	}
	pvr_vertex_t *vert = pvr_dr_target(dr_state);
	memcpy(vert, v0, sizeof(pvr_vertex_t));
	pvr_dr_commit(vert);
	vert = pvr_dr_target(dr_state);
	memcpy(vert, v1, sizeof(pvr_vertex_t));
	pvr_dr_commit(vert);
	vert = pvr_dr_target(dr_state);
	memcpy(vert, v2, sizeof(pvr_vertex_t));
	pvr_dr_commit(vert);
#endif
	// equivalent to above Direct Rendering API usage
	if (context_change) {
			sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), hdr, 1);	
	}
	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), v0, 1);
	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), v1, 1);
	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), v2, 1);

	subd_verts += 3;

	context_change = 0;
}

void R_RenderWorld(subsector_t *sub);

void R_WallPrep(seg_t *seg);
void R_RenderWall(seg_t *seg, int flags, int texture, int topHeight,
		  int bottomHeight, int topOffset, int bottomOffset,
		  int topColor, int bottomColor);
void R_RenderSwitch(seg_t *seg, int texture, int topOffset, int color);

void R_RenderPlane(leaf_t *leaf, int numverts, int zpos, int texture,
					int xpos, int ypos, int color, int ceiling, 
					int lightlevel, int alpha);

void R_RenderThings(subsector_t *sub);
void R_RenderLaser(mobj_t *thing);
void R_RenderPSprites(void);

static uint32_t R_SectorLightColor(uint32_t c, int ll)
{
	uint8_t r = (uint8_t)((((c >> 16) & 0xff) * ll) >> 8);
	uint8_t g = (uint8_t)((((c >>  8) & 0xff) * ll) >> 8);
	uint8_t b = (uint8_t)(( (c        & 0xff) * ll) >> 8);
	uint8_t a = (uint8_t)(  (c >> 24) & 0xff);

	uint32_t rc = D64_PVR_PACK_COLOR(a, r, g, b);

	return rc;
}

void R_RenderAll(void)
{
	subsector_t *sub;
xform_polys = 0;
xform_verts = 0;
subd_verts = 0;
	R_TransformProjectileLights();

	while (endsubsector--, (endsubsector >= solidsubsectors)) {
		sub = *endsubsector;
		frontsector = sub->sector;
		R_RenderWorld(sub);
		sub->drawindex = 0x7fff;
	}
}

subsector_t *global_sub;

void R_RenderWorld(subsector_t *sub)
{
	fixed_t checkdist;
	leaf_t *lf;
	seg_t *seg;
	vertex_t *vrt; 

	fixed_t xoffset;
	fixed_t yoffset;
	int numverts;
	int i;

	global_sub = sub;

	numverts = sub->numverts;

	lf = &leafs[sub->leaf];
	vrt = lf[0].vertex;

	if (gamemap == 9 || 
		gamemap == 17 || 
		gamemap == 19 ||
		gamemap == 24 || 
		gamemap == 26 || 
		gamemap == 27 || 
		gamemap == 28 || 
		gamemap == 33) {
		goto skip_distcheck;
	}

	if (gamemap == 21) {
		checkdist = 896 << 16;
	} else if (gamemap == 23) {
		checkdist = (2048-256) << 16;
	}
	else if (gamemap == 12 || gamemap == 16) {
		checkdist = (1024+256) << 16;
	} else {
		checkdist = (1024+128) << 16;
	}

	fixed_t dx = D_abs(vrt->x - viewx);
	fixed_t dy = D_abs(vrt->y - viewy);	
	if (!quickDistCheck(dx,dy,checkdist)) {
		return;
	}
	
skip_distcheck:

	// render walls
	lf = &leafs[sub->leaf];
	for (i = 0; i < numverts; i++) {
		seg = lf->seg;
		if (seg && (seg->flags & 1)) {
			R_WallPrep(seg);
		}
		lf++;
	}

	// render ceilings
	if ((frontsector->ceilingpic != -1) &&
	    (viewz < frontsector->ceilingheight)) {
		if (frontsector->flags & MS_SCROLLCEILING) {
			xoffset = frontsector->xoffset;
			yoffset = frontsector->yoffset;
		} else {
			xoffset = 0;
			yoffset = 0;
		}
		lf = &leafs[sub->leaf];
		R_RenderPlane(lf, numverts,
			      frontsector->ceilingheight >> FRACBITS,
			      textures[frontsector->ceilingpic], xoffset,
			      yoffset, lights[frontsector->colors[0]].rgba, 1,
			      frontsector->lightlevel, 255);
	}

	// Render Floors
	if ((frontsector->floorpic != -1) &&
	    (frontsector->floorheight < viewz)) {
		if (!(frontsector->flags & MS_LIQUIDFLOOR)) {
			if (frontsector->flags & MS_SCROLLFLOOR) {
				xoffset = frontsector->xoffset;
				yoffset = frontsector->yoffset;
			} else {
				xoffset = 0;
				yoffset = 0;
			}
			lf = &leafs[sub->leaf];
			R_RenderPlane(lf, numverts,
				      frontsector->floorheight >> FRACBITS,
				      textures[frontsector->floorpic], xoffset,
				      yoffset,
				      lights[frontsector->colors[1]].rgba, 0,
				      frontsector->lightlevel, 255);
		} else { // liquid floors
			if (frontsector->flags & MS_SCROLLFLOOR) {
				xoffset = frontsector->xoffset;
				yoffset = frontsector->yoffset;
			} else {
				xoffset = scrollfrac;
				yoffset = 0;
			}
			lf = &leafs[sub->leaf];
			R_RenderPlane(lf, numverts,
				      frontsector->floorheight >> FRACBITS,
				      textures[frontsector->floorpic + 1],
				      xoffset, yoffset,
				      lights[frontsector->colors[1]].rgba, 0,
				      frontsector->lightlevel, 255);
			// don't light the transparent part of the floor
			dont_color = 1;
			lf = &leafs[sub->leaf];
			R_RenderPlane(
				lf, numverts,
				(frontsector->floorheight >> FRACBITS) + 1,
				textures[frontsector->floorpic], -yoffset,
				xoffset, lights[frontsector->colors[1]].rgba, 0,
				frontsector->lightlevel, 160);
			dont_color = 0;
		}
	}
	// render things
	R_RenderThings(sub);
}

void R_WallPrep(seg_t *seg)
{
	sector_t *backsector;
	line_t *li;
	side_t *side;
	fixed_t f_ceilingheight;
	fixed_t f_floorheight;
	fixed_t b_ceilingheight;
	fixed_t b_floorheight;
	fixed_t m_top;
	fixed_t m_bottom;
	fixed_t rowoffs;
	fixed_t height;
	int frontheight;
	int sideheight;
	short pic;

	unsigned int r1, g1, b1;
	unsigned int r2, g2, b2;
	float rn, gn, bn;
	float scale;
	unsigned int thingcolor;
	unsigned int upcolor;
	unsigned int lowcolor;
	unsigned int topcolor;
	unsigned int bottomcolor;
	unsigned int tmp_upcolor;
	unsigned int tmp_lowcolor;
	int curRowoffset;

	r1 = g1 = b1 = 0;
	r2 = g2 = b2 = 0;

	topcolor = tmp_upcolor = bottomcolor = tmp_lowcolor = 0;

	li = seg->linedef;
	side = seg->sidedef;

	// [GEC] Prevents errors in textures in T coordinates, but is not applied to switches
	curRowoffset = side->rowoffset & (127 << FRACBITS);

	thingcolor = lights[frontsector->colors[2]].rgba;
	upcolor = lights[frontsector->colors[3]].rgba;
	lowcolor = lights[frontsector->colors[4]].rgba;

	// get front side top and bottom
	f_ceilingheight = frontsector->ceilingheight >> 16;
	f_floorheight = frontsector->floorheight >> 16;
	frontheight = f_ceilingheight - f_floorheight;

	if (li->flags & ML_BLENDING) {
		r1 = ((upcolor >> 24) & 0xff);
		g1 = ((upcolor >> 16) & 0xff);
		b1 = ((upcolor >> 8) & 0xff);
		r2 = ((lowcolor >> 24) & 0xff);
		g2 = ((lowcolor >> 16) & 0xff);
		b2 = ((lowcolor >> 8) & 0xff);
		tmp_upcolor = upcolor;
		tmp_lowcolor = lowcolor;
	} else {
		topcolor = thingcolor;
		bottomcolor = thingcolor;
	}

	m_bottom = f_floorheight; // set middle bottom
	m_top = f_ceilingheight; // set middle top

	backsector = seg->backsector;
	if (backsector) {
		b_floorheight = backsector->floorheight >> 16;
		b_ceilingheight = backsector->ceilingheight >> 16;

		if ((b_ceilingheight < f_ceilingheight) &&
		    (backsector->ceilingpic != -1)) {
			height = f_ceilingheight - b_ceilingheight;

			if (li->flags & ML_DONTPEGTOP) {
				rowoffs = (curRowoffset >> 16) + height;
			} else {
				rowoffs = ((height + 127) & ~127) +
					  (curRowoffset >> 16);
			}

			if (li->flags & ML_BLENDING) {
				if (frontheight &&
				    !(li->flags & ML_BLENDFULLTOP)) {
					sideheight = b_ceilingheight -
						     f_ceilingheight;

					scale = (float)sideheight /
						((float)frontheight);

					rn = ((float)r1 - (float)r2) * scale +
					     (float)r1;
					gn = ((float)g1 - (float)g2) * scale +
					     (float)g1;
					bn = ((float)b1 - (float)b2) * scale +
					     (float)b1;

					if (!((rn < 256) && (gn < 256) &&
					      (bn <
					       256))) { // Rescale if out of color bounds
						scale = 255.0f;

						if (rn >= gn && rn >= bn) {
							scale /= (rn);
						} else if (gn >= rn &&
							   gn >= bn) {
							scale /= (gn);
						} else {
							scale /= (bn);
						}

						rn *= scale;
						gn *= scale;
						bn *= scale;
					}

					tmp_lowcolor = ((int)rn << 24) |
						       ((int)gn << 16) |
						       ((int)bn << 8) | 0xff;

					if (gamemap == 3 && (brightness > 57) &&
					    (brightness < 90)) {
						int x1 = li->v1->x >> 16;
						int y1 = li->v1->y >> 16;
						int x2 = li->v2->x >> 16;
						int y2 = li->v2->y >> 16;

						if (((x1 == 1040 &&
						      y1 == -176) &&
						     (x2 == 1008 &&
						      y2 == -176)) ||
						    ((x1 == 1008 &&
						      y1 == -464) &&
						     (x2 == 1040 &&
						      y2 == -464))) {
							float scale =
								1.0f -
								((float)((/*brightness*/
									  75 -
									  60) *
									 3.0f) *
								 0.0025f);

							tmp_upcolor =
								((int)(r1 *
								       scale)
								 << 24) |
								((int)(g1 *
								       scale)
								 << 16) |
								((int)(b1 *
								       scale)
								 << 8) |
								0xff;

							tmp_lowcolor =
								((int)(rn *
								       scale)
								 << 24) |
								((int)(gn *
								       scale)
								 << 16) |
								((int)(bn *
								       scale)
								 << 8) |
								0xff;
						}
					}
				}

				if (li->flags & ML_INVERSEBLEND) {
					bottomcolor = tmp_upcolor;
					topcolor = tmp_lowcolor;
				} else {
					topcolor = tmp_upcolor;
					bottomcolor = tmp_lowcolor;
				}

				// clip middle color upper
				upcolor = tmp_lowcolor;
			}


			R_RenderWall(seg, li->flags, textures[side->toptexture],
				     f_ceilingheight, b_ceilingheight,
				     rowoffs - height, rowoffs, topcolor,
				     bottomcolor);

			m_top = b_ceilingheight; // clip middle top height

			if ((li->flags & (ML_CHECKFLOORHEIGHT |
					  ML_SWITCHX08)) == ML_SWITCHX08) {
				if (SWITCHMASK(li->flags) == ML_SWITCHX04) {
					pic = side->bottomtexture;
					rowoffs = side->rowoffset >> 16;
				} else {
					pic = side->midtexture;
					rowoffs = side->rowoffset >> 16;
				}
				R_RenderSwitch(seg, pic,
					       b_ceilingheight + rowoffs + 48,
					       thingcolor);
			}
		}

		if (f_floorheight < b_floorheight) {
			height = f_ceilingheight - b_floorheight;

			if ((li->flags & ML_DONTPEGBOTTOM) == 0) {
				rowoffs = curRowoffset >> 16;
			} else {
				rowoffs = height + (curRowoffset >> 16);
			}

			if (li->flags & ML_BLENDING) {
				if (frontheight &&
				    !(li->flags & ML_BLENDFULLBOTTOM)) {
					sideheight =
						b_floorheight - f_ceilingheight;

					scale = (float)sideheight /
						((float)frontheight);

					rn = ((float)r1 - (float)r2) * scale +
					     (float)r1;
					gn = ((float)g1 - (float)g2) * scale +
					     (float)g1;
					bn = ((float)b1 - (float)b2) * scale +
					     (float)b1;

					if (!((rn < 256) && (gn < 256) &&
					      (bn <
					       256))) { // Rescale if out of color bounds
						scale = 255.0f;

						if (rn >= gn && rn >= bn) {
							scale /= rn;
						} else if (gn >= rn &&
							   gn >= bn) {
							scale /= gn;
						} else {
							scale /= bn;
						}

						rn *= scale;
						gn *= scale;
						bn *= scale;
					}

					tmp_upcolor = ((int)rn << 24) |
						      ((int)gn << 16) |
						      ((int)bn << 8) | 0xff;
					if (gamemap == 3 && (brightness > 57) &&
					    (brightness < 90)) {
						int x1 = li->v1->x >> 16;
						int y1 = li->v1->y >> 16;
						int x2 = li->v2->x >> 16;
						int y2 = li->v2->y >> 16;

						if (((x1 == 1040 &&
						      y1 == -176) &&
						     (x2 == 1008 &&
						      y2 == -176)) ||
						    ((x1 == 1008 &&
						      y1 == -464) &&
						     (x2 == 1040 &&
						      y2 == -464))) {
							float scale =
								1.0f -
								((float)((/*brightness*/
									  75 -
									  60) *
									 3.0f) *
								 0.0025f);

							tmp_lowcolor =
								((int)(r2 *
								       scale)
								 << 24) |
								((int)(g2 *
								       scale)
								 << 16) |
								((int)(b2 *
								       scale)
								 << 8) |
								0xff;

							tmp_upcolor =
								((int)(rn *
								       scale)
								 << 24) |
								((int)(gn *
								       scale)
								 << 16) |
								((int)(bn *
								       scale)
								 << 8) |
								0xff;
						}
					}
				}

				topcolor = tmp_upcolor;
				bottomcolor = lowcolor;

				// clip middle color lower
				lowcolor = tmp_upcolor;
			}

			R_RenderWall(seg, li->flags,
				     textures[side->bottomtexture],
				     b_floorheight, f_floorheight, rowoffs,
				     rowoffs + (b_floorheight - f_floorheight),
				     topcolor, bottomcolor);

			m_bottom = b_floorheight; // clip middle bottom height
			if ((li->flags &
			     (ML_CHECKFLOORHEIGHT | ML_SWITCHX08)) ==
			    ML_CHECKFLOORHEIGHT) {
				if (SWITCHMASK(li->flags) == ML_SWITCHX02) {
					pic = side->toptexture;
					rowoffs = side->rowoffset >> 16;
				} else {
					pic = side->midtexture;
					rowoffs = side->rowoffset >> 16;
				}
				R_RenderSwitch(seg, pic,
					       b_floorheight + rowoffs - 16,
					       thingcolor);
			}
		}

		if (!(li->flags & ML_DRAWMASKED)) {
			return;
		}
	}

	height = m_top - m_bottom;

	if (li->flags & ML_DONTPEGBOTTOM) {
		rowoffs = ((height + 127) & ~127) + (curRowoffset >> 16);
	} else if (li->flags & ML_DONTPEGTOP) {
		rowoffs = (curRowoffset >> 16) - m_bottom;
	} else {
		rowoffs = (curRowoffset >> 16) + height;
	}

	if (li->flags & ML_BLENDING) {
		topcolor = upcolor;
		bottomcolor = lowcolor;
	}

	R_RenderWall(seg, li->flags, textures[side->midtexture], m_top,
		     m_bottom, rowoffs - height, rowoffs, topcolor,
		     bottomcolor);

	if ((li->flags & (ML_CHECKFLOORHEIGHT | ML_SWITCHX08)) ==
	    (ML_CHECKFLOORHEIGHT | ML_SWITCHX08)) {
		if (SWITCHMASK(li->flags) == ML_SWITCHX02) {
			pic = side->toptexture;
			rowoffs = side->rowoffset >> 16;
		} else {
			pic = side->bottomtexture;
			rowoffs = side->rowoffset >> 16;
		}
		R_RenderSwitch(seg, pic, m_bottom + rowoffs + 48, thingcolor);
	}
}

float last_width_inv = 1.0f / 64.0f;
float last_height_inv = 1.0f / 64.0f;

void *P_CachePvrTexture(int i, int tag);

extern pvr_ptr_t *bump_txr_ptr;
extern pvr_poly_cxt_t *bump_cxt;

void R_RenderWall(seg_t *seg, int flags, int texture, int topHeight,
		  int bottomHeight, int topOffset, int bottomOffset,
		  int topColor, int bottomColor)
{
	d64ListVert_t *dV[4];
	byte *data;
	vertex_t *v1;
	vertex_t *v2;
	int cms, cmt;
	int wshift, hshift;
	int texnum = (texture >> 4) - firsttex;
	// [GEC] Prevents errors in textures in S coordinates
	int curTextureoffset = (seg->sidedef->textureoffset + seg->offset) &
			       (127 << FRACBITS);

	int ll = frontsector->lightlevel;

	uint32_t tdc_col = D64_PVR_REPACK_COLOR(topColor);
	uint32_t bdc_col = D64_PVR_REPACK_COLOR(bottomColor);

	uint32_t tl_col = R_SectorLightColor(tdc_col, ll);
	uint32_t bl_col = R_SectorLightColor(bdc_col, ll);

	in_floor = 0;
	in_things = 0;
	has_bump = 0;
	dont_color = 0;

	if (bump_txr_ptr[texnum]) {
		has_bump = 1;
	}

	if (texture != 16) {
		if (flags & ML_HMIRROR) {
			cms = 2;
		} else {
			cms = 0;
		}

		if (flags & ML_VMIRROR) {
			cmt = 1;
		} else {
			cmt = 0;
		}

		if ((texture != globallump) || (globalcm != (cms | cmt))) {
			pvr_poly_cxt_t *curcxt;

			context_change = 1;

			data = P_CachePvrTexture(texnum, PU_CACHE);

			wshift = SwapShort(((textureN64_t *)data)->wshift);
			hshift = SwapShort(((textureN64_t *)data)->hshift);
			last_width_inv = 1.0f / (float)(1 << wshift);
			last_height_inv = 1.0f / (float)(1 << hshift);

			if (has_bump) {
				curcxt = &txr_cxt_bump[texnum][texture & 15];
			} else {
				curcxt = &txr_cxt_nobump[texnum][texture & 15];
			}

			// cms is S/H mirror
			// cmt is T/V mirror
			if (cms && !cmt) {
				curcxt->txr.uv_flip = PVR_UVFLIP_U;
			} else if (!cms && cmt) {
				curcxt->txr.uv_flip = PVR_UVFLIP_V;
			} else if (cms && cmt) {
				curcxt->txr.uv_flip = PVR_UVFLIP_UV;
			} else {
				curcxt->txr.uv_flip = PVR_UVFLIP_NONE;
			}

			if (!VideoFilter) {
				curcxt->txr.filter = PVR_FILTER_BILINEAR;
			} else {
				curcxt->txr.filter = PVR_FILTER_NONE;
			}

			if (has_bump) {
				bump_cxt[texnum].txr.uv_flip =
					txr_cxt_bump[texnum][texture & 15].txr.uv_flip;
				bump_cxt[texnum].txr.filter =
					txr_cxt_bump[texnum][texture & 15].txr.filter;
				pvr_poly_compile(&bumphdr, &bump_cxt[texnum]);
			}

			globallump = texture;
			globalcm = (cms | cmt);

			pvr_poly_compile(&thdr, curcxt);
		}

		// if texture v is flipped, rotate the default "light"
		// direction by 180 degrees
		if (has_bump) {
			if (!globalcm) {
				defboargb = 0x7f5a00c0;
			} else if (globalcm & 1) {
				defboargb = 0x7f5a0040;
			}			
		}

		v1 = seg->v1;
		v2 = seg->v2;

		normx = seg->nx;
		normy = 0;
		normz = seg->nz;

		float x1 = (float)v1->x / 65536.0f;
		float z1 = -((float)v1->y / 65536.0f);
		float x2 = (float)v2->x / 65536.0f;
		float z2 = -((float)v2->y / 65536.0f);
		float y1 = (float)topHeight;
		float y2 = (float)bottomHeight;

		short stu1 = curTextureoffset >> 16;
		short stu2 = stu1 + (seg->length >> 4);
		short stv1 = topOffset;
		short stv2 = bottomOffset;

		float tu1 = (float)stu1 * last_width_inv;
		float tu2 = (float)stu2 * last_width_inv;
		float tv1 = (float)stv1 * last_height_inv;
		float tv2 = (float)stv2 * last_height_inv;

		float yd = fabs(y2 - y1);
		float xd = fabs(x2 - x1);
		float zd = fabs(z2 - z1);
		
		dV[0] = &next_poly.dVerts[0];
		dV[1] = &next_poly.dVerts[1];
		dV[2] = &next_poly.dVerts[2];
		dV[3] = &next_poly.dVerts[3];

		fixed_t dx = D_abs(v1->x - viewx);
		fixed_t dy = D_abs(v1->y - viewy);	

		if (!quickDistCheck(dx,dy,512<<16)) {
			goto regular_wall;
		}

		if (gamemap == 28 || gamemap == 33) {
			goto regular_wall;
		}

		// very tall walls are hard to light properly
		// sub-divide into vertically stacked segments
		// very wide walls are also hard to light properly
		// sub-divide into horizontal segments
		// tall AND wide is even worse so do both

		if ((yd > 96.0f) && ((xd > 96.0f) || (zd > 96.0f)) && (lightidx + 1)) {
			int ysteps = 2;
			int xsteps = 2;

			if (yd > 256) {
				ysteps = 4;
			} else if (yd > 128) {
				ysteps = 3;
			}

			if ((xd > 256 || zd > 256)) {
				xsteps = 4;
			} else if (xd > 128 || zd > 128) {
				xsteps = 3;
			}

			float xstepsize = 1.0f / (float)xsteps;
			float xs = (( x2 -  x1) * xstepsize);
			float zs = (( z2 -  z1) * xstepsize);
			float us = ((tu2 - tu1) * xstepsize);

			float ystepsize = 1.0f / (float)ysteps;
			float ys = (( y2 -  y1) * ystepsize);
			float vs = ((tv2 - tv1) * ystepsize);

			for (int i = 0; i < ysteps; i++) {
				for (int j = 0; j < xsteps; j++) {
					float tx1 = x1 + (xs * j);
					float tx2 = tx1 + xs;

					float tz1 = z1 + (zs * j);
					float tz2 = tz1 + zs;

					float ttu1 = tu1 + (us*j);
					float ttu2 = ttu1 + us;

					float ty1 = y1 + (ys * i);
					float ty2 = ty1 + ys;

					float ttv1 = tv1 + (vs*i);
					float ttv2 = ttv1 + vs;

					init_poly(&next_poly, &thdr, 4);

					uint32_t ucol = color_lerp(((i  )*ystepsize), 
												tdc_col, bdc_col);
					uint32_t lcol = color_lerp(((i+1)*ystepsize),
												tdc_col, bdc_col);

					uint32_t ulcol = color_lerp(((i  )*ystepsize),
												tl_col, bl_col);
					uint32_t llcol = color_lerp(((i+1)*ystepsize),
												tl_col, bl_col);

					dV[0]->v->argb = lcol;
					dV[0]->v->oargb = llcol;
					dV[1]->v->argb = ucol;
					dV[1]->v->oargb = ulcol;
					dV[2]->v->argb = lcol;
					dV[2]->v->oargb = llcol;
					dV[3]->v->argb = ucol;
					dV[3]->v->oargb = ulcol;

					dV[0]->v->x = dV[1]->v->x = tx1;
					dV[0]->v->z = dV[1]->v->z = tz1;
					dV[0]->v->u = dV[1]->v->u = ttu1;
					dV[1]->v->y = dV[3]->v->y = ty1;
					dV[1]->v->v = dV[3]->v->v = ttv1;

					dV[2]->v->x = dV[3]->v->x = tx2;
					dV[2]->v->z = dV[3]->v->z = tz2;
					dV[2]->v->u = dV[3]->v->u = ttu2;
					dV[0]->v->y = dV[2]->v->y = ty2;
					dV[0]->v->v = dV[2]->v->v = ttv2;

					tnl_poly(&next_poly);
				}			
			}
		} else if ((yd > 96.0f) && (lightidx > -1)) {
			int steps = 2;
			if (yd > 256) {
				steps = 4;
			} else if (yd > 128) {
				steps = 3;
			}
			float stepsize = 1.0f / (float)steps;
			float ys = (( y2 -  y1) * stepsize);
			float vs = ((tv2 - tv1) * stepsize);

			for (int i = 0;i < steps; i++) {
				float tx1 = x1;
				float tx2 = x2;

				float tz1 = z1;
				float tz2 = z2;

				float ty1 = y1 + (ys * i);
				float ty2 = ty1 + ys;

				float ttv1 = tv1 + (vs*i);
				float ttv2 = ttv1 + vs;

				init_poly(&next_poly, &thdr, 4);
				
				uint32_t ucol = color_lerp(((i  )*stepsize),
											tdc_col, bdc_col);
				uint32_t lcol = color_lerp(((i+1)*stepsize),
											tdc_col, bdc_col);

				uint32_t ulcol = color_lerp(((i  )*stepsize),
											tl_col, bl_col);
				uint32_t llcol = color_lerp(((i+1)*stepsize),	
											tl_col, bl_col);

				dV[0]->v->argb = lcol;
				dV[0]->v->oargb = llcol;
				dV[1]->v->argb = ucol;
				dV[1]->v->oargb = ulcol;
				dV[2]->v->argb = lcol;
				dV[2]->v->oargb = llcol;
				dV[3]->v->argb = ucol;
				dV[3]->v->oargb = ulcol;

				dV[0]->v->x = dV[1]->v->x = tx1;
				dV[0]->v->z = dV[1]->v->z = tz1;
				dV[0]->v->u = dV[1]->v->u = tu1;
				dV[1]->v->y = dV[3]->v->y = ty1;
				dV[1]->v->v = dV[3]->v->v = ttv1;

				dV[2]->v->x = dV[3]->v->x = tx2;
				dV[2]->v->z = dV[3]->v->z = tz2;
				dV[2]->v->u = dV[3]->v->u = tu2;
				dV[0]->v->y = dV[2]->v->y = ty2;
				dV[0]->v->v = dV[2]->v->v = ttv2;

				tnl_poly(&next_poly);
			}
		} else if (((xd > 96.0f) || (zd > 96.0f)) && (lightidx > -1)) {
			int steps = 2;
			if ((xd > 256 || yd > 256)) {
				steps = 4;
			} else if (xd > 128 || yd > 128) {
				steps = 3;
			}

			float stepsize = 1.0f / (float)steps;
			float xs = (( x2 -  x1) * stepsize);
			float zs = (( z2 -  z1) * stepsize);
			float us = ((tu2 - tu1) * stepsize);

			for (int i = 0; i < steps; i++) {
				float ty1 = y1;
				float ty2 = y2;

				float tx1 = x1 + (xs * i);
				float tx2 = tx1 + xs;
				float tz1 = z1 + (zs * i);
				float tz2 = tz1 + zs;

				float ttu1 = tu1 + (us*i);
				float ttu2 = ttu1 + us;

				init_poly(&next_poly, &thdr, 4);

				dV[0]->v->argb = bdc_col;
				dV[0]->v->oargb = bl_col;
				dV[1]->v->argb = tdc_col;
				dV[1]->v->oargb = tl_col;
				dV[2]->v->argb = bdc_col;
				dV[2]->v->oargb = bl_col;
				dV[3]->v->argb = tdc_col;
				dV[3]->v->oargb = tl_col;

				dV[0]->v->x = dV[1]->v->x = tx1;
				dV[0]->v->z = dV[1]->v->z = tz1;
				dV[0]->v->u = dV[1]->v->u = ttu1;
				dV[1]->v->y = dV[3]->v->y = ty1;
				dV[1]->v->v = dV[3]->v->v = tv1;

				dV[2]->v->x = dV[3]->v->x = tx2;
				dV[2]->v->z = dV[3]->v->z = tz2;
				dV[2]->v->u = dV[3]->v->u = ttu2;
				dV[0]->v->y = dV[2]->v->y = ty2;
				dV[0]->v->v = dV[2]->v->v = tv2;

				tnl_poly(&next_poly);
			}
		} else {
regular_wall:
			init_poly(&next_poly, &thdr, 4);

			dV[0]->v->argb = bdc_col;
			dV[0]->v->oargb = bl_col;
			dV[1]->v->argb = tdc_col;
			dV[1]->v->oargb = tl_col;
			dV[2]->v->argb = bdc_col;
			dV[2]->v->oargb = bl_col;
			dV[3]->v->argb = tdc_col;
			dV[3]->v->oargb = tl_col;

			dV[0]->v->x = dV[1]->v->x = x1;
			dV[0]->v->z = dV[1]->v->z = z1;
			dV[0]->v->u = dV[1]->v->u = tu1;
			dV[1]->v->y = dV[3]->v->y = y1;
			dV[1]->v->v = dV[3]->v->v = tv1;

			dV[2]->v->x = dV[3]->v->x = x2;
			dV[2]->v->z = dV[3]->v->z = z2;
			dV[2]->v->u = dV[3]->v->u = tu2;
			dV[0]->v->y = dV[2]->v->y = y2;
			dV[0]->v->v = dV[2]->v->v = tv2;

			tnl_poly(&next_poly);
		}
	}

	has_bump = 0;
}

void R_RenderSwitch(seg_t *seg, int texture, int topOffset, int color)
{
	d64ListVert_t *dV[4];
	vertex_t *v1;
	vertex_t *v2;
	fixed_t x, y;
	fixed_t swx_sin, swx_cos;

	pvr_poly_cxt_t *curcxt;

	uint32_t new_color = D64_PVR_REPACK_COLOR(color);
	uint32_t switch_lit_color = R_SectorLightColor(new_color, frontsector->lightlevel);

	in_floor = 0;
	in_things = 0;
	has_bump = 0;

	P_CachePvrTexture(texture, PU_CACHE);

	context_change = 1;

	if (bump_txr_ptr[texture]) {
		has_bump = 1;
		defboargb = 0x7f5a00c0;
	}

	if (has_bump) {
		curcxt = &txr_cxt_bump[texture][0];
	} else {
		curcxt = &txr_cxt_nobump[texture][0];
	}

	if (!VideoFilter) {
		curcxt->txr.filter = PVR_FILTER_BILINEAR;
	} else {
		curcxt->txr.filter = PVR_FILTER_NONE;
	}
		
	curcxt->txr.uv_flip = PVR_UVFLIP_NONE;

	if (has_bump) {
		bump_cxt[texture].txr.uv_flip = PVR_UVFLIP_NONE;
		bump_cxt[texture].txr.filter =
			txr_cxt_bump[texture][0].txr.filter;
		pvr_poly_compile(&bumphdr, &bump_cxt[texture]);
	}

	pvr_poly_compile(&thdr, curcxt);
	globallump = texture;

	v1 = seg->linedef->v1;
	v2 = seg->linedef->v2;

	x = v1->x + v2->x;
	if (x < 0) {
		x = x + 1;
	}

	y = v1->y + v2->y;
	if (y < 0) {
		y = y + 1;
	}

	x >>= 1;
	y >>= 1;

	swx_cos = finecosine[seg->angle >> ANGLETOFINESHIFT] << 1;
	swx_sin = finesine[seg->angle >> ANGLETOFINESHIFT] << 1;

	float y1 = (float)topOffset;
	float y2 = y1 - 32.0f;
// (2*sin) - (16*cos) + x
// (2*sin) + (16*cos) + x
	float x1 = (float)((x) - (swx_cos << 3) + swx_sin) / 65536.0f;
	float x2 = (float)((x) + (swx_cos << 3) + swx_sin) / 65536.0f;
// (2*cos) + (16*sin) - y
// (2*cos) - (16*sin) - y
	float z1 = (float)((-y) + (swx_sin << 3) + swx_cos) / 65536.0f;
	float z2 = (float)((-y) - (swx_sin << 3) + swx_cos) / 65536.0f;

	normx = seg->nx;
	normy = 0;
	normz = seg->nz;

	init_poly(&next_poly, &thdr, 4);
	dV[0] = &next_poly.dVerts[0];
	dV[1] = &next_poly.dVerts[1];
	dV[2] = &next_poly.dVerts[2];
	dV[3] = &next_poly.dVerts[3];

	dV[0]->v->argb = new_color;
	dV[0]->v->oargb = switch_lit_color;
	dV[1]->v->argb = new_color;
	dV[1]->v->oargb = switch_lit_color;
	dV[2]->v->argb = new_color;
	dV[2]->v->oargb = switch_lit_color;
	dV[3]->v->argb = new_color;
	dV[3]->v->oargb = switch_lit_color;

	dV[0]->v->x = dV[1]->v->x = x1;
	dV[0]->v->z = dV[1]->v->z = z1;
	dV[0]->v->u = dV[1]->v->u = 0.0f;
	dV[1]->v->y = dV[3]->v->y = y1;
	dV[1]->v->v = dV[3]->v->v = 0.0f;

	dV[2]->v->x = dV[3]->v->x = x2;
	dV[2]->v->z = dV[3]->v->z = z2;
	dV[2]->v->u = dV[3]->v->u = 1.0f;
	dV[0]->v->y = dV[2]->v->y = y2;
	dV[0]->v->v = dV[2]->v->v = 1.0f;

	tnl_poly(&next_poly);

	has_bump = 0;
	context_change = 1;
}

extern int floor_split_override;
extern fvertex_t **split_verts;
extern int dont_bump;
static pvr_vertex_t __attribute__ ((aligned(32))) dv0;
static pvr_vertex_t __attribute__ ((aligned(32))) ipv[3];
static pvr_vertex_t __attribute__ ((aligned(32))) spv[5];

void R_RenderPlane(leaf_t *leaf, int numverts, int zpos, int texture, int xpos,
		   int ypos, int color, int ceiling, int lightlevel,
		   int alpha)
{
	pvr_vertex_t *dV[3];
	vertex_t *vrt;
	fixed_t x;
	fixed_t y;
	int idx;
	int v00, v01, v02;
	short stu, stv;
	float tu, tv;
	uint32_t new_color = D64_PVR_REPACK_COLOR_ALPHA(color, alpha);
	uint32_t floor_lit_color = R_SectorLightColor(new_color, lightlevel);

	int texnum = (texture >> 4) - firsttex;
	leaf_t *lf = leaf;

	has_bump = 0;
	// dont_bump gets set in automap
	// so we don't do pointless bump-mapping for the top-down view
	if (bump_txr_ptr[texnum] && !dont_bump) {
		has_bump = 1;
		float angle = doomangletoQ(viewangle);
		defboargb = 0x7f5a5a00 | (int)(angle * 255);
	}

	in_floor = 1 + ceiling;

	if (texture != globallump || globalcm != -1) {
		pvr_poly_cxt_t *curcxt;

		P_CachePvrTexture(texnum, PU_CACHE);

		if (has_bump) {
			curcxt = &txr_cxt_bump[texnum][texture & 15];

			bump_cxt[texnum].txr.uv_flip = PVR_UVFLIP_NONE;
			if (!VideoFilter) {
				bump_cxt[texnum].txr.filter =
					PVR_FILTER_BILINEAR;
			} else {
				bump_cxt[texnum].txr.filter = PVR_FILTER_NONE;
			}

			pvr_poly_compile(&bumphdr, &bump_cxt[texnum]);
		} else {
			curcxt = &txr_cxt_nobump[texnum][texture & 15];
		}

		if (!VideoFilter) {
			curcxt->txr.filter = PVR_FILTER_BILINEAR;
		} else {
			curcxt->txr.filter = PVR_FILTER_NONE;
		}
		curcxt->txr.uv_flip = PVR_UVFLIP_NONE;

		if (!has_bump) {
			if (alpha != 255) {
				curcxt->blend.src = PVR_BLEND_ONE;
				curcxt->blend.dst = PVR_BLEND_DESTALPHA;
			} else {
				curcxt->blend.src = PVR_BLEND_SRCALPHA;
				curcxt->blend.dst = PVR_BLEND_INVSRCALPHA;
			}
		}

		pvr_poly_compile(&thdr, curcxt);

		globallump = texture;
		globalcm = -1;

		context_change = 1;
#if 0
		normx = 0.0f;
		normz = 0.0f;
		normy = (ceiling ? -1.0f : 1.0f);
#endif
	}


	if (numverts >= 32) {
		numverts = 32;
	}

	vrt = lf[0].vertex;

	dv0.x = ((float)vrt->x / 65536.0f);
	dv0.y = (float)(zpos);
	dv0.z = -((float)vrt->y / 65536.0f);

	x = ((vrt->x + xpos) >> 16) & -64;
	y = ((vrt->y + ypos) >> 16) & -64;

	stu = (((vrt->x + xpos) & 0x3f0000U) >> 16);
	stv = -(((vrt->y + ypos) & 0x3f0000U) >> 16);
	tu = (float)stu / 64.0f;
	tv = (float)stv / 64.0f;

	dv0.u = tu;
	dv0.v = tv;

	dv0.argb = new_color;
	dv0.oargb = floor_lit_color;

	if (!dont_bump && gamemap != 28 && !floor_split_override && global_sub->is_split && (lightidx + 1)) {
		vertex_t *i1,*i2,*i3;
		fvertex_t *s12,*s23,*s31,*s30,*s10;
		float test_dist;
		player_t *p = &players[0];
		fvertex_t *subsplits = split_verts[global_sub->index];
		int is_odd = numverts & 1;

		float px = ( p->mo->x / 65536.0f) - dv0.x;
		float pz = (-p->mo->y / 65536.0f) - dv0.z;

		vec3f_length(px,0,pz, test_dist);
		if (test_dist > 640) {
			goto too_far_away;
		}
		float scaled_xpos = ((float)xpos / 65536.0f);
		float scaled_ypos = ((float)ypos / 65536.0f);
		float txo = (scaled_xpos - x) / 64.0f;
		float tyo = (scaled_ypos - y) / 64.0f;

		idx = 1;
		if(is_odd) {
			int s00 = 0;
			float i1x,i1y;
			float i2x,i2y;
			float i3x,i3y;

			idx = 2;
			s12 = &subsplits[s00];
			s23 = &subsplits[s00+1];
			s31 = &subsplits[s00+2];
			i1 = lf[0].vertex;
			i2 = lf[1].vertex;
			i3 = lf[2].vertex;

			i1x = i1->x / 65536.0f;
			i1y = i1->y / 65536.0f;

			i2x = i2->x / 65536.0f;
			i2y = i2->y / 65536.0f;

			i3x = i3->x / 65536.0f;
			i3y = i3->y / 65536.0f;

			init_poly(&next_poly, &thdr, 3);
			if(ceiling) {
				dV[0] = next_poly.dVerts[2].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[0].v;		
			} else {
				dV[0] = next_poly.dVerts[0].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[2].v;		
			}

			tu = (i1x / 64.0f) + txo;
			tv = (i1y / 64.0f) + tyo;
			dV[0]->x = i1x;
			dV[0]->y = (float)(zpos);
			dV[0]->z = -i1y;
			dV[0]->u = tu;
			dV[0]->v = -tv;
			dV[0]->argb = new_color;
			dV[0]->oargb = floor_lit_color;

			tu = (s12->x / 64.0f) + txo;
			tv = (s12->y / 64.0f) + tyo;
			dV[1]->x = s12->x;
			dV[1]->y = (float)(zpos);
			dV[1]->z = -s12->y;
			dV[1]->u = tu;
			dV[1]->v = -tv;
			dV[1]->argb = new_color;
			dV[1]->oargb = floor_lit_color;

			tu = (s31->x / 64.0f) + txo;
			tv = (s31->y / 64.0f) + tyo;
			dV[2]->x = s31->x;
			dV[2]->y = (float)(zpos);
			dV[2]->z = -s31->y;
			dV[2]->u = tu;
			dV[2]->v = -tv;
			dV[2]->argb = new_color;
			dV[2]->oargb = floor_lit_color;

			tnl_poly(&next_poly);

			init_poly(&next_poly, &thdr, 3);
			if(ceiling) {
				dV[0] = next_poly.dVerts[2].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[0].v;		
			} else {
				dV[0] = next_poly.dVerts[0].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[2].v;		
			}

			tu = (s12->x / 64.0f) + txo;
			tv = (s12->y / 64.0f) + tyo;
			dV[0]->x = s12->x;
			dV[0]->y = (float)(zpos);
			dV[0]->z = -s12->y;
			dV[0]->u = tu;
			dV[0]->v = -tv;
			dV[0]->argb = new_color;
			dV[0]->oargb = floor_lit_color;
			
			tu = (i2x / 64.0f) + txo;
			tv = (i2y / 64.0f) + tyo;
			dV[1]->x = i2x;
			dV[1]->y = (float)(zpos);
			dV[1]->z = -i2y;
			dV[1]->u = tu;
			dV[1]->v = -tv;
			dV[1]->argb = new_color;
			dV[1]->oargb = floor_lit_color;

			tu = (s23->x / 64.0f) + txo;
			tv = (s23->y / 64.0f) + tyo;
			dV[2]->x = s23->x;
			dV[2]->y = (float)(zpos);
			dV[2]->z = -s23->y;
			dV[2]->u = tu;
			dV[2]->v = -tv;
			dV[2]->argb = new_color;
			dV[2]->oargb = floor_lit_color;

			tnl_poly(&next_poly);

			init_poly(&next_poly, &thdr, 3);
			if(ceiling) {
				dV[0] = next_poly.dVerts[2].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[0].v;		
			} else {
				dV[0] = next_poly.dVerts[0].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[2].v;		
			}

			tu = (s23->x / 64.0f) + txo;
			tv = (s23->y / 64.0f) + tyo;
			dV[0]->x = s23->x;
			dV[0]->y = (float)(zpos);
			dV[0]->z = -s23->y;
			dV[0]->u = tu;
			dV[0]->v = -tv;
			dV[0]->argb = new_color;
			dV[0]->oargb = floor_lit_color;

			tu = (i3x / 64.0f) + txo;
			tv = (i3y / 64.0f) + tyo;
			dV[1]->x = i3x;
			dV[1]->y = (float)(zpos);
			dV[1]->z = -i3y;
			dV[1]->u = tu;
			dV[1]->v = -tv;
			dV[1]->argb = new_color;
			dV[1]->oargb = floor_lit_color;

			tu = (s31->x / 64.0f) + txo;
			tv = (s31->y / 64.0f) + tyo;
			dV[2]->x = s31->x;
			dV[2]->y = (float)(zpos);
			dV[2]->z = -s31->y;
			dV[2]->u = tu;
			dV[2]->v = -tv;
			dV[2]->argb = new_color;
			dV[2]->oargb = floor_lit_color;

			tnl_poly(&next_poly);

			init_poly(&next_poly, &thdr, 3);
			if(ceiling) {
				dV[0] = next_poly.dVerts[2].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[0].v;		
			} else {
				dV[0] = next_poly.dVerts[0].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[2].v;		
			}
			tu = (s12->x / 64.0f) + txo;
			tv = (s12->y / 64.0f) + tyo;
			dV[0]->x = s12->x;
			dV[0]->y = (float)(zpos);
			dV[0]->z = -s12->y;
			dV[0]->u = tu;
			dV[0]->v = -tv;
			dV[0]->argb = new_color;
			dV[0]->oargb = floor_lit_color;

			tu = (s23->x / 64.0f) + txo;
			tv = (s23->y / 64.0f) + tyo;
			dV[1]->x = s23->x;
			dV[1]->y = (float)(zpos);
			dV[1]->z = -s23->y;
			dV[1]->u = tu;
			dV[1]->v = -tv;
			dV[1]->argb = new_color;
			dV[1]->oargb = floor_lit_color;

			tu = (s31->x / 64.0f) + txo;
			tv = (s31->y / 64.0f) + tyo;
			dV[2]->x = s31->x;
			dV[2]->y = (float)(zpos);
			dV[2]->z = -s31->y;
			dV[2]->u = tu;
			dV[2]->v = -tv;
			dV[2]->argb = new_color;
			dV[2]->oargb = floor_lit_color;

			tnl_poly(&next_poly);
		}

		numverts--;

		if (idx < numverts) {
			v00 = idx + 0;
			v01 = idx + 1;
			v02 = idx + 2;

			// only triangle is out of the way
			// do all quads from here
			do {
				// reuses the same 8 verts repeatedly
				// set up 8 pvr_vertex_t
				// and just memcpy them each time
				float ix[3];
				float iy[3];
				float iu[3];
				float iv[3];
				float su[5];
				float sv[5];
				
				int s00;
				if (is_odd) {
					s00 = (5*(v00))/2;
				} else {
					s00 = (5*(v00-1))/2;
				}
				i1 = lf[v00].vertex;
				i2 = lf[v01].vertex;
				i3 = lf[v02].vertex;

				ix[0] = i1->x / 65536.0f;
				iy[0] = -i1->y / 65536.0f;

				ix[1] = i2->x / 65536.0f;
				iy[1] = -i2->y / 65536.0f;

				ix[2] = i3->x / 65536.0f;
				iy[2] = -i3->y / 65536.0f;

				iu[0] = (ix[0] / 64.0f) + txo;
				iv[0] = -((-iy[0] / 64.0f) + tyo);
				iu[1] = (ix[1] / 64.0f) + txo;
				iv[1] = -((-iy[1] / 64.0f) + tyo);
				iu[2] = (ix[2] / 64.0f) + txo;
				iv[2] = -((-iy[2] / 64.0f) + tyo);

				s12 = &subsplits[s00];
				s23 = &subsplits[s00+1];
				s31 = &subsplits[s00+2];
				s30 = &subsplits[s00+3];
				s10 = &subsplits[s00+4];

				su[0] = (s12->x / 64.0f) + txo;
				sv[0] = -((s12->y / 64.0f) + tyo);
				su[1] = (s23->x / 64.0f) + txo;
				sv[1] = -((s23->y / 64.0f) + tyo);
				su[2] = (s31->x / 64.0f) + txo;
				sv[2] = -((s31->y / 64.0f) + tyo);
				su[3] = (s30->x / 64.0f) + txo;
				sv[3] = -((s30->y / 64.0f) + tyo);
				su[4] = (s10->x / 64.0f) + txo;
				sv[4] = -((s10->y / 64.0f) + tyo);

				ipv[0].x = ix[0];
				ipv[0].z = iy[0];
				ipv[0].y = (float)zpos;
				ipv[0].u = iu[0];
				ipv[0].v = iv[0];
				ipv[0].argb = new_color;
				ipv[0].oargb = floor_lit_color;

				ipv[1].x = ix[1];
				ipv[1].z = iy[1];
				ipv[1].y = (float)zpos;
				ipv[1].u = iu[1];
				ipv[1].v = iv[1];
				ipv[1].argb = new_color;
				ipv[1].oargb = floor_lit_color;

				ipv[2].x = ix[2];
				ipv[2].z = iy[2];
				ipv[2].y = (float)zpos;
				ipv[2].u = iu[2];
				ipv[2].v = iv[2];
				ipv[2].argb = new_color;
				ipv[2].oargb = floor_lit_color;
#define spv12 0
#define spv23 1
#define spv31 2
#define spv30 3
#define spv10 4
				spv[spv12].x = s12->x;
				spv[spv12].z = -s12->y;
				spv[spv12].y = (float)zpos;
				spv[spv12].u = su[spv12];
				spv[spv12].v = sv[spv12];
				spv[spv12].argb = new_color;
				spv[spv12].oargb = floor_lit_color;

				spv[spv23].x = s23->x;
				spv[spv23].z = -s23->y;
				spv[spv23].y = (float)zpos;
				spv[spv23].u = su[spv23];
				spv[spv23].v = sv[spv23];
				spv[spv23].argb = new_color;
				spv[spv23].oargb = floor_lit_color;

				spv[spv31].x = s31->x;
				spv[spv31].z = -s31->y;
				spv[spv31].y = (float)zpos;
				spv[spv31].u = su[spv31];
				spv[spv31].v = sv[spv31];
				spv[spv31].argb = new_color;
				spv[spv31].oargb = floor_lit_color;

				spv[spv30].x = s30->x;
				spv[spv30].z = -s30->y;
				spv[spv30].y = (float)zpos;
				spv[spv30].u = su[spv30];
				spv[spv30].v = sv[spv30];
				spv[spv30].argb = new_color;
				spv[spv30].oargb = floor_lit_color;

				spv[spv10].x = s10->x;
				spv[spv10].z = -s10->y;
				spv[spv10].y = (float)zpos;
				spv[spv10].u = su[spv10];
				spv[spv10].v = sv[spv10];
				spv[spv10].argb = new_color;
				spv[spv10].oargb = floor_lit_color;

				init_poly(&next_poly,&thdr,3);
				if(ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;		
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;		
				}
				vertcpy(dV[0], &ipv[0]);
				vertcpy(dV[1], &spv[spv12]);
				vertcpy(dV[2], &spv[spv31]);
				tnl_poly(&next_poly);

				init_poly(&next_poly,&thdr,3);
				if(ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;		
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;		
				}
				vertcpy(dV[0], &spv[spv12]);
				vertcpy(dV[1], &ipv[1]);
				vertcpy(dV[2], &spv[spv23]);
				tnl_poly(&next_poly);
				
				init_poly(&next_poly,&thdr,3);
				if(ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;		
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;		
				}
				vertcpy(dV[0], &spv[spv23]);
				vertcpy(dV[1], &ipv[2]);
				vertcpy(dV[2], &spv[spv31]);
				tnl_poly(&next_poly);

				init_poly(&next_poly,&thdr,3);
				if(ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;		
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;		
				}
				vertcpy(dV[0], &spv[spv12]);
				vertcpy(dV[1], &spv[spv23]);
				vertcpy(dV[2], &spv[spv31]);
				tnl_poly(&next_poly);

				init_poly(&next_poly,&thdr,3);
				if(ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;		
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;		
				}
				vertcpy(dV[0], &ipv[2]);
				vertcpy(dV[1], &spv[spv30]);
				vertcpy(dV[2], &spv[spv31]);
				tnl_poly(&next_poly);

				init_poly(&next_poly,&thdr,3);
				if(ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;		
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;		
				}
				vertcpy(dV[0], &spv[spv30]);
				vertcpy(dV[1], &dv0);
				vertcpy(dV[2], &spv[spv10]);
				tnl_poly(&next_poly);

				init_poly(&next_poly,&thdr,3);
				if(ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;		
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;		
				}
				vertcpy(dV[0], &spv[spv10]);
				vertcpy(dV[1], &ipv[0]);
				vertcpy(dV[2], &spv[spv31]);
				tnl_poly(&next_poly);

				init_poly(&next_poly,&thdr,3);
				if(ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;		
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;		
				}
				vertcpy(dV[0], &spv[spv31]);
				vertcpy(dV[1], &spv[spv30]);
				vertcpy(dV[2], &spv[spv10]);
				tnl_poly(&next_poly);

				v00 += 2;
				v01 += 2;
				v02 += 2;
			} while (v02 < (numverts + 2));			
		}

		in_floor = 0;
		has_bump = 0;

		return;
	}

too_far_away:

	// odd number of verts, there is a single triangle to draw
	// before drawing all of the "quads"
	if (numverts & 1) {
		vertex_t *vrt1;
		vertex_t *vrt2;
		init_poly(&next_poly, &thdr, 3);
		if(ceiling) {
			dV[0] = next_poly.dVerts[2].v;
			dV[1] = next_poly.dVerts[1].v;
			dV[2] = next_poly.dVerts[0].v;		
		} else {
			dV[0] = next_poly.dVerts[0].v;
			dV[1] = next_poly.dVerts[1].v;
			dV[2] = next_poly.dVerts[2].v;		
		}

		idx = 2;

		vrt1 = lf[1].vertex;
		vertcpy(dV[0], &dv0);

		stu = (((vrt1->x + xpos) >> FRACBITS) - x);
		stv = -(((vrt1->y + ypos) >> FRACBITS) - y);
		tu = (float)stu / 64.0f;
		tv = (float)stv / 64.0f;

		dV[1]->x = ((float)vrt1->x / 65536.0f);
		dV[1]->y = (float)(zpos);
		dV[1]->z = -((float)vrt1->y / 65536.0f);
		dV[1]->u = tu;
		dV[1]->v = tv;
		dV[1]->argb = new_color;
		dV[1]->oargb = floor_lit_color;

		vrt2 = lf[2].vertex;

		stu = (((vrt2->x + xpos) >> FRACBITS) - x);
		stv = -(((vrt2->y + ypos) >> FRACBITS) - y);
		tu = (float)stu / 64.0f;
		tv = (float)stv / 64.0f;

		dV[2]->x = ((float)vrt2->x / 65536.0f);
		dV[2]->y = (float)(zpos);
		dV[2]->z = -((float)vrt2->y / 65536.0f);
		dV[2]->u = tu;
		dV[2]->v = tv;
		dV[2]->argb = new_color;
		dV[2]->oargb = floor_lit_color;

		tnl_poly(&next_poly);
	} else {
		idx = 1;
	}

	numverts--;

	if (idx < numverts) {
		v00 = idx + 0;
		v01 = idx + 1;
		v02 = idx + 2;

		do {
			vertex_t *vrt1;
			vertex_t *vrt2;
			vertex_t *vrt3;
			
			vrt1 = lf[v00].vertex;
			vrt2 = lf[v01].vertex;
			vrt3 = lf[v02].vertex;

			init_poly(&next_poly, &thdr, 3);
			if(ceiling) {
				dV[0] = next_poly.dVerts[2].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[0].v;		
			} else {
				dV[0] = next_poly.dVerts[0].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[2].v;		
			}

			stu = (((vrt1->x + xpos) >> FRACBITS) - x);
			stv = -(((vrt1->y + ypos) >> FRACBITS) - y);
			tu = (float)stu / 64.0f;
			tv = (float)stv / 64.0f;

			dV[0]->x = ((float)vrt1->x / 65536.0f);
			dV[0]->y = (float)(zpos);
			dV[0]->z = -((float)vrt1->y / 65536.0f);
			dV[0]->u = tu;
			dV[0]->v = tv;
			dV[0]->argb = new_color;
			dV[0]->oargb = floor_lit_color;

			stu = (((vrt2->x + xpos) >> FRACBITS) - x);
			stv = -(((vrt2->y + ypos) >> FRACBITS) - y);
			tu = (float)stu / 64.0f;
			tv = (float)stv / 64.0f;

			dV[1]->x = ((float)vrt2->x / 65536.0f);
			dV[1]->y = (float)(zpos);
			dV[1]->z = -((float)vrt2->y / 65536.0f);
			dV[1]->u = tu;
			dV[1]->v = tv;
			dV[1]->argb = new_color;
			dV[1]->oargb = floor_lit_color;

			stu = (((vrt3->x + xpos) >> FRACBITS) - x);
			stv = -(((vrt3->y + ypos) >> FRACBITS) - y);
			tu = (float)stu / 64.0f;
			tv = (float)stv / 64.0f;

			dV[2]->x = ((float)vrt3->x / 65536.0f);
			dV[2]->y = (float)(zpos);
			dV[2]->z = -((float)vrt3->y / 65536.0f);
			dV[2]->u = tu;
			dV[2]->v = tv;
			dV[2]->argb = new_color;
			dV[2]->oargb = floor_lit_color;

			tnl_poly(&next_poly);

			init_poly(&next_poly, &thdr, 3);	
			if(ceiling) {
				dV[0] = next_poly.dVerts[2].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[0].v;		
			} else {
				dV[0] = next_poly.dVerts[0].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[2].v;		
			}

			stu = (((vrt1->x + xpos) >> FRACBITS) - x);
			stv = -(((vrt1->y + ypos) >> FRACBITS) - y);
			tu = (float)stu / 64.0f;
			tv = (float)stv / 64.0f;

			dV[0]->x = ((float)vrt1->x / 65536.0f);
			dV[0]->y = (float)(zpos);
			dV[0]->z = -((float)vrt1->y / 65536.0f);
			dV[0]->u = tu;
			dV[0]->v = tv;
			dV[0]->argb = new_color;
			dV[0]->oargb = floor_lit_color;

			stu = (((vrt3->x + xpos) >> FRACBITS) - x);
			stv = -(((vrt3->y + ypos) >> FRACBITS) - y);
			tu = (float)stu / 64.0f;
			tv = (float)stv / 64.0f;

			dV[1]->x = ((float)vrt3->x / 65536.0f);
			dV[1]->y = (float)(zpos);
			dV[1]->z = -((float)vrt3->y / 65536.0f);
			dV[1]->u = tu;
			dV[1]->v = tv;
			dV[1]->argb = new_color;
			dV[1]->oargb = floor_lit_color;
	
			vertcpy(dV[2], &dv0);

			tnl_poly(&next_poly);
		
			v00 += 2;
			v01 += 2;
			v02 += 2;
		} while (v02 < (numverts + 2));
	}

	in_floor = 0;
	has_bump = 0;
}

pvr_ptr_t pvr_spritecache[MAX_CACHED_SPRITES];
pvr_poly_hdr_t hdr_spritecache[MAX_CACHED_SPRITES];
pvr_poly_cxt_t cxt_spritecache[MAX_CACHED_SPRITES];

int lump_frame[575 + 310] = { -1 };
int used_lumps[575 + 310] = { -1 };
int used_lump_idx = 0;
int delidx = 0;
int total_cached_vram = 0;

int last_flush_frame = 0;

char *W_GetNameForNum(int num);
extern int force_filter_flush;
int vram_low = 0;

// 1 - 348 decoration and item sprites (non-enemy)
// 349 - 923 enemy sprites
// 924 - 965 weapon sprites (non-enemy)
void R_RenderThings(subsector_t *sub)
{
	d64ListVert_t *dV[4];
	pvr_poly_hdr_t *theheader;

	byte *data;
	vissprite_t *vissprite_p;

	mobj_t *thing;
	boolean flip;
	int lump;

	int compressed;
	int height;
	int width;
	int color;
	byte *src;
	fixed_t xx, yy;
	int xpos1, xpos2;
	int ypos;
	int zpos1, zpos2;
	int spos;
	int external_pal = 0;
	int nosprite = 0;
	int sheet = 0;

	dV[0] = &next_poly.dVerts[0];
	dV[1] = &next_poly.dVerts[1];
	dV[2] = &next_poly.dVerts[2];
	dV[3] = &next_poly.dVerts[3];

	in_things = 1;
	in_floor = 0;
	has_bump = 0;

	vissprite_p = sub->vissprite;
	if (vissprite_p) {
		context_change = 1;

		if (vissprite_p->thing->flags & MF_RENDERLASER) {
			do {
				R_RenderLaser(vissprite_p->thing);
				vissprite_p = vissprite_p->next;
				if (vissprite_p == NULL) {
					break;
				}
				context_change = 1;
			} while (vissprite_p->thing->flags & MF_RENDERLASER);

			context_change = 1;

			if (vissprite_p == NULL) {
				in_things = 0;
				return;
			}
		}

		while (vissprite_p) {
			uint32_t new_color;
			uint32_t thing_lit_color;

			thing = vissprite_p->thing;
			lump = vissprite_p->lump;
			flip = vissprite_p->flip;
			has_bump = 0;
			context_change = 1;

			if (thing->frame & FF_FULLBRIGHT) {
				color = 0xffffffff;
			} else {
				color = lights[vissprite_p->sector->colors[2]].rgba;
			}
			new_color = D64_PVR_REPACK_COLOR_ALPHA(color, thing->alpha);
			thing_lit_color = R_SectorLightColor(new_color, vissprite_p->sector->lightlevel);

			data = W_CacheLumpNum(lump, PU_CACHE, dec_jag);
			src = data + sizeof(spriteN64_t);
			compressed = SwapShort(((spriteN64_t *)data)->compressed);
			width = SwapShort(((spriteN64_t *)data)->width);
			height = SwapShort(((spriteN64_t *)data)->height);

			spos = width;

			external_pal = 0;

			if (compressed) {
				int cmpsize = SwapShort(((spriteN64_t *)data)->cmpsize);
				if (cmpsize & 1) {
					external_pal = 1;
				}
			}

			if (flip) {
				xx = thing->x +
				     (SwapShort(((spriteN64_t *)data)->xoffs) * viewsin);

				xpos2 = (xx) >> 16;
				xpos1 = (xx - (width * viewsin)) >> 16;

				yy = thing->y -
				     (SwapShort(((spriteN64_t *)data)->xoffs) * viewcos);

				zpos2 = -(yy) >> 16;
				zpos1 = -(yy + (width * viewcos)) >> 16;
			} else {
				xx = thing->x -
				     (SwapShort(((spriteN64_t *)data)->xoffs) * viewsin);

				xpos2 = (xx + (width * viewsin)) >> 16;
				xpos1 = (xx) >> 16;

				yy = thing->y +
				     (SwapShort(((spriteN64_t *)data)->xoffs) * viewcos);

				zpos2 = -(yy - (width * viewcos)) >> 16;
				zpos1 = -(yy) >> 16;
			}

			ypos = (thing->z >> 16) + SwapShort(((spriteN64_t *)data)->yoffs);

			if ((lump <= 348) || ((lump >= 924) && (lump <= 965))) {
				nosprite = 0;
				sheet++;

				context_change = 1;

				if (!VideoFilter) {
					theheader = &pvr_sprite_hdr;
				} else {
					theheader = &pvr_sprite_hdr_nofilter;
				}

				init_poly(&next_poly, theheader, 4);

				// pull in each side of sprite by half pixel
				// fix for filtering 'crud' around the edge
				// due to lack of padding
				if (!flip) {
					dV[0]->v->u = dV[1]->v->u = all_u[lump] + (0.5f / 1024.0f);
					dV[2]->v->u = dV[3]->v->u = all_u[lump] +
						(((float)spos - 0.5f) / 1024.0f);
				} else {
					dV[0]->v->u = dV[1]->v->u = all_u[lump] +
						(((float)spos - 0.5f) / 1024.0f);
					dV[2]->v->u = dV[3]->v->u = all_u[lump] + (0.5f / 1024.0f);
				}
				dV[1]->v->v = dV[3]->v->v = all_v[lump] + (0.5f / 1024.0f);
				dV[0]->v->v = dV[2]->v->v = all_v[lump] +
					(((float)height - 0.5f) / 1024.0f);
			} else {
				int lumpoff = lump - 349;
				int cached_index = -1;
				int monster_w = (width + 7) & ~7;
				uint32_t wp2 = np2((uint32_t)monster_w);
				uint32_t hp2 = np2((uint32_t)height);

				sheet = 0;
				context_change = 1;

				if (external_pal && thing->info->palette) {
					void *newlump;
					int newlumpnum;
					char *lumpname = W_GetNameForNum(lump);

					switch (lumpname[0]) {
						case 'B':
							// BARO
							*(int *)lumpname = 0x4F524142;
							break;
						case 'P':
							switch (lumpname[1]) {
								case 'O':
									// ZOMB
									*(int *)lumpname = 0x424D4F5A;
									break;
								default:
									// PLY1 / PLY2
									*(int *)lumpname = 0x30594C50 + (thing->info->palette << 24);
									break;
							}
							break;
						case 'S':
							// SPEC
							*(int *)lumpname = 0x43455053;
							break;
						case 'T':
							// NITE
							*(int *)lumpname = 0x4554494E;
							break;
						default:
							break;
					}

					newlumpnum = W_S2_GetNumForName(lumpname);
					newlump = W_S2_CacheLumpNum(newlumpnum, PU_CACHE, dec_jag);
					src = newlump + sizeof(spriteN64_t);
					lumpoff = 574 + newlumpnum;
				}

				// cache flush conditions
				// 1) explicit flags
				// 2) wasn't enough VRAM for last caching attempt
				// 3) this code has run before, it has been more than 2 seconds 
				//    since the last time the cache code was called
				//    and more than 3/4 of the cache slots are used
				//      (MAX_CACHED_SPRITES * 3 / 4) == 192
				// with these conditions, the caching code works well,
				// handles the worst scenes (Absolution) without slowdown
				// (without lights)
				int flush_cond1 = force_filter_flush;
				int flush_cond2 = vram_low;
				int flush_cond3 = (last_flush_frame && 
					((NextFrameIdx - last_flush_frame) > 60) &&
					(used_lump_idx > 192 ));
				if (flush_cond1 || flush_cond2 || flush_cond3) {
					// this causes a *noticeable* pause, one frame hiccup
					//pvr_wait_ready();
					force_filter_flush = 0;
					vram_low = 0;
#define ALL_SPRITES_INDEX (575 + 310)
					for (int i = 0; i < ALL_SPRITES_INDEX; i++) {
						if (used_lumps[i] != -1) {
							pvr_mem_free(pvr_spritecache[used_lumps[i]]);
						}
					}
					memset(used_lumps, 0xff,
					       sizeof(int) * ALL_SPRITES_INDEX);
					memset(lump_frame, 0xff,
					       sizeof(int) * ALL_SPRITES_INDEX);

					used_lump_idx = 0;
					delidx = 0;

					last_flush_frame = NextFrameIdx;
				}

				if (used_lumps[lumpoff] != -1) {
					// found an index
					cached_index = used_lumps[lumpoff];
					lump_frame[lumpoff] = NextFrameIdx;
					goto skip_cached_setup;
				}

				if (last_flush_frame == 0)
					last_flush_frame = NextFrameIdx;

				if (used_lump_idx < MAX_CACHED_SPRITES) {
					used_lumps[lumpoff] = used_lump_idx;
					lump_frame[lumpoff] = NextFrameIdx;
					cached_index = used_lump_idx;
					used_lump_idx += 1;
				} else {
					// here it gets worse
					// find if any of the lumps have the delidx as their index
					// if so, set their index to -1
					nosprite = 1;

					// this gets incremented if all possible cache indices are 
					// used in a single frame and nothing can be evicted
					int passes = 0;

					int start_delidx = delidx;
					int next_lump_delidx = -1;

					// for every possible enemy sprite lump number
					for (int i = 0; i < (575 + 310); i++) {
						// this means we went past everything without evicting
						if (passes) {
							nosprite = 1;
							goto bail_evict;
						}

						// try to help this along by noting if we found 
						// the next del idx along the way
						if (used_lumps[i] == (delidx + 1)) {
							next_lump_delidx = i;
						}

						// if this enemy sprite lump number is already cached
						// and the cache index is our "del idx"
						// we should attempt to evict this one first
						if (used_lumps[i] == delidx) {
							if (lump_frame[i] == NextFrameIdx) {
								// this can help us skip more passes through 
								// the entire lump set
								if (next_lump_delidx != -1) {
									if (lump_frame[next_lump_delidx] !=
											NextFrameIdx) {
										delidx = used_lumps[next_lump_delidx];
										pvr_mem_free(pvr_spritecache[delidx]);
										used_lumps[i] = -1;
										lump_frame[i] = -1;
										goto done_evicting;
									}
								}

								i = 0;
								delidx += 1;

								// wrap
								if (delidx == MAX_CACHED_SPRITES) {
									delidx = 0;
								}

								// if after increment and/or wrap we are at 
								// the starting index, nothing was evictable
								if (delidx == start_delidx) {
									passes = 1;
								}

								continue;
							} else {
								pvr_mem_free(pvr_spritecache[delidx]);
								used_lumps[i] = -1;
								lump_frame[i] = -1;
								goto done_evicting;
							}
						}
					}

done_evicting:
					cached_index = delidx;
					used_lumps[lumpoff] = cached_index;
					lump_frame[lumpoff] = NextFrameIdx;

					delidx += 1;
					if (delidx == MAX_CACHED_SPRITES) {
						delidx = 0;
					}
				}
bail_evict:
				if (!nosprite) {
					uint32_t sprite_size = wp2 * hp2;
					// vram_low gets set if the sprite will use 
					// more than 1/4 available VRAM
					if ((sprite_size << 2) > pvr_mem_available()) {
						nosprite = 1;
						lump_frame[lumpoff] = -1;
						used_lumps[lumpoff] = -1;
						vram_low = 1;
						goto bail_pvr_alloc;
					}

					pvr_spritecache[cached_index] =
						pvr_mem_malloc(sprite_size);
					if (!pvr_spritecache[cached_index]) {
						I_Error("PVR OOM for RenderThings sprite cache");
					}

					pvr_poly_cxt_txr(&cxt_spritecache[cached_index],
						PVR_LIST_TR_POLY,
						D64_TPAL(0),
						wp2, hp2,
						pvr_spritecache[cached_index],
						PVR_FILTER_BILINEAR);

					cxt_spritecache[cached_index].gen.specular =
						PVR_SPECULAR_ENABLE;
					cxt_spritecache[cached_index].gen.fog_type =
						PVR_FOG_TABLE;
					cxt_spritecache[cached_index].gen.fog_type2 =
						PVR_FOG_TABLE;

					if (VideoFilter) {
						cxt_spritecache[cached_index].txr.filter =
							PVR_FILTER_NONE;
					}

					pvr_poly_compile(
						&hdr_spritecache[cached_index],
						&cxt_spritecache[cached_index]);

					pvr_txr_load(src,
						     pvr_spritecache[cached_index],
						     sprite_size);

					theheader = &hdr_spritecache[cached_index];

skip_cached_setup:
					init_poly(&next_poly, &hdr_spritecache[cached_index], 4);

					// some of the monsters have "the crud"
					// pull them in by half pixel on each edge
					if (!flip) {
						dV[0]->v->u = dV[1]->v->u = 0.0f + (0.5f / 1024.0f);
						dV[2]->v->u = dV[3]->v->u = 
							((float)monster_w / (float)wp2) - (0.5f / 1024.0f);
					} else {
						dV[0]->v->u = dV[1]->v->u = 
							((float)monster_w / (float)wp2) - (0.5f / 1024.0f);
						dV[2]->v->u = dV[3]->v->u = 0.0f + (0.5f / 1024.0f);
					}
					dV[1]->v->v = dV[3]->v->v = 0.0f + (0.5f / 1024.0f);
					dV[0]->v->v = dV[2]->v->v = ((float)height / (float)hp2) - (0.5f / 1024.0f);
				}
			}

bail_pvr_alloc:
			if (!nosprite) {
				float dx = xpos2 - xpos1;
				float dz = zpos2 - zpos1;
				// not 100% sure of this condition but it seems to look ok
				if (flip) {
					dx = -dx;
					dz = -dz;
				}
				float ilen = frsqrt((dx * dx) + (dz * dz));

				normx = -dz * ilen;
				normy = 0;
				normz = dx * ilen;

				dV[0]->v->x = dV[1]->v->x = xpos1;
				dV[0]->v->z = dV[1]->v->z = zpos1;
				dV[1]->v->y = dV[3]->v->y = ypos;

				dV[2]->v->x = dV[3]->v->x = xpos2;
				dV[2]->v->z = dV[3]->v->z = zpos2;
				dV[0]->v->y = dV[2]->v->y = ypos - height;

				dV[0]->v->argb = new_color;
				dV[1]->v->argb = new_color;
				dV[2]->v->argb = new_color;
				dV[3]->v->argb = new_color;

				dV[0]->v->oargb = thing_lit_color;
				dV[1]->v->oargb = thing_lit_color;
				dV[2]->v->oargb = thing_lit_color;
				dV[3]->v->oargb = thing_lit_color;

				tnl_poly(&next_poly);
			}

			vissprite_p = vissprite_p->next;
		}

		globallump = -1;
	}
	in_things = 0;
}

#define DC_RED		0xffff0000
#define DC_BLACK	0xff000000

static d64Vertex_t __attribute__((aligned(32))) laserverts[6];

void R_RenderLaser(mobj_t *thing)
{
	pvr_poly_cxt_t laser_cxt;
	pvr_poly_hdr_t laser_hdr;

	laserdata_t *laserdata = (laserdata_t *)thing->extradata;

	pvr_poly_cxt_col(&laser_cxt, PVR_LIST_OP_POLY);
	pvr_poly_compile(&laser_hdr, &laser_cxt);

	laserverts[0].v.x = (laserdata->x1 >> 16);
	laserverts[0].v.y = (laserdata->z1 >> 16);
	laserverts[0].v.z = -(laserdata->y1 >> 16);
	laserverts[0].v.argb = DC_RED;
	transform_vert(&laserverts[0]);
	perspdiv(&laserverts[0]);

	laserverts[1].v.x = ((laserdata->x1 - laserdata->slopey) >> 16);
	laserverts[1].v.y = (laserdata->z1 >> 16);
	laserverts[1].v.z = (-(laserdata->y1 + laserdata->slopex) >> 16);
	laserverts[1].v.argb = DC_BLACK;
	transform_vert(&laserverts[1]);
	perspdiv(&laserverts[1]);

	laserverts[2].v.x = ((laserdata->x2 - laserdata->slopey) >> 16);
	laserverts[2].v.y = (laserdata->z2 >> 16);
	laserverts[2].v.z = (-(laserdata->y2 + laserdata->slopex) >> 16);
	laserverts[2].v.argb = DC_BLACK;
	transform_vert(&laserverts[2]);
	perspdiv(&laserverts[2]);

	laserverts[3].v.x = (laserdata->x2 >> 16);
	laserverts[3].v.y = (laserdata->z2 >> 16);
	laserverts[3].v.z = -(laserdata->y2 >> 16);
	laserverts[3].v.argb = DC_RED;
	transform_vert(&laserverts[3]);
	perspdiv(&laserverts[3]);

	laserverts[4].v.x = ((laserdata->x2 + laserdata->slopey) >> 16);
	laserverts[4].v.y = (laserdata->z2 >> 16);
	laserverts[4].v.z = (-(laserdata->y2 - laserdata->slopex) >> 16);
	laserverts[4].v.argb = DC_BLACK;
	transform_vert(&laserverts[4]);
	perspdiv(&laserverts[4]);

	laserverts[5].v.x = ((laserdata->x1 + laserdata->slopey) >> 16);
	laserverts[5].v.y = (laserdata->z1 >> 16);
	laserverts[5].v.z = (-(laserdata->y1 - laserdata->slopex) >> 16);
	laserverts[5].v.argb = DC_BLACK;
	transform_vert(&laserverts[5]);
	perspdiv(&laserverts[5]);

	// 0 2 3
	// 0 1 2
	submit_triangle(&laserverts[0].v,
		&laserverts[2].v, 
		&laserverts[3].v,
		&laser_hdr, PVR_LIST_OP_POLY);

	submit_triangle(&laserverts[0].v,
		&laserverts[1].v, 
		&laserverts[2].v,
		&laser_hdr, PVR_LIST_OP_POLY);

	// 0 3 5
	// 3 4 5
	submit_triangle(&laserverts[0].v,
		&laserverts[3].v, 
		&laserverts[5].v,
		&laser_hdr, PVR_LIST_OP_POLY);
	submit_triangle(&laserverts[3].v,
		&laserverts[4].v, 
		&laserverts[5].v,
		&laser_hdr, PVR_LIST_OP_POLY);
}

extern pvr_poly_hdr_t pvr_sprite_hdr_bump;
extern pvr_poly_hdr_t pvr_sprite_hdr_nofilter_bump;
extern pvr_poly_hdr_t wepnbump_hdr;
extern pvr_poly_hdr_t wepndecs_hdr;
extern pvr_poly_hdr_t wepndecs_hdr_nofilter;

static float bump_atan2f(float y, float x) {
	float abs_y = fabs(y) + 1e-10f; // kludge to prevent 0/0 condition
	float r = (x - copysignf(abs_y, x)) / (abs_y + fabs(x));
	float angle = (F_PI * 0.5f) - copysignf(F_PI * 0.25f, x);

	angle += (0.1963f * r * r - 0.9817f) * r;
	return copysignf(angle, y);
}

void R_RenderPSprites(void)
{
	int i;
	pspdef_t *psp;
	state_t *state;
	spritedef_t *sprdef;
	spriteframe_t *sprframe;
	int lump;
	int flagtranslucent;

	byte *data;

	int width;
	int height;
	int width2;
	int x, y;

	// if you remove this the game won't work
	if (gamemap == 33) return;

	angle_t angle = viewangle >> ANGLETOFINESHIFT;
	fixed_t dist = 8;

	fixed_t lv_x = (dist * finecosine[angle]) + players[0].mo->x;
	fixed_t lv_y = (dist * finesine[angle]) + players[0].mo->y;

	float px = (lv_x / 65536.0f);
	float py = (players[0].mo->z / 65536.0f);
	float pz = -(lv_y / 65536.0f);

	quad2[0].flags = PVR_CMD_VERTEX;
	quad2[1].flags = PVR_CMD_VERTEX;
	quad2[2].flags = PVR_CMD_VERTEX;
	quad2[3].flags = PVR_CMD_VERTEX_EOL;

	psp = &viewplayer->psprites[0];

	flagtranslucent = (viewplayer->mo->flags & MF_SHADOW) != 0;

	for (i = 0; i < NUMPSPRITES; i++, psp++) {
		has_bump = 0;
		/* a null state means not active */
		if ((state = psp->state) != 0) {
			pvr_vertex_t *vert = quad2;
			float u1, v1, u2, v2;
			float x1, y1, x2, y2;

			uint8_t a1;

			uint32_t quad_color;
			uint32_t quad_light_color = 0;

			float lightingr = 0.0f;
			float lightingg = 0.0f;
			float lightingb = 0.0f;
			uint32_t projectile_light = 0;
			int applied = 0;
			int zbump = 0;

			sprdef = &sprites[state->sprite];
			sprframe = &sprdef->spriteframes[state->frame & FF_FRAMEMASK];
			lump = sprframe->lump[0];

			data = W_CacheLumpNum(lump, PU_CACHE, dec_jag);
			width = SwapShort(((spriteN64_t *)data)->width);
			width2 = (width + 7) & ~7;
			height = SwapShort(((spriteN64_t *)data)->height);

			u1 = all_u[lump];
			v1 = all_v[lump];
			u2 = all_u2[lump];
			v2 = all_v2[lump];

			if (flagtranslucent) {
				a1 = 144;
			} else {
				a1 = psp->alpha;
			}

			if (psp->state->frame & FF_FULLBRIGHT) {
				quad_color = D64_PVR_REPACK_COLOR_ALPHA(0xffffffff, a1);
			} else {
				uint32_t color = lights[frontsector->colors[2]].rgba;
				quad_color = D64_PVR_REPACK_COLOR_ALPHA(color, a1);
				quad_light_color = R_SectorLightColor(
					D64_PVR_REPACK_COLOR_ALPHA(color, a1),
					frontsector->lightlevel);
			}

			/*
			weapon lumps

			chainsaw 924-927
			fist 928-931
			pistol 932-934
			shotgun 936-938
			double shotgun 940-942
			chaingun 944-945
			rocket launcher 948-949
			plasma rifle 954,958
			bfg 959-960
			unmaker 964
			*/

			if (
			// chainsaw
			lump == 924 || lump == 925 ||
			lump == 926 || lump == 927 ||

			// fist
			lump == 928 || lump == 929 || 
			lump == 930 || lump == 931 ||

			// pistol
			lump == 932 || lump == 933 ||
			lump == 934 || 

			// shotgun
			lump == 936 || lump == 937 ||
			lump == 938 ||

			// double shotgun
			lump == 940 || lump == 941 || 
			lump == 942 ||

			// chaingun
			lump == 944 || lump == 945 ||

			// rocket launcher
			lump == 948 || lump == 949 ||

			// plasma rifle
			lump == 954 || lump == 958 ||

			// bfg
			lump == 959 || lump == 960 || 

			// unmaker
			lump == 964
			) {
				has_bump = 1;
			} else if (
				lump == 935 ||
				lump == 939 ||
				lump == 943 || 
				lump == 946 || lump == 947 ||
				lump == 950 || lump == 951 || lump == 952 || lump == 953 ||
				lump == 955 || lump == 956 || lump == 957 ||
				lump == 961 || lump == 962 || lump == 963 ||
				lump == 965) {
				zbump = 1;
			}

			if (zbump) {
				quad2[0].z = 4.5;
				quad2[1].z = 4.5;
				quad2[2].z = 4.5;
				quad2[3].z = 4.5;
			} else {
				quad2[0].z = 4.0;
				quad2[1].z = 4.0;
				quad2[2].z = 4.0;
				quad2[3].z = 4.0;
			}

			float avg_dx = 0;
			float avg_dy = 0;
			float avg_dz = 0;
			uint32_t wepn_boargb;

			for (int j = 0; j < lightidx + 1; j++) {
#if 0
				int visible;
#endif
				float dx = projectile_lights[j].x - px;
				float dy = projectile_lights[j].y - py;
				float dz = projectile_lights[j].z - pz;
				float lr = projectile_lights[j].radius;
#if 0
				if (in_floor == 1) {
					visible = dy >= 0;
				} else {
					visible = dy <= 0;
				}

				if (!visible) {
					continue;
				}
#endif
				float lightdist;
				vec3f_length(dx, dy, dz, lightdist);

				if (lightdist < lr) {
					float light_scale = (lr - lightdist) / lr;

					applied += 1;

					if (has_bump) {
						avg_dx += dx;
						avg_dy += dy;
						avg_dz += dz;
					}

					if(!zbump) {
						lightingr += (projectile_lights[j].r * light_scale);
						lightingg += (projectile_lights[j].g * light_scale);
						lightingb += (projectile_lights[j].b * light_scale);
					}
				}
			}

			for (int j = 0; j < 4; j++) {
				quad2[j].argb = quad_color;
				quad2[j].oargb = quad_light_color;
			}

			if (applied) {
				if (quad_light_color != 0) {
					float coord_r =
						(float)((quad_light_color >> 16) & 0xff) / 255.0f;
					float coord_g =
						(float)((quad_light_color >> 8) & 0xff) / 255.0f;
					float coord_b =
						(float)(quad_light_color & 0xff) / 255.0f;
					lightingr += coord_r;
					lightingg += coord_g;
					lightingb += coord_b;
				}

				if ((lightingr > 1.0f) ||
					(lightingg > 1.0f) ||
					(lightingb > 1.0f)) {
					float maxrgb = 0.0f;
					float invmrgb;
					if (lightingr > maxrgb)
						maxrgb = lightingr;
					if (lightingg > maxrgb)
						maxrgb = lightingg;
					if (lightingb > maxrgb)
						maxrgb = lightingb;

					invmrgb = 1.0f / maxrgb;

					lightingr *= invmrgb;
					lightingg *= invmrgb;
					lightingb *= invmrgb;
				}

				if ((lightingr + lightingg + lightingb) > 0.0f) {
					const int intensity = 96;
					projectile_light =
						0xff000000 |
						(((int)(lightingr * intensity) & 0xff) << 16) |
						(((int)(lightingg * intensity) & 0xff) << 8) |
						(((int)(lightingb * intensity) & 0xff));
				}

				for (int j = 0; j < 4; j++) {
#if 0
					// this is a hack to handle dark areas
					// because I am using a specular highlight color for dynamic lighting,
					// really dark vertices with textures applied are still basically solid black
					// with a colorful full-face specular highlight
					// set the vertex color to a dark but not too dark gray
					// now the dynamic lighting has a useful effect
					int coord_r = (quad_color >> 16) & 0xff;
					int coord_g = (quad_color >> 8) & 0xff;
					int coord_b = quad_color & 0xff;
					if (coord_r < 0x10 && coord_g < 0x10 &&
						coord_b < 0x10) {
						coord_r += 0x37;
						coord_g += 0x37;
						coord_b += 0x37;

						quad_color = D64_PVR_PACK_COLOR(
							0xff, coord_r, coord_g,
							coord_b);
						quad2[j].argb = quad_color;
					}
#endif					
					quad2[j].oargb = projectile_light;
				}

				if (has_bump) {
					float bax, bay, baz;
					float ts, tc;
					float adxP;
					float adzP;

					float BQ = F_PI;

					bax = avg_dx;
					bay = avg_dy;
					baz = avg_dz;

					vec3f_normalize(bax, bay, baz);

					float avg_cos = finecosine[angle] / 65536.0f;
					float avg_sin = finesine[angle] / 65536.0f;
					adxP = (-bax * avg_cos) + (baz * avg_sin);
					adzP = (baz * avg_cos) + (bax * avg_sin);

					BQ += bump_atan2f(adxP, adzP);

					BQ += F_PI;
					if (BQ > (F_PI * 2.0f)) {
						BQ -= (F_PI * 2.0f);
					}

					// elevation above floor
					float T = fabs(F_PI * 0.5f * bay);

					if (T < (F_PI * 0.25f)) {
						T = (F_PI * 0.25f);
					}

					fsincosr(T, &ts, &tc);
					int K1 = 127;
					int K2 = (int)(ts * 128);
					int K3 = (int)(tc * 128);
					int lq = (int)(BQ * 255.0f / (2.0f * F_PI));

					wepn_boargb = ((int)K1 << 24) |
									((int)K2 << 16) |
									((int)K3 << 8) |
									(int)lq;
				}
			}

			x = (((psp->sx >> 16) - SwapShort(((spriteN64_t *)data)->xoffs)) +
				 160);
			y = (((psp->sy >> 16) - SwapShort(((spriteN64_t *)data)->yoffs)) +
				 239);

			if (viewplayer->onground) {
				x += (quakeviewx >> 22);
				y += (quakeviewy >> 16);
			}

			x1 = (float)x * RES_RATIO;
			y1 = (float)y * RES_RATIO;
			x2 = x1 + ((float)width2 * RES_RATIO);
			y2 = y1 + ((float)height * RES_RATIO);

			if(lump == 935) {
				u1 = 0.0f / 64.0f;
				v1 = 0.0f / 64.0f;
				u2 = 24.0f / 64.0f;
				v2 = 30.0f / 64.0f;
			}

			if(lump == 939) {
				u1 = 24.0f / 64.0f;
				v1 = 0.0f / 64.0f;
				u2 = 48.0f / 64.0f;
				v2 = 34.0f / 64.0f;
			}

			if(lump == 943) {
				u1 = 0.0f / 64.0f;
				v1 = 30.0f / 64.0f;
				u2 = 40.0f / 64.0f;
				v2 = 62.0f / 64.0f;
			}

			// pull in each side of sprite by half pixel
			// fix for filtering 'crud' around the edge due to lack of padding
			vert->x = x1;
			vert->y = y2;
			vert->u = u1 + (0.5f / 1024.0f);
			vert->v = v2 - (0.5f / 1024.0f);
			vert++;

			vert->x = x1;
			vert->y = y1;
			vert->u = u1 + (0.5f / 1024.0f);
			vert->v = v1 + (0.5f / 1024.0f);
			vert++;

			vert->x = x2;
			vert->y = y2;
			vert->u = u2 - (0.5f / 1024.0f);
			vert->v = v2 - (0.5f / 1024.0f);
			vert++;

			vert->x = x2;
			vert->y = y1;
			vert->u = u2 - (0.5f / 1024.0f);
			vert->v = v1 + (0.5f / 1024.0f);

			if (has_bump) {
				memcpy(bump_verts, quad2, 4 * sizeof(pvr_vertex_t));

				for(int bi=0;bi<4;bi++) {
					bump_verts[bi].argb = 0xff000000;

					if (!applied) {
						bump_verts[bi].oargb = pvr_pack_bump(0.625, F_PI * 0.5f, F_PI * 0.5f);
					} else {
						bump_verts[bi].oargb = wepn_boargb;
					}
				}
				
				/*
				weapon lumps

				chainsaw 924-927
				fist 928-931
				pistol 932-934
				shotgun 936-938
				double shotgun 940-942
				chaingun 944-945
				rocket launcher 948-949
				plasma rifle 954,958
				bfg 959-960
				unmaker 964
				*/	

				float bu1,bv1,bu2,bv2;

				bu1 = bv1 = 0.0f;

				// chainsaw
				if (lump == 924) {
					bu2 = 113.0f / 512.0f;
					bv2 = 82.0f / 128.0f;
				}
				if (lump == 925) {
					bu1 = 120.0f / 512.0f;
					bu2 = 233.0f / 512.0f;
					bv2 = 80.0f / 128.0f;
				}
				if (lump == 926) {
					bu1 = 240.0f / 512.0f;
					bu2 = 353.0f / 512.0f;
					bv2 = 68.0f / 128.0f;
				}
				if (lump == 927) {
					bu1 = 360.0f / 512.0f;
					bu2 = 473.0f / 512.0f;
					bv2 = 68.0f / 128.0f;
				}

				// fist
				if (lump == 928) {
					// 81 -> 88
					bu2 = 81.0f / 512.0f;
					bv2 = 43.0f / 64.0f;
				}
				if (lump == 929) {
					bu1 = 88.0f / 512.0f;
					// 176 - 7
					bu2 = 169.0f / 512.0f;
					bv2 = 42.0f / 64.0f;
				}
				if (lump == 930) {
					bu1 = 176.0f / 512.0f;
					// 296 - 7
					bu2 = 289.0f / 512.0f;
					bv2 = 53.0f / 128.0f;
				}
				if (lump == 931) {
					bu1 = 296.0f / 512.0f;
					// 416 - 7
					bu2 = 409.0f / 512.0f;
					bv2 = 61.0f / 128.0f;
				}

				// pistol
				if (lump == 932) {
					bu2 = 56.0f / 256.0f;
					bv2 = 87.0f / 128.0f;
				}
				if (lump == 933) {
					bu1 = 56.0f / 256.0f;
					bu2 = 112.0f / 256.0f;
					bv2 = 97.0f / 128.0f;
				}
				if (lump == 934) {
					bu1 = 112.0f / 256.0f;
					// 59 -> 64
					bu2 = 171.0f / 256.0f;
					bv2 = 96.0f / 128.0f;
				}

				// shotgun
				if (lump == 936) {
					// 56 - 55 = 1
					bu2 = 55.0f / 256.0f;
					bv2 = 73.0f / 128.0f;
				}
				if (lump == 937) {
					bu1 = 56.0f / 256.0f;
					// 56 + 55 = 111
					bu2 = 111.0f / 256.0f;
					bv2 = 77.0f / 128.0f;
				}
				if (lump == 938) {
					bu1 = 112.0f / 256.0f;
					bu2 = 168.0f / 256.0f;
					bv2 = 75.0f / 128.0f;
				}

				// double shotgun
				if (lump == 940) {
					bu2 = 56.0f / 256.0f;
					bv2 = 63.0f / 64.0f;
				}
				if (lump == 941) {
					bu1 = 56.0f / 256.0f;
					bu2 = 120.0f / 256.0f;
					bv2 = 61.0f / 64.0f;
				}
				if (lump == 942) {
					bu1 = 120.0f / 256.0f;
					bu2 = 184.0f / 256.0f;
					bv2 = 62.0f / 64.0f;
				}

				// chaingun
				if (lump == 944) {
					// 112 - 108 4
					bu2 = 112.0f / 256.0f;
					bv2 = 72.0f / 128.0f;
				}
				if (lump == 945) {
					bu1 = 128.0f / 256.0f;
					bu2 = 240.0f / 256.0f;
					bv2 = 72.0f / 128.0f;
				}
				if (lump == 946) {
					bu1 = 0.0f / 256.0f;
					bv1 = 218.0f / 256.0f;
					bu2 = 32.0f / 256.0f;
					bv2 = 245.0f / 256.0f;
				}

				// rocker launcher
				if(lump == 948) {
					// 80 - 78
					bu2 = 78.0f / 256.0f;
					bv2 = 79.0f / 128.0f;
				}
				if (lump == 949) {
					bu1 = 80.0f / 256.0f;
					// 80 + 87
					bu2 = 167.0f / 256.0f;
					bv2 = 83.0f / 128.0f;
				}

				// plasma rifle
				if (lump == 954) {
					// 128 - 123 5
					bu2 = 128.0f / 256.0f;
					bv2 = 83.0f / 128.0f;
				}
				if (lump == 958) {
					bu1 = 128.0f / 256.0f;
					// 128 + 123
					bu2 = 256.0f / 256.0f;
					bv2 = 83.0f / 128.0f;
				}

				// bfg
				if (lump == 959) {
					// 160 - 154
					bu2 = 154.f / 512.0f;
					bv2 = 79.0f / 128.0f;
				}
				if (lump == 960) {
					bu1 = 160.f / 512.0f;
					// 160 + 154
					bu2 = 314.f / 512.0f;
					bv2 = 76.0f / 128.0f;
				}

				// laser
				if (lump == 964) {
					// 128 - 1213
					bu2 = 128.0f / 256.0f;
					bv2 = 90.0f / 128.0f;
				}

				pvr_list_prim(PVR_LIST_TR_POLY,
					&wepnbump_hdr,
					sizeof(pvr_poly_hdr_t));			

				bump_verts[0].u = bu1;
				bump_verts[0].v = bv2;

				bump_verts[1].u = bu1;
				bump_verts[1].v = bv1;
				
				bump_verts[2].u = bu2;
				bump_verts[2].v = bv2;

				bump_verts[3].u = bu2;
				bump_verts[3].v = bv1;

				pvr_list_prim(PVR_LIST_TR_POLY, bump_verts,
					4 * sizeof(pvr_vertex_t));
				subd_verts += 4;
			}

			pvr_poly_hdr_t *pspr_diffuse_hdr;
			if(	lump == 935 ||
				lump == 939 ||
				lump == 943) {
				if(!VideoFilter) {
					pspr_diffuse_hdr = &wepndecs_hdr;
				} else {
					pspr_diffuse_hdr = &wepndecs_hdr_nofilter;
				}
			} else {
				if (!VideoFilter) {
					if (has_bump) {
						pspr_diffuse_hdr = &pvr_sprite_hdr_bump;
					} else {
						pspr_diffuse_hdr = &pvr_sprite_hdr;
					}
				} else {
					if (has_bump) {
						pspr_diffuse_hdr = &pvr_sprite_hdr_nofilter_bump;
					} else {
						pspr_diffuse_hdr = &pvr_sprite_hdr_nofilter;
					}
				}
			}

			pvr_list_prim(PVR_LIST_TR_POLY, pspr_diffuse_hdr,
				sizeof(pvr_poly_hdr_t));
			pvr_list_prim(PVR_LIST_TR_POLY, quad2, sizeof(quad2));	
			subd_verts += 4;

			if (has_bump) {
				pvr_list_prim(PVR_LIST_TR_POLY, &flush_hdr,
					sizeof(pvr_poly_hdr_t));
				pvr_list_prim(PVR_LIST_TR_POLY, quad2, sizeof(quad2));
				subd_verts += 4;
			}

			has_bump = 0;
		} // if ((state = psp->state) != 0)
	} // for i < numsprites

	has_bump = 0;
	in_floor = 0;
	in_things = 0;
	context_change = 1;
}
