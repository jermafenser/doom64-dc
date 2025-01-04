//Renderer phase 1 - BSP traversal

#include "doomdef.h"
#include "r_local.h"

void R_LightTest(subsector_t *sub);

int checkcoord[12][4] = { { 3, 0, 2, 1 }, /* Above,Left */
			{ 3, 0, 2, 0 }, /* Above,Center */
			{ 3, 1, 2, 0 }, /* Above,Right */
			{ 0, 0, 0, 0 }, { 2, 0, 2, 1 }, /* Center,Left */
			{ 0, 0, 0, 0 }, /* Center,Center */
			{ 3, 1, 3, 0 }, /* Center,Right */
			{ 0, 0, 0, 0 }, { 2, 0, 3, 1 }, /* Below,Left */
			{ 2, 1, 3, 1 }, /* Below,Center */
			{ 2, 1, 3, 0 }, /* Below,Right */
			{ 0, 0, 0, 0 } };

void R_RenderBSPNode(int bspnum);
boolean R_CheckBBox(const fixed_t bspcoord[4]);
void R_Subsector(int num);
void R_AddLine(seg_t *line);
void R_AddSprite(subsector_t *sub);
void R_RenderBSPNodeNoClip(int bspnum);

int light_tz[NUM_DYNLIGHT];
int light_type[NUM_DYNLIGHT];
int light_count[26];
projectile_light_t __attribute__((aligned(32))) projectile_lights[NUM_DYNLIGHT];
int lightidx = -1;

typedef enum {
	gun_l,
	laser_l,
	yellow_torch_l,
	blue_torch_l,
	red_torch_l,
	mother_rocket_l,
	generic_fire_l,
	blue_fire_l,
	red_fire_l,
	yellow_fire_l,
	candle_l,
	red_key_l,
	yellow_key_l,
	blue_key_l,
	rocket_barrel_l,
	trac_l,
	imp_ball_l,
	nite_ball_l,
	hell_fire_l,
	baro_fire_l,
	manc_rocket_l,
	caco_ball_l,
	bfg_l,
	plasma_l,
	spider_l,
	skull_l,
	numtypes_l,
} dynlight_type_t;

int max_light_by_type[26][2] = {
	{gun_l, 1000},
	{laser_l,4},
	{yellow_torch_l, 4},
	{blue_torch_l, 4},
	{red_torch_l, 4},
	{mother_rocket_l, 2},
	{generic_fire_l, 4},
	{blue_fire_l, 4},
	{red_fire_l, 4},
	{yellow_fire_l, 4},
	{candle_l, 12},
	{red_key_l, 1},
	{yellow_key_l, 1},
	{blue_key_l, 1},
	{rocket_barrel_l, 4},
	{trac_l, 2},
	{imp_ball_l, 2},
	{nite_ball_l, 2},
	{hell_fire_l, 2},
	{baro_fire_l, 2},
	{manc_rocket_l, 2},
	{caco_ball_l, 2},
	{bfg_l, 1},
	{plasma_l, 3},
	{spider_l, 4},
	{skull_l, 4},
};

int map37_yf1 = 0;
int map37_yf2 = 0;

int map16_candle1 = 0;
int map16_candle2 = 0;
int map16_candle3 = 0;
int map16_candle4 = 0;
int map16_candle5 = 0;

int map15_rt1 = 0;
int map15_rt2 = 0;
int map15_rt3 = 0;
int map15_rt4 = 0;

int map22_candle1 = 0;

int map21_rt1 = 0;
int map21_rt2 = 0;
int map21_rt3 = 0;
int map21_rt4 = 0;

int map21_yellow = 0;

int map18_yellow1 = 0;
int map18_red1 = 0;
int map18_red2 = 0;
int map18_red3 = 0;
int map18_red4 = 0;

int map18_c1 = 0;
int map18_c2 = 0;
int map18_c3 = 0;
int map18_c4 = 0;
int map18_c5 = 0;
int map18_c6 = 0;
int map18_c7 = 0;
int map18_c8 = 0;

int map13_flame = 0;
int map13_rt1 = 0;
int map13_rt2 = 0;
int map13_rt3 = 0;
int map13_rt4 = 0;
int map13_rt5 = 0;
int map13_rt6 = 0;

int map23_yt1 = 0;
int map23_yt2 = 0;
int map23_yt3 = 0;
int map23_yt4 = 0;
int map23_yt5 = 0;
int map23_yt6 = 0;
int map23_yt7 = 0;


static void R_ResetProjectileLights(void)
{
	lightidx = -1;
	memset(light_count,0,sizeof(int)*numtypes_l);
	memset(light_tz,0,sizeof(int)*NUM_DYNLIGHT);
	map16_candle1 = 0;
	map16_candle2 = 0;
	map16_candle3 = 0;
	map16_candle4 = 0;
	map16_candle5 = 0;
	map15_rt1 = 0;
	map15_rt2 = 0;
	map15_rt3 = 0;
	map15_rt4 = 0;
	map22_candle1 = 0;
	map21_rt1 = 0;
	map21_rt2 = 0;
	map21_rt3 = 0;
	map21_rt4 = 0;
	map21_yellow = 0;
	map18_yellow1 = 0;
	map18_red1 = 0;
	map18_red2 = 0;
	map18_red3 = 0;
	map18_red4 = 0;
	map18_c1 = 0;
	map18_c2 = 0;
	map18_c3 = 0;
	map18_c4 = 0;
	map18_c5 = 0;
	map18_c6 = 0;
	map18_c7 = 0;
	map18_c8 = 0;
	map13_flame = 0;
	map13_rt1 = 0;
	map13_rt2 = 0;
	map13_rt3 = 0;
	map13_rt4 = 0;
	map13_rt5 = 0;
	map13_rt6 = 0;
	map23_yt1 = 0;
	map23_yt2 = 0;
	map23_yt3 = 0;
	map23_yt4 = 0;
	map23_yt5 = 0;
	map23_yt6 = 0;
	map23_yt7 = 0;
	map37_yf1 = 0;
	map37_yf2 = 0;
}

static void R_AddProjectileLight(fixed_t x, fixed_t y, fixed_t z, float rad,
				 uint32_t lightc, int replace, int type)
{
	player_t *p;
	fixed_t dx;
	fixed_t dy;
	fixed_t dz;
	float dist;

	if (!global_render_state.quality) return;

	p = &players[0];
	
	if (lightidx >= (NUM_DYNLIGHT - 1)) {
		return;
	}

	dx = D_abs(p->mo->x - x) >> 16;
	dy = D_abs(p->mo->y - y) >> 16;
	dz = D_abs(p->mo->z - z) >> 16;	

	// only disable far away lights if we aren't on the title map
	if (gamemap != 33) {
		if (gamemap == 37) {
			if (!quickDistCheck(dx,dy,512)) {
				return;
			}
		} else {
			if (!quickDistCheck(dx,dy,640)) {
				return;
			}
		}
	}

	vec3f_length((float)dx,(float)dy,(float)dz,dist);

	if (light_count[type] < max_light_by_type[type][1]) {
		lightidx++;
		light_type[lightidx] = type;
		light_count[type] += 1;
		light_tz[lightidx] = replace;
		projectile_lights[lightidx].x = (float)(x >> 16);
		projectile_lights[lightidx].y = (float)(y >> 16);
		projectile_lights[lightidx].z = (float)(z >> 16);

		projectile_lights[lightidx].r = (float)((lightc >> 16) & 255)
			* recip255;
		projectile_lights[lightidx].g = (float)((lightc >> 8) & 255)
			* recip255;
		projectile_lights[lightidx].b = (float)(lightc & 255)
			* recip255;

		projectile_lights[lightidx].radius = rad;
		projectile_lights[lightidx].distance = dist;
	} else {
		for (int li=0;li<lightidx+1;li++) {
			if (light_type[li] == type) {
				if (projectile_lights[li].distance > dist) {
					light_type[li] = type;
					light_tz[li] = replace;

					projectile_lights[li].x = (float)(x >> 16);
					projectile_lights[li].y = (float)(y >> 16);
					projectile_lights[li].z = (float)(z >> 16);

					projectile_lights[li].r = (float)((lightc >> 16) & 255)
						* recip255;
					projectile_lights[li].g = (float)((lightc >> 8) & 255)
						* recip255;
					projectile_lights[li].b = (float)(lightc & 255)
						* recip255;

					projectile_lights[li].radius = rad;
					projectile_lights[li].distance = dist;

					break;
				}
			}
		}
	}
}

extern int player_shooting;
extern int player_light;
extern int player_last_weapon;

int player_light_fade = -1;
extern int add_lightning;

// Kick off the rendering process by initializing the solidsubsectors array and then
// starting the BSP traversal.

void R_BSP(void)
{
	int count;
	subsector_t **sub;
	player_t *p;
	p = &players[0];

	validcount++;

	rendersky = false;

	numdrawsubsectors = 0;

	numdrawvissprites = 0;
	R_ResetProjectileLights();

	global_render_state.floor_split_override = 0;

	fixed_t px = p->mo->x >> 16;
	fixed_t py = p->mo->y >> 16;

	if (gamemap >= 40) {
		global_render_state.floor_split_override = 1;
	} else if (gamemap == 18) {
		global_render_state.floor_split_override = 1;
	} else if (gamemap == 28) {
		global_render_state.floor_split_override = 1;
	} else if (gamemap == 3) {
		if (-2900 < py && py < -1950) {
			if (-800 < px && px < 450) {
				global_render_state.floor_split_override = 1;
			}
		}
	} else if (gamemap == 6) {
		if (-1600 < py && py < -560) {
			if (-470 < px && px < 1200) {
				global_render_state.floor_split_override = 1;
			}
		}
	} else if (gamemap == 23) {
		if (-2390 < py && py < -1210) {
			if (-1084 < px && px < 1028) {
				global_render_state.floor_split_override = 1;
			}
		}
		global_render_state.floor_split_override = 1;
	}

	if (add_lightning) {
		fixed_t lv_x = FixedMul((32<<16),viewcos);
		fixed_t lv_y = FixedMul((32<<16),viewsin);

		if (p->mo->subsector->sector->ceilingpic != -1) {
			R_AddProjectileLight(p->mo->x + lv_x, p->mo->y + lv_y,
								players[0].viewz + (128 << 16),
								512, 0xff5f5f9f, 0, gun_l);
		} else {
			R_AddProjectileLight(p->mo->x + lv_x, p->mo->y + lv_y,
								players[0].viewz + (192 << 16),
								768, 0xff7f7faf, 0, gun_l);
		}
	}

	// convoluted logic for making a light appear when a player shoots and then
	// making it fade out over slightly different times for different weapons
	if (player_light) {
		if (player_shooting) {
			fixed_t lv_x = FixedMul((8<<16),viewcos);
			fixed_t lv_y = FixedMul((8<<16),viewsin);
		
			R_AddProjectileLight(p->mo->x + lv_x, p->mo->y + lv_y,
								players[0].viewz,	
								384, 0xff7f7f7f, 0, gun_l);
			player_shooting = 0;
			goto skip_player_light;
		} else if (!player_shooting && player_light_fade == -1) {
			if (player_last_weapon == wp_pistol) {
				player_light_fade = 2;
			} else if (player_last_weapon == wp_shotgun) {
				player_light_fade = 4;
			} else if (player_last_weapon == wp_supershotgun) {
				player_light_fade = 6;
			} else if (player_last_weapon == wp_chaingun) {
				player_light_fade = 4;
			}
		}

		if (!player_shooting && player_light_fade != -1) {
			int scale_start = 0;
			if (player_last_weapon == wp_pistol) {
				scale_start = 3;
			} else if (player_last_weapon == wp_shotgun) {
				scale_start = 5;
			} else if (player_last_weapon == wp_supershotgun) {
				scale_start = 7;
			} else if (player_last_weapon == wp_chaingun) {
				scale_start = 5;
			}

			int8_t c = 0x7f -
				((scale_start - player_light_fade - 1) * 2);

			if (player_light_fade == 0) {
				player_light = 0;
				player_light_fade = -1;
				goto skip_player_light;
			} else {
				player_light_fade -= 1;
			}

			uint32_t color = 0xff00000 | ((c & 0xff) << 16) |
					 ((c & 0xff) << 8) | (c & 0xff);

			fixed_t lv_x = FixedMul((8<<16),viewcos);
			fixed_t lv_y = FixedMul((8<<16),viewsin);
		
			R_AddProjectileLight(p->mo->x + lv_x, p->mo->y + lv_y,
								players[0].viewz,	
								384 - ((scale_start - player_light_fade) * 32),
								color, 0, gun_l);
		}
	}

skip_player_light:
	visspritehead = vissprites;

	endsubsector = solidsubsectors; /* Init the free memory pointer */

	D_memset(solidcols, 0, SOLIDCOLSC);

	if (camviewpitch == 0) {
		R_RenderBSPNode(
			numnodes -
			1); /* Begin traversing the BSP tree for all walls in render range */
	} else {
		R_RenderBSPNodeNoClip(
			numnodes -
			1); /* Begin traversing the BSP tree for all walls in render range */
		rendersky = true;
	}

	sub = solidsubsectors;
	count = numdrawsubsectors;
	while (count) {
		R_AddSprite(*sub); // Render each sprite
		sub++; // Inc the sprite pointer
		count--;
	}

	if (global_render_state.quality && (lightidx + 1)) {
		projectile_light_t *pl = projectile_lights;
		for (unsigned i = 0; i < lightidx + 1; i++) {
			pl->distance = pl->radius * pl->radius;
			pl++;
		}

		sub = solidsubsectors;
		count = numdrawsubsectors;
		while (count) {
			R_LightTest(*sub); // check lights against subsec bbox
			sub++; // Inc the sprite pointer
			count--;
		}
	}
}

static boolean R_RenderBspSubsector(int bspnum)
{
	// Found a subsector?
	if (bspnum & NF_SUBSECTOR) {
		if (bspnum == -1)
			R_Subsector(0);
		else
			R_Subsector(bspnum & (~NF_SUBSECTOR));

		return true;
	}

	return false;
}


// RenderBSPNode
// Renders all subsectors below a given node,
// traversing subtree recursively.
// Just call with BSP root.

//Non recursive version.
//constant stack space used and easier to
//performance profile.
#define MAX_BSP_DEPTH 256
static int stack[MAX_BSP_DEPTH];
#define MAXBSP 192
void R_RenderBSPNode(int bspnum)
{
	const node_t *bsp;
	int side = 0;
	int sp = 0;
	int left;
	int right;
	fixed_t dx;
	fixed_t dy;

	while (true) {
		// Front sides.
		while (!R_RenderBspSubsector(bspnum)) {
//#if RANGECHECK
			if (sp == MAXBSP) //MAX_BSP_DEPTH)
				break;
//#endif
			bsp = &nodes[bspnum];
			dx = (viewx - bsp->line.x);
			dy = (viewy - bsp->line.y);

			left = (bsp->line.dy >> 16) * (dx >> 16);
			right = (dy >> 16) * (bsp->line.dx >> 16);

			if (right < left)
				side = 0;
			else
				side = 1;

			stack[sp++] = bspnum;
			stack[sp++] = side;

			bspnum = bsp->children[side];

			if (!R_CheckBBox(bsp->bbox[side])) {
				break;
			}
		}

		if (sp == 0) {
			// back at root node and not visible. All done!
			return;
		}

		// Back sides.
		side = stack[--sp];
		bspnum = stack[--sp];
		bsp = &nodes[bspnum];

		// Possibly divide back space.
		// Walk back up the tree until we find
		// a node that has a visible backspace.
		while (!R_CheckBBox(bsp->bbox[side ^ 1])) {
			if (sp == 0) {
				// back at root node and not visible. All done!
				return;
			}

			// Back side next.
			side = stack[--sp];
			bspnum = stack[--sp];

			bsp = &nodes[bspnum];
		}

		bspnum = bsp->children[side ^ 1];
	}
}


//
// Checks BSP node/subtree bounding box. Returns true if some part of the bbox
// might be visible.
//
extern fixed_t FixedDivFloat(fixed_t a, fixed_t b);
boolean R_CheckBBox(const fixed_t bspcoord[4])
{
	int boxx;
	int boxy;
	int boxpos;

	fixed_t x1, y1, x2, y2;
	byte *solid_cols;
	int vx1, vy1, vx2, vy2, delta;
	int Xstart, Xend;

	// find the corners of the box that define the edges from current viewpoint
	if (viewx < bspcoord[BOXLEFT])
		boxx = 0;
	else if (viewx <= bspcoord[BOXRIGHT])
		boxx = 1;
	else
		boxx = 2;

	if (viewy > bspcoord[BOXTOP])
		boxy = 0;
	else if (viewy >= bspcoord[BOXBOTTOM])
		boxy = 1;
	else
		boxy = 2;

	boxpos = (boxy << 2) + boxx;
	if (boxpos == 5)
		return true;

	x1 = bspcoord[checkcoord[boxpos][0]];
	y1 = bspcoord[checkcoord[boxpos][1]];
	x2 = bspcoord[checkcoord[boxpos][2]];
	y2 = bspcoord[checkcoord[boxpos][3]];

	vx1 = FixedMul(viewsin, x1 - viewx) - FixedMul(viewcos, y1 - viewy);
	vy1 = FixedMul(viewcos, x1 - viewx) + FixedMul(viewsin, y1 - viewy);
	vx2 = FixedMul(viewsin, x2 - viewx) - FixedMul(viewcos, y2 - viewy);
	vy2 = FixedMul(viewcos, x2 - viewx) + FixedMul(viewsin, y2 - viewy);

	if ((vx1 < -vy1) && (vx2 < -vy2))
		return false;

	if ((vy1 < vx1) && (vy2 < vx2))
		return false;

	if ((((vx2 >> 16) * (vy1 >> 16)) - ((vx1 >> 16) * (vy2 >> 16))) < 2)
		return true;

	if ((vy1 <= 0) && (vy2 <= 0))
		return false;
// all FixedDiv2 previously
	if (vx1 < -vy1) {
		delta = (vx1 + vy1);
		delta = FixedDivFloat(delta, ((delta - vx2) - vy2));
		delta = FixedMul(delta, (vy2 - vy1));

		vy1 += delta;
		vx1 = -vy1;
	}

	if (vy2 < vx2) {
		delta = (vx1 - vy1);
		delta = FixedDivFloat(delta, ((delta - vx2) + vy2));
		delta = FixedMul(delta, (vy2 - vy1));
		vx2 = delta + vy1;
		vy2 = vx2;
	}

	// multiply by 320
	// is (x*256) + (x*64)
	// (x << 8) + (x << 6)
	// by 160
	// is (x*128) + (x*32)
	// ((x << 7) + (x << 5)
	// Xstart = ((FixedDiv2(vx1, vy1) * 160) >> 16) + 160;
	// Xend = ((FixedDiv2(vx2, vy2) * 160) >> 16) + 160;

	fixed_t vxovery1 = FixedDivFloat(vx1, vy1) >> XOYSCALE;
	fixed_t vxovery2 = FixedDivFloat(vx2, vy2) >> XOYSCALE;

	Xstart = ((vxovery1 + (vxovery1 >> 2))) + (SOLIDCOLSC >> 1);
	Xend = ((vxovery2 + (vxovery2 >> 2))) + (SOLIDCOLSC >> 1);

	if (Xstart < 0)
		Xstart = 0;

	if (Xend >= SOLIDCOLSC)
		Xend = SOLIDCOLSC;

	solid_cols = &solidcols[Xstart];
	while (Xstart < Xend) {
		if (*solid_cols == 0)
			return true;
		solid_cols++;
		Xstart++;
	}

	return false;
}


//
// Determine floor/ceiling planes, add sprites of things in sector,
// draw one or more segments.
//

void R_Subsector(int num) // 8002451C
{
	subsector_t *sub;
	seg_t *line;
	int count;

#ifdef RANGECHECK
	if (num >= numsubsectors) {
		I_Error("R_Subsector: ss %i with numss = %i", num,
			numsubsectors);
	}
#endif

	if (numdrawsubsectors < MAXSUBSECTORS) {
		numdrawsubsectors++;

		sub = &subsectors[num];
		sub->drawindex = numdrawsubsectors;

		*endsubsector = sub; // copy subsector
		endsubsector++;

		frontsector = sub->sector;

		line = &segs[sub->firstline];
		count = sub->numlines;

		do {
			R_AddLine(line); /* Render each line */
			++line; /* Inc the line pointer */
		} while (--count); /* All done? */
	}
}

static inline int clamp_and_diff_squared(int d, int min, int max)
{
	const int t = d < min ? min : d;
	int res = d - (t > max ? max : t);
	return res*res;
}

static inline bool light_intersects_bbox(const projectile_light_t *pl, 
								const int x1, const int y1,
								const int x2, const int y2)
{
	int plx = (int)pl->x;
	int ply = (int)pl->y;

	// start with test for light origin inside of bbox
	if (x1 <= plx && plx <= x2) {
		if (y2 <= ply && ply <= y1) {
			return true;
		}
	}

	int distanceXSquared = clamp_and_diff_squared(plx, x1, x2);
	int distanceYSquared = clamp_and_diff_squared(ply, y2, y1);

	// If the distance is less than the circle's radius, an intersection occurs
	int distanceSquared = distanceXSquared + distanceYSquared;
	// after light creation, but before light transform,
	// in R_BSP, we store radius squared in distance
	return distanceSquared < (int)pl->distance;
}

void __attribute__((noinline)) R_LightTest(subsector_t *sub)
{
	const int x1 = (int)(sub->bbox[BOXLEFT]);
	const int x2 = (int)(sub->bbox[BOXRIGHT]);
	const int y1 = (int)(sub->bbox[BOXTOP]);
	const int y2 = (int)(sub->bbox[BOXBOTTOM]);

	unsigned lit = 0;
	unsigned first_idx = 0xff;
	unsigned last_idx = 0;

	projectile_light_t *pl = &projectile_lights[0];
	for (unsigned i=0;i<=lightidx;i++) {
		if (light_intersects_bbox(pl++,x1,y1,x2,y2)) {
			lit |= (1 << i);

			if (__builtin_expect((i < first_idx),0)) {
				first_idx = i;
				last_idx = i;
			}

			if (__builtin_expect((i > last_idx),1)) {
				last_idx = i;
			}
		}
	}

	if (lit) {
		lit |= (first_idx << 24);
		lit |= (last_idx << 16);
	}

	sub->lit = lit;
}


//
// Clips the given segment and adds any visible pieces to the line list.
//
extern fixed_t FixedDivFloat(fixed_t a, fixed_t b);
void R_AddLine(seg_t *line)
{
#define FRACUNITx8 (FRACUNIT << 3)
	sector_t *backsector;
	vertex_t *vrt, *vrt2;
	int x1, y1, x2, y2, count;
	int Xstart, Xend, delta;
	byte *solid_cols;

	line->flags &= ~1;

	vrt = line->v1;

	if (vrt->validcount != validcount) {
		x1 = FixedMul(viewsin, (vrt->x - viewx)) - FixedMul(viewcos, (vrt->y - viewy));
		y1 = FixedMul(viewcos, (vrt->x - viewx)) + FixedMul(viewsin, (vrt->y - viewy));		
		vrt->vx = x1;
		vrt->vy = y1;

		vrt->validcount = validcount;
	} else {
		x1 = vrt->vx;
		y1 = vrt->vy;
	}

	vrt2 = line->v2;
	if (vrt2->validcount != validcount) {
		x2 = FixedMul(viewsin, (vrt2->x - viewx)) -
			 FixedMul(viewcos, (vrt2->y - viewy));
		y2 = FixedMul(viewcos, (vrt2->x - viewx)) +
			 FixedMul(viewsin, (vrt2->y - viewy));
		vrt2->vx = x2;
		vrt2->vy = y2;

		vrt2->validcount = validcount;
	} else {
		x2 = vrt2->vx;
		y2 = vrt2->vy;
	}

	if ((x1 < -y1) && (x2 < -y2))
		return;

	if ((y1 < x1) && (y2 < x2))
		return;

	if ((y1 < (FRACUNITx8 + 1)) && (y2 < (FRACUNITx8 + 1)))
		return;

	if ((((x2 >> 16) * (y1 >> 16)) - ((x1 >> 16) * (y2 >> 16))) <= 0)
		return;

	if (y1 < FRACUNITx8) {
		// all FixedDiv2 previously
		delta = FixedDivFloat((FRACUNITx8 - y1), (y2 - y1));
		delta = FixedMul(delta, (x2 - x1));
		x1 += delta;
		y1 = FRACUNITx8;
	} else if (y2 < FRACUNITx8) {
		delta = FixedDivFloat((FRACUNITx8 - y2), (y1 - y2));
		delta = FixedMul(delta, (x1 - x2));
		x2 += delta;
		y2 = FRACUNITx8;
	}

	// multiply by 320
	// is (x*256) + (x*64)
	// (x << 8) + (x << 6)
	// by 160
	// is (x*128) + (x*32)
	// ((x << 7) + (x << 5)
	// Xstart = ((FixedDiv2(x1, y1) * 160) >> 16) + 160;
	// Xend  = ((FixedDiv2(x2, y2) * 160) >> 16) + 160;

	fixed_t xovery1 = FixedDivFloat(x1, y1) >> XOYSCALE;
	fixed_t xovery2 = FixedDivFloat(x2, y2) >> XOYSCALE;

	Xstart = ((xovery1 + (xovery1 >> 2))) + (SOLIDCOLSC >> 1);
	Xend = ((xovery2 + (xovery2 >> 2))) + (SOLIDCOLSC >> 1);

	if (Xstart < 0)
		Xstart = 0;

	if (Xend >= SOLIDCOLSC)
		Xend = SOLIDCOLSC;

	if (Xstart != Xend) {
		solid_cols = &solidcols[Xstart];
		count = Xstart;
		while (count < Xend) {
			if (*solid_cols == 0) {
				line->flags |= 1;
				line->linedef->flags |= ML_MAPPED;
				break;
			}
			solid_cols++;
			count++;
		}

		if (frontsector->ceilingpic == -1) {
			rendersky = true;
		}

		if (!(line->linedef->flags &
			(ML_DONTOCCLUDE | ML_DRAWMASKED))) {
			backsector = line->backsector;

			if (!backsector ||
				backsector->ceilingheight <=
					frontsector->floorheight ||
				backsector->floorheight >=
					frontsector->ceilingheight ||
				backsector->floorheight ==
					backsector->ceilingheight) { // New line on Doom 64
				solid_cols = &solidcols[Xstart];
				while (Xstart < Xend) {
					*solid_cols = 1;
					solid_cols++;
					Xstart += 1;
				}
			}
		}
	}
}


void R_AddSprite(subsector_t *sub) // 80024A98
{
	byte *data;
	mobj_t *thing;
	spritedef_t *sprdef;
	spriteframe_t *sprframe;

	subsector_t *pSub;
	subsector_t *CurSub;
	vissprite_t *VisSrpCur, *VisSrpCurTmp;
	vissprite_t *VisSrpNew;

	angle_t ang;
	unsigned int rot;
	boolean flip;
	int lump;
	fixed_t tx, tz;
	fixed_t x, y;
	sub->lit = 0;
	sub->vissprite = NULL;

	for (thing = sub->sector->thinglist; thing; thing = thing->snext) {
		if (thing->subsector != sub)
			continue;

		if (numdrawvissprites >= MAXVISSPRITES)
			break;

		if (thing->flags & MF_RENDERLASER) {
			visspritehead->zdistance = MAXINT;
			visspritehead->thing = thing;
			visspritehead->next = sub->vissprite;
			sub->vissprite = visspritehead;

			R_AddProjectileLight(thing->x, thing->y,
								thing->z, 304, 0x00ff0000, -1, laser_l);

			visspritehead++;
			numdrawvissprites++;
		} else {
			// transform origin relative to viewpoint
			x = (thing->x - viewx) >> 16;
			y = (thing->y - viewy) >> 16;
			tx = ((viewsin * x) - (viewcos * y)) >> 16;
			tz = ((viewcos * x) + (viewsin * y)) >> 16;

			int atz = tz < MINZ;

			sprdef = &sprites[thing->sprite];
			sprframe = &sprdef->spriteframes[thing->frame &
							 FF_FRAMEMASK];

			if (sprframe->rotate != 0) {
				ang = R_PointToAngle2(viewx, viewy, thing->x,
							thing->y);
				rot = ((ang - thing->angle) +
					((unsigned int)(ANG45 / 2) * 9)) >> 29;
				lump = sprframe->lump[rot];
				flip = (boolean)(sprframe->flip[rot]);
			} else {
				lump = sprframe->lump[0];
				flip = (boolean)(sprframe->flip[0]);
			}

			int random_factor = I_Random() % 24;

			// yellow torch
			if (lump >= 26 && lump <= 30) {
				int r = 192 - random_factor;
				int g = 160 - random_factor;
				int b = 64 - random_factor;

				uint32_t color = (r << 16) | (g << 8) | b;

				if (gamemap == 23) {
					int tvx = thing->x >> 16;
					int tvy = thing->y >> 16;

					uint32_t color2 = ((r+32) << 16) | ((g+32) << 8) | (b+32);

					if (-2920 < tvy && tvy < -2660) {
						if (-350 < tvx && tvx < -120) {
							if (!map23_yt1) {
								map23_yt1 = 1;
								R_AddProjectileLight(-230 << 16, -2790 << 16,
													thing->z + (45<<16), 208, color2,
													atz,yellow_torch_l);
							}
						} else if (120 < tvx && tvx < 350) {
							if (!map23_yt2) {
								map23_yt2 = 2;
								R_AddProjectileLight(230 << 16, -2790 << 16,
													thing->z + (45<<16), 208, color2,
													atz,yellow_torch_l);
							}
						}

					} else if (-1800 < tvy && tvy < -1670) {
						if (-450 < tvx && tvx < -270) {
							if (!map23_yt3) {
								map23_yt3 = 3;
								R_AddProjectileLight(-290 << 16, -1780 << 16,
													thing->z + (45<<16), 208, color2,
													atz,yellow_torch_l);
							}
						} else if (250 < tvx && tvx < 450) {
							if (!map23_yt4) {
								map23_yt4 = 4;
								R_AddProjectileLight(270 << 16, -1780 << 16,
													thing->z + (45<<16), 208, color2,
													atz,yellow_torch_l);
							}
						}
					} else if (-2234 < tvy && tvy < -2211) {
						if (-450 < tvx && tvx < -270) {
							if (!map23_yt5) {
								map23_yt5 = 5;
								R_AddProjectileLight(-290 << 16, -2230 << 16,
													thing->z + (45<<16), 208, color2,
													atz,yellow_torch_l);
							}
						} else if (250 < tvx && tvx < 450) {
							if (!map23_yt6) {
								map23_yt6 = 6;
								R_AddProjectileLight(270 << 16, -2230 << 16,
													thing->z + (45<<16), 208, color2,
													atz,yellow_torch_l);
							}
						}
					} else {
						R_AddProjectileLight(thing->x, thing->y,
								thing->z + (45<<16), 128, color,
								atz,yellow_torch_l);
					}
				} else {
					R_AddProjectileLight(thing->x, thing->y,
										thing->z + (45<<16), 128, color,
										atz,yellow_torch_l);
				}
			}

			// blue torch
			if (lump >= 31 && lump <= 35) {
				int r = 64 - random_factor;
				int g = 64 - random_factor;
				int b = 255 - random_factor;

				uint32_t color = (r << 16) | (g << 8) | b;

				R_AddProjectileLight(thing->x, thing->y,
							thing->z + (45<<16), 128, color,
							atz,blue_torch_l);
			}

			// red torch
			if (lump >= 36 && lump <= 40) {
				int r = 192 - random_factor;
				int g = 32 - random_factor;
				int b = 32 - random_factor;

				uint32_t color = (r << 16) | (g << 8) | b;
				if (gamemap == 13) {
					int tvx = thing->x >> 16;
					int tvy = thing->y >> 16;
					r = 255 - random_factor;
					g = 48 - random_factor;
					b = 48 - random_factor;

					color = (r << 16) | (g << 8) | b;

					if (3266 < tvx && tvx < 3484) {
						if (-894 < tvy && tvy < -470) {
							if (!map13_rt1) {
								map13_rt1 = 1;
								r = 255 - random_factor;
								g = 0;
								b = 0;

								color = (r << 16) | (g << 8) | b;
								R_AddProjectileLight(3346 << 16, -666 << 16,
													thing->z + (25<<16), 320, 
													color,
													atz,red_torch_l);								
							}
						}
					} else if (1586 < tvx && tvx < 1756) {
						if (-894 < tvy && tvy < -470) {
							if (!map13_rt2) {
								r = 255 - random_factor;
								g = 0;
								b = 0;

								color = (r << 16) | (g << 8) | b;
								map13_rt2 = 2;
								R_AddProjectileLight(1700 << 16, -666 << 16,
													thing->z + (25<<16), 320, 
													color,
													atz,red_torch_l);								
							}
						}
					} else if (1756 < tvx && tvx < 2450) {
						if (-894 < tvy && tvy < -470) {
							if (!map13_rt3) {
								r = 255 - random_factor;
								g = 0;
								b = 0;

								color = (r << 16) | (g << 8) | b;
								map13_rt3 = 3;
								R_AddProjectileLight(2176 << 16, -666 << 16,
													thing->z + (25<<16), 448, 
													color,
													atz,red_torch_l);								
							}
						}
					} else if (2450 < tvx && tvx < 3266) {
						if (-894 < tvy && tvy < -470) {
							if (!map13_rt4) {
								r = 255 - random_factor;
								g = 0;
								b = 0;

								color = (r << 16) | (g << 8) | b;
								map13_rt4 = 4;
								R_AddProjectileLight(2815 << 16, -666 << 16,
													thing->z + (25<<16), 448, 
													color,
													atz,red_torch_l);								
							}
						}
					} else {
						R_AddProjectileLight(thing->x, thing->y,
											thing->z + (45<<16), 128, color,
											atz,red_torch_l);
					} 
				} else if (gamemap == 21) {
					int tvx = thing->x >> 16;
					int tvy = thing->y >> 16;

					r = 255 - random_factor;
					g = 48 - random_factor;
					b = 48 - random_factor;

					color = (r << 16) | (g << 8) | b;

					if (-180 < tvx && tvx < 106) {
						if (920 < tvy && tvy < 1210) {
							if (!map21_rt1) {
								map21_rt1 = 1;
								R_AddProjectileLight((-32) << 16, 1060 << 16,
													thing->z + (25<<16), 256, 
													color,
													atz,red_torch_l);
							}
						}
					} else if (-930 < tvx && tvx < -650) {
						if (1270 < tvy && tvy < 1580) {
							if (!map21_rt2) {
								map21_rt2 = 2;
								R_AddProjectileLight((-780) << 16, 1420 << 16,
													thing->z + (25<<16), 256,
													color,
													atz,red_torch_l);
							}
						}
					} else {
						R_AddProjectileLight(thing->x, thing->y,
											thing->z + (45<<16), 128, color,
											atz,red_torch_l);
					}
				} else if (gamemap == 15) {
					int tvx = thing->x >> 16;
					int tvy = thing->y >> 16;
					
					if (-810 < (viewx >> 16) && (viewx >> 16) < 424) {
						if(-750 < (viewy >> 16) && (viewy >> 16) < 529) {
							if (-655 < tvx && tvx < -503) {
								if (-87 < tvy && tvy < 371) {
									if (!map15_rt1) {
										map15_rt1 = 1;
										R_AddProjectileLight(-580 << 16,
															142 << 16,
															thing->z + (25<<16),
															256, color,
															atz,red_torch_l);
									}
								} else if (-546 < tvy && tvy < -90) {
									if (!map15_rt2) {
										map15_rt2 = 2;
										R_AddProjectileLight(-580 << 16,
															-318 << 16,
															thing->z + (25<<16),
															256, color,
															atz, red_torch_l);
									}
								}
							} else if (120 < tvx && tvx < 256) {
								if (-87 < tvy && tvy < 371) {
									if (!map15_rt3) {
										map15_rt3 = 3;
										R_AddProjectileLight(188 << 16, 142 << 16,
															thing->z + (25<<16), 256,
															color, atz, red_torch_l);
									}
								} else if (-546 < tvy && tvy < -90) {
									if (!map15_rt4) {
										map15_rt4 = 4;
										R_AddProjectileLight(188 << 16, -318 << 16,
															thing->z + (25<<16), 256,
															color, atz, red_torch_l);
									}
								}
							}
						}
					} 
				} else {
					R_AddProjectileLight(thing->x, thing->y, thing->z + (45<<16),
										128, color, atz, red_torch_l);
				}
			}

			// rockets from m o t h e r
			if (lump >= 53 && lump <= 68) {
				float radius = 256;

				float r = (float)(255 - random_factor);

				if (lump > 62) {
					float scale = 1.0f / ((float)((lump - 63) * 0.5f) + 1);
					r *= scale;
					radius -= 2;
				}

				uint32_t color = ((int)r << 16);

				// 299 to 304 are when it hits and disappears
				R_AddProjectileLight(thing->x, thing->y, thing->z + (20<<16),
									radius, color, atz, mother_rocket_l);
			}

			// fire
			if (lump >= 105 && lump <= 109) {
				int r = 255 - random_factor;
				int g = 127 - random_factor;
				int b = 39 - random_factor;

				uint32_t color = (r << 16) | (g << 8) | b;

				if (gamemap != 21) {
					if (gamemap == 37) {
						int tvx = thing->x >> 16;
						int tvy = thing->y >> 16;

						if (-256 < tvy && tvy < 0) {
							if (-960 < tvx && tvx < -830) {
								if (!map37_yf1) {
									map37_yf1 = 1;
									R_AddProjectileLight(-900 << 16, -128 << 16,
														thing->z + (50<<16),
														224, color,
														atz, generic_fire_l);						
								}
							} else if (-180 < tvx && tvx < -70) {
								if (!map37_yf2) {
									map37_yf2 = 2;
									R_AddProjectileLight(-130 << 16, -128 << 16,
														thing->z + (50<<16),
														224, color,
														atz, generic_fire_l);						
								}
							} else {
								R_AddProjectileLight(thing->x, thing->y,
													thing->z + (50<<16), 102, color,
													atz, generic_fire_l);
							}
						} else {
								R_AddProjectileLight(thing->x, thing->y,
													thing->z + (50<<16), 102, color,
													atz, generic_fire_l);
						}
					} else if (gamemap == 13) {
						int tvx = thing->x >> 16;
						int tvy = thing->y >> 16;

						if (-224 < tvy && tvy < -96) {
							if (32 < tvx && tvx < 160) {
								if (!map13_flame) {
									map13_flame = 1;
									R_AddProjectileLight(thing->x, thing->y,
														thing->z + (50<<16),
														224, color,
														atz, generic_fire_l);						
								}
							}
						}
					}
#if 0
					if (gamemap == 18) {
						int tvx = thing->x >> 16;
						int tvy = thing->y >> 16;
						int vx = viewx >> 16;
						int vy = viewy >> 16;
						
						if (-1246 < vx && vx < -738) {
							if (0 < vy && vy < 640) {
if(66 < tvy && tvy < 334) {
							if(-1137 < tvx && tvx < -1056) {
	//-1090,174
R_AddProjectileLight((-1090<<16), (174<<16),
					thing->z + (55<<16), 320,
				color, atz, generic_fire_l);	
} else if (-860 < tvx && tvx < -800) {
	// -830,174
R_AddProjectileLight((-830<<16), (174<<16),
					thing->z + (55<<16), 320,
				color, atz, generic_fire_l);	
}

							} else if (0 < tvy && tvy < 66) {								
								if(-1128 < tvx && tvx < -794) {
//-960,32
R_AddProjectileLight((-960<<16), (32<<16),
					thing->z + (55<<16), 320,
				color, atz, generic_fire_l);	
									
								}
							}
						}
						}
						else if (555 < tvy && tvy < 730) {
							if (-2890 < tvx && tvx < -2730) {
								if (!map18_yellow1) {
									map18_yellow1 = 1;

									R_AddProjectileLight((-2810<<16), (640<<16),
														thing->z + (55<<16), 224,
														color, atz, generic_fire_l);
								}
							}
						} else {
							R_AddProjectileLight(thing->x, thing->y,
												thing->z + (50<<16), 102, color,
												atz, generic_fire_l);
						}
					}
#endif
					else {
						R_AddProjectileLight(thing->x, thing->y,
											thing->z + (50<<16), 102, color,
											atz, generic_fire_l);
					}
				}
			}

			// blue fire
			if (lump >= 144 && lump <= 148) {
				int r = 64 - random_factor;
				int g = 64 - random_factor;
				int b = 255 - random_factor;

				uint32_t color = (r << 16) | (g << 8) | b;

				R_AddProjectileLight(thing->x, thing->y, thing->z + (35<<16),
									102, color, atz, blue_fire_l);
			}

			// red fire
			if (lump >= 149 && lump <= 153) {
				int r = 192 - random_factor;
				int g = 32 - random_factor;
				int b = 32 - random_factor;

				uint32_t color = (r << 16) | (g << 8) | b;

				if (gamemap == 18) {
					int tvx = thing->x >> 16;
					int tvy = thing->y >> 16;
				
					if (935 < tvy && tvy < 1090) {
						if (-3260 < tvx && tvx < -3080) {
							//-3192,1032
							if (!map18_red1) {
								map18_red1 = 1;
								R_AddProjectileLight((-3192 << 16), 1032 << 16,
													thing->z + (35<<16), 224,
													color, atz, red_fire_l);
							}
						}
					} else if(190 < tvy && tvy < 275) {
						if (-3260 < tvx && tvx < -3010) {
							if (!map18_red2) {
								map18_red2 = 1;
								R_AddProjectileLight((-3124 << 16), 240 << 16,
													thing->z + (35<<16), 224,
													color, atz, red_fire_l);
							}
						}
					} else {
						R_AddProjectileLight(thing->x, thing->y,
											thing->z + (35<<16), 102, color,
											atz, red_fire_l);
					}
				} else {
					R_AddProjectileLight(thing->x, thing->y, thing->z + (35<<16),
										102, color, atz, red_fire_l);
				}
			}

			// yellow fire
			if (lump >= 154 && lump <= 158) {
				int r = 192 - random_factor;
				int g = 160 - random_factor;
				int b = 64 - random_factor;

				uint32_t color = (r << 16) | (g << 8) | b;

				R_AddProjectileLight(thing->x, thing->y, thing->z + (35<<16),
									160, color, atz, yellow_fire_l);
			}

			// candle (see Altar of Pain)
			if (lump == 184 || lump == 185) {
				int r = 224 - random_factor;
				int g = 102 - random_factor;
				int b = 32 - random_factor;

				uint32_t color = (r << 16) | (g << 8) | b;

				if (gamemap == 18) {
					int vx = viewx >> 16;
					int vy = viewy >> 16;
					int tvx = thing->x >> 16;
					int tvy = thing->y >> 16;

					if (192 < vy && vy < 1860) {
#if 0
						if (670 < tvy && tvy < 1148) {
							if (-1244 < tvx && tvx < -1174) {
								if (!map18_c1) {
									map18_c1 = 1;
									R_AddProjectileLight(-1200 << 16, 900 << 16,
														thing->z + (32<<16), 400,
														color, atz, candle_l);
								}
							} else if (-1010 < tvx && tvx < -950) {
								if (!map18_c2) {
									map18_c2 = 2;
									R_AddProjectileLight(-980 << 16, 900 << 16,
														thing->z + (32<<16), 400,
														color, atz, candle_l);								
								}
							}

						}
						else
#endif							
						if (-3394 < vx && vx < -2116) {
							if (1010 < tvy && tvy < 1090) {
								if (-3075 < tvx && tvx < -2350) {
									if (!map18_c1) {
										map18_c1 = 1;
										R_AddProjectileLight(-2800 << 16, 990 << 16,
															thing->z + (32<<16), 400,
															color, atz, candle_l);
									}
								}
							} else if (320 < tvy && tvy < 1010) {
								if (-2440 < tvx && tvx < -2360) {
									if (!map18_c2) {
										map18_c2 = 2;
										R_AddProjectileLight(-2464 << 16, 650 << 16,
															thing->z + (32<<16), 400,
															color, atz, candle_l);
									}
								} else if (-3250 < tvx && tvx < -3150) {
									if (!map18_c3) {
										map18_c3 = 3;
										R_AddProjectileLight(-3132 << 16, 650 << 16,
															thing->z + (32<<16), 400,
															color, atz, candle_l);
									}
								}
							} else if (190 < tvy && tvy < 320) {
								if (-3075 < tvx && tvx < -2350) {
									if (!map18_c4) {
										map18_c4 = 4;
										R_AddProjectileLight(-2800 << 16, 290 << 16,
															thing->z + (32<<16), 400,
															color, atz, candle_l);

										R_AddProjectileLight(-2460 << 16, 320 << 16,
															thing->z + (32<<16), 256,
															color, atz, candle_l);

									}
								}
							} else {
								R_AddProjectileLight(thing->x, thing->y,
										thing->z + (32<<16), 128, color,
										atz, candle_l);
							}
						} else {
							R_AddProjectileLight(thing->x, thing->y,
											thing->z + (32<<16), 128, color,
											atz, candle_l);
						}
					} else {
						R_AddProjectileLight(thing->x, thing->y,
										thing->z + (32<<16), 128, color,
										atz, candle_l);
					}
				} else if (gamemap == 22) {
					int cvx = thing->x >> 16;
					int cvy = thing->y >> 16;

					if (-1922 < cvx && cvx < -512) {
						if (488 < cvy && cvy < 1566) {
							if (!map22_candle1) {
								map22_candle1 = 1;
								R_AddProjectileLight(-1635 << 16, 1245 << 16,
													thing->z + (32<<16), 384,
													color, atz, candle_l);
								R_AddProjectileLight(-1635 << 16, 800 << 16,
													thing->z + (32<<16), 384,
													color, atz, candle_l);
							}
						}
					}
				} else if (gamemap == 16) {
					int cvx = thing->x >> 16;
					int cvy = thing->y >> 16;

					if (1380 < cvx && cvx < 1690) {
						if (-1975 < cvy && cvy < -1920) {
							if (!map16_candle1) {
								map16_candle1 = 1;
								R_AddProjectileLight(1536 << 16, -1952 << 16,
													thing->z + (32<<16), 256,
													color, atz, candle_l);
							}
						}
					} else if (1264 < cvx && cvx < 1330) {
						if (-2175 < cvy && cvy < -1983) {
							if (!map16_candle2) {
								map16_candle2 = 2;
								R_AddProjectileLight(1292 << 16, -2080 << 16,
													thing->z + (32<<16), 256,
													color, atz, candle_l);
							}
						}
					} else if (1580 < cvx && cvx < 1745) {
						if (-2500 < cvy && cvy < -2425) {
							if (!map16_candle3) {
								map16_candle3 = 3;
								R_AddProjectileLight(1663 << 16, -2462 << 16,
													thing->z + (32<<16), 256,
													color, atz, candle_l);
							}
						}
					} else if (1743 < cvx && cvx < 1874) {
						if (-2618 < cvy && cvy < -2495) {
							if (!map16_candle4) {
								map16_candle4 = 4;
								R_AddProjectileLight(1774 << 16, -2590 << 16,
													thing->z + (32<<16), 256,
													color, atz, candle_l);
							}							
						}
					} else if (1965 < cvx && cvx < 2125) {
						if (-2618 < cvy && cvy < -2560) {
							if (!map16_candle5) {
								map16_candle5 = 5;
								R_AddProjectileLight(2048 << 16, -2590 << 16,
													thing->z + (32<<16), 256,
													color, atz, candle_l);
							}
						}
					}
				} else {
					R_AddProjectileLight(thing->x, thing->y,
										thing->z + (32<<16), 128, color,
										atz, candle_l);
				}
			}

			// red keycard / skull key
			if (lump == 188 || lump == 208) {
				int r = 255 - random_factor;

				uint32_t color = (r << 16);

				R_AddProjectileLight(thing->x, thing->y, thing->z + (20<<16),
									160, color, atz, red_key_l);
			}

			// yellow keycard / skull key
			if (lump == 189 || lump == 210) {
				int r = 255 - random_factor;
				int g = 255 - random_factor;

				uint32_t color = (r << 16) | (g << 8);

				R_AddProjectileLight(thing->x, thing->y, thing->z + (20<<16),
									160, color, atz, yellow_key_l);
			}

			// blue keycard / skull key
			if (lump == 190 || lump == 209) {
				int b = 255 - random_factor;

				uint32_t color = b;

				R_AddProjectileLight(thing->x, thing->y, thing->z + (24<<16),
									160, color, atz, blue_key_l);
			}

			// rockets and barrels
			if (lump >= 211 && lump <= 220) {
				// 255 127 0
				float radius = 304;
				float r = (float)(255 - random_factor);
				float g = (float)(127 - random_factor);
				int zofs = 8;
				if (lump > 215) {
					float scale =
						1.0f /
						((float)((lump - 216) * 0.5f) +
						 1);
					r *= scale;
					g *= scale;
					radius -= 24;//16;
				}
				uint32_t color = ((int)r << 16) | ((int)g << 8);
				// 216 to 220 are when it hits and disappears
				R_AddProjectileLight(thing->x, thing->y, thing->z + (zofs<<16),
									radius, color, atz, rocket_barrel_l);
			}

			// tracers
			if (lump >= 221 && lump <= 237) {
				// 255 127 0
				float radius = 256;
				float r = (float)(255 - random_factor);
				float g = (float)(127 - random_factor);

				if (lump > 230) {
					float scale = 1.0f / ((float)((lump - 231) * 0.5f) + 1);
					r *= scale;
					g *= scale;
					radius -= 2;
				}

				uint32_t color = ((int)r << 16) | ((int)g << 8);

				R_AddProjectileLight(thing->x, thing->y,
									thing->z + (20<<16), radius, color,
									atz, trac_l);
			}

			// normal imp
			if (lump >= 238 && lump <= 246) { // 246
				// 255 127 0
				float radius = 280;//256;

				float r = (float)(255 - random_factor);
				float g = (float)(127 - random_factor);

				if (lump > 240) {
					float scale = 1.0f / ((float)((lump - 241) * 0.5f) + 1);
					r *= scale;
					g *= scale;
					radius -= 2;
				}
				uint32_t color = ((int)r << 16) | ((int)g << 8);

				// 241 to 246 are when it hits and disappears
				R_AddProjectileLight(thing->x, thing->y, thing->z + (16<<16),
									radius, color, atz, imp_ball_l);
			}

			// nightmare imp
			if (lump >= 247 && lump <= 255) { //255) {
				float radius = 280;//256;
				float r = (float)(0x1a + 0x8a - random_factor);
				float g = (float)(0x1a + 0x2b - random_factor);
				float b = (float)(0x1a + 0xe2 - random_factor);

				if (lump > 249) {
					float scale =
						1.0f /
						((float)((lump - 250) * 0.5f) +
						 1);
					r *= scale;
					g *= scale;
					b *= scale;
					radius -= 2;
				}
				uint32_t color = ((int)r << 16) |
						 ((int)g << 8) | (int)b;

				// 250 to 255 are when it hits and disappears
				R_AddProjectileLight(thing->x, thing->y, thing->z + (16<<16),
									radius, color, atz, nite_ball_l);
			}

			// hell knight
			if (lump >= 256 && lump <= 263) { //269) {
				float radius = 256;
				float g = (float)(255 - random_factor);
				// 264
				if (lump > 263) {
					float scale =
						1.0f /
						((float)((lump - 264) * 0.5f) +
						 1);
					g *= scale;
					radius -= 2;
				}
				uint32_t color = ((int)g << 8);
				R_AddProjectileLight(thing->x, thing->y, thing->z + (16<<16),
									radius, color,-1,hell_fire_l);
			}

			// baron of hell
			if (lump >= 270 && lump <= 277) { //283) {
				float radius = 256;
				float r = (float)(255 - random_factor);
				// 278
				if (lump > 277) {
					float scale =
						1.0f /
						((float)((lump - 278) * 0.5f) +
						 1);
					r *= scale;
					radius -= 2;
				}
				uint32_t color = ((int)r << 16);
				R_AddProjectileLight(thing->x, thing->y,
							thing->z + (16<<16), radius, color,
							-1, baro_fire_l);
			}

			// mancubus???
			if (lump >= 284 && lump <= 298) { //304) {
				float radius = 256;

				float r = (float)(255 - random_factor);
				float g = (float)(127 - random_factor);

				if (lump > 298) {
					float scale =
						1.0f /
						((float)((lump - 299) * 0.5f) +
						 1);
					r *= scale;
					g *= scale;
					radius -= 2;
				}
				uint32_t color = ((int)r << 16) | ((int)g << 8);

				// 299 to 304 are when it hits and disappears
				R_AddProjectileLight(thing->x, thing->y,
							thing->z + (26<<16), radius, color,
							-1, manc_rocket_l);
			}

			// cacodemon
			if (lump >= 305 && lump <= 307) { // 314) {
				// 255 63 0
				float radius = 256;
				float r = (float)(255 - random_factor);
				float g = (float)(63 - random_factor);

				if (lump > 307) {
					float scale =
						1.0f /
						((float)((lump - 308) * 0.5f) +
						 1);
					r *= scale;
					g *= scale;
					radius -= 2;
				}
				uint32_t color = ((int)r << 16) | ((int)g << 8);

				// 308 to 314 are when it hits and disappears
				R_AddProjectileLight(thing->x, thing->y,
									thing->z + (20<<16), radius, color,
									-1, caco_ball_l);
			}

			// bfg
			if (lump >= 315 && lump <= 322) {//316) { //322) {
				float radius = 304;
				float g = (float)(255 - random_factor);
				// 317
				if (lump > 316) {
					float scale =
						1.0f /
						((float)((lump - 317) * 0.5f) +
						 1);
					g *= scale;
					radius -= 24;
				}
				uint32_t color = ((int)g << 8);
				R_AddProjectileLight(thing->x, thing->y,
									thing->z + (32<<16), radius, color, atz, bfg_l);
			}

			// plasma
			if (lump >= 323 && lump <= 330) { //324) { //330) {
				float radius = 304;
				float b = (float)(255 - random_factor);
				// 325
				if (lump > 324) {
					float scale =
						1.0f /
						((float)((lump - 325) *
							 0.5f) +
						 1);
					b *= scale;
					radius -= 24;
				}
				uint32_t color = b;
				R_AddProjectileLight(thing->x, thing->y,
									thing->z + (16<<16), radius,
									color, atz, plasma_l);
			}

			// spider shot
			if (lump >= 331 && lump <= 332) { //338) {
				//dbgio_printf("\tlight for lump %d\n", lump);
				float radius = 224;
				float r = (float)(0x8a - random_factor);
				float g = (float)(0xa3 - random_factor);
				float b = (float)(0xfa - random_factor);
				// 333
				if (lump > 332) {
					float scale =
						1.0f /
						((float)((lump - 333) *
							 0.5f) +
						 1);
					r *= scale;
					g *= scale;
					b *= scale;
					radius -= 2;
				}
				uint32_t color = ((int)r << 16) |
						 ((int)g << 8) | (int)b;

				R_AddProjectileLight(thing->x, thing->y,
							 thing->z + (16<<16), radius,
							 color,-1,spider_l);
			}

			// skul
			if (lump >= 619 && lump <= 649) { //658) {
				//dbgio_printf("\tlight for lump %d\n", lump);
				// 255 127 0
				float radius = 224;
				float r = (float)(128 - random_factor);
				float g = (float)(63 - random_factor);
				uint32_t color = ((int)r << 16) |
						 ((int)g << 8);
				if (lump > 649) {
					float scale =
						1.0f /
						((float)((lump - 650) *
							 0.5f) +
						 1);
					r *= scale;
					g *= scale;
					radius -= 2;
				}

				//if (skull_count < 4) {
					R_AddProjectileLight(thing->x, thing->y,
								 thing->z + (40<<16),//(80<<16), 
								 radius,
								 color,-1,skull_l);
				/*	skull_count++;
				} else {
					player_t *p;

					p = &players[0];

					fixed_t dx = D_abs(p->mo->x - x);
					fixed_t dy = D_abs(p->mo->y - y);	

					if (quickDistCheck(dx,dy,(640<<16))) {
						dx >>= 16;
						dy >>= 16;
						float dist = fsqrt(((float)dx*(float)dx) + ((float)dy*(float)dy));
						
						for (int li=0;li<lightidx+1;li++) {
							if (light_type[li] == spider_l) {
								if (projectile_lights[li].distance > dist) {
									R_AddProjectileLight(thing->x, thing->y,
								 thing->z + (40<<16),//(80<<16), 
								 radius,
								 color,li,skull_l);
									break;
								}
							}
						}
					}					
				}*/
			}
			
			// thing is behind view plane?
			if (tz < MINZ)
				continue;

			// too far off the side?
			if (tx > (tz << 1) || tx < -(tz << 1))
				continue;

			visspritehead->zdistance = tz;
			visspritehead->thing = thing;
			visspritehead->lump = lump;
			visspritehead->flip = flip;
			visspritehead->next = NULL;
			visspritehead->sector = sub->sector;

			data = (byte *)W_CacheLumpNum(lump, PU_CACHE, dec_jag);

			CurSub = sub;
			if (tz < MAXZ) {
				if (thing->flags & (MF_CORPSE | MF_SHOOTABLE)) {
					x = ((SwapShort(((spriteN64_t *)data)
								->width) >> 1) *
						 viewsin);
					y = ((SwapShort(((spriteN64_t *)data)
								->width) >> 1) *
						 viewcos);

					pSub = R_PointInSubsector(
						(thing->x - x), (thing->y + y));
					if ((pSub->drawindex) &&
						(pSub->drawindex <
						 sub->drawindex)) {
						CurSub = pSub;
					}

					pSub = R_PointInSubsector(
						(thing->x + x), (thing->y - y));
					if ((pSub->drawindex) &&
						(pSub->drawindex <
						 CurSub->drawindex)) {
						CurSub = pSub;
					}
				}
			}

			VisSrpCur = CurSub->vissprite;
			VisSrpNew = NULL;

			if (VisSrpCur) {
				VisSrpCurTmp = VisSrpCur;
				while ((VisSrpCur = VisSrpCurTmp,
					tz < VisSrpCur->zdistance)) {
					VisSrpCur = VisSrpCurTmp->next;
					VisSrpNew = VisSrpCurTmp;

					if (VisSrpCur == NULL)
						break;

					VisSrpCurTmp = VisSrpCur;
				}
			}

			if (VisSrpNew)
				VisSrpNew->next = visspritehead;
			else
				CurSub->vissprite = visspritehead;

			visspritehead->next = VisSrpCur;

			numdrawvissprites++;
			visspritehead++;
		}
	}
}

void R_RenderBSPNodeNoClip(int bspnum) // 80024E64
{
	subsector_t *sub;
	seg_t *line;
	int count;
	node_t *bsp;
	int side;
	fixed_t dx, dy;
	fixed_t left, right;

	while (!(bspnum & NF_SUBSECTOR)) {
		bsp = &nodes[bspnum];

		// Decide which side the view point is on.
		//side = R_PointOnSide(viewx, viewy, bsp);
		dx = (viewx - bsp->line.x);
		dy = (viewy - bsp->line.y);

		left = (bsp->line.dy >> 16) * (dx >> 16);
		right = (dy >> 16) * (bsp->line.dx >> 16);

		if (right < left)
			side = 1; /* back side */
		else
			side = 0; /* front side */

		R_RenderBSPNodeNoClip(bsp->children[side ^ 1]);

		bspnum = bsp->children[side];
	}

	// subsector with contents
	// add all the drawable elements in the subsector

	numdrawsubsectors++;

	sub = &subsectors[bspnum & ~NF_SUBSECTOR];
	sub->drawindex = numdrawsubsectors;

	*endsubsector = sub; //copy subsector
	endsubsector++;

	frontsector = sub->sector;

	line = &segs[sub->firstline];
	count = sub->numlines;
	do {
		line->flags |= 1; /* Render each line */
		++line; /* Inc the line pointer */
	} while (--count); /* All done? */
}
