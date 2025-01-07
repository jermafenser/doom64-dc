// Renderer phase 3 - World Rendering Routines
#include "doomdef.h"
#include "r_local.h"

#include <dc/matrix.h>
#include <dc/pvr.h>
#include <math.h>

render_state_t __attribute__((aligned(32))) global_render_state;

d64Poly_t next_poly;
extern pvr_poly_hdr_t __attribute__((aligned(32))) laser_hdr;

extern pvr_poly_cxt_t **txr_cxt_bump;
extern pvr_poly_cxt_t **txr_cxt_nobump;

extern pvr_poly_hdr_t **txr_hdr_bump;
extern pvr_poly_hdr_t **txr_hdr_nobump;

extern pvr_poly_cxt_t **bump_cxt;
extern pvr_poly_hdr_t **bump_hdrs;

extern pvr_ptr_t *bump_txr_ptr;

extern pvr_poly_hdr_t pvr_sprite_hdr;
extern pvr_poly_hdr_t pvr_sprite_hdr_nofilter;

extern float *all_u;
extern float *all_v;
extern float *all_u2;
extern float *all_v2;

pvr_vertex_t __attribute__((aligned(32))) wepn_verts[4];
pvr_vertex_t __attribute__((aligned(32))) bump_verts[4];

// when dynamic lighting was introduced, skies with clouds were getting lit
//	by high-flying projectiles
// now this is just used to keep from lighting the transparent liquid floor
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
pvr_poly_hdr_t *bumphdr;

void *P_CachePvrTexture(int i, int tag);

void light_wall_hasbump(d64Poly_t *p, unsigned lightmask);
void light_wall_nobump(d64Poly_t *p, unsigned lightmask);
void light_plane_hasbump(d64Poly_t *p, unsigned lightmask);
void light_plane_nobump(d64Poly_t *p, unsigned lightmask);
void light_thing(d64Poly_t *p, unsigned lightmask);

void (*poly_light_func[5])(d64Poly_t *p, unsigned lightmask) = {
	light_plane_nobump,
	light_plane_hasbump,
	light_wall_nobump,
	light_wall_hasbump,
	light_thing
};

extern pvr_poly_cxt_t flush_cxt;
extern pvr_poly_hdr_t __attribute__((aligned(32))) flush_hdr;

extern pvr_dr_state_t dr_state;

extern void draw_pvr_line_hdr(vector_t *v1, vector_t *v2, int color);
extern void array_fast_cpy(void **dst, const void **src, size_t n);
extern void single_fast_cpy(void *dst, const void *src);

// convenience macro for copying pvr_vertex_t
#define vertcpy(d, s) single_fast_cpy((d),(s))

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
static inline unsigned nearz_vismask(d64Poly_t *poly)
{
	int nvert = poly->n_verts;
	unsigned rvm = (nvert == 4) ? 16 : 0;

	d64ListVert_t *vi = poly->dVerts;
	for (unsigned i = 0; i < nvert; i++) {
		rvm |= ((vi->v->z >= -vi->w) << i);
		vi++;
	}

	return rvm;
}

// must have
// `float t` and `float invt = 1.0f - t;`
// defined local to calling function
// this is just for cleaner code
#define lerp(a, b) (invt * (a) + t * (b))

// lerp two 32-bit colors
uint32_t color_lerp(float t, uint32_t v1c, uint32_t v2c) {
	const float invt = 1.0f - t;

	// ARGB8888
	uint8_t c0 = lerp(((v1c >> 24) & 0xff), ((v2c >> 24) & 0xff));
	uint8_t c1 = lerp(((v1c >> 16) & 0xff), ((v2c >> 16) & 0xff));
	uint8_t c2 = lerp(((v1c >> 8) & 0xff), ((v2c >> 8) & 0xff));
	uint8_t c3 = lerp(((v1c) & 0xff), ((v2c) & 0xff));

	return D64_PVR_PACK_COLOR(c0, c1, c2, c3);
}

// lerp two d64ListVert_t
// called if one of the input verts is determined to be behind the near-z plane
void nearz_clip(const d64ListVert_t *restrict v1,
				const d64ListVert_t *restrict v2,
				d64ListVert_t *out)
{
	const float d0 = v1->w + v1->v->z;
	const float d1 = v2->w + v2->v->z;
	// abs(d0 / (d1 - d0))
	float t = 0.000001f;
	if (d1 != d0) {
		t += (fabs(d0) * (1.0f / sqrtf((d1 - d0) * (d1 - d0))));
	}
	float invt = 1.0f - t;

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
void R_TransformProjectileLights(void)
{
	projectile_light_t *pl = projectile_lights;
	for (unsigned i = 0; i < lightidx + 1; i++) {
		float tmp = pl->z;

		pl->z = -pl->y;
		pl->y = tmp;

		// store reciprocal of radius in distance field for light code
		pl->distance = frapprox_inverse(pl->radius);

		pl++;
	}
}

// initialize a d64Poly_t * for rendering the next polygon
// n_verts	3 for triangle	(planes)
//			4 for quad		(walls, switches, things)
// diffuse_hdr is pointer to header to submit if context change required
void init_poly(d64Poly_t *poly, pvr_poly_hdr_t *diffuse_hdr, unsigned n_verts)
{
	void *list_tail;
	poly->n_verts = n_verts;
	list_tail = (void *)pvr_vertbuf_tail(PVR_LIST_TR_POLY);
	// header always points to next usable position in vertbuf/DMA list
	poly->hdr = (pvr_poly_hdr_t *)list_tail;

	// when header must be re-submitted
	if (global_render_state.context_change) {
		// copy the contents of the header into poly struct
		sq_fast_cpy(poly->hdr, diffuse_hdr, 1);
		// advance the vertbuf/DMA list position
		list_tail += sizeof(pvr_poly_hdr_t);
	}

	memset(poly->dVerts, 0, sizeof(d64ListVert_t) * n_verts);

	// set up 5 d64ListVert_t entries
	// each entry maintains a pointer into the vertbuf/DMA list for a vertex
	// near-z clipping is done in-place in the vertbuf/DMA list
	// some quad clipping cases require an extra vert added to triangle strip
	// this necessitates having contiguous space for 5 pvr_vertex_t available
	d64ListVert_t *dv = poly->dVerts;
	for (unsigned i = 0; i < 5; i++) {
		// each d64ListVert_t gets a pointer to the corresponding pvr_vertex_t
		(dv++)->v = (pvr_vertex_t *)list_tail;
		list_tail += sizeof(pvr_vertex_t);
		// each vert also maintains float rgb for dynamic lighting
		// and a flag that gets set if the vertex is ever lit during TNL loop
		// advance the vertbuf/DMA list position for next vert
	}
}

static int lf_idx(void)
{
	if (!global_render_state.in_things) {
		// 0 -> plane nobump
		// 1 -> plane hasbump
		// 2 -> wall nobump
		// 3 -> wall hasbump
		return ((!global_render_state.in_floor) << 1) + global_render_state.has_bump;
	} else {
		// 4 -> thing
		return 4;
	}
}

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

unsigned clip_poly(d64Poly_t *p, unsigned p_vismask);

void __attribute__((noinline)) tnl_poly(d64Poly_t *p)
{
	unsigned i;
	unsigned p_vismask;
	unsigned verts_to_process = p->n_verts;

	// set current bumpmap parameters to the default from whoever called us
	boargb = defboargb;

	// the condition for doing lighting/normal stuff is:
	//  if any dynamic lights exist
	//   AND
	//  we aren't drawing the transparent layer of a liquid floor
	if (global_render_state.quality) 
	{
		uint32_t gl = global_render_state.global_lit;
		if (gl && (!global_render_state.dont_color)) {
			switch (lf_idx()) {
			case 0:
				light_plane_nobump(p, gl);
				break;

			case 1:
				light_plane_hasbump(p, gl);
				break;

			case 2:
				light_wall_nobump(p, gl);
				break;

			case 3:
				light_wall_hasbump(p, gl);
				break;

			case 4:
				light_thing(p, gl);
				break;

			default:
				break;
			}
		}
	}
	
	// apply viewport/modelview/projection transform matrix to each vertex
	// all matrices are multiplied together once per frame in r_main.c
	// transform is a single `mat_trans_single3_nodivw` per vertex
	d64ListVert_t *dv = p->dVerts;
	for (i = 0; i < verts_to_process; i++) {
		transform_d64ListVert(dv);
		dv++;
	}

	p_vismask = nearz_vismask(p);

	// 0 or 16 means nothing visible, this happens
	if (!(p_vismask & ~16)) {
		return;
	}

	// set vert flags to defaults for poly type
	p->dVerts[0].v->flags = p->dVerts[1].v->flags = PVR_CMD_VERTEX;
	p->dVerts[3].v->flags = p->dVerts[4].v->flags = PVR_CMD_VERTEX_EOL;

	if (verts_to_process == 4) {
		p->dVerts[2].v->flags = PVR_CMD_VERTEX;
	} else {
		p->dVerts[2].v->flags = PVR_CMD_VERTEX_EOL;
	}

	verts_to_process = clip_poly(p, p_vismask);
	// we used to crash on invalid vismask
	// now we just return 0 from clip_poly and return early
	if (!verts_to_process) {
		return;
	}

	dv = p->dVerts;
	for (i = 0; i < verts_to_process; i++) {
		pvr_vertex_t *pv = dv->v;
		float invw = frapprox_inverse(dv->w);
		pv->x *= invw;
		pv->y *= invw;
		pv->z = invw;

		dv++;
	}

	uint32_t hdr_size = (global_render_state.context_change * sizeof(pvr_poly_hdr_t));
	uint32_t amount = hdr_size + (verts_to_process * sizeof(pvr_vertex_t));

	if (__builtin_expect(global_render_state.has_bump, 1)) {
		// they are laid out consecutively in memory starting at the first pointer
		pvr_vertex_t *diffuse_vert = p->dVerts[0].v;

		if (global_render_state.context_change) {
			sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), bumphdr, global_render_state.context_change);
		}

		for (i = 0; i < verts_to_process; i++) {
			pvr_vertex_t *vert = pvr_dr_target(dr_state);
			*vert = diffuse_vert[i];
			vert->argb = 0xff000000;
			vert->oargb = boargb;
			pvr_dr_commit(vert);
		}
	}

	// update diffuse/DMA list pointer
	pvr_vertbuf_written(PVR_LIST_TR_POLY, amount);

	global_render_state.context_change = 0;
}

unsigned __attribute__((noinline)) clip_poly(d64Poly_t *p, unsigned p_vismask)
{
	unsigned verts_to_process = p->n_verts;

	if (p_vismask == 7) {
		return verts_to_process;
	}

	if (p_vismask == 31) {
		return verts_to_process;
	}

	// this is the most common case, handled before the switch
	// p_vismask of 31 or 7: quad or tri all vertices visible
	switch (p_vismask) {

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
	case 22:
		verts_to_process = 0;
		break;

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
	case 25:
		verts_to_process = 0;
		break;

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

// we used to crash on invalid vismask
// now we return 0 to signal tnl_poly to submit nothing and return early
//	default:
//		I_Error("tnl_poly invalid vismask %d", p_vismask);
//		break;
	}

	return verts_to_process;
}

// unclipped triangles
// this is used to draw laser beams and nothing else
static void laser_triangle(const pvr_vertex_t *v0, const pvr_vertex_t *v1,
	const pvr_vertex_t *v2)
{
	if (global_render_state.context_change) {
		sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), &laser_hdr, 1);
	}

	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), v0, 1);
	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), v1, 1);
	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), v2, 1);

	global_render_state.context_change = 0;
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
int maxll = 0;
uint32_t R_SectorLightColor(uint32_t c, int ll)
{
	unsigned or = (c >> 16) & 0xff;
	unsigned og = (c >> 8) & 0xff;
	unsigned ob = c & 0xff;

	or = (or * ll) >> 6;
	og = (og * ll) >> 6;
	ob = (ob * ll) >> 6;

	uint8_t a = (uint8_t)((c >> 24) & 0xff);
	uint8_t r = or;
	uint8_t g = og;
	uint8_t b = ob;

	uint32_t rc = D64_PVR_PACK_COLOR(a, r, g, b);

	return rc;
}

void R_RenderAll(void)
{
	subsector_t *sub;

	global_render_state.context_change = 1;

	R_TransformProjectileLights();

	while (endsubsector--, (endsubsector >= solidsubsectors)) {
		sub = *endsubsector;
		frontsector = sub->sector;
		R_RenderWorld(sub);
		sub->drawindex = 0x7fff;
	}
}

void R_RenderWorld(subsector_t *sub)
{
	leaf_t *lf;
	seg_t *seg;
	fixed_t xoffset;
	fixed_t yoffset;
	int numverts;
	int i;

	global_render_state.global_sub = sub;
	global_render_state.global_lit = global_render_state.global_sub->lit;

	numverts = sub->numverts;

	lf = &leafs[sub->leaf];

	global_render_state.dont_color = 0;

	// render walls
	lf = &leafs[sub->leaf];
	for (i = 0; i < numverts; i++) {
		seg = lf->seg;

		if (seg && (seg->flags & 1))
			R_WallPrep(seg);

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
			global_render_state.dont_color = 1;

			lf = &leafs[sub->leaf];

			R_RenderPlane(
				lf, numverts,
				(frontsector->floorheight >> FRACBITS) + 4,
				textures[frontsector->floorpic], -yoffset,
				xoffset, lights[frontsector->colors[1]].rgba, 0,
				frontsector->lightlevel, 160);

			global_render_state.dont_color = 0;
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
	m_top = f_ceilingheight;  // set middle top

	backsector = seg->backsector;
	if (backsector) {
		b_floorheight = backsector->floorheight >> 16;
		b_ceilingheight = backsector->ceilingheight >> 16;

		if ((b_ceilingheight < f_ceilingheight) &&
			(backsector->ceilingpic != -1)) {
			height = f_ceilingheight - b_ceilingheight;

			if (li->flags & ML_DONTPEGTOP)
				rowoffs = (curRowoffset >> 16) + height;
			else
				rowoffs = ((height + 127) & ~127) + (curRowoffset >> 16);

			if (li->flags & ML_BLENDING) {
				if (frontheight &&
					!(li->flags & ML_BLENDFULLTOP)) {
					sideheight = b_ceilingheight -
								 f_ceilingheight;

					scale = (float)sideheight * frapprox_inverse(
							((float)frontheight));

					rn = ((float)r1 - (float)r2) * scale +
						 (float)r1;
					gn = ((float)g1 - (float)g2) * scale +
						 (float)g1;
					bn = ((float)b1 - (float)b2) * scale +
						 (float)b1;
					float maxc = 255.0f;
					if (rn > maxc) maxc = rn;
					if (gn > maxc) maxc = gn;
					if (bn > maxc) maxc = bn;
					maxc = 255.0f * frapprox_inverse(maxc);
					rn *= maxc;
					gn *= maxc;
					bn *= maxc;

					tmp_lowcolor = ((int)rn << 24) |
								   ((int)gn << 16) |
								   ((int)bn << 8) | 0xff;
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

			if ((li->flags & ML_DONTPEGBOTTOM) == 0)
				rowoffs = curRowoffset >> 16;
			else
				rowoffs = height + (curRowoffset >> 16);

			if (li->flags & ML_BLENDING) {
				if (frontheight &&
					!(li->flags & ML_BLENDFULLBOTTOM)) {
					sideheight =
						b_floorheight - f_ceilingheight;

					scale = (float)sideheight * frapprox_inverse(
							((float)frontheight));

					rn = ((float)r1 - (float)r2) * scale +
						 (float)r1;
					gn = ((float)g1 - (float)g2) * scale +
						 (float)g1;
					bn = ((float)b1 - (float)b2) * scale +
						 (float)b1;

					float maxc = 255.0f;
					if (rn > maxc) maxc = rn;
					if (gn > maxc) maxc = gn;
					if (bn > maxc) maxc = bn;
					maxc = 255.0f * frapprox_inverse(maxc);
					rn *= maxc;
					gn *= maxc;
					bn *= maxc;

					tmp_upcolor = ((int)rn << 24) |
								  ((int)gn << 16) |
								  ((int)bn << 8) | 0xff;
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

		if (!(li->flags & ML_DRAWMASKED))
			return;
	}

	height = m_top - m_bottom;

	if (li->flags & ML_DONTPEGBOTTOM)
		rowoffs = ((height + 127) & ~127) + (curRowoffset >> 16);
	else if (li->flags & ML_DONTPEGTOP)
		rowoffs = (curRowoffset >> 16) - m_bottom;
	else
		rowoffs = (curRowoffset >> 16) + height;

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

static float last_width_inv = recip64;
static float last_height_inv = recip64;
static pvr_poly_hdr_t *cur_wall_hdr;

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
	int ll = frontsector->lightlevel;
	uint32_t tdc_col = D64_PVR_REPACK_COLOR(topColor);
	uint32_t bdc_col = D64_PVR_REPACK_COLOR(bottomColor);
	uint32_t tl_col = R_SectorLightColor(tdc_col, ll);
	uint32_t bl_col = R_SectorLightColor(bdc_col, ll);

	// [GEC] Prevents errors in textures in S coordinates
	int curTextureoffset = (seg->sidedef->textureoffset + seg->offset) &
						   (127 << FRACBITS);

	global_render_state.in_floor = 0;
	global_render_state.in_things = 0;
	global_render_state.has_bump = 0;

	dV[0] = &next_poly.dVerts[0];
	dV[1] = &next_poly.dVerts[1];
	dV[2] = &next_poly.dVerts[2];
	dV[3] = &next_poly.dVerts[3];

	if (bump_txr_ptr[texnum]) {
		if (global_render_state.quality == 2) {
			global_render_state.has_bump = 1;
			defboargb = 0x7f5a00c0;
		}
	}

	if (texture != 16) {
		if (flags & ML_HMIRROR)
			cms = 2;
		else
			cms = 0;

		if (flags & ML_VMIRROR)
			cmt = 1;
		else
			cmt = 0;

		if ((texture != globallump) || (globalcm != (cms | cmt))) {
			pvr_poly_hdr_t *lastbh;
			int *hdr_ptr;
			int *bh_ptr;
			int newhp2v;
			int newbv;

			data = P_CachePvrTexture(texnum, PU_CACHE);

			wshift = SwapShort(((textureN64_t *)data)->wshift);
			hshift = SwapShort(((textureN64_t *)data)->hshift);
			last_width_inv = 1.0f / (float)(1 << wshift);
			last_height_inv = 1.0f / (float)(1 << hshift);

			if (global_render_state.has_bump) {
				cur_wall_hdr = &txr_hdr_bump[texnum][texture & 15];
				lastbh = &bump_hdrs[texnum][0];
				bumphdr = lastbh;
				bh_ptr = &((int *)lastbh)[2];
				newbv = *bh_ptr;
				newbv = (newbv & 0xFFF9DFFF) | ((cms | cmt) << 17) | (menu_settings.VideoFilter << 12);
				*bh_ptr = newbv;
			} else {
				cur_wall_hdr = &txr_hdr_nobump[texnum][texture & 15];
			}

			hdr_ptr = &((int *)cur_wall_hdr)[2];
			newhp2v = *hdr_ptr;
			// cms is S (U) mirror
			// cmt is T (V) mirror
			newhp2v = (newhp2v & 0xFFF9DFFF) | ((cms | cmt) << 17) | (menu_settings.VideoFilter << 12);
			if (!global_render_state.has_bump) {
				// fix Lost Levels map 2 "BLOOD" waterfall
				newhp2v = (newhp2v & 0x00FFFFFF) | 0x94000000;
			}

			*hdr_ptr = newhp2v;

			globallump = texture;
			globalcm = (cms | cmt);

			global_render_state.context_change = 1;
		}

#if 0
		// if texture v is flipped, rotate the default "light"
		// direction by 180 degrees
		if (global_render_state.has_bump) {
#if 0
            defboargb = 0x7f5a00c0;

            // horizontal flip doesn't matter
            // when it is directly "above"
            if (globalcm & 1) {
                defboargb = 0x7f5a0040;
            }
#endif

#if 0

			if (!(globalcm & 1)) {
				defboargb = 0x7f5a00c0;
			} else if (globalcm & 1) {
				defboargb = 0x7f5a0040;
			}

			if (globalcm & 2) {
				defboargb += 0x39;
			}

#endif
			defboargb = 0x7f5a00c0;
		}
#endif

		v1 = seg->v1;
		v2 = seg->v2;

		normx = seg->nx;
		normy = 0;
		normz = seg->nz;

		float x1 = v1->x >> 16;
		float y1 = (float)topHeight;
		float z1 = -(v1->y >> 16);

		float x2 = v2->x >> 16;
		float y2 = (float)bottomHeight;
		float z2 = -(v2->y >> 16);

		float stu1 = (curTextureoffset >> 16);
		float tu1 = stu1 * last_width_inv;
		float tv1 = (float)topOffset * last_height_inv;

		float stu2 = stu1 + (seg->length >> 4);
		float tu2 = stu2 * last_width_inv;
		float tv2 = (float)bottomOffset * last_height_inv;

		if (!global_render_state.global_lit) {
			goto regular_wall;
		}

		if (gamemap == 28) { // || gamemap == 33) {
			goto regular_wall;
		}

		fixed_t dx = D_abs(v1->x - viewx);
		fixed_t dy = D_abs(v1->y - viewy);

#define WALLDIST (512 << 16)

		if (!quickDistCheck(dx, dy, WALLDIST)) {
			goto regular_wall;
		}

		float yd = fabs(y2 - y1);
		float xd = fabs(x2 - x1);
		float zd = fabs(z2 - z1);

		unsigned i,j;

		// very tall walls are hard to light properly
		// sub-divide into vertically stacked segments
		// very wide walls are also hard to light properly
		// sub-divide into horizontal segments
		// tall AND wide is even worse so do both
		if ((yd > 96.0f) && ((xd > 96.0f) || (zd > 96.0f))) {
			unsigned ysteps = 2;
			unsigned xsteps = 2;

			if (yd > 256)
				ysteps = 4;
			else if (yd > 128)
				ysteps = 3;

			if ((xd > 256 || zd > 256))
				xsteps = 4;
			else if (xd > 128 || zd > 128)
				xsteps = 3;

			float xstepsize = 1.0f / (float)xsteps;
			float xs = ((x2 - x1) * xstepsize);
			float zs = ((z2 - z1) * xstepsize);
			float us = ((tu2 - tu1) * xstepsize);

			float ystepsize = 1.0f / (float)ysteps;
			float ys = ((y2 - y1) * ystepsize);
			float vs = ((tv2 - tv1) * ystepsize);

			for (i = 0; i < ysteps; i++) {
				for (j = 0; j < xsteps; j++) {
					float tx1 = x1 + (xs * j);
					float tx2 = tx1 + xs;

					float tz1 = z1 + (zs * j);
					float tz2 = tz1 + zs;

					float ttu1 = tu1 + (us * j);
					float ttu2 = ttu1 + us;

					float ty1 = y1 + (ys * i);
					float ty2 = ty1 + ys;

					float ttv1 = tv1 + (vs * i);
					float ttv2 = ttv1 + vs;

					uint32_t ucol = color_lerp(((i)*ystepsize),
											   tdc_col, bdc_col);
					uint32_t lcol = color_lerp(((i + 1) * ystepsize),
											   tdc_col, bdc_col);

					uint32_t ulcol = color_lerp(((i)*ystepsize),
												tl_col, bl_col);
					uint32_t llcol = color_lerp(((i + 1) * ystepsize),
												tl_col, bl_col);

					init_poly(&next_poly, cur_wall_hdr, 4);

#if 1
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

					dV[0]->v->argb = lcol;
					dV[0]->v->oargb = llcol;
					dV[1]->v->argb = ucol;
					dV[1]->v->oargb = ulcol;
					dV[2]->v->argb = lcol;
					dV[2]->v->oargb = llcol;
					dV[3]->v->argb = ucol;
					dV[3]->v->oargb = ulcol;
#else
					dV[0]->v->x = tx1;
					dV[0]->v->y = ty2;
					dV[0]->v->z = tz1;
					dV[0]->v->u = ttu1;
					dV[0]->v->v = ttv2;
					dV[0]->v->argb = lcol;
					dV[0]->v->oargb = llcol;

					dV[1]->v->x = tx1;
					dV[1]->v->y = ty1;
					dV[1]->v->z = tz1;
					dV[1]->v->u = ttu1;
					dV[1]->v->v = ttv1;
					dV[1]->v->argb = ucol;
					dV[1]->v->oargb = ulcol;

					dV[2]->v->x = tx2;
					dV[2]->v->y = ty2;
					dV[2]->v->z = tz2;
					dV[2]->v->u = ttu2;
					dV[2]->v->v = ttv2;
					dV[2]->v->argb = lcol;
					dV[2]->v->oargb = llcol;

					dV[3]->v->x = tx2;
					dV[3]->v->y = ty1;
					dV[3]->v->z = tz2;
					dV[3]->v->u = ttu2;
					dV[3]->v->v = ttv1;
					dV[3]->v->argb = ucol;
					dV[3]->v->oargb = ulcol;
#endif
					tnl_poly(&next_poly);
				}
			}
		} else if (yd > 96.0f) {
			unsigned steps = 2;

			if (yd > 256)
				steps = 4;
			else if (yd > 128)
				steps = 3;

			float stepsize = 1.0f / (float)steps;
			float ys = ((y2 - y1) * stepsize);
			float vs = ((tv2 - tv1) * stepsize);

			for (i = 0; i < steps; i++) {
				float tx1 = x1;
				float tx2 = x2;

				float tz1 = z1;
				float tz2 = z2;

				float ty1 = y1 + (ys * i);
				float ty2 = ty1 + ys;

				float ttv1 = tv1 + (vs * i);
				float ttv2 = ttv1 + vs;

				uint32_t ucol = color_lerp(((i)*stepsize),
										   tdc_col, bdc_col);
				uint32_t lcol = color_lerp(((i + 1) * stepsize),
										   tdc_col, bdc_col);

				uint32_t ulcol = color_lerp(((i)*stepsize),
											tl_col, bl_col);
				uint32_t llcol = color_lerp(((i + 1) * stepsize),
											tl_col, bl_col);

				init_poly(&next_poly, cur_wall_hdr, 4);
#if 1
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

				dV[0]->v->argb = lcol;
				dV[0]->v->oargb = llcol;
				dV[1]->v->argb = ucol;
				dV[1]->v->oargb = ulcol;
				dV[2]->v->argb = lcol;
				dV[2]->v->oargb = llcol;
				dV[3]->v->argb = ucol;
				dV[3]->v->oargb = ulcol;
#else
				dV[0]->v->x = tx1;
				dV[0]->v->y = ty2;
				dV[0]->v->z = tz1;
				dV[0]->v->u = tu1;
				dV[0]->v->v = ttv2;
				dV[0]->v->argb = lcol;
				dV[0]->v->oargb = llcol;

				dV[1]->v->x = tx1;
				dV[1]->v->y = ty1;
				dV[1]->v->z = tz1;
				dV[1]->v->u = tu1;
				dV[1]->v->v = ttv1;
				dV[1]->v->argb = ucol;
				dV[1]->v->oargb = ulcol;

				dV[2]->v->x = tx2;
				dV[2]->v->y = ty2;
				dV[2]->v->z = tz2;
				dV[2]->v->u = tu2;
				dV[2]->v->v = ttv2;
				dV[2]->v->argb = lcol;
				dV[2]->v->oargb = llcol;

				dV[3]->v->x = tx2;
				dV[3]->v->y = ty1;
				dV[3]->v->z = tz2;
				dV[3]->v->u = tu2;
				dV[3]->v->v = ttv1;
				dV[3]->v->argb = ucol;
				dV[3]->v->oargb = ulcol;
#endif
				tnl_poly(&next_poly);
			}
		} else if (((xd > 96.0f) || (zd > 96.0f))) {
			unsigned steps = 2;

			if ((xd > 256 || yd > 256))
				steps = 4;
			else if (xd > 128 || yd > 128)
				steps = 3;

			float stepsize = 1.0f / (float)steps;
			float xs = ((x2 - x1) * stepsize);
			float zs = ((z2 - z1) * stepsize);
			float us = ((tu2 - tu1) * stepsize);

			for (i = 0; i < steps; i++) {
				float ty1 = y1;
				float ty2 = y2;

				float tx1 = x1 + (xs * i);
				float tx2 = tx1 + xs;
				float tz1 = z1 + (zs * i);
				float tz2 = tz1 + zs;

				float ttu1 = tu1 + (us * i);
				float ttu2 = ttu1 + us;

				init_poly(&next_poly, cur_wall_hdr, 4);
#if 1
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

				dV[0]->v->argb = bdc_col;
				dV[0]->v->oargb = bl_col;
				dV[1]->v->argb = tdc_col;
				dV[1]->v->oargb = tl_col;
				dV[2]->v->argb = bdc_col;
				dV[2]->v->oargb = bl_col;
				dV[3]->v->argb = tdc_col;
				dV[3]->v->oargb = tl_col;
#else
				dV[0]->v->x = tx1;
				dV[0]->v->y = ty2;
				dV[0]->v->z = tz1;
				dV[0]->v->u = ttu1;
				dV[0]->v->v = tv2;
				dV[0]->v->argb = bdc_col;
				dV[0]->v->oargb = bl_col;

				dV[1]->v->x = tx1;
				dV[1]->v->y = ty1;
				dV[1]->v->z = tz1;
				dV[1]->v->u = ttu1;
				dV[1]->v->v = tv1;
				dV[1]->v->argb = tdc_col;
				dV[1]->v->oargb = tl_col;

				dV[2]->v->x = tx2;
				dV[2]->v->y = ty2;
				dV[2]->v->z = tz2;
				dV[2]->v->u = ttu2;
				dV[2]->v->v = tv2;
				dV[2]->v->argb = bdc_col;
				dV[2]->v->oargb = bl_col;

				dV[3]->v->x = tx2;
				dV[3]->v->y = ty1;
				dV[3]->v->z = tz2;
				dV[3]->v->u = ttu2;
				dV[3]->v->v = tv1;
				dV[3]->v->argb = tdc_col;
				dV[3]->v->oargb = tl_col;

#endif
				tnl_poly(&next_poly);
			}
		} else {
		regular_wall:
			init_poly(&next_poly, cur_wall_hdr, 4);
#if 1
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

			dV[0]->v->argb = bdc_col;
			dV[0]->v->oargb = bl_col;
			dV[1]->v->argb = tdc_col;
			dV[1]->v->oargb = tl_col;
			dV[2]->v->argb = bdc_col;
			dV[2]->v->oargb = bl_col;
			dV[3]->v->argb = tdc_col;
			dV[3]->v->oargb = tl_col;
#else
				dV[0]->v->x = x1;
				dV[0]->v->y = y2;
				dV[0]->v->z = z1;
				dV[0]->v->u = tu1;
				dV[0]->v->v = tv2;
				dV[0]->v->argb = bdc_col;
				dV[0]->v->oargb = bl_col;

				dV[1]->v->x = x1;
				dV[1]->v->y = y1;
				dV[1]->v->z = z1;
				dV[1]->v->u = tu1;
				dV[1]->v->v = tv1;
				dV[1]->v->argb = tdc_col;
				dV[1]->v->oargb = tl_col;

				dV[2]->v->x = x2;
				dV[2]->v->y = y2;
				dV[2]->v->z = z2;
				dV[2]->v->u = tu2;
				dV[2]->v->v = tv2;
				dV[2]->v->argb = bdc_col;
				dV[2]->v->oargb = bl_col;

				dV[3]->v->x = x2;
				dV[3]->v->y = y1;
				dV[3]->v->z = z2;
				dV[3]->v->u = tu2;
				dV[3]->v->v = tv1;
				dV[3]->v->argb = tdc_col;
				dV[3]->v->oargb = tl_col;
#endif
			tnl_poly(&next_poly);
		}
	}

	global_render_state.has_bump = 0;
	global_render_state.in_floor = 0;
	global_render_state.in_things = 0;
}

void R_RenderSwitch(seg_t *seg, int texture, int topOffset, int color)
{
	pvr_poly_hdr_t *lastbh;
	pvr_poly_hdr_t *curhdr;
	int *hdr_ptr;
	int *bh_ptr;
	int newhp2v;
	int newbv;

	pvr_vertex_t *dV[4];

	vertex_t *v1;
	vertex_t *v2;
	fixed_t x, y;
	fixed_t swx_sin, swx_cos;

	if (texture <= 1)
		return;

	uint32_t new_color = D64_PVR_REPACK_COLOR(color);
	uint32_t switch_lit_color = R_SectorLightColor(new_color, frontsector->lightlevel);

	global_render_state.in_floor = 0;
	global_render_state.in_things = 0;
	global_render_state.has_bump = 0;

	P_CachePvrTexture(texture, PU_CACHE);

	global_render_state.context_change = 1;

	v1 = seg->linedef->v1;
	v2 = seg->linedef->v2;

	if (bump_txr_ptr[texture]) {
		if (global_render_state.quality == 2) {
			global_render_state.has_bump = 1;
			defboargb = 0x7f5a00c0;
		}
	}

	// there are some dark switches that appear to be caused by
	// some confusion on the PVR over depth order
	// they do occur if walls are drawn with TR polys
	// they do not occur if the walls are drawn with PT polys
	// why it only happens in these two instances I have not determined
#if 1
	if (gamemap == 2) {
		// Terraformer - 4 dark switches in "puzzle room"
		if ((-820 << 16) < v1->y && v1->y < (270 << 16)) {
			if ((-960 << 16) < v1->x && v1->x < (90 << 16)) {
				global_render_state.has_bump = 0;
			}
		}
	} else if (gamemap == 21) {
		// Pitfalls - 1 dark switch in "cave"
		if ((1730 << 16) < v1->y && v1->y < (1790 << 16)) {
			if ((-64 << 16) < v1->x && v1->x < (32 << 16)) {
				global_render_state.has_bump = 0;
			}
		}
	} else if (gamemap == 39) {
		global_render_state.has_bump = 0;
	}

	// TODO
	// add the switches from Knee Deep In The Dead that also do this
#endif

	if (global_render_state.has_bump) {
		curhdr = &txr_hdr_bump[texture][0];

		lastbh = &bump_hdrs[texture][0];
		bumphdr = lastbh;

		bh_ptr = &((int *)lastbh)[2];
		newbv = *bh_ptr;
		newbv = (newbv & 0xFFF9DFFF) | (menu_settings.VideoFilter << 12);

		*bh_ptr = newbv;
	} else {
		curhdr = &txr_hdr_nobump[texture][0];
	}

	hdr_ptr = &((int *)curhdr)[2];
	newhp2v = *hdr_ptr;
	newhp2v = (newhp2v & 0xFFF9DFFF) | (menu_settings.VideoFilter << 12);
	*hdr_ptr = newhp2v;

	globallump = texture;

	x = v1->x + v2->x;

	if (x < 0)
		x = x + 1;

	y = v1->y + v2->y;

	if (y < 0)
		y = y + 1;

	x >>= 1;
	y >>= 1;

	swx_cos = finecosine[seg->angle >> ANGLETOFINESHIFT] << 1;
	swx_sin = finesine[seg->angle >> ANGLETOFINESHIFT] << 1;

	float y1 = (float)topOffset;
	float y2 = y1 - 32.0f;
	float x1 = (float)(((x) - (swx_cos << 3) + swx_sin) >> 16);
	float x2 = (float)(((x) + (swx_cos << 3) + swx_sin) >> 16);
	float z1 = (float)(((-y) + (swx_sin << 3) + swx_cos) >> 16);
	float z2 = (float)(((-y) - (swx_sin << 3) + swx_cos) >> 16);

	normx = seg->nx;
	normy = 0;
	normz = seg->nz;

	init_poly(&next_poly, curhdr, 4);
	dV[0] = next_poly.dVerts[0].v;
	dV[1] = next_poly.dVerts[1].v;
	dV[2] = next_poly.dVerts[2].v;
	dV[3] = next_poly.dVerts[3].v;

#if 1
	dV[0]->x = dV[1]->x = x1;
	dV[0]->z = dV[1]->z = z1;
	dV[0]->u = dV[1]->u = 0.0f;
	dV[1]->y = dV[3]->y = y1;
	dV[1]->v = dV[3]->v = 0.0f;

	dV[2]->x = dV[3]->x = x2;
	dV[2]->z = dV[3]->z = z2;
	dV[2]->u = dV[3]->u = 1.0f;
	dV[0]->y = dV[2]->y = y2;
	dV[0]->v = dV[2]->v = 1.0f;

	dV[0]->argb = new_color;
	dV[0]->oargb = switch_lit_color;
	dV[1]->argb = new_color;
	dV[1]->oargb = switch_lit_color;
	dV[2]->argb = new_color;
	dV[2]->oargb = switch_lit_color;
	dV[3]->argb = new_color;
	dV[3]->oargb = switch_lit_color;
#else
	dV[0]->x = x1;
	dV[0]->y = y2;
	dV[0]->z = z1;
	dV[0]->u = 0;
	dV[0]->v = 1;
	dV[0]->argb = new_color;
	dV[0]->oargb = switch_lit_color;

	dV[1]->x = x1;
	dV[1]->y = y1;
	dV[1]->z = z1;
	dV[1]->u = 0;
	dV[1]->v = 0;
	dV[1]->argb = new_color;
	dV[1]->oargb = switch_lit_color;

	dV[2]->x = x2;
	dV[2]->y = y2;
	dV[2]->z = z2;
	dV[2]->u = 1;
	dV[2]->v = 1;
	dV[2]->argb = new_color;
	dV[2]->oargb = switch_lit_color;

	dV[3]->x = x2;
	dV[3]->y = y1;
	dV[3]->z = z2;
	dV[3]->u = 1;
	dV[3]->v = 0;
	dV[3]->argb = new_color;
	dV[3]->oargb = switch_lit_color;
#endif
	tnl_poly(&next_poly);

	global_render_state.has_bump = 0;
	global_render_state.context_change = 1;
}

extern fvertex_t **split_verts;
static pvr_vertex_t __attribute__((aligned(32))) dv0;
static pvr_vertex_t __attribute__((aligned(32))) ipv[3];
static pvr_vertex_t __attribute__((aligned(32))) spv[5];
static pvr_poly_hdr_t *cur_plane_hdr;

// PVR texture memory pointers for texture[texnum][palnum]
extern pvr_ptr_t **pvr_texture_ptrs;

void R_RenderPlane(leaf_t *leaf, int numverts, int zpos, int texture, int xpos,
	int ypos, int color, int ceiling, int lightlevel,
	int alpha)
{
	pvr_poly_hdr_t *lastbh;
	pvr_vertex_t *dV[4];

	void *srca[3];

	vertex_t *vrt;

	fixed_t x;
	fixed_t y;

	int idx;
	int v00, v01, v02;

	leaf_t *lf = leaf;

	int texnum = (texture >> 4) - firsttex;

	uint32_t new_color = D64_PVR_REPACK_COLOR_ALPHA(color, alpha);
	uint32_t floor_lit_color = R_SectorLightColor(new_color, lightlevel);

	global_render_state.has_bump = 0;

	// dont_bump gets set in automap
	// so we don't do pointless bump-mapping for the top-down view
	if (bump_txr_ptr[texnum] && !global_render_state.dont_bump) {
		if (global_render_state.quality == 2) {
			float angle = doomangletoQ(viewangle);
			defboargb = 0x7f5a5a00 | (int)(angle * 255);
			global_render_state.has_bump = 1;
		}
	}

	global_render_state.in_floor = 1 + ceiling;
	if (texture != globallump || globalcm != -1) {
		P_CachePvrTexture(texnum, PU_CACHE);
		int *hdr_ptr;
		int *bh_ptr;
		int newhp2v;
		int newbv;
		if (global_render_state.has_bump) {
			cur_plane_hdr = &txr_hdr_bump[texnum][texture & 15];

			lastbh = &bump_hdrs[texnum][0];
			bumphdr = lastbh;
			bh_ptr = &((int *)lastbh)[2];
			newbv = *bh_ptr;
			newbv = (newbv & 0xFFF9DFFF) | (menu_settings.VideoFilter << 12);
			*bh_ptr = newbv;
		} else {
			cur_plane_hdr = &txr_hdr_nobump[texnum][texture & 15];
		}

		hdr_ptr = &((int *)cur_plane_hdr)[2];
		newhp2v = *hdr_ptr;
		newhp2v = (newhp2v & 0xFFF9DFFF) | (menu_settings.VideoFilter << 12);
		if (!global_render_state.has_bump) {
			if (alpha != 255)
				newhp2v = (newhp2v & 0x00FFFFFF) | 0x38000000;
			else
				newhp2v = (newhp2v & 0x00FFFFFF) | 0x94000000;
		}
		*hdr_ptr = newhp2v;

		globallump = texture;
		globalcm = -1;

		global_render_state.context_change = 1;
	}

	if (numverts >= 32)
		numverts = 32;

	vrt = lf[0].vertex;

	x = (fixed_t)((vrt->x + xpos) >> 16) & -64;
	y = (fixed_t)((vrt->y + ypos) >> 16) & -64;

	dv0.x = ((float)(vrt->x >> 16));
	dv0.y = (float)(zpos);
	dv0.z = -((float)(vrt->y >> 16));
	dv0.u = (float)(((vrt->x + xpos) & 0x3f0000U) >> 16) * recip64;
	dv0.v = -((float)(((vrt->y + ypos) & 0x3f0000U) >> 16)) * recip64;
	dv0.argb = new_color;
	dv0.oargb = floor_lit_color;

	float scaled_xpos = (float)(xpos >> 16) - x;
	float scaled_ypos = (float)(ypos >> 16) - y;

	if (!global_render_state.global_lit ||
		!global_render_state.global_sub->is_split ||
		global_render_state.floor_split_override ||
		global_render_state.dont_color || 
		global_render_state.dont_bump) {
		goto too_far_away;
	} else {
#define spv12 0
#define spv23 1
#define spv31 2
#define spv30 3
#define spv10 4
		vertex_t *i1, *i2, *i3;
		fvertex_t *s12, *s23, *s31, *s30, *s10;
		player_t *p = &players[0];
		fvertex_t *subsplits = split_verts[global_render_state.global_sub->index];
		int is_odd = numverts & 1;

		fixed_t px = (fixed_t)((p->mo->x >> 16) - dv0.x);
		fixed_t pz = (fixed_t)((-(p->mo->y >> 16)) - dv0.z);

		if (!quickDistCheck(px,pz,640)) {
			goto too_far_away;
		}

		idx = 1;
		if (is_odd) {
			float i1x, i1y;
			float i2x, i2y;
			float i3x, i3y;

			idx = 2;

			s12 = &subsplits[0];
			s23 = &subsplits[1];
			s31 = &subsplits[2];

			i1 = lf[0].vertex;
			i2 = lf[1].vertex;
			i3 = lf[2].vertex;

			i1x = i1->x >> 16;
			i1y = i1->y >> 16;
			i2x = i2->x >> 16;
			i2y = i2->y >> 16;
			i3x = i3->x >> 16;
			i3y = i3->y >> 16;

			spv[spv12].x = s12->x;
			spv[spv12].y = (float)zpos;
			spv[spv12].z = -s12->y;
			spv[spv12].u = (s12->x + scaled_xpos) * recip64;
			spv[spv12].v = -((s12->y + scaled_ypos) * recip64);
			spv[spv12].argb = new_color;
			spv[spv12].oargb = floor_lit_color;

			spv[spv23].x = s23->x;
			spv[spv23].y = (float)zpos;
			spv[spv23].z = -s23->y;
			spv[spv23].u = (s23->x + scaled_xpos) * recip64;
			spv[spv23].v = -((s23->y + scaled_ypos) * recip64);
			spv[spv23].argb = new_color;
			spv[spv23].oargb = floor_lit_color;

			spv[spv31].x = s31->x;
			spv[spv31].y = (float)zpos;
			spv[spv31].z = -s31->y;
			spv[spv31].u = (s31->x + scaled_xpos) * recip64;
			spv[spv31].v = -((s31->y + scaled_ypos) * recip64);
			spv[spv31].argb = new_color;
			spv[spv31].oargb = floor_lit_color;

			/////////////////////////////////

			init_poly(&next_poly, cur_plane_hdr, 3);
			if (ceiling) {
				dV[0] = next_poly.dVerts[2].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[0].v;
			} else {
				dV[0] = next_poly.dVerts[0].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[2].v;
			}

			srca[0] = &spv[spv12];
			srca[1] = &spv[spv31];
			array_fast_cpy((void **)dV, (const void **)srca, 2);

			dV[2]->x = i1x;
			dV[2]->y = (float)(zpos);
			dV[2]->z = -i1y;
			dV[2]->u = (i1x + scaled_xpos) * recip64;
			dV[2]->v = -(i1y + scaled_ypos) * recip64;
			dV[2]->argb = new_color;
			dV[2]->oargb = floor_lit_color;

			tnl_poly(&next_poly);

			/////////////////////////////////

			init_poly(&next_poly, cur_plane_hdr, 3);
			if (ceiling) {
				dV[0] = next_poly.dVerts[2].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[0].v;
			} else {
				dV[0] = next_poly.dVerts[0].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[2].v;
			}

			srca[0] = &spv[spv23];
			srca[1] = &spv[spv12];
			array_fast_cpy((void **)dV, (const void **)srca, 2);

			dV[2]->x = i2x;
			dV[2]->y = (float)(zpos);
			dV[2]->z = -i2y;
			dV[2]->u = (i2x + scaled_xpos) * recip64;
			dV[2]->v = -(i2y + scaled_ypos) * recip64;
			dV[2]->argb = new_color;
			dV[2]->oargb = floor_lit_color;

			tnl_poly(&next_poly);

			/////////////////////////////////

			init_poly(&next_poly, cur_plane_hdr, 3);
			if (ceiling) {
				dV[0] = next_poly.dVerts[2].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[0].v;
			} else {
				dV[0] = next_poly.dVerts[0].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[2].v;
			}

			srca[0] = &spv[spv31];
			srca[1] = &spv[spv23];
			array_fast_cpy((void **)dV, (const void **)srca, 2);

			dV[2]->x = i3x;
			dV[2]->y = (float)(zpos);
			dV[2]->z = -i3y;
			dV[2]->u = (i3x + scaled_xpos) * recip64;
			dV[2]->v = -(i3y + scaled_ypos) * recip64;
			dV[2]->argb = new_color;
			dV[2]->oargb = floor_lit_color;

			tnl_poly(&next_poly);

			/////////////////////////////////

			init_poly(&next_poly, cur_plane_hdr, 3);
			if (ceiling) {
				dV[0] = next_poly.dVerts[2].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[0].v;
			} else {
				dV[0] = next_poly.dVerts[0].v;
				dV[1] = next_poly.dVerts[1].v;
				dV[2] = next_poly.dVerts[2].v;
			}

			srca[0] = &spv[spv12];
			srca[1] = &spv[spv23];
			srca[2] = &spv[spv31];
			array_fast_cpy((void **)dV, (const void **)srca, 3);

			tnl_poly(&next_poly);

			/////////////////////////////////
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
				// and fast asm copy them each time
				float ix[3];
				float iy[3];

				unsigned s00;
				if (is_odd) {
					s00 = (5 * (v00)) >> 1;
				} else {
					s00 = (5 * (v00 - 1)) >> 1;
				}
				i1 = lf[v00].vertex;
				i2 = lf[v01].vertex;
				i3 = lf[v02].vertex;

				ix[0] = i1->x >> 16;
				iy[0] = i1->y >> 16;
				ix[1] = i2->x >> 16;
				iy[1] = i2->y >> 16;
				ix[2] = i3->x >> 16;
				iy[2] = i3->y >> 16;

				ipv[0].x = ix[0];
				ipv[0].y = (float)zpos;
				ipv[0].z = -iy[0];
				ipv[0].u = (ix[0] + scaled_xpos) * recip64;
				ipv[0].v = -((iy[0] + scaled_ypos) * recip64);
				ipv[0].argb = new_color;
				ipv[0].oargb = floor_lit_color;

				ipv[1].x = ix[1];
				ipv[1].y = (float)zpos;
				ipv[1].z = -iy[1];
				ipv[1].u = (ix[1] + scaled_xpos) * recip64;
				ipv[1].v = -((iy[1] + scaled_ypos) * recip64);
				ipv[1].argb = new_color;
				ipv[1].oargb = floor_lit_color;

				ipv[2].x = ix[2];
				ipv[2].y = (float)zpos;
				ipv[2].z = -iy[2];
				ipv[2].u = (ix[2] + scaled_xpos) * recip64;
				ipv[2].v = -((iy[2] + scaled_ypos) * recip64);
				ipv[2].argb = new_color;
				ipv[2].oargb = floor_lit_color;

				s12 = &subsplits[s00 + spv12];
				s23 = &subsplits[s00 + spv23];
				s31 = &subsplits[s00 + spv31];
				s30 = &subsplits[s00 + spv30];
				s10 = &subsplits[s00 + spv10];

				spv[spv12].x = s12->x;
				spv[spv12].y = (float)zpos;
				spv[spv12].z = -s12->y;
				spv[spv12].u = (s12->x + scaled_xpos) * recip64;
				spv[spv12].v = -((s12->y + scaled_ypos) * recip64);
				spv[spv12].argb = new_color;
				spv[spv12].oargb = floor_lit_color;

				spv[spv23].x = s23->x;
				spv[spv23].y = (float)zpos;
				spv[spv23].z = -s23->y;
				spv[spv23].u = (s23->x + scaled_xpos) * recip64;
				spv[spv23].v = -((s23->y + scaled_ypos) * recip64);
				spv[spv23].argb = new_color;
				spv[spv23].oargb = floor_lit_color;

				spv[spv31].x = s31->x;
				spv[spv31].y = (float)zpos;
				spv[spv31].z = -s31->y;
				spv[spv31].u = (s31->x + scaled_xpos) * recip64;
				spv[spv31].v = -((s31->y + scaled_ypos) * recip64);
				spv[spv31].argb = new_color;
				spv[spv31].oargb = floor_lit_color;

				spv[spv30].x = s30->x;
				spv[spv30].y = (float)zpos;
				spv[spv30].z = -s30->y;
				spv[spv30].u = (s30->x + scaled_xpos) * recip64;
				spv[spv30].v = -((s30->y + scaled_ypos) * recip64);
				spv[spv30].argb = new_color;
				spv[spv30].oargb = floor_lit_color;

				spv[spv10].x = s10->x;
				spv[spv10].y = (float)zpos;
				spv[spv10].z = -s10->y;
				spv[spv10].u = (s10->x + scaled_xpos) * recip64;
				spv[spv10].v = -((s10->y + scaled_ypos) * recip64);
				spv[spv10].argb = new_color;
				spv[spv10].oargb = floor_lit_color;

				/////////////////////////////////

				init_poly(&next_poly, cur_plane_hdr, 3);
				if (ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;
				}

				srca[0] = &ipv[0];
				srca[1] = &spv[spv12];
				srca[2] = &spv[spv31];
				array_fast_cpy((void **)dV, (const void **)srca, 3);

				tnl_poly(&next_poly);

				/////////////////////////////////

				init_poly(&next_poly, cur_plane_hdr, 3);
				if (ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;
				}

				srca[0] = &spv[spv12];
				srca[1] = &ipv[1];
				srca[2] = &spv[spv23];
				array_fast_cpy((void **)dV, (const void **)srca, 3);

				tnl_poly(&next_poly);

				/////////////////////////////////

				init_poly(&next_poly, cur_plane_hdr, 3);
				if (ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;
				}

				srca[0] = &spv[spv23];
				srca[1] = &ipv[2];
				srca[2] = &spv[spv31];
				array_fast_cpy((void **)dV, (const void **)srca, 3);

				tnl_poly(&next_poly);

				/////////////////////////////////

				init_poly(&next_poly, cur_plane_hdr, 3);
				if (ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;
				}

				srca[0] = &spv[spv12];
				srca[1] = &spv[spv23];
				srca[2] = &spv[spv31];
				array_fast_cpy((void **)dV, (const void **)srca, 3);

				tnl_poly(&next_poly);

				/////////////////////////////////

				init_poly(&next_poly, cur_plane_hdr, 3);
				if (ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;
				}

				srca[0] = &ipv[2];
				srca[1] = &spv[spv30];
				srca[2] = &spv[spv31];
				array_fast_cpy((void **)dV, (const void **)srca, 3);

				tnl_poly(&next_poly);

				/////////////////////////////////

				init_poly(&next_poly, cur_plane_hdr, 3);
				if (ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;
				}

				srca[0] = &spv[spv30];
				srca[1] = &dv0;
				srca[2] = &spv[spv10];
				array_fast_cpy((void **)dV, (const void **)srca, 3);

				tnl_poly(&next_poly);

				/////////////////////////////////

				init_poly(&next_poly, cur_plane_hdr, 3);
				if (ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;
				}

				srca[0] = &spv[spv10];
				srca[1] = &ipv[0];
				srca[2] = &spv[spv31];
				array_fast_cpy((void **)dV, (const void **)srca, 3);

				tnl_poly(&next_poly);

				/////////////////////////////////

				init_poly(&next_poly, cur_plane_hdr, 3);
				if (ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;
				}

				srca[0] = &spv[spv31];
				srca[1] = &spv[spv30];
				srca[2] = &spv[spv10];
				array_fast_cpy((void **)dV, (const void **)srca, 3);

				tnl_poly(&next_poly);

				/////////////////////////////////

				v00 += 2;
				v01 += 2;
				v02 += 2;
			} while (v02 < (numverts + 2));
		}

		global_render_state.in_floor = 0;
		global_render_state.has_bump = 0;

		return;
	}

too_far_away:

	// odd number of verts, there is a single triangle to draw
	// before drawing all of the "quads"
	if (numverts & 1) {
		vertex_t *vrt1 = lf[1].vertex;
		vertex_t *vrt2 = lf[2].vertex;

		idx = 2;

		/////////////////////////////////

		init_poly(&next_poly, cur_plane_hdr, 3);
		if (ceiling) {
			dV[0] = next_poly.dVerts[2].v;
			dV[1] = next_poly.dVerts[1].v;
			dV[2] = next_poly.dVerts[0].v;
		} else {
			dV[0] = next_poly.dVerts[0].v;
			dV[1] = next_poly.dVerts[1].v;
			dV[2] = next_poly.dVerts[2].v;
		}

		vertcpy(dV[0], &dv0);

		dV[1]->x = (float)(vrt1->x >> 16);
		dV[1]->y = (float)(zpos);
		dV[1]->z = -((float)(vrt1->y >> 16));
		dV[1]->u = (float)((vrt1->x >> 16) + scaled_xpos) * recip64;
		dV[1]->v = -(float)((vrt1->y >> 16) + scaled_ypos) * recip64;
		dV[1]->argb = new_color;
		dV[1]->oargb = floor_lit_color;

		dV[2]->x = (float)(vrt2->x >> 16);
		dV[2]->y = (float)(zpos);
		dV[2]->z = -((float)(vrt2->y >> 16));
		dV[2]->u = (float)((vrt2->x >> 16) + scaled_xpos) * recip64;
		dV[2]->v = -(float)((vrt2->y >> 16) + scaled_ypos) * recip64;
		dV[2]->argb = new_color;
		dV[2]->oargb = floor_lit_color;

		tnl_poly(&next_poly);

		/////////////////////////////////
	} else {
		idx = 1;
	}

	numverts--;

	if (idx < numverts) {
		v00 = idx + 0;
		v01 = idx + 1;
		v02 = idx + 2;

		if (!global_render_state.global_lit) {
			global_render_state.in_floor = 0;
		}

		do {
			vertex_t *vrt1;
			vertex_t *vrt2;
			vertex_t *vrt3;

			vrt1 = lf[v00].vertex;
			vrt2 = lf[v01].vertex;
			vrt3 = lf[v02].vertex;

			if (global_render_state.global_lit) {
				// vrt1 and vrt3 are duplicated
				spv[0].x = (float)(vrt1->x >> 16);
				spv[0].y = (float)(zpos);
				spv[0].z = -((float)(vrt1->y >> 16));
				spv[0].u = (float)((vrt1->x >> 16) + scaled_xpos) * recip64;
				spv[0].v = -(float)((vrt1->y >> 16) + scaled_ypos) * recip64;
				spv[0].argb = new_color;
				spv[0].oargb = floor_lit_color;

				spv[1].x = (float)(vrt3->x >> 16);
				spv[1].y = (float)(zpos);
				spv[1].z = -((float)(vrt3->y >> 16));
				spv[1].u = (float)((vrt3->x >> 16) + scaled_xpos) * recip64;
				spv[1].v = -(float)((vrt3->y >> 16) + scaled_ypos) * recip64;
				spv[1].argb = new_color;
				spv[1].oargb = floor_lit_color;

				/////////////////////////////////

				init_poly(&next_poly, cur_plane_hdr, 3);
				if (ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;
				}

				srca[0] = &spv[1];
				srca[1] = &spv[0];
				array_fast_cpy((void **)dV, (const void **)srca, 2);

				dV[2]->x = (float)(vrt2->x >> 16);
				dV[2]->y = (float)(zpos);
				dV[2]->z = -((float)(vrt2->y >> 16));
				dV[2]->u = (float)((vrt2->x >> 16) + scaled_xpos) * recip64;
				dV[2]->v = -(float)((vrt2->y >> 16) + scaled_ypos) * recip64;
				dV[2]->argb = new_color;
				dV[2]->oargb = floor_lit_color;

				tnl_poly(&next_poly);

				/////////////////////////////////

				init_poly(&next_poly, cur_plane_hdr, 3);
				if (ceiling) {
					dV[0] = next_poly.dVerts[2].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;
				} else {
					dV[0] = next_poly.dVerts[0].v;
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[2].v;
				}

				srca[0] = &spv[0];
				srca[1] = &spv[1];
				srca[2] = &dv0;

				array_fast_cpy((void **)dV, (const void **)srca, 3);

				tnl_poly(&next_poly);

				/////////////////////////////////
			} else {
				init_poly(&next_poly, cur_plane_hdr, 4);

				if (ceiling) {
					dV[1] = next_poly.dVerts[1].v;
					dV[2] = next_poly.dVerts[0].v;
					dV[3] = next_poly.dVerts[2].v;
				} else {
					dV[1] = next_poly.dVerts[2].v;
					dV[2] = next_poly.dVerts[0].v;
					dV[3] = next_poly.dVerts[1].v;
				}

				dV[0] = next_poly.dVerts[3].v;

				dV[1]->x = (float)(vrt1->x >> 16);
				dV[1]->y = (float)(zpos);
				dV[1]->z = -((float)(vrt1->y >> 16));
				dV[1]->u = (float)((vrt1->x >> 16) + scaled_xpos) * recip64;
				dV[1]->v = -(float)((vrt1->y >> 16) + scaled_ypos) * recip64;
				dV[1]->argb = new_color;
				dV[1]->oargb = floor_lit_color;

				dV[2]->x = (float)(vrt2->x >> 16);
				dV[2]->y = (float)(zpos);
				dV[2]->z = -((float)(vrt2->y >> 16));
				dV[2]->u = (float)((vrt2->x >> 16) + scaled_xpos) * recip64;
				dV[2]->v = -(float)((vrt2->y >> 16) + scaled_ypos) * recip64;
				dV[2]->argb = new_color;
				dV[2]->oargb = floor_lit_color;

				dV[3]->x = (float)(vrt3->x >> 16);
				dV[3]->y = (float)(zpos);
				dV[3]->z = -((float)(vrt3->y >> 16));
				dV[3]->u = (float)((vrt3->x >> 16) + scaled_xpos) * recip64;
				dV[3]->v = -(float)((vrt3->y >> 16) + scaled_ypos) * recip64;
				dV[3]->argb = new_color;
				dV[3]->oargb = floor_lit_color;

				vertcpy(dV[0], &dv0);

				tnl_poly(&next_poly);
			}

			v00 += 2;
			v01 += 2;
			v02 += 2;
		} while (v02 < (numverts + 2));
	}

	global_render_state.in_floor = 0;
	global_render_state.has_bump = 0;
}

pvr_ptr_t pvr_spritecache[MAX_CACHED_SPRITES];
pvr_poly_cxt_t cxt_spritecache[MAX_CACHED_SPRITES];
pvr_poly_hdr_t __attribute__((aligned(32))) hdr_spritecache[MAX_CACHED_SPRITES];

int lump_frame[575 + 310] = {-1};
int used_lumps[575 + 310] = {-1};
int used_lump_idx = 0;
int delidx = 0;

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

	global_render_state.in_things = 1;
	global_render_state.in_floor = 0;
	global_render_state.has_bump = 0;

	vissprite_p = sub->vissprite;
	if (vissprite_p) {
		global_render_state.context_change = 1;

		if (vissprite_p->thing->flags & MF_RENDERLASER) {
			do {
				R_RenderLaser(vissprite_p->thing);
				vissprite_p = vissprite_p->next;
				if (vissprite_p == NULL) {
					break;
				}
				global_render_state.context_change = 1;
			} while (vissprite_p->thing->flags & MF_RENDERLASER);

			global_render_state.context_change = 1;

			if (vissprite_p == NULL) {
				global_render_state.in_things = 0;
				return;
			}
		}

		while (vissprite_p) {
			uint32_t new_color;
			uint32_t thing_lit_color;

			thing = vissprite_p->thing;
			lump = vissprite_p->lump;
			flip = vissprite_p->flip;
			global_render_state.has_bump = 0;
			global_render_state.context_change = 1;

			if (thing->frame & FF_FULLBRIGHT)
				color = 0xffffffff;
			else
				color = lights[vissprite_p->sector->colors[2]].rgba;

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
				if (cmpsize & 1)
					external_pal = 1;
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

				global_render_state.context_change = 1;

				if (menu_settings.VideoFilter) {
					theheader = &pvr_sprite_hdr;
				} else {
					theheader = &pvr_sprite_hdr_nofilter;
				}

				int *hdr_ptr = &((int *)theheader)[2];
				int newhp2v = *hdr_ptr;
				newhp2v = (newhp2v & 0xFFF9DFFF) | (menu_settings.VideoFilter << 12);
				*hdr_ptr = newhp2v;

				init_poly(&next_poly, theheader, 4);

				// pull in each side of sprite by half pixel
				// fix for filtering 'crud' around the edge
				// due to lack of padding
				if (!flip) {
					dV[0]->v->u = dV[1]->v->u = all_u[lump] + halfover1024;
					dV[2]->v->u = dV[3]->v->u = all_u[lump] +
												(((float)spos - 0.5f) * recip1k);
				} else {
					dV[0]->v->u = dV[1]->v->u = all_u[lump] +
												(((float)spos - 0.5f) * recip1k);
					dV[2]->v->u = dV[3]->v->u = all_u[lump] + halfover1024;
				}
				dV[1]->v->v = dV[3]->v->v = all_v[lump] + halfover1024;
				dV[0]->v->v = dV[2]->v->v = all_v[lump] +
											(((float)height - 0.5f) * recip1k);
			} else {
				int lumpoff = lump - 349;
				int cached_index = -1;
				int monster_w = (width + 7) & ~7;
				uint32_t wp2 = np2((uint32_t)monster_w);
				uint32_t hp2 = np2((uint32_t)height);

				sheet = 0;
				global_render_state.context_change = 1;

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
				// 1) explicit flag
				// 2) wasn't enough VRAM for last caching attempt
				// 3) this code has run before, it has been more than 2 seconds
				//		since the last time the cache code was called
				//		and more than 3/4 of the cache slots are used
				//		(MAX_CACHED_SPRITES * 3 / 4) == 192
				// with these conditions, the caching code works well,
				// handles the worst scenes (Absolution) without slowdown
				// (without lights)
				int flush_cond1 = force_filter_flush;
				int flush_cond2 = vram_low;
				int flush_cond3 = (last_flush_frame &&
								   ((NextFrameIdx - last_flush_frame) > 120) &&
								   (used_lump_idx > 192));  
				if (flush_cond1 || flush_cond2 || flush_cond3) {
//					dbgio_printf("sprite eviction %d %d %d\n", flush_cond1, flush_cond2, flush_cond3);
					force_filter_flush = 0;
					vram_low = 0;
#define ALL_SPRITES_INDEX (575 + 310)
					for (unsigned i = 0; i < ALL_SPRITES_INDEX; i++) {
						if (used_lumps[i] != -1) {
							pvr_mem_free(pvr_spritecache[used_lumps[i]]);
							pvr_spritecache[used_lumps[i]] = NULL;
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
//					dbgio_printf("sprite already cached\n");
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
					for (unsigned i = 0; i < ALL_SPRITES_INDEX; i++) {
						// this means we went past everything without evicting
						if (passes) {
							nosprite = 1;
							goto bail_evict;
						}

						// try to help this along by noting if we found
						// the next del idx along the way
						if (used_lumps[i] == (delidx + 1))
							next_lump_delidx = i;

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
								if (delidx == MAX_CACHED_SPRITES)
									delidx = 0;

								// if after increment and/or wrap we are at
								// the starting index, nothing was evictable
								if (delidx == start_delidx)
									passes = 1;

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
					if (delidx == MAX_CACHED_SPRITES)
						delidx = 0;
				}
			bail_evict:
				if (!nosprite) {
					uint32_t sprite_size = wp2 * hp2;
					// vram_low gets set if the sprite will use
					// more than 1/2 available VRAM
					//	with a 256kb reservation for weapon bumpmap
					if (((sprite_size << 1) + 262144) > pvr_mem_available()) {
						nosprite = 1;
						lump_frame[lumpoff] = -1;
						used_lumps[lumpoff] = -1;
						vram_low = 1;
//						dbgio_printf("sprite code saw low vram\n");
						goto bail_pvr_alloc;
					}

					pvr_spritecache[cached_index] =
						pvr_mem_malloc(sprite_size);

//#if RANGECHECK
					if (!pvr_spritecache[cached_index]) {
						I_Error("PVR OOM for RenderThings sprite cache");
					}
//#endif

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

					if (!menu_settings.VideoFilter) {
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

					int *hdr_ptr = &((int *)&hdr_spritecache[cached_index])[2];
					int newhp2v = *hdr_ptr;
					newhp2v = (newhp2v & 0xFFF9DFFF) | (menu_settings.VideoFilter << 12);
					*hdr_ptr = newhp2v;

					init_poly(&next_poly, &hdr_spritecache[cached_index], 4);

					// some of the monsters have "the crud"
					// pull them in by half pixel on each edge
					if (!flip) {
						dV[0]->v->u = dV[1]->v->u = 0.0f + halfover1024;
						dV[2]->v->u = dV[3]->v->u =
							((float)monster_w / (float)wp2) - halfover1024;
					} else {
						dV[0]->v->u = dV[1]->v->u =
							((float)monster_w / (float)wp2) - halfover1024;
						dV[2]->v->u = dV[3]->v->u = halfover1024;
					}
					dV[1]->v->v = dV[3]->v->v = halfover1024;
					dV[0]->v->v = dV[2]->v->v =
						((float)height / (float)hp2) - halfover1024;
				}
			}

		bail_pvr_alloc:
			if (!nosprite) {
#if 0
				float dx, dz;
				if (global_render_state.global_lit) {
					dx = xpos2 - xpos1;
					dz = zpos2 - zpos1;
					// not 100% sure of this condition but it seems to look ok
					if (flip) {
						dx = -dx;
						dz = -dz;
					}
					float ilen = frsqrt((dx * dx) + (dz * dz));

					normx = -dz * ilen;
					normy = 0;
					normz = dx * ilen;
				}
#endif
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
	global_render_state.in_things = 0;
}

#define DC_RED 0xffff0000
#define DC_BLACK 0xff000000
#include "dc/vector.h"
static vector_t __attribute__((aligned(32))) laserverts[6];
static pvr_vertex_t __attribute__((aligned(32))) plv[6];

void R_RenderLaser(mobj_t *thing)
{
	laserdata_t *laserdata = (laserdata_t *)thing->extradata;

	laserverts[0].x = (laserdata->x1 >> 16);
	laserverts[0].y = (laserdata->z1 >> 16);
	laserverts[0].z = -(laserdata->y1 >> 16);
	transform_vector(&laserverts[0]);
	perspdiv_vector(&laserverts[0]);
	plv[0].flags = PVR_CMD_VERTEX;
	plv[0].x = laserverts[0].x;
	plv[0].y = laserverts[0].y;
	plv[0].z = laserverts[0].z;
	plv[0].argb = DC_RED;

	laserverts[1].x = ((laserdata->x1 - laserdata->slopey) >> 16);
	laserverts[1].y = (laserdata->z1 >> 16);
	laserverts[1].z = (-(laserdata->y1 + laserdata->slopex) >> 16);
	transform_vector(&laserverts[1]);
	perspdiv_vector(&laserverts[1]);
	plv[1].flags = PVR_CMD_VERTEX;
	plv[1].x = laserverts[1].x;
	plv[1].y = laserverts[1].y;
	plv[1].z = laserverts[1].z;
	plv[1].argb = DC_BLACK;

	laserverts[2].x = ((laserdata->x2 - laserdata->slopey) >> 16);
	laserverts[2].y = (laserdata->z2 >> 16);
	laserverts[2].z = (-(laserdata->y2 + laserdata->slopex) >> 16);
	transform_vector(&laserverts[2]);
	perspdiv_vector(&laserverts[2]);
	plv[2].flags = PVR_CMD_VERTEX;
	plv[2].x = laserverts[2].x;
	plv[2].y = laserverts[2].y;
	plv[2].z = laserverts[2].z;
	plv[2].argb = DC_BLACK;

	laserverts[3].x = (laserdata->x2 >> 16);
	laserverts[3].y = (laserdata->z2 >> 16);
	laserverts[3].z = -(laserdata->y2 >> 16);
	transform_vector(&laserverts[3]);
	perspdiv_vector(&laserverts[3]);
	plv[3].flags = PVR_CMD_VERTEX_EOL;
	plv[3].x = laserverts[3].x;
	plv[3].y = laserverts[3].y;
	plv[3].z = laserverts[3].z;
	plv[3].argb = DC_RED;

	laserverts[4].x = ((laserdata->x2 + laserdata->slopey) >> 16);
	laserverts[4].y = (laserdata->z2 >> 16);
	laserverts[4].z = (-(laserdata->y2 - laserdata->slopex) >> 16);
	transform_vector(&laserverts[4]);
	perspdiv_vector(&laserverts[4]);
	plv[4].flags = PVR_CMD_VERTEX;
	plv[4].x = laserverts[4].x;
	plv[4].y = laserverts[4].y;
	plv[4].z = laserverts[4].z;
	plv[4].argb = DC_BLACK;

	laserverts[5].x = ((laserdata->x1 + laserdata->slopey) >> 16);
	laserverts[5].y = (laserdata->z1 >> 16);
	laserverts[5].z = (-(laserdata->y1 - laserdata->slopex) >> 16);
	transform_vector(&laserverts[5]);
	perspdiv_vector(&laserverts[5]);
	plv[5].flags = PVR_CMD_VERTEX_EOL;
	plv[5].x = laserverts[5].x;
	plv[5].y = laserverts[5].y;
	plv[5].z = laserverts[5].z;
	plv[5].argb = DC_BLACK;

	// 0 2 3
	// 0 1 2

	laser_triangle(&plv[0],
					&plv[2],
					&plv[3]);

	plv[2].flags = PVR_CMD_VERTEX_EOL;
	laser_triangle(&plv[0],
					&plv[1],
					&plv[2]);

	// 0 3 5
	// 3 4 5
	plv[3].flags = PVR_CMD_VERTEX;
	laser_triangle(&plv[0],
					&plv[3],
					&plv[5]);

	laser_triangle(&plv[3],
					&plv[4],
					&plv[5]);
}

extern pvr_poly_hdr_t __attribute__((aligned(32))) pvr_sprite_hdr_bump;
extern pvr_poly_hdr_t __attribute__((aligned(32))) pvr_sprite_hdr_nofilter_bump;
extern pvr_poly_hdr_t __attribute__((aligned(32))) wepnbump_hdr;
extern pvr_poly_hdr_t __attribute__((aligned(32))) wepndecs_hdr;
extern pvr_poly_hdr_t __attribute__((aligned(32))) wepndecs_hdr_nofilter;

// return 2pi + approximate atan2f(y,x), range-adjusted into [0,2pi]
static float wepn_atan2f(float y, float x)
{
	float res = twopi_i754;
	float abs_y = fabs(y) + 1e-10f; // kludge to prevent 0/0 condition
	float absy_plus_absx = abs_y + fabs(x);
	float inv_absy_plus_absx = frapprox_inverse(absy_plus_absx);
	float angle = halfpi_i754 - copysignf(quarterpi_i754, x);
	float r = (x - copysignf(abs_y, x)) * inv_absy_plus_absx;
	angle += (0.1963f * r * r - 0.9817f) * r;
	res += copysignf(angle, y);
	// adjust the angle into the range (0,2pi)
	if (res > twopi_i754) {
		res -= twopi_i754;
	}
	return res;
}

void R_RenderPSprites(void)
{
	unsigned i, j;
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
	if (gamemap == 33)
		return;

	angle_t angle = viewangle >> ANGLETOFINESHIFT;
	fixed_t dist = 8;

	fixed_t lv_x = (dist * finecosine[angle]) + players[0].mo->x;
	fixed_t lv_y = (dist * finesine[angle]) + players[0].mo->y;

	float px = (lv_x >> 16);
	float py = (players[0].mo->z >> 16);
	float pz = -(lv_y >> 16);

	wepn_verts[0].flags = PVR_CMD_VERTEX;
	wepn_verts[1].flags = PVR_CMD_VERTEX;
	wepn_verts[2].flags = PVR_CMD_VERTEX;
	wepn_verts[3].flags = PVR_CMD_VERTEX_EOL;

	psp = &viewplayer->psprites[0];

	flagtranslucent = (viewplayer->mo->flags & MF_SHADOW) != 0;

	for (i = 0; i < NUMPSPRITES; i++, psp++) {
		global_render_state.has_bump = 0;
		/* a null state means not active */
		if ((state = psp->state) != 0) {
			pvr_vertex_t *vert = wepn_verts;
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

			if (flagtranslucent)
				a1 = 144;
			else
				a1 = psp->alpha;

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
				lump == 964) {
				if (global_render_state.quality == 2)
					global_render_state.has_bump = 1;
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
				wepn_verts[0].z = 4.5;
				wepn_verts[1].z = 4.5;
				wepn_verts[2].z = 4.5;
				wepn_verts[3].z = 4.5;
			} else {
				wepn_verts[0].z = 4.0;
				wepn_verts[1].z = 4.0;
				wepn_verts[2].z = 4.0;
				wepn_verts[3].z = 4.0;
			}

			float avg_dx = 0;
			float avg_dy = 0;
			float avg_dz = 0;
			uint32_t wepn_boargb;

			if (global_render_state.quality) {
				for (j = 0; j < lightidx + 1; j++) {
					float dx = projectile_lights[j].x - px;
					float dy = projectile_lights[j].y - py;
					float dz = projectile_lights[j].z - pz;
					float lr = projectile_lights[j].radius;
					float lightdist;
					vec3f_length(dx, dy, dz, lightdist);

					if (lightdist < lr) {
						float light_scale = (lr - lightdist) / lr;

						applied += 1;

						if (global_render_state.has_bump) {
							avg_dx += dx;
							avg_dy += dy;
							avg_dz += dz;
						}

						if (!zbump) {
							lightingr += (projectile_lights[j].r * light_scale);
							lightingg += (projectile_lights[j].g * light_scale);
							lightingb += (projectile_lights[j].b * light_scale);
						}
					}
				}
			}

			for (j = 0; j < 4; j++) {
				wepn_verts[j].argb = quad_color;
				wepn_verts[j].oargb = quad_light_color;
			}

			if (global_render_state.quality) {
				if (applied) {
					if (quad_light_color) {
						float coord_r =
							(float)((quad_light_color >> 16) & 0xff) * recip255;
						float coord_g =
							(float)((quad_light_color >> 8) & 0xff) * recip255;
						float coord_b =
							(float)(quad_light_color & 0xff) * recip255;

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

						invmrgb = frapprox_inverse(maxrgb);

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

					for (j = 0; j < 4; j++) {
						wepn_verts[j].oargb = projectile_light;
					}

					if (global_render_state.has_bump) {
						float sin_el, cos_el;
						float adxP;
						float adzP;

						float azimuth;
						float elevation;
						float avg_cos = finecosine[angle] * recip64k;
						float avg_sin = finesine[angle] * recip64k;

						vec3f_normalize(avg_dx, avg_dy, avg_dz);

						// elevation above floor
						elevation = halfpi_i754 * fabs(avg_dy);
						if (elevation < quarterpi_i754)
							elevation = quarterpi_i754;

						fsincosr(elevation, &sin_el, &cos_el);
//						sin_el = sinf(elevation);
//						cos_el = cosf(elevation);

						adxP = (-avg_dx * avg_cos) + (avg_dz * avg_sin);
						adzP = (avg_dz * avg_cos) + (avg_dx * avg_sin);

						azimuth = wepn_atan2f(adxP, adzP);

						int K1 = 127;
						int K2 = (int)(sin_el * 128);
						int K3 = (int)(cos_el * 128);
						int Q = (int)(azimuth * 40.584510f);
						//(int)(azimuth * 255.0f / (2.0f * F_PI));

						wepn_boargb = ((int)K1 << 24) |
									  ((int)K2 << 16) |
									  ((int)K3 << 8) |
									  (int)Q;
					}
				}
			}

			x = ((psp->sx >> 16) - SwapShort(((spriteN64_t *)data)->xoffs)) + 160;
			y = ((psp->sy >> 16) - SwapShort(((spriteN64_t *)data)->yoffs)) + 239;

			if (viewplayer->onground) {
				x += (quakeviewx >> 22);
				y += (quakeviewy >> 16);
			}

			x1 = (float)x * RES_RATIO;
			y1 = (float)y * RES_RATIO;
			x2 = x1 + ((float)width2 * RES_RATIO);
			y2 = y1 + ((float)height * RES_RATIO);

			if (lump == 935) {
				u1 = 0.0f * recip64;
				v1 = 0.0f * recip64;
				u2 = 24.0f * recip64;
				v2 = 30.0f * recip64;
			}

			if (lump == 939) {
				u1 = 24.0f * recip64;
				v1 = 0.0f * recip64;
				u2 = 48.0f * recip64;
				v2 = 34.0f * recip64;
			}

			if (lump == 943) {
				u1 = 0.0f * recip64;
				v1 = 30.0f * recip64;
				u2 = 40.0f * recip64;
				v2 = 62.0f * recip64;
			}

			// pull in each side of sprite by half pixel
			// fix for filtering 'crud' around the edge due to lack of padding
			vert->x = x1;
			vert->y = y2;
			vert->u = u1 + halfover1024;
			vert->v = v2 - halfover1024;
			vert++;

			vert->x = x1;
			vert->y = y1;
			vert->u = u1 + halfover1024;
			vert->v = v1 + halfover1024;
			vert++;

			vert->x = x2;
			vert->y = y2;
			vert->u = u2 - halfover1024;
			vert->v = v2 - halfover1024;
			vert++;

			vert->x = x2;
			vert->y = y1;
			vert->u = u2 - halfover1024;
			vert->v = v1 + halfover1024;

			if (global_render_state.has_bump) {
				memcpy(bump_verts, wepn_verts, 4 * sizeof(pvr_vertex_t));

				for (int bi = 0; bi < 4; bi++) {
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

				float bu1, bv1, bu2, bv2;

				bu1 = bv1 = 0.0f;

				// chainsaw
				if (lump == 924) {
					bu2 = 113.0f * recip512;
					bv2 = 82.0f * recip128;
				}
				if (lump == 925) {
					bu1 = 120.0f * recip512;
					bu2 = 233.0f * recip512;
					bv2 = 80.0f * recip128;
				}
				if (lump == 926) {
					bu1 = 240.0f * recip512;
					bu2 = 353.0f * recip512;
					bv2 = 68.0f * recip128;
				}
				if (lump == 927) {
					bu1 = 360.0f * recip512;
					bu2 = 473.0f * recip512;
					bv2 = 68.0f * recip128;
				}

				// fist
				if (lump == 928) {
					// 81 -> 88
					bu2 = 81.0f * recip512;
					bv2 = 43.0f * recip64;
				}
				if (lump == 929) {
					bu1 = 88.0f * recip512;
					// 176 - 7
					bu2 = 169.0f * recip512;
					bv2 = 42.0f * recip64;	
				}
				if (lump == 930) {
					bu1 = 176.0f * recip512;
					// 296 - 7
					bu2 = 289.0f * recip512;
					bv2 = 53.0f * recip128;
				}
				if (lump == 931) {
					bu1 = 296.0f * recip512;
					// 416 - 7
					bu2 = 409.0f * recip512;
					bv2 = 61.0f * recip128;
				}

				// pistol
				if (lump == 932) {
					bu2 = 56.0f * recip256;
					bv2 = 87.0f * recip128;
				}
				if (lump == 933) {
					bu1 = 56.0f * recip256;
					bu2 = 112.0f * recip256;
					bv2 = 97.0f * recip128;
				}
				if (lump == 934) {
					bu1 = 112.0f * recip256;
					// 59 -> 64
					bu2 = 171.0f * recip256;
					bv2 = 96.0f * recip128;
				}

				// shotgun
				if (lump == 936) {
					// 56 - 55 = 1
					bu2 = 55.0f * recip256;
					bv2 = 73.0f * recip128;
				}
				if (lump == 937) {
					bu1 = 56.0f * recip256;
					// 56 + 55 = 111
					bu2 = 111.0f * recip256;
					bv2 = 77.0f * recip128;
				}
				if (lump == 938) {
					bu1 = 112.0f * recip256;
					bu2 = 168.0f * recip256;
					bv2 = 75.0f * recip128;
				}

				// double shotgun
				if (lump == 940) {
					bu2 = 56.0f * recip256;
					bv2 = 63.0f * recip64;  
				}
				if (lump == 941) {
					bu1 = 56.0f * recip256;
					bu2 = 120.0f * recip256;
					bv2 = 61.0f * recip64;
				}
				if (lump == 942) {
					bu1 = 120.0f * recip256;
					bu2 = 184.0f * recip256;
					bv2 = 62.0f * recip64;
				}

				// chaingun
				if (lump == 944) {
					// 112 - 108 4
					bu2 = 112.0f * recip256;
					bv2 = 72.0f * recip128;
				}
				if (lump == 945) {
					bu1 = 128.0f * recip256;
					bu2 = 240.0f * recip256;
					bv2 = 72.0f * recip128;
				}
				if (lump == 946) {
					bu1 = 0.0f * recip256;
					bv1 = 218.0f * recip256;
					bu2 = 32.0f * recip256;
					bv2 = 245.0f * recip256;
				}

				// rocker launcher
				if (lump == 948) {
					// 80 - 78
					bu2 = 78.0f * recip256;
					bv2 = 79.0f * recip128;
				}
				if (lump == 949) {
					bu1 = 80.0f * recip256;
					// 80 + 87
					bu2 = 167.0f * recip256;
					bv2 = 83.0f * recip128;
				}

				// plasma rifle
				if (lump == 954) {
					// 128 - 123 5
					bu2 = 128.0f * recip256;
					bv2 = 83.0f * recip128;
				}
				if (lump == 958) {
					bu1 = 128.0f * recip256;
					// 128 + 123
					bu2 = 256.0f * recip256;
					bv2 = 83.0f * recip128;
				}

				// bfg
				if (lump == 959)
				{
					// 160 - 154
					bu2 = 154.f * recip512;
					bv2 = 79.0f * recip128;
				}
				if (lump == 960) {
					bu1 = 160.f * recip512;
					// 160 + 154
					bu2 = 314.f * recip512;
					bv2 = 76.0f * recip128;
				}

				// laser
				if (lump == 964) {
					// 128 - 1213
					bu2 = 128.0f * recip256;
					bv2 = 90.0f * recip128;
				}

				pvr_list_prim(PVR_LIST_TR_POLY,
							  &wepnbump_hdr,
							  sizeof(pvr_poly_hdr_t));

				bump_verts[0].u = bu1;
				bump_verts[0].v = bv2;
				bump_verts[0].z -= 0.1;

				bump_verts[1].u = bu1;
				bump_verts[1].v = bv1;
				bump_verts[1].z -= 0.1;

				bump_verts[2].u = bu2;
				bump_verts[2].v = bv2;
				bump_verts[2].z -= 0.1;

				bump_verts[3].u = bu2;
				bump_verts[3].v = bv1;
				bump_verts[3].z -= 0.1;

				pvr_list_prim(PVR_LIST_TR_POLY, bump_verts,
							  4 * sizeof(pvr_vertex_t));
			}

			pvr_poly_hdr_t *pspr_diffuse_hdr;
			if (lump == 935 ||
				lump == 939 ||
				lump == 943) {
				if (menu_settings.VideoFilter) {
					pspr_diffuse_hdr = &wepndecs_hdr;
				} else {
					pspr_diffuse_hdr = &wepndecs_hdr_nofilter;
				}
			} else {
				if (menu_settings.VideoFilter) {
					if (global_render_state.has_bump) {
						pspr_diffuse_hdr = &pvr_sprite_hdr_bump;
					} else {
						pspr_diffuse_hdr = &pvr_sprite_hdr;
					}
				}
				else {
					if (global_render_state.has_bump) {
						pspr_diffuse_hdr = &pvr_sprite_hdr_nofilter_bump;
					} else {
						pspr_diffuse_hdr = &pvr_sprite_hdr_nofilter;
					}
				}
			}

			pvr_list_prim(PVR_LIST_TR_POLY, pspr_diffuse_hdr,
						  sizeof(pvr_poly_hdr_t));
			pvr_list_prim(PVR_LIST_TR_POLY, wepn_verts, sizeof(wepn_verts));

			if (global_render_state.has_bump) {
				pvr_list_prim(PVR_LIST_TR_POLY, &flush_hdr,
							  sizeof(pvr_poly_hdr_t));
				pvr_list_prim(PVR_LIST_TR_POLY, wepn_verts, sizeof(wepn_verts));
			}

			global_render_state.has_bump = 0;
		} // if ((state = psp->state) != 0)
	} // for i < numsprites

	global_render_state.has_bump = 0;
	global_render_state.in_floor = 0;
	global_render_state.in_things = 0;
	global_render_state.context_change = 1;
}
