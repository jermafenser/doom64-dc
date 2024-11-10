//Renderer phase 3 - World Rendering Routines
#include "doomdef.h"
#include "r_local.h"

#include <dc/matrix.h>
#include <dc/pvr.h>
#include <math.h>

int do_switch = 0;

int context_change;
int in_things = 0;

extern int brightness;
extern short SwapShort(short dat);
extern int VideoFilter;

extern pvr_poly_cxt_t **tcxt;
extern pvr_poly_cxt_t **tcxt_forbump;

extern pvr_poly_hdr_t pvr_sprite_hdr;
extern pvr_poly_hdr_t pvr_sprite_hdr_nofilter;

extern float *all_u;
extern float *all_v;
extern float *all_u2;
extern float *all_v2;

extern uint8_t *pt;

int has_bump = 0;
int in_floor = 0;

pvr_vertex_t __attribute__((aligned(32))) quad2[4];
pvr_vertex_t __attribute__((aligned(32))) bump_verts[4];

pvr_poly_hdr_t hdr;
pvr_poly_cxt_t cxt;

pvr_poly_hdr_t thdr;

d64Vertex_t *dVTX[4];
d64Triangle_t dT1, dT2;

const float f_piover5 = F_PI / 5.0f;
const float f_piover4 = F_PI / 4.0f;
const float f_piover3 = F_PI / 3.0f;
const float f_piover2 = F_PI / 2.0f;
const float f_2pi = F_PI * 2.0f;
const float f_3piover2 = (3.0f * F_PI / 2.0f);

static float bat_piover4 = F_PI / 4.0f;

static float bump_atan2f(float y, float x) {
	float abs_y = fabs(y) + 1e-10f; // kludge to prevent 0/0 condition
//	float r = (x - cpsgn(&abs_y, &x)) / (abs_y + fabs(x));
	float r = (x - copysignf(abs_y, x)) / (abs_y + fabs(x));
//	float angle = f_piover2 - cpsgn(&bat_piover4, &x);
	float angle = f_piover2 - copysignf(bat_piover4, x);

	angle += (0.1963f * r * r - 0.9817f) * r;
//	return cpsgn(&angle, &y);
	return copysignf(angle, y);
}

int vm1, vm2;

static inline int get_vm(d64Poly_t *poly) {
	int nvert = poly->n_verts;
	int rvm = (nvert == 4) ? 16 : 0;
	
	for(int i=0;i<nvert;i++) {
		d64ListVert_t *vi = &poly->dVerts[i];
		rvm |= ((vi->v->z >= -vi->w) << i);
	}
	
	return rvm;
}

/* 
credit to Kazade / glDC code for my clipping implementation
https://github.com/Kazade/GLdc/blob/572fa01b03b070e8911db43ca1fb55e3a4f8bdd5/GL/platforms/software.c#L140
*/
uint32_t blend_color(float t, uint32_t v1c, uint32_t v2c)
{
	//a,r,g,b
	float v1[4];
	float v2[4];
	float d[4];
	const float invt = 1.0f - t;

	v1[0] = (float)((v1c >> 24) & 0xff);
	v1[1] = (float)((v1c >> 16) & 0xff);
	v1[2] = (float)((v1c >> 8) & 0xff);
	v1[3] = (float)((v1c)&0xff);

	v2[0] = (float)((v2c >> 24) & 0xff);
	v2[1] = (float)((v2c >> 16) & 0xff);
	v2[2] = (float)((v2c >> 8) & 0xff);
	v2[3] = (float)((v2c)&0xff);

	d[0] = invt * v1[0] + t * v2[0];
	d[1] = invt * v1[1] + t * v2[1];
	d[2] = invt * v1[2] + t * v2[2];
	d[3] = invt * v1[3] + t * v2[3];

	return D64_PVR_PACK_COLOR((uint8_t)d[0], (uint8_t)d[1], (uint8_t)d[2],
				 (uint8_t)d[3]);
}

static void clip(d64ListVert_t *v1, d64ListVert_t *v2, d64ListVert_t *out)
{
	const float d0 = v1->w + v1->v->z;
	const float d1 = v2->w + v2->v->z;
	const float t = (fabs(d0) * frsqrt((d1 - d0) * (d1 - d0))) + 0.000001f;
	const float invt = 1.0f - t;

	out->w = invt * v1->w + t * v2->w;

	out->v->x = invt * v1->v->x + t * v2->v->x;
	out->v->y = invt * v1->v->y + t * v2->v->y;
	out->v->z = invt * v1->v->z + t * v2->v->z;

	out->v->u = invt * v1->v->u + t * v2->v->u;
	out->v->v = invt * v1->v->v + t * v2->v->v;

	out->v->argb = blend_color(t, v1->v->argb, v2->v->argb);
	out->v->oargb = blend_color(t, v1->v->oargb, v2->v->oargb);
}

uint32_t lit_color(uint32_t c, int ll)
{
//	if (ll < 16) ll = 16;
	
	uint8_t r = (uint8_t)((((c >> 16) & 0xff) * ll) >> 8);
	uint8_t g = (uint8_t)((((c >>  8) & 0xff) * ll) >> 8);
	uint8_t b = (uint8_t)(( (c        & 0xff) * ll) >> 8);
	uint8_t a = (uint8_t)(  (c >> 24) & 0xff);

	uint32_t rc = D64_PVR_PACK_COLOR(a, r, g, b);

	return rc;
}

// when lighting was introduced, skies with clouds were getting lit
//   by high-flying projectiles
// now this is just used to keep from lighting the transparent liquid floor
extern int dont_color;
// the current number of lights - 1
extern int lightidx;
// array of lights generated in r_phase1.c
extern projectile_light_t __attribute__((aligned(32))) projectile_lights[NUM_DYNLIGHT];

uint32_t boargb;

static void R_TransformProjectileLights(void)
{
	for (int i = 0; i < lightidx + 1; i++) {
		float tmp = projectile_lights[i].z;
		projectile_lights[i].z = -projectile_lights[i].y;
		projectile_lights[i].y = tmp;
	}
}

// unit normal vector for currently rendering primitive
d64Vertex_t norm;
// eventually replace norm.v.x , norm.v.y, norm.v.z with 
float norm_x, norm_y, norm_z;

// bump-mapping parameters and variables
pvr_poly_hdr_t bumphdr;

float center_x, center_y, center_z;

const float scaled_inv2pi = 255.0f / (2.0f * F_PI);
#define doomangletoQ(x) (((float)((x) >> ANGLETOFINESHIFT) / (float)FINEANGLES))

// this is roughly the DMA equivalent of getting a dr target
void init_poly(d64Poly_t *poly, pvr_poly_hdr_t *diffuse_hdr, int n_verts) {
	void *tr_tail = (void *)pvr_vertbuf_tail(PVR_LIST_TR_POLY);
	size_t hdr_copy_size = context_change * sizeof(pvr_poly_hdr_t);

	poly->n_verts = n_verts;

	poly->hdr[0] = (pvr_poly_hdr_t *)tr_tail;
	memcpy(poly->hdr[0], diffuse_hdr, hdr_copy_size);

#if !HYBRID
	if (has_bump) {
		void *op_tail = (void *)pvr_vertbuf_tail(PVR_LIST_OP_POLY);
		poly->hdr[1] = (pvr_poly_hdr_t *)op_tail;
		memcpy(poly->hdr[1], &bumphdr, hdr_copy_size);
	}
#endif

	tr_tail += hdr_copy_size;

	for (int i=0;i<5;i++) {
		poly->dVerts[i].v = (pvr_vertex_t *)tr_tail;
		tr_tail += sizeof(pvr_vertex_t);
	}
}

d64Poly_t next_poly;

void light_wall_hasbump(d64Poly_t *p);
void light_wall_nobump(d64Poly_t *p);
void light_plane_hasbump(d64Poly_t *p);
void light_plane_nobump(d64Poly_t *p);
void light_thing(d64Poly_t *p);

int lf_idx(void) {
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

void (*light_func[5]) (d64Poly_t *p) = {
	light_plane_nobump,
	light_plane_hasbump,
	light_wall_nobump,
	light_wall_hasbump,
	light_thing
};

extern pvr_poly_cxt_t flush_cxt;
extern pvr_poly_hdr_t flush_hdr;

#if HYBRID
extern pvr_dr_state_t dr_state;
#endif

extern void draw_pvr_line_hdr(d64Vertex_t *v1, d64Vertex_t *v2, int color);

void clip_poly(d64Poly_t *p) {
	int vm;
	int v2pd = p->n_verts;

	// set vert flags to defaults for poly type
	p->dVerts[0].v->flags = p->dVerts[1].v->flags = PVR_CMD_VERTEX;
	p->dVerts[3].v->flags = p->dVerts[4].v->flags = PVR_CMD_VERTEX_EOL;

	if (__builtin_expect((v2pd == 4),1)) {
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

	int light_cond = (!dont_color) && (lightidx + 1);

	if (__builtin_expect(light_cond,0)) {
		(*light_func[lf_idx()])(p);
	}

	for (int i = 0;i < v2pd; i++) {
		transform_lvert(&p->dVerts[i]);
	}

	vm = get_vm(p);

	// 0 or 16 means nothing visible, this happens
	if (!(vm & ~16)) {
		return;
	}
	
	switch (vm) {
		// quad all visible
		case 31:
			break;

		// tri all visible
		case 7:
			break;

		// tri only 0 visible
		case 1:
			clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[1]);
			clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[2]);
			break;

		// tri only 1 visible
		case 2:
			clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[0]);
			clip(&p->dVerts[1], &p->dVerts[2], &p->dVerts[2]);
			break;

		// tri 0 + 1 visible
		case 3:
			v2pd = 4;
			clip(&p->dVerts[1], &p->dVerts[2], &p->dVerts[3]);
			clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[2]);
			p->dVerts[2].v->flags = PVR_CMD_VERTEX;
			break;

		// tri only 2 visible
		case 4:
			clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[0]);
			clip(&p->dVerts[1], &p->dVerts[2], &p->dVerts[1]);
			break;

		// tri 0 + 2 visible
		case 5:
			v2pd = 4;
			clip(&p->dVerts[1], &p->dVerts[2], &p->dVerts[3]);
			clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[1]);
			p->dVerts[2].v->flags = PVR_CMD_VERTEX;
			break;

		// tri 1 + 2 visible
		case 6:
			v2pd = 4;
			p->dVerts[3].w = p->dVerts[2].w;
			memcpy(p->dVerts[3].v, p->dVerts[2].v, sizeof(pvr_vertex_t)); 
			clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[2]);
			clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[0]);
			p->dVerts[2].v->flags = PVR_CMD_VERTEX;
			break;

		// quad only 0 visible
		case 17:
			v2pd = 3;
			clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[1]);
			clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[2]);
			p->dVerts[2].v->flags = PVR_CMD_VERTEX_EOL;
			break;

		// quad only 1 visible
		case 18:
			v2pd = 3;
			clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[0]);
			clip(&p->dVerts[1], &p->dVerts[3], &p->dVerts[2]);
			p->dVerts[2].v->flags = PVR_CMD_VERTEX_EOL;
			break;

		// quad 0 + 1 visible
		case 19:
			clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[2]);
			clip(&p->dVerts[1], &p->dVerts[3], &p->dVerts[3]);
			break;

		// quad only 2 visible
		case 20:
			v2pd = 3;
			clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[0]);
			clip(&p->dVerts[2], &p->dVerts[3], &p->dVerts[1]);
			p->dVerts[2].v->flags = PVR_CMD_VERTEX_EOL;
			break;

		// quad 0 + 2 visible
		case 21:
			clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[1]);
			clip(&p->dVerts[2], &p->dVerts[3], &p->dVerts[3]);
			break;

		// quad 1 + 2 visible is not possible
		// it is a middle diagonal
		// case 22:

		// quad 0 + 1 + 2 visible
		case 23:
			v2pd = 5;
			clip(&p->dVerts[2], &p->dVerts[3], &p->dVerts[4]);
			clip(&p->dVerts[1], &p->dVerts[3], &p->dVerts[3]);
			p->dVerts[3].v->flags = PVR_CMD_VERTEX;
			break;

		// quad only 3 visible
		case 24:
			v2pd = 3;
			clip(&p->dVerts[1], &p->dVerts[3], &p->dVerts[0]);
			clip(&p->dVerts[2], &p->dVerts[3], &p->dVerts[2]);
			p->dVerts[1].w = p->dVerts[3].w;
			memcpy(p->dVerts[1].v, p->dVerts[3].v, sizeof(pvr_vertex_t)); 
			p->dVerts[1].v->flags = PVR_CMD_VERTEX;
			p->dVerts[2].v->flags = PVR_CMD_VERTEX_EOL;
			break;

		// quad 0 + 3 visible is not possible
		// it is the other middle diagonal
		// case 25:
		
		// quad 1 + 3 visible
		case 26:
			clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[0]);
			clip(&p->dVerts[2], &p->dVerts[3], &p->dVerts[2]);
			break;

		// quad 0 + 1 + 3 visible
		case 27:
			v2pd = 5;
			clip(&p->dVerts[2], &p->dVerts[3], &p->dVerts[4]);
			clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[2]);
			p->dVerts[3].v->flags = PVR_CMD_VERTEX;
			break;

		// quad 2 + 3 visible
		case 28:
			clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[0]);
			clip(&p->dVerts[1], &p->dVerts[3], &p->dVerts[1]);
			break;

		// quad 0 + 2 + 3 visible
		case 29:
			v2pd = 5;
			p->dVerts[4].w = p->dVerts[3].w;
			memcpy(p->dVerts[4].v, p->dVerts[3].v, sizeof(pvr_vertex_t)); 
			clip(&p->dVerts[1], &p->dVerts[3], &p->dVerts[3]);
			clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[1]);
			p->dVerts[3].v->flags = PVR_CMD_VERTEX;
			p->dVerts[4].v->flags = PVR_CMD_VERTEX_EOL;
			break;

		// quad 1 + 2 + 3 visible
		case 30:
			v2pd = 5;
			p->dVerts[4].w = p->dVerts[2].w;
			memcpy(p->dVerts[4].v, p->dVerts[2].v, sizeof(pvr_vertex_t)); 		
			p->dVerts[4].v->flags = PVR_CMD_VERTEX_EOL;
			clip(&p->dVerts[0], &p->dVerts[2], &p->dVerts[2]);
			clip(&p->dVerts[0], &p->dVerts[1], &p->dVerts[0]);		
			p->dVerts[3].v->flags = PVR_CMD_VERTEX;
			break;
		
		default:
			return;
			break;
	}

	for (int i = 0; i < v2pd; i++) {
		perspdiv_lv(p->dVerts[i].v, p->dVerts[i].w);
	}

#if 0
#if HYBRID
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
#endif

	uint32_t hdr_size = (context_change * sizeof(pvr_poly_hdr_t)); 
	uint32_t amount = hdr_size + (v2pd * sizeof(pvr_vertex_t));
	if (__builtin_expect(has_bump, 1)) {
#if !HYBRID
		uintptr_t tr_liststart = (uintptr_t)p->hdr[0];
		uintptr_t tr_vertstart = tr_liststart + hdr_size;
		uintptr_t op_liststart = (uintptr_t)p->hdr[1];
		uintptr_t op_vertstart = op_liststart + hdr_size;
		pvr_vertex_t *bmverts = (pvr_vertex_t *)op_vertstart;

		memcpy((void*)op_vertstart, (void*)tr_vertstart, amount - hdr_size);

		for (int i=0;i<v2pd;i++) {
			bmverts[i].argb = 0xff000000;
			bmverts[i].oargb = boargb;
		}

		// update bump/OP list pointer
		pvr_vertbuf_written(PVR_LIST_OP_POLY, amount);
#else
		if (context_change) {
			void *hdr1 = pvr_dr_target(dr_state);
			memcpy4(hdr1, &bumphdr, sizeof(pvr_poly_hdr_t));
			pvr_dr_commit(hdr1);
		}

		uintptr_t tr_liststart = (uintptr_t)p->hdr[0];
		uintptr_t tr_vertstart = tr_liststart + hdr_size;
		pvr_vertex_t *tr_vert = (pvr_vertex_t *)(tr_vertstart);

		for (int i=0;i<v2pd;i++) {
			pvr_vertex_t *vert = pvr_dr_target(dr_state);
			memcpy4(vert, (void*)&tr_vert[i], sizeof(pvr_vertex_t));
			vert->argb = 0xff000000;
			vert->oargb = boargb;
			pvr_dr_commit(vert);
		}
#endif	
	}

	// update diffuse/TR list pointer
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

#if !HYBRID
	if (context_change) {
		pvr_list_prim(list, hdr, sizeof(pvr_poly_hdr_t));
	}
	pvr_list_prim(list, v0, sizeof(pvr_vertex_t));
	pvr_list_prim(list, v1, sizeof(pvr_vertex_t));
	pvr_list_prim(list, v2, sizeof(pvr_vertex_t));
#else
	if (context_change) {
		pvr_vertex_t *hdr1 = pvr_dr_target(dr_state);
		memcpy4(hdr1, hdr, sizeof(pvr_poly_hdr_t));
		pvr_dr_commit(hdr1);
	}
	pvr_vertex_t *vert = pvr_dr_target(dr_state);
	memcpy4(vert, v0, sizeof(pvr_vertex_t));
	pvr_dr_commit(vert);
	vert = pvr_dr_target(dr_state);
	memcpy4(vert, v1, sizeof(pvr_vertex_t));
	pvr_dr_commit(vert);
	vert = pvr_dr_target(dr_state);
	memcpy4(vert, v2, sizeof(pvr_vertex_t));
	pvr_dr_commit(vert);
#endif

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

void R_RenderAll(void)
{
	subsector_t *sub;

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
				do_switch = 1;
				R_RenderSwitch(seg, pic,
					       b_ceilingheight + rowoffs + 48,
					       thingcolor);
				do_switch = 0;
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
				do_switch = 1;
				R_RenderSwitch(seg, pic,
					       b_floorheight + rowoffs - 16,
					       thingcolor);
				do_switch = 0;
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
		do_switch = 1;
		R_RenderSwitch(seg, pic, m_bottom + rowoffs + 48, thingcolor);
		do_switch = 0;
	}
}

float last_width_inv = 1.0f / 64.0f;
float last_height_inv = 1.0f / 64.0f;

void *P_CachePvrTexture(int i, int tag);

extern pvr_ptr_t **tex_txr_ptr;

extern pvr_ptr_t *bump_txr_ptr;
extern pvr_poly_cxt_t *bumpcxt;

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

	uint32_t tl_col = lit_color(tdc_col, ll);
	uint32_t bl_col = lit_color(bdc_col, ll);

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
				curcxt = &tcxt[texnum][texture & 15];
			} else {
				curcxt = &tcxt_forbump[texnum][texture & 15];
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
				bumpcxt[texnum].txr.uv_flip =
					tcxt[texnum][texture & 15].txr.uv_flip;
				bumpcxt[texnum].txr.filter =
					tcxt[texnum][texture & 15].txr.filter;
				pvr_poly_compile(&bumphdr, &bumpcxt[texnum]);
			}

			globallump = texture;
			globalcm = (cms | cmt);

			pvr_poly_compile(&thdr, curcxt);
		}

		// if texture v is flipped, rotate the default "light"
		// direction by 180 degrees
		if (has_bump) {
			if (!globalcm) {
				boargb = 0x7f5a00c0;
			} else if (globalcm & 1) {
				boargb = 0x7f5a0040;
			}			
		}

		v1 = seg->v1;
		v2 = seg->v2;

		norm.v.x = seg->nx;
		norm.v.y = 0;
		norm.v.z = seg->nz;

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

					uint32_t ucol = blend_color(((i  )*ystepsize), 
												tdc_col, bdc_col);
					uint32_t lcol = blend_color(((i+1)*ystepsize),
												tdc_col, bdc_col);

					uint32_t ulcol = blend_color(((i  )*ystepsize),
												tl_col, bl_col);
					uint32_t llcol = blend_color(((i+1)*ystepsize),
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

					clip_poly(&next_poly);
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
				
				uint32_t ucol = blend_color(((i  )*stepsize),
											tdc_col, bdc_col);
				uint32_t lcol = blend_color(((i+1)*stepsize),
											tdc_col, bdc_col);

				uint32_t ulcol = blend_color(((i  )*stepsize),
											tl_col, bl_col);
				uint32_t llcol = blend_color(((i+1)*stepsize),	
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

				clip_poly(&next_poly);
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

				clip_poly(&next_poly);
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

			clip_poly(&next_poly);
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
	uint32_t switch_lit_color = lit_color(new_color, frontsector->lightlevel);

	in_floor = 0;
	in_things = 0;
	has_bump = 0;

	P_CachePvrTexture(texture, PU_CACHE);

	context_change = 1;

	if (bump_txr_ptr[texture]) {
		has_bump = 1;
		boargb = 0x7f5a00c0;
	}


	if (has_bump) {
		curcxt = &tcxt[texture][0];
	} else {
		curcxt = &tcxt_forbump[texture][0];
	}

	if (!VideoFilter) {
		curcxt->txr.filter = PVR_FILTER_BILINEAR;
	} else {
		curcxt->txr.filter = PVR_FILTER_NONE;
	}
		
	curcxt->txr.uv_flip = PVR_UVFLIP_NONE;

	if (has_bump) {
		bumpcxt[texture].txr.uv_flip = PVR_UVFLIP_NONE;
		bumpcxt[texture].txr.filter =
			tcxt[texture][0].txr.filter;
		pvr_poly_compile(&bumphdr, &bumpcxt[texture]);
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

	norm.v.x = seg->nx;
	norm.v.y = 0;
	norm.v.z = seg->nz;

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

	clip_poly(&next_poly);

	has_bump = 0;
	context_change = 1;
}

extern int floor_split_override;
extern fvertex_t **split_verts;
extern int dont_bump;
static pvr_vertex_t __attribute__ ((aligned(32))) dv0;
void R_RenderPlane(leaf_t *leaf, int numverts, int zpos, int texture, int xpos,
		   int ypos, int color, int ceiling, int lightlevel,
		   int alpha)
{
	d64ListVert_t *dV[3];
	vertex_t *vrt;
	fixed_t x;
	fixed_t y;
	int idx;
	int v00, v01, v02;
	short stu, stv;
	float tu, tv;
	uint32_t new_color = D64_PVR_REPACK_COLOR_ALPHA(color, alpha);
	uint32_t floor_lit_color = lit_color(new_color, lightlevel);

	int texnum = (texture >> 4) - firsttex;
	leaf_t *lf = leaf;

	has_bump = 0;
	// dont_bump gets set in automap
	// so we don't do pointless bump-mapping for the top-down view
	if (bump_txr_ptr[texnum] && !dont_bump) {
		has_bump = 1;
		float angle = doomangletoQ(viewangle);
		boargb = 0x7f5a5a00 | (int)(angle * 255);
	}

	in_floor = 1 + ceiling;

	if (texture != globallump || globalcm != -1) {
		pvr_poly_cxt_t *curcxt;

		P_CachePvrTexture(texnum, PU_CACHE);

		if (has_bump) {
			curcxt = &tcxt[texnum][texture & 15];

			bumpcxt[texnum].txr.uv_flip = PVR_UVFLIP_NONE;
			if (!VideoFilter) {
				bumpcxt[texnum].txr.filter =
					PVR_FILTER_BILINEAR;
			} else {
				bumpcxt[texnum].txr.filter = PVR_FILTER_NONE;
			}

			pvr_poly_compile(&bumphdr, &bumpcxt[texnum]);
		} else {
			curcxt = &tcxt_forbump[texnum][texture & 15];
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
		norm.v.x = 0.0f;
		norm.v.z = 0.0f;
		norm.v.y = (ceiling ? -1.0f : 1.0f);
	}

	if(ceiling) {
		dV[0] = &next_poly.dVerts[2];
		dV[1] = &next_poly.dVerts[1];
		dV[2] = &next_poly.dVerts[0];		
	} else {
		dV[0] = &next_poly.dVerts[0];
		dV[1] = &next_poly.dVerts[1];
		dV[2] = &next_poly.dVerts[2];		
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
		if (test_dist > 640) goto too_far_away;

		float scaled_xpos = (float)xpos / 65536.0f;
		float scaled_ypos = (float)ypos / 65536.0f;
		float xdiv64 = x / 64.0f;
		float ydiv64 = y / 64.0f;

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
			tu = (i1x + scaled_xpos) / 64.0f - xdiv64;
			tv = (i1y + scaled_ypos) / 64.0f - ydiv64;
			dV[0]->v->x = i1x;
			dV[0]->v->y = (float)(zpos);
			dV[0]->v->z = -i1y;
			dV[0]->v->u = tu;
			dV[0]->v->v = -tv;

			tu = (s12->x + scaled_xpos) / 64.0f - xdiv64;
			tv = (s12->y + scaled_ypos) / 64.0f - ydiv64;
			dV[1]->v->x = s12->x;
			dV[1]->v->y = (float)(zpos);
			dV[1]->v->z = -s12->y;
			dV[1]->v->u = tu;
			dV[1]->v->v = -tv;

			tu = (s31->x + scaled_xpos) / 64.0f - xdiv64;
			tv = (s31->y + scaled_ypos) / 64.0f - ydiv64;
			dV[2]->v->x = s31->x;
			dV[2]->v->y = (float)(zpos);
			dV[2]->v->z = -s31->y;
			dV[2]->v->u = tu;
			dV[2]->v->v = -tv;

			dV[0]->v->argb = new_color;
			dV[0]->v->oargb = floor_lit_color;
			dV[1]->v->argb = new_color;
			dV[1]->v->oargb = floor_lit_color;
			dV[2]->v->argb = new_color;
			dV[2]->v->oargb = floor_lit_color;
			clip_poly(&next_poly);

			init_poly(&next_poly, &thdr, 3);
			tu = (s12->x + scaled_xpos) / 64.0f - xdiv64;
			tv = (s12->y + scaled_ypos) / 64.0f - ydiv64;
			dV[0]->v->x = s12->x;
			dV[0]->v->y = (float)(zpos);
			dV[0]->v->z = -s12->y;
			dV[0]->v->u = tu;
			dV[0]->v->v = -tv;
			
			tu = (i2x + scaled_xpos) / 64.0f - xdiv64;
			tv = (i2y + scaled_ypos) / 64.0f - ydiv64;
			dV[1]->v->x = i2x;
			dV[1]->v->y = (float)(zpos);
			dV[1]->v->z = -i2y;
			dV[1]->v->u = tu;
			dV[1]->v->v = -tv;

			tu = (s23->x + scaled_xpos) / 64.0f - xdiv64;
			tv = (s23->y + scaled_ypos) / 64.0f - ydiv64;
			dV[2]->v->x = s23->x;
			dV[2]->v->y = (float)(zpos);
			dV[2]->v->z = -s23->y;
			dV[2]->v->u = tu;
			dV[2]->v->v = -tv;

			dV[0]->v->argb = new_color;
			dV[0]->v->oargb = floor_lit_color;
			dV[1]->v->argb = new_color;
			dV[1]->v->oargb = floor_lit_color;
			dV[2]->v->argb = new_color;
			dV[2]->v->oargb = floor_lit_color;
			clip_poly(&next_poly);

			init_poly(&next_poly, &thdr, 3);
			tu = (s23->x + scaled_xpos) / 64.0f - xdiv64;
			tv = (s23->y + scaled_ypos) / 64.0f - ydiv64;
			dV[0]->v->x = s23->x;
			dV[0]->v->y = (float)(zpos);
			dV[0]->v->z = -s23->y;
			dV[0]->v->u = tu;
			dV[0]->v->v = -tv;

			tu = (i3x + scaled_xpos) / 64.0f - xdiv64;
			tv = (i3y + scaled_ypos) / 64.0f - ydiv64;
			dV[1]->v->x = i3x;
			dV[1]->v->y = (float)(zpos);
			dV[1]->v->z = -i3y;
			dV[1]->v->u = tu;
			dV[1]->v->v = -tv;

			tu = (s31->x + scaled_xpos) / 64.0f - xdiv64;
			tv = (s31->y + scaled_ypos) / 64.0f - ydiv64;
			dV[2]->v->x = s31->x;
			dV[2]->v->y = (float)(zpos);
			dV[2]->v->z = -s31->y;
			dV[2]->v->u = tu;
			dV[2]->v->v = -tv;

			dV[0]->v->argb = new_color;
			dV[0]->v->oargb = floor_lit_color;
			dV[1]->v->argb = new_color;
			dV[1]->v->oargb = floor_lit_color;
			dV[2]->v->argb = new_color;
			dV[2]->v->oargb = floor_lit_color;
			clip_poly(&next_poly);

			init_poly(&next_poly, &thdr, 3);
			tu = (s12->x + scaled_xpos) / 64.0f - xdiv64;
			tv = (s12->y + scaled_ypos) / 64.0f - ydiv64;
			dV[0]->v->x = s12->x;
			dV[0]->v->y = (float)(zpos);
			dV[0]->v->z = -s12->y;
			dV[0]->v->u = tu;
			dV[0]->v->v = -tv;

			tu = (s23->x + scaled_xpos) / 64.0f - xdiv64;
			tv = (s23->y + scaled_ypos) / 64.0f - ydiv64;
			dV[1]->v->x = s23->x;
			dV[1]->v->y = (float)(zpos);
			dV[1]->v->z = -s23->y;
			dV[1]->v->u = tu;
			dV[1]->v->v = -tv;

			tu = (s31->x + scaled_xpos) / 64.0f - xdiv64;
			tv = (s31->y + scaled_ypos) / 64.0f - ydiv64;
			dV[2]->v->x = s31->x;
			dV[2]->v->y = (float)(zpos);
			dV[2]->v->z = -s31->y;
			dV[2]->v->u = tu;
			dV[2]->v->v = -tv;

			dV[0]->v->argb = new_color;
			dV[0]->v->oargb = floor_lit_color;
			dV[1]->v->argb = new_color;
			dV[1]->v->oargb = floor_lit_color;
			dV[2]->v->argb = new_color;
			dV[2]->v->oargb = floor_lit_color;
			clip_poly(&next_poly);
		}

		numverts--;

		if (idx < numverts) {
			v00 = idx + 0;
			v01 = idx + 1;
			v02 = idx + 2;

			// only triangle is out of the way
			// do all quads from here
			do {
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

				iu[0] = (ix[0] + scaled_xpos)/64.0f - xdiv64;
				iv[0] = -((-iy[0] + scaled_ypos)/64.0f - ydiv64);
				iu[1] = (ix[1] + scaled_xpos)/64.0f - xdiv64;
				iv[1] = -((-iy[1] + scaled_ypos)/64.0f - ydiv64);
				iu[2] = (ix[2] + scaled_xpos)/64.0f - xdiv64;
				iv[2] = -((-iy[2] + scaled_ypos)/64.0f - ydiv64);

				s12 = &subsplits[s00];
				s23 = &subsplits[s00+1];
				s31 = &subsplits[s00+2];
				s30 = &subsplits[s00+3];
				s10 = &subsplits[s00+4];

				su[0] = (s12->x + scaled_xpos)/64.0f - xdiv64;
				sv[0] = -((s12->y + scaled_ypos)/64.0f - ydiv64);
				su[1] = (s23->x + scaled_xpos)/64.0f - xdiv64;
				sv[1] = -((s23->y + scaled_ypos)/64.0f - ydiv64);
				su[2] = (s31->x + scaled_xpos)/64.0f - xdiv64;
				sv[2] = -((s31->y + scaled_ypos)/64.0f - ydiv64);
				su[3] = (s30->x + scaled_xpos)/64.0f - xdiv64;
				sv[3] = -((s30->y + scaled_ypos)/64.0f - ydiv64);
				su[4] = (s10->x + scaled_xpos)/64.0f - xdiv64;
				sv[4] = -((s10->y + scaled_ypos)/64.0f - ydiv64);

				init_poly(&next_poly,&thdr,3);
				dV[0]->v->x = ix[0];
				dV[0]->v->y = (float)(zpos);
				dV[0]->v->z = iy[0];
				dV[0]->v->u = iu[0];
				dV[0]->v->v = iv[0];

				dV[1]->v->x = s12->x;
				dV[1]->v->y = (float)(zpos);
				dV[1]->v->z = -s12->y;
				dV[1]->v->u = su[0];
				dV[1]->v->v = sv[0];

				dV[2]->v->x = s31->x;
				dV[2]->v->y = (float)(zpos);
				dV[2]->v->z = -s31->y;
				dV[2]->v->u = su[2];
				dV[2]->v->v = sv[2];

				dV[0]->v->argb = new_color;
				dV[0]->v->oargb = floor_lit_color;
				dV[1]->v->argb = new_color;
				dV[1]->v->oargb = floor_lit_color;
				dV[2]->v->argb = new_color;
				dV[2]->v->oargb = floor_lit_color;
				clip_poly(&next_poly);

				init_poly(&next_poly,&thdr,3);
				dV[0]->v->x = s12->x;
				dV[0]->v->y = (float)(zpos);
				dV[0]->v->z = -s12->y;
				dV[0]->v->u = su[0];
				dV[0]->v->v = sv[0];

				dV[1]->v->x = ix[1];
				dV[1]->v->y = (float)(zpos);
				dV[1]->v->z = iy[1];
				dV[1]->v->u = iu[1];
				dV[1]->v->v = iv[1];

				dV[2]->v->x = s23->x;
				dV[2]->v->y = (float)(zpos);
				dV[2]->v->z = -s23->y;
				dV[2]->v->u = su[1];
				dV[2]->v->v = sv[1];

				dV[0]->v->argb = new_color;
				dV[0]->v->oargb = floor_lit_color;
				dV[1]->v->argb = new_color;
				dV[1]->v->oargb = floor_lit_color;
				dV[2]->v->argb = new_color;
				dV[2]->v->oargb = floor_lit_color;
				clip_poly(&next_poly);
				
				init_poly(&next_poly,&thdr,3);
				dV[0]->v->x = s23->x;
				dV[0]->v->y = (float)(zpos);
				dV[0]->v->z = -s23->y;
				dV[0]->v->u = su[1];
				dV[0]->v->v = sv[1];

				dV[1]->v->x = ix[2];
				dV[1]->v->y = (float)(zpos);
				dV[1]->v->z = iy[2];
				dV[1]->v->u = iu[2];
				dV[1]->v->v = iv[2];

				dV[2]->v->x = s31->x;
				dV[2]->v->y = (float)(zpos);
				dV[2]->v->z = -s31->y;
				dV[2]->v->u = su[2];
				dV[2]->v->v = sv[2];

				dV[0]->v->argb = new_color;
				dV[0]->v->oargb = floor_lit_color;
				dV[1]->v->argb = new_color;
				dV[1]->v->oargb = floor_lit_color;
				dV[2]->v->argb = new_color;
				dV[2]->v->oargb = floor_lit_color;
				clip_poly(&next_poly);

				init_poly(&next_poly,&thdr,3);
				dV[0]->v->x = s12->x;
				dV[0]->v->y = (float)(zpos);
				dV[0]->v->z = -s12->y;
				dV[0]->v->u = su[0];
				dV[0]->v->v = sv[0];

				dV[1]->v->x = s23->x;
				dV[1]->v->y = (float)(zpos);
				dV[1]->v->z = -s23->y;
				dV[1]->v->u = su[1];
				dV[1]->v->v = sv[1];

				dV[2]->v->x = s31->x;
				dV[2]->v->y = (float)(zpos);
				dV[2]->v->z = -s31->y;
				dV[2]->v->u = su[2];
				dV[2]->v->v = sv[2];

				dV[0]->v->argb = new_color;
				dV[0]->v->oargb = floor_lit_color;
				dV[1]->v->argb = new_color;
				dV[1]->v->oargb = floor_lit_color;
				dV[2]->v->argb = new_color;
				dV[2]->v->oargb = floor_lit_color;
				clip_poly(&next_poly);

				init_poly(&next_poly,&thdr,3);
				dV[0]->v->x = ix[2];
				dV[0]->v->y = (float)(zpos);
				dV[0]->v->z = iy[2];
				dV[0]->v->u = iu[2];
				dV[0]->v->v = iv[2];

				dV[1]->v->x = s30->x;
				dV[1]->v->y = (float)(zpos);
				dV[1]->v->z = -s30->y;
				dV[1]->v->u = su[3];
				dV[1]->v->v = sv[3];

				dV[2]->v->x = s31->x;
				dV[2]->v->y = (float)(zpos);
				dV[2]->v->z = -s31->y;
				dV[2]->v->u = su[2];
				dV[2]->v->v = sv[2];

				dV[0]->v->argb = new_color;
				dV[0]->v->oargb = floor_lit_color;
				dV[1]->v->argb = new_color;
				dV[1]->v->oargb = floor_lit_color;
				dV[2]->v->argb = new_color;
				dV[2]->v->oargb = floor_lit_color;
				clip_poly(&next_poly);

				init_poly(&next_poly,&thdr,3);
				dV[0]->v->x = s30->x;
				dV[0]->v->y = (float)(zpos);
				dV[0]->v->z = -s30->y;
				dV[0]->v->u = su[3];
				dV[0]->v->v = sv[3];

				memcpy(dV[1]->v, &dv0, sizeof(pvr_vertex_t));

				dV[2]->v->x = s10->x;
				dV[2]->v->y = (float)(zpos);
				dV[2]->v->z = -s10->y;
				dV[2]->v->u = su[4];
				dV[2]->v->v = sv[4];

				dV[0]->v->argb = new_color;
				dV[0]->v->oargb = floor_lit_color;
				dV[1]->v->argb = new_color;
				dV[1]->v->oargb = floor_lit_color;
				dV[2]->v->argb = new_color;
				dV[2]->v->oargb = floor_lit_color;
				clip_poly(&next_poly);

				init_poly(&next_poly,&thdr,3);
				dV[0]->v->x = s10->x;
				dV[0]->v->y = (float)(zpos);
				dV[0]->v->z = -s10->y;
				dV[0]->v->u = su[4];
				dV[0]->v->v = sv[4];

				dV[1]->v->x = ix[0];
				dV[1]->v->y = (float)(zpos);
				dV[1]->v->z = iy[0];
				dV[1]->v->u = iu[0];
				dV[1]->v->v = iv[0];

				dV[2]->v->x = s31->x;
				dV[2]->v->y = (float)(zpos);
				dV[2]->v->z = -s31->y;
				dV[2]->v->u = su[2];
				dV[2]->v->v = sv[2];

				dV[0]->v->argb = new_color;
				dV[0]->v->oargb = floor_lit_color;
				dV[1]->v->argb = new_color;
				dV[1]->v->oargb = floor_lit_color;
				dV[2]->v->argb = new_color;
				dV[2]->v->oargb = floor_lit_color;
				clip_poly(&next_poly);

				init_poly(&next_poly,&thdr,3);
				dV[0]->v->x = s31->x;
				dV[0]->v->y = (float)(zpos);
				dV[0]->v->z = -s31->y;
				dV[0]->v->u = su[2];
				dV[0]->v->v = sv[2];

				dV[1]->v->x = s30->x;
				dV[1]->v->y = (float)(zpos);
				dV[1]->v->z = -s30->y;
				dV[1]->v->u = su[3];
				dV[1]->v->v = sv[3];

				dV[2]->v->x = s10->x;
				dV[2]->v->y = (float)(zpos);
				dV[2]->v->z = -s10->y;
				dV[2]->v->u = su[4];
				dV[2]->v->v = sv[4];

				dV[0]->v->argb = new_color;
				dV[0]->v->oargb = floor_lit_color;
				dV[1]->v->argb = new_color;
				dV[1]->v->oargb = floor_lit_color;
				dV[2]->v->argb = new_color;
				dV[2]->v->oargb = floor_lit_color;
				clip_poly(&next_poly);

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

		idx = 2;

		vrt1 = lf[1].vertex;
		memcpy(dV[0]->v, &dv0, sizeof(pvr_vertex_t));

		dV[1]->v->x = ((float)vrt1->x / 65536.0f);
		dV[1]->v->y = (float)(zpos);
		dV[1]->v->z = -((float)vrt1->y / 65536.0f);

		stu = (((vrt1->x + xpos) >> FRACBITS) - x);
		stv = -(((vrt1->y + ypos) >> FRACBITS) - y);
		tu = (float)stu / 64.0f;
		tv = (float)stv / 64.0f;

		dV[1]->v->u = tu;
		dV[1]->v->v = tv;
		dV[1]->v->argb = new_color;
		dV[1]->v->oargb = floor_lit_color;

		vrt2 = lf[2].vertex;

		dV[2]->v->x = ((float)vrt2->x / 65536.0f);
		dV[2]->v->y = (float)(zpos);
		dV[2]->v->z = -((float)vrt2->y / 65536.0f);

		stu = (((vrt2->x + xpos) >> FRACBITS) - x);
		stv = -(((vrt2->y + ypos) >> FRACBITS) - y);
		tu = (float)stu / 64.0f;
		tv = (float)stv / 64.0f;

		dV[2]->v->u = tu;
		dV[2]->v->v = tv;
		dV[2]->v->argb = new_color;
		dV[2]->v->oargb = floor_lit_color;

		clip_poly(&next_poly);
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

			stu = (((vrt1->x + xpos) >> FRACBITS) - x);
			stv = -(((vrt1->y + ypos) >> FRACBITS) - y);
			tu = (float)stu / 64.0f;
			tv = (float)stv / 64.0f;

			dV[0]->v->x = ((float)vrt1->x / 65536.0f);
			dV[0]->v->y = (float)(zpos);
			dV[0]->v->z = -((float)vrt1->y / 65536.0f);

			dV[0]->v->u = tu;
			dV[0]->v->v = tv;
			dV[0]->v->argb = new_color;
			dV[0]->v->oargb = floor_lit_color;

			stu = (((vrt2->x + xpos) >> FRACBITS) - x);
			stv = -(((vrt2->y + ypos) >> FRACBITS) - y);
			tu = (float)stu / 64.0f;
			tv = (float)stv / 64.0f;

			dV[1]->v->x = ((float)vrt2->x / 65536.0f);
			dV[1]->v->y = (float)(zpos);
			dV[1]->v->z = -((float)vrt2->y / 65536.0f);

			dV[1]->v->u = tu;
			dV[1]->v->v = tv;
			dV[1]->v->argb = new_color;
			dV[1]->v->oargb = floor_lit_color;

			stu = (((vrt3->x + xpos) >> FRACBITS) - x);
			stv = -(((vrt3->y + ypos) >> FRACBITS) - y);
			tu = (float)stu / 64.0f;
			tv = (float)stv / 64.0f;

			dV[2]->v->x = ((float)vrt3->x / 65536.0f);
			dV[2]->v->y = (float)(zpos);
			dV[2]->v->z = -((float)vrt3->y / 65536.0f);

			dV[2]->v->u = tu;
			dV[2]->v->v = tv;
			dV[2]->v->argb = new_color;
			dV[2]->v->oargb = floor_lit_color;

			clip_poly(&next_poly);

			init_poly(&next_poly, &thdr, 3);	

			stu = (((vrt1->x + xpos) >> FRACBITS) - x);
			stv = -(((vrt1->y + ypos) >> FRACBITS) - y);
			tu = (float)stu / 64.0f;
			tv = (float)stv / 64.0f;

			dV[0]->v->x = ((float)vrt1->x / 65536.0f);
			dV[0]->v->y = (float)(zpos);
			dV[0]->v->z = -((float)vrt1->y / 65536.0f);

			dV[0]->v->u = tu;
			dV[0]->v->v = tv;
			dV[0]->v->argb = new_color;
			dV[0]->v->oargb = floor_lit_color;

			stu = (((vrt3->x + xpos) >> FRACBITS) - x);
			stv = -(((vrt3->y + ypos) >> FRACBITS) - y);
			tu = (float)stu / 64.0f;
			tv = (float)stv / 64.0f;

			dV[1]->v->x = ((float)vrt3->x / 65536.0f);
			dV[1]->v->y = (float)(zpos);
			dV[1]->v->z = -((float)vrt3->y / 65536.0f);

			dV[1]->v->u = tu;
			dV[1]->v->v = tv;
			dV[1]->v->argb = new_color;
			dV[1]->v->oargb = floor_lit_color;
	
			memcpy4(dV[2]->v, &dv0, sizeof(pvr_vertex_t));

			clip_poly(&next_poly);
		
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

static inline uint32_t np2(uint32_t v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

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
			thing_lit_color = lit_color(new_color, vissprite_p->sector->lightlevel);

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
				xpos1 = (xx - (width * viewsin)) >> 16;
				xpos2 = (xx) >> 16;

				yy = thing->y -
				     (SwapShort(((spriteN64_t *)data)->xoffs) * viewcos);
				zpos1 = -(yy + (width * viewcos)) >> 16;
				zpos2 = -(yy) >> 16;
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
				int troowid = (width + 7) & ~7;
				uint32_t wp2 = np2((uint32_t)troowid);
				uint32_t hp2 = np2((uint32_t)height);

				sheet = 0;
				context_change = 1;

				if (external_pal && thing->info->palette) {
					void *newlump;
					int newlumpnum;
					char *lumpname = W_GetNameForNum(lump);

					if (lumpname[0] == 'T') {
						// troo; [450,
						lumpname[0] = 'N';
						lumpname[1] = 'I';
						lumpname[2] = 'T';
						lumpname[3] = 'E';
					} else if (lumpname[0] == 'S') {
						// sarg; [349,394]
						lumpname[1] = 'P';
						lumpname[2] = 'E';
						lumpname[3] = 'C';
					} else if (lumpname[0] == 'B') {
						// boss
						lumpname[1] = 'A';
						lumpname[2] = 'R';
						lumpname[3] = 'O';
					} else if (lumpname[0] == 'P') {
						if (lumpname[1] == 'O') {
							// poss; [
							lumpname[0] = 'Z';
							lumpname[2] = 'M';
							lumpname[3] = 'B';
						} else {
							// play; [398-447]
							if (thing->info->palette == 1) {
								lumpname[2] = 'Y';
								lumpname[3] = '1';
							} else {
								lumpname[2] = 'Y';
								lumpname[3] = '2';
							}
						}
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
					//dbgio_printf("SPRITE CACHE FLUSH\n");
					//dbgio_printf("filter ; sprite > 4x avail; cache slots ;\n");
					//dbgio_printf("%d ; %d ; %d ;\n", flush_cond1, flush_cond2, flush_cond3);
					pvr_wait_ready();
					force_filter_flush = 0;
					vram_low = 0;
#define ALL_SPRITES_INDEX (575 + 310)
					for (int i = 0; i < ALL_SPRITES_INDEX; i++) {
						if (used_lumps[i] != -1) {
							pvr_mem_free(
								pvr_spritecache[used_lumps
										 [i]]);
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
						I_Error("PVR OOM for RenderThings sprite cache\n");
					}

					pvr_poly_cxt_txr(&cxt_spritecache[cached_index],
						PVR_LIST_TR_POLY,
						PVR_TXRFMT_PAL8BPP | 
						PVR_TXRFMT_8BPP_PAL(0) | 
						PVR_TXRFMT_TWIDDLED,
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

					if (!flip) {
						dV[0]->v->u = dV[1]->v->u = 0.0f;
						dV[2]->v->u = dV[3]->v->u = 
							(float)troowid / (float)wp2;
					} else {
						dV[0]->v->u = dV[1]->v->u = 
							(float)troowid / (float)wp2;
						dV[2]->v->u = dV[3]->v->u = 0.0f;
					}
					dV[1]->v->v = dV[3]->v->v = 0.0f;
					dV[0]->v->v = dV[2]->v->v = (float)height / (float)hp2;
				}
			}

bail_pvr_alloc:
			if (!nosprite) {
				float dx = xpos2 - xpos1;
				float dz = zpos2 - zpos1;
				float ilen = frsqrt((dx * dx) + (dz * dz));

				norm.v.x = -dz * ilen;
				norm.v.y = 0;
				norm.v.z = dx * ilen;

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

				clip_poly(&next_poly);
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
				quad_light_color = lit_color(
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
					float T = fabs(f_piover2 * bay);

					if (T < (F_PI * 0.25f)) {
						T = (F_PI * 0.25f);
					}

					fsincosr(T, &ts, &tc);
					int K1 = 127;
					int K2 = (int)(ts * 128);
					int K3 = (int)(tc * 128);
					int lq = (int)(BQ * scaled_inv2pi);

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
				memcpy(&bump_verts[0], &quad2[0], 4 * sizeof(pvr_vertex_t));

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
			pvr_list_prim(PVR_LIST_TR_POLY, &quad2, sizeof(quad2));	

			if (has_bump) {
				pvr_list_prim(PVR_LIST_TR_POLY, &flush_hdr,
					sizeof(pvr_poly_hdr_t));
				pvr_list_prim(PVR_LIST_TR_POLY, &quad2, sizeof(quad2));
			}

			has_bump = 0;
		} // if ((state = psp->state) != 0)
	} // for i < numsprites

	has_bump = 0;
	in_floor = 0;
	in_things = 0;
	context_change = 1;
}
