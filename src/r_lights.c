#include "doomdef.h"
#include "r_local.h"

#include <dc/matrix.h>
#include <dc/pvr.h>
#include <math.h>

extern int in_floor;

extern float center_x;
extern float center_y;
extern float center_z;

extern float normx, normy, normz;

extern int dont_color;
extern int lightidx;

// array of lights generated in r_phase1.c
extern projectile_light_t __attribute__((aligned(32))) projectile_lights[NUM_DYNLIGHT];
// packed bumpmap parameters
extern uint32_t boargb;

static float bump_atan2f(float y, float x)
{
	float abs_y = fabs(y) + 1e-10f; // kludge to prevent 0/0 condition
	float invbotr = frapprox_inverse(abs_y + fabs(x));
	float r = (x - copysignf(abs_y, x)) * invbotr; // / (abs_y + fabs(x));
	float angle = (F_PI * 0.5f) - copysignf(F_PI * 0.25f, x);
	angle += (0.1963f * r * r - 0.9817f) * r;
	return copysignf(angle, y);
}

const int bumpyint = 127;
const int K1 = 255 - bumpyint;

static void assign_lightcolor(d64ListVert_t *v)
{
	if (v->lit) {
		const float component_intensity = 112.0f;
		float maxrgb = 1.0f; 
		float invmrgb;
		uint32_t cocol = v->v->oargb;

		float lightingr =
			(float)((cocol >> 16) & 0xff) * 0.003921f;

		float lightingg =
			(float)((cocol >> 8) & 0xff) * 0.003921f;

		float lightingb =
			(float)(cocol & 0xff) * 0.003921f;

		// blend projectile light with dynamic sector light
		lightingr += v->r;
		lightingg += v->g;
		lightingb += v->b;

		// scale blended light down
		// clamping individual components gives incorrect colors
		if (lightingr > maxrgb) maxrgb = lightingr;
		if (lightingg > maxrgb) maxrgb = lightingg;
		if (lightingb > maxrgb) maxrgb = lightingb;

		invmrgb = frapprox_inverse(maxrgb) * component_intensity;

		lightingr *= invmrgb;
		lightingg *= invmrgb;
		lightingb *= invmrgb;

		// any contribution from projectile lights
		// we overwrite the vertex oargb with the new blended color
		v->v->oargb = 0xff000000 | 
					((int)(lightingr) << 16) |
					((int)(lightingg) << 8) |
					((int)(lightingb));
	}
}


// calculate light intensity on array of vertices
static void light_vert(d64ListVert_t *v, const projectile_light_t *l, int c)
{
	float light_dist;
	float light_distrad_diff;
	float light_rad = l->radius;
	float inverse_lightrad = frapprox_inverse(light_rad);
	//1.0f / light_rad;

	// for every vertex in input array
	for (int i = 0; i < c; i++) {
		// calculate direction vector from input light to current vertex
		float dx = l->x - v->v->x;
		float dy = l->y - v->v->y;
		float dz = l->z - v->v->z;

		// magnitude of light direction vector
		vec3f_length(dx, dy, dz, light_dist);

		light_distrad_diff = light_rad - light_dist;

		// light distance is less than light radius
		if (light_distrad_diff > 0) {
			// linear attentuation of light components
			float light_scale = light_distrad_diff * inverse_lightrad;

			float lightingr = l->r * light_scale;
			float lightingg = l->g * light_scale;
			float lightingb = l->b * light_scale;

			// accumulate light contributions in vertex
			v->r += lightingr;
			v->g += lightingg;
			v->b += lightingb;

			// indicate any light was applied to this vertex
			v->lit = 1;
		}

		v++;
	}
}

// calculates per-vertex light contributions and normal mapping parameters
// for a Doom wall polygon
void light_wall_hasbump(d64Poly_t *p)
{
	// set if any light is close enough to center of wall to "light" it
	// we consider those lights as contributing to light direction vector
	int bump_applied = 0;

	// average light direction vector
	float avg_ldx = 0.0f;
	float avg_ldy = 0.0f;
	float avg_ldz = 0.0f;

	// 3d center of wall
	center_x = (p->dVerts[0].v->x + p->dVerts[3].v->x) * 0.5f;
	center_y = (p->dVerts[0].v->y + p->dVerts[3].v->y) * 0.5f;
	center_z = (p->dVerts[0].v->z + p->dVerts[3].v->z) * 0.5f;

	// for every dynamic light that was generated this frame
	for (unsigned i = 0; i < lightidx + 1; i++) {
		float dotprod;
		float lightdist;
		float lr;
		float invlr;
		float lrdiff;
		// calculate direction vector between light and center of wall
		float dx = projectile_lights[i].x - center_x;
		float dy = projectile_lights[i].y - center_y;
		float dz = projectile_lights[i].z - center_z;

		// light direction isn't normalized
		// just need sign of dotprod, so that is ok
		vec3f_dot(dx, dy, dz, normx, normy, normz, dotprod);

		// light is on correct side of wall
		if (dotprod >= 0.0f) {
			lr = projectile_lights[i].radius;
			invlr = frapprox_inverse(lr);

			// this is the reason we don't want light direction normalized
			// we need the magnitude of the light direction now
			vec3f_length(dx, dy, dz, lightdist);
			lrdiff = lr - lightdist;
			// distance from surface to light is less than ligut radius
			// this is our final condition for a light to contribute to
			// average light direction vector for normal mapping
			if (lrdiff > 0) {
				// scale the light direction vector down
				// use linear attenuation based on distance
				// but then make it a little stronger
				// gives more of a "pop" on screen
				float light_scale = (lrdiff * invlr) * 1.2f;

				// the names implied they are averaged
				// but really they are just accumulated
				// and once accumulation is done
				// it gets normalized
				avg_ldx += dx * light_scale;
				avg_ldy += dy * light_scale;
				avg_ldz += dz * light_scale;

				// indicate a light contributed to direction vector
				bump_applied = 1;
			}

			// calculate per-vertex light contribution from current light
			light_vert(p->dVerts, &projectile_lights[i], 4);
		}
	}

	// for every vertex in wall poly
	for (unsigned i = 0; i < 4; i++) {
		// combine per-vertex dynamic light with per-vertex static sector lighting
		assign_lightcolor(&p->dVerts[i]);
	}

	// the following is a simplifcation of calculating the light direction
	// (elevation and azimuth angles) for the wall
	// because walls in Doom are always perfectly vertical,
	// there is no need for tangent/bitangent calculations
	// we can just rotate the wall and the light position in the x,z plane
	// so they are oriented with (x,y) plane with surface normal (0,0,1)
	// and the calculations are simpler
	if (bump_applied) {
		// light direction vector after orienting to surface normal
		float rotated_ldx;
		float rotated_ldy;
		float rotated_ldz;

		// bump-mapping parameters
		// elevation (height angle over surface) (aka T)
		float elevation;
		// azimuth (rotation angle around surface) (aka Q)
		float azimuth;
		// (sin(elevation) * bumpiness) * 255
		int K2;
		// (cos(elevation) * bumpiness) * 255
		int K3;
		// (azimuth / 2pi) * 255
		int Q;
		// sin / cos of T
		float sin_el;
		float cos_el;

		// 2d length of light direction vector along surface plane
		float ld_xy_len;

		// normalize avg light direction vector over surface
		vec3f_normalize(avg_ldx, avg_ldy, avg_ldz);

		rotated_ldy = -avg_ldy;

		// wall theta is rotation angle of surface unit normal relative to (0,0,1)
		// for two points v1, 21
		// angle of v2 relative to v1 = atan2(v2.y,v2.x) - atan2(v1.y,v1.x)
		//
		// z is "v.x"
		// x is "v.y"
		// wall_theta	=	atan2f(normx,normz) - atan2(0,1);
		//		|->		=	atan2f(nx,nz) - 0;
		//			|->	=	atan(sinf(wall_theta) / cosf(wall_theta))
		// sinf(wall_theta) = normx
		// cosf(wall_theta) = normz

		// 2d rotation in x,z plane by angle -(wall_theta)
		// we flip x so negative is left, positive is right
		// operating in un-transformed world space
		rotated_ldx = (-avg_ldx * normz) + (avg_ldz * normx);
		rotated_ldz = (avg_ldz * normz) + (avg_ldx * normx);

#if 0
		if (globalcm & 1) {
			rotated_ldx = -rotated_ldx;
		}
		if (globalcm & 2) {
			rotated_ldy = -rotated_ldy;
		}
#endif

		// atan(y/x) is the rotation angle of the normalized light direction vector
		// over the surface of the wall
		// then offset by 180 degrees
		azimuth = bump_atan2f(rotated_ldy, rotated_ldx) + F_PI;

#if 1
		// this is a hack that adds an extra 180 degrees to light direction
		// when the wall texture is v-flipped
		if (globalcm & 1) {
			azimuth += F_PI;
			if (azimuth > (F_PI * 2.0f)) {
				azimuth -= (F_PI * 2.0f);
			}
		}
#endif

		// get the length of the normalized light direction vector
		// over the surface of the wall
		vec3f_length(rotated_ldx, rotated_ldy, 0.0f, ld_xy_len);

		// atan(z / length(x,y)) gives "height" of the light direction vector
		// over the surface of the wall
		// we just need the magnitude of the angle
		elevation = fabs(bump_atan2f(rotated_ldz, ld_xy_len));

		// when elevation is greater than pi/2,
		// it should be pi - elevation
		// i.e. 
		// (in degrees)
		// 180 -> 0
		// ...
		// 130 -> 50
		// 120 -> 60
		// 110 -> 70
		// 100 -> 80
		// 90 -> 90
		if (elevation > (F_PI * 0.5f)) {
			elevation = F_PI - elevation;
		}

		// we limit the lowest elevation angle over the wall
		// to pi/4
		// this eliminates ugly artifacts
		// when a light is close to a wall in the "height" angle dimension
		// but far in distance
		if (elevation < (F_PI * 0.25f)) {
			elevation = (F_PI * 0.25f);
		}

		// FSCA wrapper, sin(el)/cos(el) approximations in one call
		fsincosr(elevation, &sin_el, &cos_el);

		// scale bumpmap parameters
		K2 = (int)(sin_el * bumpyint);
		K3 = (int)(cos_el * bumpyint);
		Q = (int)(azimuth * 40.584510f);
		//(int)(azimuth * 255.0f / (2.0f * F_PI));
		// pack bumpmap parameters
		boargb = (K1 << 24) | (K2 << 16) | (K3 << 8) | Q;
	}
}

// calculates per-vertex light contributions
// for a Doom wall polygon
// with no normal mapping
void light_wall_nobump(d64Poly_t *p)
{
	// 3d center of wall
	center_x = (p->dVerts[0].v->x + p->dVerts[3].v->x) * 0.5f;
	center_y = (p->dVerts[0].v->y + p->dVerts[3].v->y) * 0.5f;
	center_z = (p->dVerts[0].v->z + p->dVerts[3].v->z) * 0.5f;

	// for every dynamic light that was generated this frame
	for (unsigned i = 0; i < lightidx + 1; i++) {
		float dotprod;
		// calculate light direction vector between light and center of wall
		float dx = projectile_lights[i].x - center_x;
		float dy = projectile_lights[i].y - center_y;
		float dz = projectile_lights[i].z - center_z;

		// light direction isn't normalized
		// just need sign of dotprod, so that is ok
		vec3f_dot(dx, dy, dz, normx, normy, normz, dotprod);

		// light is on correct side of wall
		if (dotprod >= 0.0f) {
			// calculate per-vertex light contribution from current light
			light_vert(p->dVerts, &projectile_lights[i], 4);
		}
	}

	// for every vertex in wall poly
	for (unsigned i = 0; i < 4; i++) {
		// combine per-vertex dynamic light with per-vertex static sector lighting
		assign_lightcolor(&p->dVerts[i]);
	}
}


// calculates per-vertex light contributions
// for a Doom thing (monster/decoration sprite)
// with no normal mapping
void light_thing(d64Poly_t *p)
{
	for (unsigned i = 0; i < lightidx + 1; i++) {
		float dotprod;
		// calculate light direction vector between light and a vertex of thing
		float dx = projectile_lights[i].x - p->dVerts[0].v->x;
		float dy = projectile_lights[i].y - p->dVerts[0].v->y;
		float dz = projectile_lights[i].z - p->dVerts[0].v->z;

		// need sign of dotprod
		vec3f_dot(dx, dy, dz, normx, normy, normz, dotprod);

		// light is on correct side of thing
		if (dotprod >= 0.0f) {
			// calculate per-vertex light contribution from current light
			light_vert(p->dVerts, &projectile_lights[i], 4);
		}
	}

	// for every vertex in thing poly
	for (unsigned i = 0; i < 4; i++) {
		// combine per-vertex dynamic light with per-vertex static sector lighting
		assign_lightcolor(&p->dVerts[i]);
	}
}


// calculates per-vertex light contributions and normal mapping parameters
// for a triangle belonging to a Doom plane (floor/ceiling)
void light_plane_hasbump(d64Poly_t *p)
{
	// set if any light is close enough to center of triangle to "light" it
	// we consider those lights as contributing to light direction vector
	int bump_applied = 0;

	// average light direction vector
	float avg_ldx = 0.0f;
	float avg_ldy = 0.0f;
	float avg_ldz = 0.0f;

	// 3d center of floor/ceiling triangle
	center_x = (p->dVerts[0].v->x + p->dVerts[1].v->x +
		  p->dVerts[2].v->x) *
		 0.333333f;
	center_y = (p->dVerts[0].v->y + p->dVerts[1].v->y +
		  p->dVerts[2].v->y) *
		 0.333333f;
	center_z = (p->dVerts[0].v->z + p->dVerts[1].v->z +
		  p->dVerts[2].v->z) *
		0.333333f;

	// for every dynamic light that was generated this frame
	for (unsigned i = 0; i < lightidx + 1; i++) {
		int visible;
		float lightdist;
		float lrdiff;
		float dx;
		float dz;
		float lr;
		float invlr;
		// visibility test for light is simpler for floors/ceilings
		// no dot product needed
		// just check the sign of the light direction along y axis
		float dy = projectile_lights[i].y - center_y;
		if (in_floor == 1) {
			// for floors, light y should be greater than triangle y
			visible = dy >= 0;
		} else {
			// for ceilings, light y should be lower than triangle y
			visible = dy <= 0;
		}

		// light on the correct side of the triangle
		if (visible) {
			// finish calculating light direction vector
			dx = projectile_lights[i].x - center_x;
			dz = projectile_lights[i].z - center_z;
			lr = projectile_lights[i].radius;
			invlr = frapprox_inverse(lr);
			vec3f_length(dx, dy, dz, lightdist);
			lrdiff = lr - lightdist;

			// distance from light to surface is less than light radius
			// this is our final condition for a light to contribute to
			// average light direction vector for normal mapping
			if (lrdiff > 0) {
				// scale the light direction vector down
				// use linear attenuation based on distance
				// but then make it a little stronger
				// gives more of a "pop" on screen
				float light_scale = (lrdiff * invlr) * 1.2f;//1.1875f;

				// the names implied they are averaged
				// but really they are just accumulated
				// and once accumulation is done
				// it gets normalized
				avg_ldx += dx * light_scale;
				avg_ldy += dy * light_scale;
				avg_ldz += dz * light_scale;

				// indicate a light contributed to direction vector
				bump_applied = 1;
			}

			// calculate per-vertex light contribution from current light
			light_vert(p->dVerts, &projectile_lights[i], 3);
		}
	}

	// for every vertex in plane poly
	for (unsigned i = 0; i < 3; i++) {
		// combine per-vertex dynamic light with per-vertex static sector lighting
		assign_lightcolor(&p->dVerts[i]);
	}

	// the following is a simplifcation of calculating the light direction
	// (elevation and azimuth angles) for a triangle in plane (floor/ceiling)
	// because planes in Doom are always perfectly horiztonal,
	// there is no need for tangent/bitangent calculations
	// and they are even simpler than walls, because their normals are always
	// either (0,1,0) or (0,-1,0)
	// average light direction in the x,z plane is sufficient to calculate
	// azimuth
	// average light direction along the y axis is sufficient to calculate
	// elevation
	if (bump_applied) {
		// bump-mapping parameters
		// elevation (height angle over surface) (aka T)
		float elevation;
		// azimuth (rotation angle around surface) (aka Q)
		float azimuth;
		// (sin(elevation) * bumpiness) * 255
		int K2;
		// (cos(elevation) * bumpiness) * 255
		int K3;
		// (azimuth / 2pi) * 255
		int Q;
		// sin / cos of elevation
		float sin_el;
		float cos_el;

		vec3f_normalize(avg_ldx, avg_ldy, avg_ldz);

		// we flip x so negative is left, positive is right
		// operating in un-transformed world space
		// atan(z/x) gets us rotation angle in (x,z) plane of the triangle
		// then offset by 180 degrees
		azimuth = bump_atan2f(avg_ldz, -avg_ldx) + F_PI;
		// this leads to almost workable results, but there were artifacts
		// they seemed to be related to moving lights
		// and the view angle seemed to have some correlation with them
		// took a shot in the dark with something to resolve/reduce artifacts
		// adding the viewangle to the rotation angle
		// it helped
		azimuth += doomangletoQ(viewangle);
		// clamp the angle into the range (0,2pi)
		if (azimuth > (F_PI * 2.0f)) {
			azimuth -= (F_PI * 2.0f);
		} else if (azimuth < 0.0f) {
			azimuth += (F_PI * 2.0f);
		}

		// elevation above floor
		// directly overhead is pi/2
		// scale that by the normalized y component of light direction vector
		// good "approximation" of elevation angle
		elevation = fabs((F_PI * 0.5f) * avg_ldy);

		// we limit the lowest elevation angle over the floor/ceiling
		// to pi/4
		// this eliminates ugly artifacts
		// when a light is close to triangle surface in the "height" angle dimension
		// but far from triangle in distance
		if (elevation < (F_PI * 0.25f)) {
			elevation = (F_PI * 0.25f);
		}

		// FSCA wrapper, sin(el)/cos(el) approximations in one call
		fsincosr(elevation, &sin_el, &cos_el);

		// scale bumpmap parameters
		K2 = (int)(sin_el * bumpyint);
		K3 = (int)(cos_el * bumpyint);
		Q = (int)(azimuth * 40.584510f);
		//255.0f / (2.0f * F_PI));
		// pack bumpmap parameters
		boargb = (K1 << 24) | (K2 << 16) | (K3 << 8) | Q;
	}
}


// calculates per-vertex light contributions
// for a triangle belonging to a Doom plane (floor/ceiling)
// with no normal mapping
void light_plane_nobump(d64Poly_t *p)
{
	// 3d center of floor/ceiling triangle
	center_x = (p->dVerts[0].v->x + p->dVerts[1].v->x +
		  p->dVerts[2].v->x) *
		 0.333333f;
	center_y = (p->dVerts[0].v->y + p->dVerts[1].v->y +
		  p->dVerts[2].v->y) *
		 0.333333f;
	center_z = (p->dVerts[0].v->z + p->dVerts[1].v->z +
		  p->dVerts[2].v->z) *
		0.333333f;

	// for every dynamic light that was generated this frame
	for (int i = 0; i < lightidx + 1; i++) {
		int visible;
		float dy = projectile_lights[i].y - center_y;

		if (in_floor == 1) {
			visible = dy >= 0;
		} else {
			visible = dy <= 0;
		}

		if (visible) {
			// calculate per-vertex light contribution from current light
			light_vert(p->dVerts, &projectile_lights[i], 3);
		}
	}

	// for every vertex in plane poly
	for (unsigned i = 0; i < 3; i++) {
		// combine per-vertex dynamic light with per-vertex static sector lighting
		assign_lightcolor(&p->dVerts[i]);
	}
}
