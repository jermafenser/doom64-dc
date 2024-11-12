#include "doomdef.h"
#include "r_local.h"

#include <dc/matrix.h>
#include <dc/pvr.h>
#include <math.h>

extern int has_bump;
extern int in_floor;

extern const float inv64;
extern const float inv128;
extern const float inv255;
extern const float inv1024;
extern const float halfinv1024;
extern const float inv65536;

extern const float f_piover5;
extern const float f_piover4;
extern const float f_piover3;
extern const float f_piover2;
extern const float f_2pi;
extern const float f_3piover2;

extern float center_x;
extern float center_y;
extern float center_z;

extern const float inv_fineangles;

extern d64Vertex_t norm;

extern int dont_color;
extern int lightidx;

// array of lights generated in r_phase1.c
extern projectile_light_t __attribute__((aligned(32))) projectile_lights[NUM_DYNLIGHT];
extern uint32_t boargb;
extern int in_things;

static float bat_piover4 = F_PI / 4.0f;

static float bump_atan2f(float y, float x) {
	float abs_y = fabs(y) + 1e-10f; // kludge to prevent 0/0 condition
	float r = (x - copysignf(abs_y, x)) / (abs_y + fabs(x));
	float angle = (F_PI * 0.5f) - copysignf(bat_piover4, x);
	angle += (0.1963f * r * r - 0.9817f) * r;
	return copysignf(angle, y);
}

static float avg_dx = 0.0f;
static float avg_dy = 0.0f;
static float avg_dz = 0.0f;
static int bump_applied = 0;

const int bumpyint = 127;
const int K1 = 255 - bumpyint;

static void assign_lightcolor(d64ListVert_t *v, int v_i) {
	pvr_vertex_t *coord;
	float coord_r;
	float coord_g;
	float coord_b;
	float lightingr;
	float lightingg;
	float lightingb;

	coord = v->v;

	coord_r =
		(float)((coord->oargb >> 16) & 0xff) / 255;

	coord_g =
		(float)((coord->oargb >> 8) & 0xff) / 255;

	coord_b =
		(float)(coord->oargb & 0xff) / 255;

	lightingr = v->r;
	lightingg = v->g;
	lightingb = v->b;

	// blend projectile light with dynamic sector light
	lightingr += coord_r;
	lightingg += coord_g;
	lightingb += coord_b;

	// scale blended light down
	// clamping individual components gives incorrect colors
	float maxrgb = 1.0f; 
	if (lightingr > maxrgb) maxrgb = lightingr;
	if (lightingg > maxrgb) maxrgb = lightingg;
	if (lightingb > maxrgb) maxrgb = lightingb;
	float invmrgb = frapprox_inverse(maxrgb);
	lightingr *= invmrgb;
	lightingg *= invmrgb;
	lightingb *= invmrgb;

	const int component_intensity = 112;

	// any contribution from projectile lights
	// we overwrite the vertex oargb with the new blended color
	coord->oargb = 0xff000000 | 
					((int)(lightingr * component_intensity) << 16) |
					((int)(lightingg * component_intensity) << 8) |
					((int)(lightingb * component_intensity));
}

static void light_vert_hasbump(d64ListVert_t *v, int light_i, int v_i) {
	float lightdist;
	float lrdiff;

	float dx = projectile_lights[light_i].x - v->v->x;
	float dy = projectile_lights[light_i].y - v->v->y;
	float dz = projectile_lights[light_i].z - v->v->z;
	float lr = projectile_lights[light_i].radius;

	vec3f_length(dx, dy, dz, lightdist);

	lrdiff = lr - lightdist;

	if (lrdiff > 0) {
		float light_scale = lrdiff / lr;

		float lightingr = projectile_lights[light_i].r * light_scale;
		float lightingg = projectile_lights[light_i].g * light_scale;
		float lightingb = projectile_lights[light_i].b * light_scale;

		v->r += lightingr;
		v->g += lightingg;
		v->b += lightingb;
	}
}

static void light_vert_nobump(d64ListVert_t *v, int light_i, int v_i) {
	float lightdist;
	float lrdiff;

	float dx = projectile_lights[light_i].x - v->v->x;
	float dy = projectile_lights[light_i].y - v->v->y;
	float dz = projectile_lights[light_i].z - v->v->z;
	float lr = projectile_lights[light_i].radius;

	vec3f_length(dx, dy, dz, lightdist);

	lrdiff = lr - lightdist;

	if (lrdiff > 0) {
		float light_scale = lrdiff / lr;
		float lightingr = projectile_lights[light_i].r * light_scale;
		float lightingg = projectile_lights[light_i].g * light_scale;
		float lightingb = projectile_lights[light_i].b * light_scale;

		v->r += lightingr;
		v->g += lightingg;
		v->b += lightingb;
	}
}

void light_wall_hasbump(d64Poly_t *p)
{
	center_x = (p->dVerts[0].v->x + p->dVerts[3].v->x) * 0.5f;
	center_y = (p->dVerts[0].v->y + p->dVerts[3].v->y) * 0.5f;
	center_z = (p->dVerts[0].v->z + p->dVerts[3].v->z) * 0.5f;
	
	avg_dx = 0.0f;
	avg_dy = 0.0f;
	avg_dz = 0.0f;
	bump_applied = 0;

	for (int i = 0; i < lightidx + 1; i++) {
		float dotprod;
		float lightdist;
		float lr;
		float lrdiff;
		float dx = projectile_lights[i].x - center_x;
		float dy = projectile_lights[i].y - center_y;
		float dz = projectile_lights[i].z - center_z;

		vec3f_dot(dx, dy, dz, norm.v.x, norm.v.y, norm.v.z, dotprod);

		if (dotprod < 0) {
			continue;
		}

		lr = projectile_lights[i].radius;
		vec3f_length(dx, dy, dz, lightdist);
		lrdiff = lr - lightdist;
		if (lrdiff > 0) {
			float light_scale = (lrdiff / lr) * 1.1875f;

			avg_dx += dx * light_scale;
			avg_dy += dy * light_scale;
			avg_dz += dz * light_scale;

			bump_applied += 1;
		}

		light_vert_hasbump(&p->dVerts[0], i, 0);
		light_vert_hasbump(&p->dVerts[1], i, 1);
		light_vert_hasbump(&p->dVerts[2], i, 2);
		light_vert_hasbump(&p->dVerts[3], i, 3);
	}

	assign_lightcolor(&p->dVerts[0], 0);
	assign_lightcolor(&p->dVerts[1], 1);
	assign_lightcolor(&p->dVerts[2], 2);
	assign_lightcolor(&p->dVerts[3], 3);

	if (bump_applied) {
		float T;
		float BQ;
		int K2;
		int K3;
		int lq;
		float bax, bay, baz;
		float ts, tc;
		float avg_cos, avg_sin;
		float adxP;
		float adyP;
		float adzP;
		float lenxy2;

		BQ = F_PI;

		bax = avg_dx;
		bay = avg_dy;
		baz = avg_dz;

		vec3f_normalize(bax, bay, baz);

		adyP = -bay;

		//angle of 2 relative to 1= atan2(v2.y,v2.x) - atan2(v1.y,v1.x)
		//avg_theta	=	atan2f(norm.v.x,norm.v.z) - atan2(0,1);
		//	|->		=	atan2f(nx,nz) - 0;
		avg_cos = norm.v.z; //cosf(avg_theta);
		avg_sin = norm.v.x; //sinf(avg_theta);
		// x is "y"
		// x' = x cos - z sin
		//adxP = (bax * avg_cos) - (baz * avg_sin);
		//adxP = -adxP;
		adxP = (-bax * avg_cos) + (baz * avg_sin);
		// z is "x"
		// z' = z cos + x sin
		adzP = (baz * avg_cos) + (bax * avg_sin);

		BQ += bump_atan2f(adyP, adxP);

		vec3f_length(adxP, adyP, 0.0f, lenxy2);
		T = fabs(bump_atan2f(adzP, lenxy2));

		if (T > (F_PI * 0.5f)) {
			T = F_PI - T;
		}

		if (T < (F_PI * 0.25f)) {
			T = (F_PI * 0.25f);
		}

		fsincosr(T, &ts, &tc);

		K2 = (int)(ts * bumpyint);
		K3 = (int)(tc * bumpyint);
		lq = (int)(BQ * 255.0f / (2.0f * F_PI));

		boargb = ((int)K1 << 24) | ((int)K2 << 16) | ((int)K3 << 8) | (int)lq;
	}
}

void light_wall_nobump(d64Poly_t *p)
{
	center_x = (p->dVerts[0].v->x + p->dVerts[3].v->x) * 0.5f;
	center_y = (p->dVerts[0].v->y + p->dVerts[3].v->y) * 0.5f;
	center_z = (p->dVerts[0].v->z + p->dVerts[3].v->z) * 0.5f;

	for (int i = 0; i < lightidx + 1; i++) {
		float dx = projectile_lights[i].x - center_x;
		float dy = projectile_lights[i].y - center_y;
		float dz = projectile_lights[i].z - center_z;

		float dotprod;
		vec3f_dot(dx, dy, dz, norm.v.x, norm.v.y, norm.v.z, dotprod);

		if (dotprod < 0) {
			continue;
		}

		light_vert_nobump(&p->dVerts[0], i, 0);
		light_vert_nobump(&p->dVerts[1], i, 1);
		light_vert_nobump(&p->dVerts[2], i, 2);
		light_vert_nobump(&p->dVerts[3], i, 3);
	}

	assign_lightcolor(&p->dVerts[0], 0);
	assign_lightcolor(&p->dVerts[1], 1);
	assign_lightcolor(&p->dVerts[2], 2);
	assign_lightcolor(&p->dVerts[3], 3);
}

void light_thing(d64Poly_t *p)
{
	for (int i = 0; i < lightidx + 1; i++) {
		// I don't bother with doing center point light
		// sprites are small
		light_vert_nobump(&p->dVerts[0], i, 0);
		light_vert_nobump(&p->dVerts[1], i, 1);
		light_vert_nobump(&p->dVerts[2], i, 2);
		light_vert_nobump(&p->dVerts[3], i, 3);
	}

	assign_lightcolor(&p->dVerts[0], 0);
	assign_lightcolor(&p->dVerts[1], 1);
	assign_lightcolor(&p->dVerts[2], 2);
	assign_lightcolor(&p->dVerts[3], 3);
}

void light_plane_hasbump(d64Poly_t *p)
{
	center_x = (p->dVerts[0].v->x + p->dVerts[1].v->x +
		  p->dVerts[2].v->x) *
		 0.333333f;
	center_y = (p->dVerts[0].v->y + p->dVerts[1].v->y +
		  p->dVerts[2].v->y) *
		 0.333333f;
	center_z = (p->dVerts[0].v->z + p->dVerts[1].v->z +
		  p->dVerts[2].v->z) *
		0.333333f;

	avg_dx = 0.0f;
	avg_dy = 0.0f;
	avg_dz = 0.0f;
	bump_applied = 0;

	for (int i = 0; i < lightidx + 1; i++) {
		int visible;
		float lightdist;
		float lrdiff;
		float dx = projectile_lights[i].x - center_x;
		float dy = projectile_lights[i].y - center_y;
		float dz = projectile_lights[i].z - center_z;
		float lr = projectile_lights[i].radius;

		if (in_floor == 1) {
			visible = dy >= 0;
		} else {
			visible = dy <= 0;
		}

		if (!visible) {
			continue;
		}

		vec3f_length(dx, dy, dz, lightdist);

		lrdiff = lr - lightdist;
		if (lrdiff > 0) {
			float light_scale = (lrdiff / lr) * 1.1875f;

			avg_dx += dx * light_scale;
			avg_dy += dy * light_scale;
			avg_dz += dz * light_scale;

			bump_applied += 1;
			
		}

		light_vert_hasbump(&p->dVerts[0], i, 0);
		light_vert_hasbump(&p->dVerts[1], i, 1);
		light_vert_hasbump(&p->dVerts[2], i, 2);
	}

	assign_lightcolor(&p->dVerts[0], 0);
	assign_lightcolor(&p->dVerts[1], 1);
	assign_lightcolor(&p->dVerts[2], 2);

	if (bump_applied) {
		float T;
		float BQ;
		int K2;
		int K3;
		int lq;
		float bax, bay, baz;
		float ts, tc;
		float adxP;
		float adzP;

		BQ = F_PI;

		bax = avg_dx;
		bay = avg_dy;
		baz = avg_dz;

		vec3f_normalize(bax, bay, baz);

		adxP = -bax;
		adzP = baz;

		BQ += bump_atan2f(adzP, adxP);

		float angle = doomangletoQ(viewangle);

		BQ += angle;

		if (BQ > (F_PI * 2.0f)) {
			BQ -= (F_PI * 2.0f);
		}

		// elevation above floor
		T = fabs((F_PI * 0.5f) * bay);

		if (T < (F_PI * 0.25f)) {
			T = (F_PI * 0.25f);
		}

		fsincosr(T, &ts, &tc);

		K2 = (int)(ts * bumpyint);
		K3 = (int)(tc * bumpyint);
		lq = (int)(BQ * 255.0f / (2.0f * F_PI));

		boargb = ((int)K1 << 24) | ((int)K2 << 16) | ((int)K3 << 8) | (int)lq;
	}
}

void light_plane_nobump(d64Poly_t *p)
{
	center_x = (p->dVerts[0].v->x + p->dVerts[1].v->x +
		  p->dVerts[2].v->x) *
		 0.333333f;
	center_y = (p->dVerts[0].v->y + p->dVerts[1].v->y +
		  p->dVerts[2].v->y) *
		 0.333333f;
	center_z = (p->dVerts[0].v->z + p->dVerts[1].v->z +
		  p->dVerts[2].v->z) *
		0.333333f;

	for (int i = 0; i < lightidx + 1; i++) {
		int visible;
		float dy = projectile_lights[i].y - center_y;

		if (in_floor == 1) {
			visible = dy >= 0;
		} else {
			visible = dy <= 0;
		}

		if (!visible) {
			continue;
		}

		light_vert_nobump(&p->dVerts[0], i, 0);
		light_vert_nobump(&p->dVerts[1], i, 1);
		light_vert_nobump(&p->dVerts[2], i, 2);
	}

	assign_lightcolor(&p->dVerts[0], 0);
	assign_lightcolor(&p->dVerts[1], 1);
	assign_lightcolor(&p->dVerts[2], 2);
}