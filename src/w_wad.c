/* W_wad.c */

#include "doomdef.h"

/*=============== */
/*   TYPES */
/*=============== */

file_t wad_file;
file_t s2_file;
file_t bump_file;

void *fullwad;
void *s2wad;
void *bumpwad;

typedef struct {
	char identification[4]; /* should be IWAD */
	int numlumps;
	int infotableofs;
} wadinfo_t;

/*============= */
/* GLOBALS */
/*============= */

static lumpcache_t *lumpcache;
static int numlumps;
static lumpinfo_t *lumpinfo;

static lumpcache_t *s2_lumpcache;
static int s2_numlumps;
static lumpinfo_t *s2_lumpinfo;

static lumpcache_t *bump_lumpcache;
static int bump_numlumps;
static lumpinfo_t *bump_lumpinfo;

static int mapnumlumps;
static lumpinfo_t *maplump;
static byte *mapfileptr;

/*=========*/
/* EXTERNS */
/*=========*/

/*
============================================================================

						LUMP BASED ROUTINES

============================================================================
*/

/*
====================
=
= W_Init
=
====================
*/

void *pnon_enemy;

pvr_ptr_t pvr_non_enemy;
pvr_poly_cxt_t pvr_sprite_cxt;
pvr_poly_hdr_t pvr_sprite_hdr;
pvr_poly_hdr_t pvr_sprite_hdr_nofilter;

pvr_poly_hdr_t pvr_sprite_hdr_bump;
pvr_poly_hdr_t pvr_sprite_hdr_nofilter_bump;

void *pwepnbump;
pvr_ptr_t wepnbump_txr;
pvr_poly_cxt_t wepnbump_cxt;
pvr_poly_hdr_t wepnbump_hdr;

pvr_ptr_t wepndecs_txr;
pvr_poly_cxt_t wepndecs_cxt;
pvr_poly_hdr_t wepndecs_hdr;
pvr_poly_hdr_t wepndecs_hdr_nofilter;

// see doomdef.h
const char *fnpre = STORAGE_PREFIX;

char fnbuf[256];

uint16_t *printtex;
pvr_ptr_t dlstex = 0;

extern pvr_dr_state_t dr_state;

void W_DrawLoadScreen(char *what, int current, int total)
{
	pvr_poly_cxt_t load_cxt;
	pvr_poly_hdr_t load_hdr;
	pvr_poly_cxt_t load2_cxt;
	pvr_poly_hdr_t load2_hdr;

	printtex = (uint16_t *)malloc(256 * 32 * sizeof(uint16_t));
	memset(printtex, 0, 256 * 32 * sizeof(uint16_t));
	if (dlstex) {
		pvr_mem_free(dlstex);
		dlstex = 0;
	}
	dlstex = pvr_mem_malloc(256 * 32 * sizeof(uint16_t));

	pvr_poly_cxt_txr(&load_cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_ARGB1555, 256,
			 32, dlstex, PVR_FILTER_NONE);
	load_cxt.blend.src = PVR_BLEND_ONE;
	load_cxt.blend.dst = PVR_BLEND_ONE;
	pvr_poly_compile(&load_hdr, &load_cxt);

	pvr_poly_cxt_col(&load2_cxt, PVR_LIST_OP_POLY);
	load2_cxt.blend.src = PVR_BLEND_ONE;
	load2_cxt.blend.dst = PVR_BLEND_ONE;
	pvr_poly_compile(&load2_hdr, &load2_cxt);

	char fullstr[256];
	sprintf(fullstr, "Loading %s", what);
	bfont_set_encoding(BFONT_CODE_ISO8859_1);
	bfont_draw_str_ex(printtex, 256, 0xffffffff, 0xff000000, 16, 1,
			  fullstr);
	pvr_txr_load_ex(printtex, dlstex, 256, 32, PVR_TXRLOAD_16BPP);
	free(printtex);

	uint32_t color = 0xff404040;
	uint32_t color2 = 0xff800000;
	uint32_t color3 = 0xffc00000;

	vid_waitvbl();
	pvr_scene_begin();
	pvr_list_begin(PVR_LIST_OP_POLY);
	pvr_dr_init(&dr_state);

	pvr_vertex_t *hdr1 = pvr_dr_target(dr_state);
	memcpy(hdr1, &load_hdr, sizeof(pvr_poly_hdr_t));
	pvr_dr_commit(hdr1);

	pvr_vertex_t *vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX;
	vert->x = 242.0f;
	vert->y = (((480 / 2) - 16));
	vert->z = 4.9f;
	vert->u = 0.0f;
	vert->v = 24.0f / 32.0f;
	vert->argb = 0xffffffff;
	pvr_dr_commit(vert);

	vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX;
	vert->x = 242.0f;
	vert->y = (((480 / 2) - 16) - 24);
	vert->z = 4.9f;
	vert->u = 0.0f;
	vert->v = 0.0f;
	vert->argb = 0xffffffff;
	pvr_dr_commit(vert);

	vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX;
	vert->x = 242.0f + 256.0f;
	vert->y = (((480 / 2) - 16));
	vert->z = 4.9f;
	vert->u = 1.0f;
	vert->v = 24.0f / 32.0f;
	vert->argb = 0xffffffff;
	pvr_dr_commit(vert);

	vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX_EOL;
	vert->x = 242.0f + 256.0f;
	vert->y = (((480 / 2) - 16) - 24);
	vert->z = 4.9f;
	vert->u = 1.0f;
	vert->v = 0.0f;
	vert->argb = 0xffffffff;
	pvr_dr_commit(vert);

	pvr_vertex_t *hdr2 = pvr_dr_target(dr_state);
	memcpy(hdr2, &load2_hdr, sizeof(pvr_poly_hdr_t));
	pvr_dr_commit(hdr2);

	vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX;
	vert->x = 240.0f;
	vert->y = (480 / 2) + 8;
	vert->z = 5.0f;
	vert->argb = color;
	pvr_dr_commit(vert);

	vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX;
	vert->x = 240.0f;
	vert->y = (480 / 2) - 16;
	vert->z = 5.0f;
	vert->argb = color;
	pvr_dr_commit(vert);

	vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX;
	vert->x = 480.0f;
	vert->y = (480 / 2) + 8;
	vert->z = 5.0f;
	vert->argb = color;
	pvr_dr_commit(vert);

	vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX_EOL;
	vert->x = 480.0f;
	vert->y = (480 / 2) - 16;
	vert->z = 5.0f;
	vert->argb = color;
	pvr_dr_commit(vert);

	vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX;
	vert->x = 242.0f;
	vert->y = (480 / 2) + 6;
	vert->z = 5.1f;
	vert->argb = color2;
	pvr_dr_commit(vert);

	vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX;
	vert->x = 242.0f;
	vert->y = (480 / 2) - 14;
	vert->z = 5.1f;
	vert->argb = color2;
	pvr_dr_commit(vert);

	vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX;
	vert->x = 242.0f + (236.0f * (float)current / (float)total);
	vert->y = (480 / 2) + 6;
	vert->z = 5.1f;
	vert->argb = color2;
	pvr_dr_commit(vert);

	vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX_EOL;
	vert->x = 242.0f + (236.0f * (float)current / (float)total);
	vert->y = (480 / 2) - 14;
	vert->z = 5.1f;
	vert->argb = color2;
	pvr_dr_commit(vert);

	vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX;
	vert->x = 243.0f;
	vert->y = (480 / 2) + 5;
	vert->z = 5.2f;
	vert->argb = color3;
	pvr_dr_commit(vert);

	vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX;
	vert->x = 243.0f;
	vert->y = (480 / 2) - 13;
	vert->z = 5.2f;
	vert->argb = color3;
	pvr_dr_commit(vert);

	vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX;
	vert->x = 243.0f + (234.0f * (float)current / (float)total);
	vert->y = (480 / 2) + 5;
	vert->z = 5.2f;
	vert->argb = color3;
	pvr_dr_commit(vert);

	vert = pvr_dr_target(dr_state);
	vert->flags = PVR_CMD_VERTEX_EOL;
	vert->x = 243.0f + (234.0f * (float)current / (float)total);
	vert->y = (480 / 2) - 13;
	vert->z = 5.2f;
	vert->argb = color3;
	pvr_dr_commit(vert);

	pvr_list_finish();
	pvr_scene_finish();
	pvr_wait_ready();
}

/*
typedef enum {
        wp_chainsaw,
        wp_fist,
        wp_pistol,
        wp_shotgun,
        wp_supershotgun, // [psx]
        wp_chaingun,
        wp_missile,
        wp_plasma,
        wp_bfg,
        wp_laser, // [d64]
        NUMWEAPONS,
        wp_nochange
} weapontype_t;
*/

weapontype_t active_wepn = wp_nochange;

static uint8_t __attribute__((aligned(32))) *all_comp_wepn_bumps[10];

static void load_all_comp_wepn_bumps(void) {
	size_t vqsize;

	sprintf(fnbuf, "%s/tex/WEPN_DECS.raw", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s\n", fnbuf);
	}
	wepndecs_txr = pvr_mem_malloc(64*64);
	pvr_txr_load_ex(pwepnbump, wepndecs_txr, 64, 64, PVR_TXRLOAD_8BPP);
	free(pwepnbump);

	pvr_poly_cxt_txr(&wepndecs_cxt, PVR_LIST_TR_POLY,
		PVR_TXRFMT_PAL8BPP | PVR_TXRFMT_8BPP_PAL(1) |
		PVR_TXRFMT_TWIDDLED,
		64, 64, wepndecs_txr,
		PVR_FILTER_BILINEAR);
	wepndecs_cxt.gen.specular = PVR_SPECULAR_ENABLE;
	wepndecs_cxt.gen.fog_type = PVR_FOG_TABLE;
	wepndecs_cxt.gen.fog_type2 = PVR_FOG_TABLE;
	pvr_poly_compile(&wepndecs_hdr, &wepndecs_cxt);
	
	pvr_poly_cxt_txr(&wepndecs_cxt, PVR_LIST_TR_POLY,
		PVR_TXRFMT_PAL8BPP | PVR_TXRFMT_8BPP_PAL(1) |
		PVR_TXRFMT_TWIDDLED,
		64, 64, wepndecs_txr,
		PVR_FILTER_NONE);
	wepndecs_cxt.gen.specular = PVR_SPECULAR_ENABLE;
	wepndecs_cxt.gen.fog_type = PVR_FOG_TABLE;
	wepndecs_cxt.gen.fog_type2 = PVR_FOG_TABLE;
	pvr_poly_compile(&wepndecs_hdr_nofilter, &wepndecs_cxt);


	sprintf(fnbuf, "%s/tex/SAWG_NRM.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s\n", fnbuf);
	}
	all_comp_wepn_bumps[0] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[0]) {
		I_Error("Could not allocate wepnbump 0\n");
	}
	memcpy(all_comp_wepn_bumps[0], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/PUNG_NRM.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s\n", fnbuf);
	}
	all_comp_wepn_bumps[1] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[1]) {
		I_Error("Could not allocate wepnbump 1\n");
	}
	memcpy(all_comp_wepn_bumps[1], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/PISG_NRM.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s\n", fnbuf);
	}
	all_comp_wepn_bumps[2] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[2]) {
		I_Error("Could not allocate wepnbump 2\n");
	}
	memcpy(all_comp_wepn_bumps[2], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/SHT1_NRM.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s\n", fnbuf);
	}
	all_comp_wepn_bumps[3] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[3]) {
		I_Error("Could not allocate wepnbump 3\n");
	}
	memcpy(all_comp_wepn_bumps[3], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/SHT2_NRM.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s\n", fnbuf);
	}
	all_comp_wepn_bumps[4] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[4]) {
		I_Error("Could not allocate wepnbump 4\n");
	}
	memcpy(all_comp_wepn_bumps[4], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/CHGG_NRM.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s\n", fnbuf);
	}
	all_comp_wepn_bumps[5] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[5]) {
		I_Error("Could not allocate wepnbump 5\n");
	}
	memcpy(all_comp_wepn_bumps[5], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/ROCK_NRM.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s\n", fnbuf);
	}
	all_comp_wepn_bumps[6] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[6]) {
		I_Error("Could not allocate wepnbump 6\n");
	}
	memcpy(all_comp_wepn_bumps[6], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/PLAS_NRM.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s\n", fnbuf);
	}
	all_comp_wepn_bumps[7] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[7]) {
		I_Error("Could not allocate wepnbump 7\n");
	}
	memcpy(all_comp_wepn_bumps[7], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/BFGG_NRM.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s\n", fnbuf);
	}
	all_comp_wepn_bumps[8] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[8]) {
		I_Error("Could not allocate wepnbump 8\n");
	}
	memcpy(all_comp_wepn_bumps[8], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/LASR_NRM.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s\n", fnbuf);
	}
	all_comp_wepn_bumps[9] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[9]) {
		I_Error("Could not allocate wepnbump 9\n");
	}
	memcpy(all_comp_wepn_bumps[9], pwepnbump, vqsize);
	free(pwepnbump);
}

void W_ReplaceWeaponBumps(weapontype_t wepn)
{
	int w,h;
	
	if (wepn == wp_nochange) {
		return;
	}
	if (gamemap == 33) {
		return;
	}

	// clean up old bump texture
	if (active_wepn != wepn && active_wepn != wp_nochange) {
		pvr_mem_free(wepnbump_txr);
	}

	active_wepn = wepn;

	switch (wepn) {
		case wp_chainsaw:
			w = 512;
			h = 128;
			break;

		case wp_fist:
			w = 512;
			h = 64;
			break;

		case wp_pistol:
			w = 256;
			h = 128;
			break;

		case wp_shotgun:
			w = 256;
			h = 128;
			break;

		case wp_supershotgun:
			w = 256;
			h = 64;
			break;

		case wp_chaingun:
			w = 256;
			h = 128;
			break;

		case wp_missile:
			w = 256;
			h = 128;
			break;

		case wp_plasma:
			w = 256;
			h = 128;
			break;

		case wp_bfg:
			w = 512;
			h = 128;
			break;

		case wp_laser:
			w = 256;
			h = 128;
			break;

		default:
			active_wepn = wp_nochange;
			return;
			break;
	}

	if (w*h*4 > pvr_mem_available()) {
		dbgio_printf("very low on vram\n");
	}

	wepnbump_txr = pvr_mem_malloc(w*h*2);
	decode_bumpmap((uint8_t *)&all_comp_wepn_bumps[wepn][0], (uint8_t *)wepnbump_txr, w, h);

	pvr_poly_cxt_txr(&wepnbump_cxt, PVR_LIST_TR_POLY,
		PVR_TXRFMT_BUMP | PVR_TXRFMT_TWIDDLED,
		w, h, wepnbump_txr,
		PVR_FILTER_BILINEAR);

	wepnbump_cxt.gen.specular = PVR_SPECULAR_ENABLE;
	wepnbump_cxt.txr.env = PVR_TXRENV_DECAL;
	wepnbump_cxt.blend.src = PVR_BLEND_ONE;
	wepnbump_cxt.blend.dst = PVR_BLEND_ZERO;
	wepnbump_cxt.blend.src_enable = 0;
	wepnbump_cxt.blend.dst_enable = 1;

	pvr_poly_compile(&wepnbump_hdr, &wepnbump_cxt);
}

void W_Init(void)
{
	wadinfo_t *wadfileptr;
	wadinfo_t *s2_wadfileptr;
	wadinfo_t *bump_wadfileptr;

	int infotableofs;
	int s2_infotableofs;
	int bump_infotableofs;

	short *pal1;
	short *pal2;

	W_DrawLoadScreen("Palettes", 0, 100);
	timer_spin_sleep(15);

	sprintf(fnbuf, "%s/doom64monster.pal", fnpre);
	fs_load(fnbuf, (void **)&pal1);

	W_DrawLoadScreen("Palettes", 50, 100);
	timer_spin_sleep(15);

	sprintf(fnbuf, "%s/doom64nonenemy.pal", fnpre);
	fs_load(fnbuf, (void **)&pal2);

	W_DrawLoadScreen("Palettes", 100, 100);
	timer_spin_sleep(15);

	pvr_set_pal_format(PVR_PAL_ARGB1555);
	for (int i = 1; i < 256; i++) {
		pvr_set_pal_entry(i, 0x8000 | pal1[i]);
	}

	for (int i = 1; i < 256; i++) {
		pvr_set_pal_entry(256 + i, 0x8000 | pal2[i]);
	}

	// color 0 is always transparent (replacing RGB ff 00 ff)
	pvr_set_pal_entry(0, 0);
	pvr_set_pal_entry(256, 0);

	// weapon bumpmaps
	load_all_comp_wepn_bumps();	

	// all non-enemy sprites are in an uncompressed, pretwiddled 8bpp 1024^2 sheet texture
	W_DrawLoadScreen("Item Tex", 0, 100);
	timer_spin_sleep(15);

	sprintf(fnbuf, "%s/tex/non_enemy.tex", fnpre);
	size_t vqsize = fs_load(fnbuf, &pnon_enemy);

	W_DrawLoadScreen("Item Tex", 50, 100);
	timer_spin_sleep(15);

	dbgio_printf("non_enemy loaded size is %d\n", vqsize);
	pvr_non_enemy = pvr_mem_malloc(vqsize);
	pvr_txr_load(pnon_enemy, pvr_non_enemy, vqsize);
	free(pnon_enemy);

	W_DrawLoadScreen("Item Tex", 100, 100);
	timer_spin_sleep(15);

	dbgio_printf("PVR mem free after non_enemy: %lu\n",
		     pvr_mem_available());

	// doom64 wad
	dbgio_printf("W_Init: Loading IWAD into RAM...\n");

	wadfileptr = (wadinfo_t *)Z_Alloc(sizeof(wadinfo_t), PU_STATIC, NULL);
	sprintf(fnbuf, "%s/pow2.wad", fnpre); // doom64.wad
	wad_file = fs_open(fnbuf, O_RDONLY);
	if (-1 == wad_file) {
		I_Error("Could not open %s for reading.\n", fnbuf);
	}

	size_t full_wad_size = fs_seek(wad_file, 0, SEEK_END);
	size_t wad_rem_size = full_wad_size;
	fullwad = malloc(wad_rem_size);
	size_t wad_read = 0;
	fs_seek(wad_file, 0, SEEK_SET);
	while (wad_rem_size > (128 * 1024)) {
		fs_read(wad_file, (void *)fullwad + wad_read, (128 * 1024));
		wad_read += (128 * 1024);
		wad_rem_size -= (128 * 1024);
		W_DrawLoadScreen("Doom 64 IWAD", wad_read, full_wad_size);
	}
	fs_read(wad_file, (void *)fullwad + wad_read, wad_rem_size);
	wad_read += wad_rem_size;
	W_DrawLoadScreen("Doom 64 IWAD", wad_read, full_wad_size);
	malloc_stats();
	dbgio_printf("Done.\n");
	fs_close(wad_file);

	memcpy((void *)wadfileptr, fullwad + 0, sizeof(wadinfo_t));
	if (D_strncasecmp(&wadfileptr->identification[1], "WAD", 3))
		I_Error("W_Init: invalid main IWAD id");
	numlumps = (wadfileptr->numlumps);
	lumpinfo = (lumpinfo_t *)Z_Malloc(numlumps * sizeof(lumpinfo_t),
					  PU_STATIC, 0);
	infotableofs = (wadfileptr->infotableofs);
	memcpy((void *)lumpinfo, fullwad + infotableofs,
	       numlumps * sizeof(lumpinfo_t));
	lumpcache = (lumpcache_t *)Z_Malloc(numlumps * sizeof(lumpcache_t),
					    PU_STATIC, 0);
	D_memset(lumpcache, 0, numlumps * sizeof(lumpcache_t));
	Z_Free(wadfileptr);

	// alternate palette sprite wad
	dbgio_printf("W_Init: Loading alt sprite PWAD into RAM...\n");

	s2_wadfileptr =
		(wadinfo_t *)Z_Alloc(sizeof(wadinfo_t), PU_STATIC, NULL);
	sprintf(fnbuf, "%s/alt.wad", fnpre);
	s2_file = fs_open(fnbuf, O_RDONLY);
	if (-1 == s2_file) {
		I_Error("Could not open %s for reading.\n", fnbuf);
	}
	size_t alt_wad_size = fs_seek(s2_file, 0, SEEK_END);
	wad_rem_size = alt_wad_size;
	s2wad = malloc(wad_rem_size);
	wad_read = 0;
	fs_seek(s2_file, 0, SEEK_SET);
	while (wad_rem_size > (128 * 1024)) {
		fs_read(s2_file, (void *)s2wad + wad_read, (128 * 1024));
		wad_read += (128 * 1024);
		wad_rem_size -= (128 * 1024);
		W_DrawLoadScreen("Sprite WAD", wad_read, alt_wad_size);
	}
	fs_read(s2_file, (void *)s2wad + wad_read, wad_rem_size);
	wad_read += wad_rem_size;
	W_DrawLoadScreen("Sprite WAD", wad_read, alt_wad_size);
	dbgio_printf("Done.\n");
	fs_close(s2_file);
	malloc_stats();

	memcpy((void *)s2_wadfileptr, s2wad + 0, sizeof(wadinfo_t));
	if (D_strncasecmp(s2_wadfileptr->identification, "PWAD", 4))
		I_Error("W_Init: invalid alt sprite PWAD id");
	s2_numlumps = (s2_wadfileptr->numlumps);
	s2_lumpinfo = (lumpinfo_t *)Z_Malloc(s2_numlumps * sizeof(lumpinfo_t),
					     PU_STATIC, 0);
	s2_infotableofs = (s2_wadfileptr->infotableofs);
	memcpy((void *)s2_lumpinfo, s2wad + s2_infotableofs,
	       s2_numlumps * sizeof(lumpinfo_t));
	s2_lumpcache = (lumpcache_t *)Z_Malloc(
		s2_numlumps * sizeof(lumpcache_t), PU_STATIC, 0);
	D_memset(s2_lumpcache, 0, s2_numlumps * sizeof(lumpcache_t));
	Z_Free(s2_wadfileptr);

	// compressed bumpmap wad
	dbgio_printf("W_Init: Loading bumpmap PWAD into RAM...\n");

	bump_wadfileptr =
		(wadinfo_t *)Z_Alloc(sizeof(wadinfo_t), PU_STATIC, NULL);
	sprintf(fnbuf, "%s/bump.wad", fnpre);
	bump_file = fs_open(fnbuf, O_RDONLY);
	if (-1 == bump_file) {
		I_Error("Could not open %s for reading.\n", fnbuf);
	}
	size_t bump_wad_size = fs_seek(bump_file, 0, SEEK_END);
	wad_rem_size = bump_wad_size;
	bumpwad = malloc(wad_rem_size);
	wad_read = 0;
	fs_seek(bump_file, 0, SEEK_SET);
	while (wad_rem_size > (128 * 1024)) {
		fs_read(bump_file, (void *)bumpwad + wad_read, (128 * 1024));
		wad_read += (128 * 1024);
		wad_rem_size -= (128 * 1024);
		W_DrawLoadScreen("Bumpmap WAD", wad_read, bump_wad_size);
	}
	fs_read(bump_file, (void *)bumpwad + wad_read, wad_rem_size);
	wad_read += wad_rem_size;
	W_DrawLoadScreen("Bumpmap WAD", wad_read, bump_wad_size);
	dbgio_printf("Done.\n");
	fs_close(bump_file);
	malloc_stats();

	memcpy((void *)bump_wadfileptr, bumpwad + 0, sizeof(wadinfo_t));
	if (D_strncasecmp(bump_wadfileptr->identification, "PWAD", 4))
		I_Error("W_Init: invalid bumpmap PWAD id");
	bump_numlumps = (bump_wadfileptr->numlumps);
	bump_lumpinfo = (lumpinfo_t *)Z_Malloc(
		bump_numlumps * sizeof(lumpinfo_t), PU_STATIC, 0);
	bump_infotableofs = (bump_wadfileptr->infotableofs);
	memcpy((void *)bump_lumpinfo, bumpwad + bump_infotableofs,
	       bump_numlumps * sizeof(lumpinfo_t));
	bump_lumpcache = (lumpcache_t *)Z_Malloc(
		bump_numlumps * sizeof(lumpcache_t), PU_STATIC, 0);
	D_memset(bump_lumpcache, 0, bump_numlumps * sizeof(lumpcache_t));
	Z_Free(bump_wadfileptr);

	// common shared poly context/header used for all non-enemy sprites
	// headers for sprite diffuse when no bumpmapping
	pvr_poly_cxt_txr(&pvr_sprite_cxt, PVR_LIST_TR_POLY,
			 PVR_TXRFMT_PAL8BPP | PVR_TXRFMT_8BPP_PAL(1) |
				 PVR_TXRFMT_TWIDDLED,
			 1024, 1024, pvr_non_enemy, PVR_FILTER_BILINEAR);
	pvr_sprite_cxt.gen.specular = PVR_SPECULAR_ENABLE;
	pvr_sprite_cxt.gen.fog_type = PVR_FOG_TABLE;
	pvr_sprite_cxt.gen.fog_type2 = PVR_FOG_TABLE;
	pvr_poly_compile(&pvr_sprite_hdr, &pvr_sprite_cxt);

	pvr_poly_cxt_txr(&pvr_sprite_cxt, PVR_LIST_TR_POLY,
			 PVR_TXRFMT_PAL8BPP | PVR_TXRFMT_8BPP_PAL(1) |
				 PVR_TXRFMT_TWIDDLED,
			 1024, 1024, pvr_non_enemy, PVR_FILTER_NONE);
	pvr_sprite_cxt.gen.specular = PVR_SPECULAR_ENABLE;
	pvr_sprite_cxt.gen.fog_type = PVR_FOG_TABLE;
	pvr_sprite_cxt.gen.fog_type2 = PVR_FOG_TABLE;
	pvr_poly_compile(&pvr_sprite_hdr_nofilter, &pvr_sprite_cxt);

	// headers for sprite diffuse when bumpmapping active (weapons)
	pvr_poly_cxt_txr(&pvr_sprite_cxt, PVR_LIST_TR_POLY,
			 PVR_TXRFMT_PAL8BPP | PVR_TXRFMT_8BPP_PAL(1) |
				 PVR_TXRFMT_TWIDDLED,
			 1024, 1024, pvr_non_enemy, PVR_FILTER_BILINEAR);
	pvr_sprite_cxt.gen.specular = PVR_SPECULAR_ENABLE;
	pvr_sprite_cxt.gen.fog_type = PVR_FOG_TABLE;
	pvr_sprite_cxt.gen.fog_type2 = PVR_FOG_TABLE;
	pvr_sprite_cxt.blend.src = PVR_BLEND_DESTCOLOR;
	pvr_sprite_cxt.blend.dst = PVR_BLEND_ZERO;
	pvr_sprite_cxt.blend.src_enable = 0;
	pvr_sprite_cxt.blend.dst_enable = 1;
	pvr_poly_compile(&pvr_sprite_hdr_bump, &pvr_sprite_cxt);

	pvr_poly_cxt_txr(&pvr_sprite_cxt, PVR_LIST_TR_POLY,
			 PVR_TXRFMT_PAL8BPP | PVR_TXRFMT_8BPP_PAL(1) |
				 PVR_TXRFMT_TWIDDLED,
			 1024, 1024, pvr_non_enemy, PVR_FILTER_NONE);
	pvr_sprite_cxt.gen.specular = PVR_SPECULAR_ENABLE;
	pvr_sprite_cxt.gen.fog_type = PVR_FOG_TABLE;
	pvr_sprite_cxt.gen.fog_type2 = PVR_FOG_TABLE;
	pvr_sprite_cxt.blend.src = PVR_BLEND_DESTCOLOR;
	pvr_sprite_cxt.blend.dst = PVR_BLEND_ZERO;
	pvr_sprite_cxt.blend.src_enable = 0;
	pvr_sprite_cxt.blend.dst_enable = 1;
	pvr_poly_compile(&pvr_sprite_hdr_nofilter_bump, &pvr_sprite_cxt);
}

static char retname[9];
// return human-readable "uncompressed" name for a lump number
char *W_GetNameForNum(int num)
{
	memset(retname, 0, 9);
	int ln_len = strlen(lumpinfo[num].name);
	if (ln_len > 8)
		ln_len = 8;
	memcpy(retname, lumpinfo[num].name, ln_len);
	retname[0] &= 0x7f;

	return retname;
}

/*
====================
=
= W_CheckNumForName
=
= Returns -1 if name not found
=
====================
*/
int W_CheckNumForName(char *name, int hibit1, int hibit2)
{
	lumpinfo_t *lump_p;
	char name8[8];
	char hibit[8];
	hibit[0] = (hibit1 >> 24);
	hibit[1] = (hibit1 >> 16) & 0xff;
	hibit[2] = (hibit1 >> 8) & 0xff;
	hibit[3] = (hibit1 >> 0) & 0xff;
	hibit[4] = (hibit2 >> 24);
	hibit[5] = (hibit2 >> 16) & 0xff;
	hibit[6] = (hibit2 >> 8) & 0xff;
	hibit[7] = (hibit2 >> 0) & 0xff;

	memset(name8, 0, 8);
	int n_len = strlen(name);
	if (n_len > 8)
		n_len = 8;
	memcpy(name8, name, n_len);

	lump_p = lumpinfo;

	for (int i = 0; i < numlumps; i++) {
		char lumpname[8];
		memset(lumpname, 0, 8);
		int ln_len = strlen(lump_p->name);
		if (ln_len > 8)
			ln_len = 8;
		memcpy(lumpname, lump_p->name, ln_len);
		lumpname[0] &= hibit[0];
		lumpname[1] &= hibit[1];
		lumpname[2] &= hibit[2];
		lumpname[3] &= hibit[3];
		lumpname[4] &= hibit[4];
		lumpname[5] &= hibit[5];
		lumpname[6] &= hibit[6];
		lumpname[7] &= hibit[7];

		int res = memcmp(name8, lumpname, 8);
		if (!res) {
			return i;
		}
		lump_p++;
	}

	return -1;
}

/*
====================
=
= W_GetNumForName
=
= Calls W_CheckNumForName, but bombs out if not found
=
====================
*/

int W_GetNumForName(char *name) // 8002C1B8
{
	int i;

	i = W_CheckNumForName(name, 0x7fffffff, 0xFFFFFFFF);
	if (i != -1)
		return i;

#if RANGECHECK
	I_Error("W_GetNumForName: %s not found!", name);
#endif
	return -1;
}

/*
====================
=
= W_LumpLength
=
= Returns the buffer size needed to load the given lump
=
====================
*/

int W_LumpLength(int lump) // 8002C204
{
#if RANGECHECK
	if ((lump < 0) || (lump >= numlumps))
		I_Error("W_LumpLength: lump %i out of range", lump);
#endif
	return lumpinfo[lump].size;
}

/*
====================
=
= W_ReadLump
=
= Loads the lump into the given buffer, which must be >= W_LumpLength()
=
====================
*/
// 256 kb buffer to replace Z_Alloc in W_ReadLump
static u64 input_w_readlump[32768];
static byte *input = (byte *)input_w_readlump;

void W_ReadLump(int lump, void *dest, decodetype dectype) // 8002C260
{
	lumpinfo_t *l;
	int lumpsize;
#if RANGECHECK
	if ((lump < 0) || (lump >= numlumps))
		I_Error("W_ReadLump: lump %i out of range", lump);
#endif
	l = &lumpinfo[lump];
	if (dectype != dec_none) {
		if ((l->name[0] & 0x80)) /* compressed */
		{
			lumpsize = l[1].filepos - (l->filepos);
			memcpy((void *)input, fullwad + l->filepos, lumpsize);
			if (dectype == dec_jag)
				DecodeJaguar((byte *)input, (byte *)dest);
			else // dec_d64
				DecodeD64((byte *)input, (byte *)dest);

			return;
		}
	}

	if (l->name[0] & 0x80)
		lumpsize = l[1].filepos - (l->filepos);
	else
		lumpsize = (l->size);

	memcpy((void *)dest, fullwad + l->filepos, lumpsize);
}

/*
====================
=
= W_CacheLumpNum
=
====================
*/

void *W_CacheLumpNum(int lump, int tag, decodetype dectype) // 8002C430
{
	int lumpsize;
	lumpcache_t *lc;
#if RANGECHECK
	if ((lump < 0) || (lump >= numlumps))
		I_Error("W_CacheLumpNum: lump %i out of range", lump);
#endif
	lc = &lumpcache[lump];

	if (!lc->cache) { /* read the lump in */
		if (dectype == dec_none)
			lumpsize = lumpinfo[lump + 1].filepos -
				   lumpinfo[lump].filepos;
		else
			lumpsize = lumpinfo[lump].size;

		Z_Malloc(lumpsize, tag, &lc->cache);
		W_ReadLump(lump, lc->cache, dectype);
	} else {
		if (tag & PU_CACHE) {
			Z_Touch(lc->cache);
		}
	}

	return lc->cache;
}

/*
====================
=
= W_CacheLumpName
=
====================
*/

void *W_CacheLumpName(char *name, int tag, decodetype dectype) // 8002C57C
{
	return W_CacheLumpNum(W_GetNumForName(name), tag, dectype);
}

/*
alt sprite routines
*/
/*
====================
=
= W_S2_CheckNumForName
=
= Returns -1 if name not found
=
====================
*/
int W_S2_CheckNumForName(char *name, int hibit1, int hibit2)
{
	lumpinfo_t *lump_p;
	char name8[8];
	char hibit[8];
	hibit[0] = (hibit1 >> 24);
	hibit[1] = (hibit1 >> 16) & 0xff;
	hibit[2] = (hibit1 >> 8) & 0xff;
	hibit[3] = (hibit1 >> 0) & 0xff;
	hibit[4] = (hibit2 >> 24);
	hibit[5] = (hibit2 >> 16) & 0xff;
	hibit[6] = (hibit2 >> 8) & 0xff;
	hibit[7] = (hibit2 >> 0) & 0xff;

	memset(name8, 0, 8);
	int n_len = strlen(name);
	if (n_len > 8)
		n_len = 8;
	memcpy(name8, name, n_len);

	lump_p = s2_lumpinfo;

	for (int i = 0; i < s2_numlumps; i++) {
		char lumpname[8];
		memset(lumpname, 0, 8);
		int ln_len = strlen(lump_p->name);
		if (ln_len > 8)
			ln_len = 8;
		memcpy(lumpname, lump_p->name, ln_len);
		lumpname[0] &= hibit[0];
		lumpname[1] &= hibit[1];
		lumpname[2] &= hibit[2];
		lumpname[3] &= hibit[3];
		lumpname[4] &= hibit[4];
		lumpname[5] &= hibit[5];
		lumpname[6] &= hibit[6];
		lumpname[7] &= hibit[7];

		int res = memcmp(name8, lumpname, 8);
		if (!res) {
			return i;
		}
		lump_p++;
	}

	return -1;
}

/*
====================
=
= W_S2_GetNumForName
=
= Calls W_S2_CheckNumForName, but bombs out if not found
=
====================
*/

int W_S2_GetNumForName(char *name) // 8002C1B8
{
	int i;

	i = W_S2_CheckNumForName(name, 0x7fffffff, 0xFFFFFFFF);
	if (i != -1)
		return i;

#if RANGECHECK
	I_Error("W_S2_GetNumForName: %s not found!", name);
#endif
	return -1;
}

/*
====================
=
= W_S2_LumpLength
=
= Returns the buffer size needed to load the given lump
=
====================
*/

int W_S2_LumpLength(int lump) // 8002C204
{
#if RANGECHECK
	if ((lump < 0) || (lump >= s2_numlumps))
		I_Error("W_S2_LumpLength: lump %i out of range", lump);
#endif
	return s2_lumpinfo[lump].size;
}

/*
====================
=
= W_S2_ReadLump
=
= Loads the lump into the given buffer, which must be >= W_S2_LumpLength()
=
====================
*/
void W_S2_ReadLump(int lump, void *dest, decodetype dectype) // 8002C260
{
	lumpinfo_t *l;
	int lumpsize;
#if RANGECHECK
	if ((lump < 0) || (lump >= s2_numlumps))
		I_Error("W_S2_ReadLump: lump %i out of range", lump);
#endif
	l = &s2_lumpinfo[lump];
	if (dectype != dec_none) {
		if ((l->name[0] & 0x80)) /* compressed */
		{
			lumpsize = l[1].filepos - (l->filepos);
			memcpy((void *)input, s2wad + l->filepos, lumpsize);
			if (dectype == dec_jag)
				DecodeJaguar((byte *)input, (byte *)dest);
			else // dec_d64
				DecodeD64((byte *)input, (byte *)dest);

			return;
		}
	}

	if (l->name[0] & 0x80)
		lumpsize = l[1].filepos - (l->filepos);
	else
		lumpsize = (l->size);

	memcpy((void *)dest, s2wad + l->filepos, lumpsize);
}

/*
====================
=
= W_S2_CacheLumpNum
=
====================
*/

void *W_S2_CacheLumpNum(int lump, int tag, decodetype dectype) // 8002C430
{
	int lumpsize;
	lumpcache_t *lc;
#if RANGECHECK
	if ((lump < 0) || (lump >= s2_numlumps))
		I_Error("W_S2_CacheLumpNum: lump %i out of range", lump);
#endif
	lc = &s2_lumpcache[lump];

	if (!lc->cache) { /* read the lump in */
		if (dectype == dec_none)
			lumpsize = s2_lumpinfo[lump + 1].filepos -
				   s2_lumpinfo[lump].filepos;
		else
			lumpsize = s2_lumpinfo[lump].size;

		Z_Malloc(lumpsize, tag, &lc->cache);
		W_S2_ReadLump(lump, lc->cache, dectype);
	} else {
		if (tag & PU_CACHE) {
			Z_Touch(lc->cache);
		}
	}

	return lc->cache;
}

/*
====================
=
= W_S2_CacheLumpName
=
====================
*/

void *W_S2_CacheLumpName(char *name, int tag, decodetype dectype) // 8002C57C
{
	return W_S2_CacheLumpNum(W_S2_GetNumForName(name), tag, dectype);
}

/*
bumpmap routines
*/
/*
====================
=
= W_Bump_CheckNumForName
=
= Returns -1 if name not found
=
====================
*/
int W_Bump_CheckNumForName(char *name, int hibit1, int hibit2)
{
	lumpinfo_t *lump_p;
	char name8[8];
	char hibit[8];
	
	hibit[0] = (hibit1 >> 24);
	hibit[1] = (hibit1 >> 16) & 0xff;
	hibit[2] = (hibit1 >> 8) & 0xff;
	hibit[3] = (hibit1 >> 0) & 0xff;
	hibit[4] = (hibit2 >> 24);
	hibit[5] = (hibit2 >> 16) & 0xff;
	hibit[6] = (hibit2 >> 8) & 0xff;
	hibit[7] = (hibit2 >> 0) & 0xff;

	memset(name8, 0, 8);
	int n_len = strlen(name);
	if (n_len > 8)
		n_len = 8;
	memcpy(name8, name, n_len);

	lump_p = bump_lumpinfo;

	for (int i = 0; i < bump_numlumps; i++) {
		char lumpname[8];
		memset(lumpname, 0, 8);
		int ln_len = strlen(lump_p->name);
		if (ln_len > 8)
			ln_len = 8;
		memcpy(lumpname, lump_p->name, ln_len);
		lumpname[0] &= hibit[0];
		lumpname[1] &= hibit[1];
		lumpname[2] &= hibit[2];
		lumpname[3] &= hibit[3];
		lumpname[4] &= hibit[4];
		lumpname[5] &= hibit[5];
		lumpname[6] &= hibit[6];
		lumpname[7] &= hibit[7];

		int res = memcmp(name8, lumpname, 8);

		if (!res) {
			if (lump_p->size == 0) {
				return -1;
			}

			return i;
		}
		lump_p++;
	}

	return -1;
}

/*
====================
=
= W_Bump_GetNumForName
=
= Calls W_Bump_CheckNumForName, but bombs out if not found
=
====================
*/

int W_Bump_GetNumForName(char *name) // 8002C1B8
{
	int i;

	i = W_Bump_CheckNumForName(name, 0x7fffffff, 0xFFFFFFFF);
	if (i != -1)
		return i;

//	dbgio_printf("bump %s wasn't found\n", name);

#if RANGECHECK
	I_Error("W_Bump_GetNumForName: %s not found!", name);
#endif
	return -1;
}

/*
====================
=
= W_Bump_LumpLength
=
= Returns the buffer size needed to load the given lump
=
====================
*/

int W_Bump_LumpLength(int lump) // 8002C204
{
#if RANGECHECK
	if ((lump < 0) || (lump >= bump_numlumps))
		I_Error("W_Bump_LumpLength: lump %i out of range", lump);
#endif
	return bump_lumpinfo[lump].size;
}

/*
====================
=
= W_Bump_ReadLump
=
= Loads the lump into the given buffer, which must be >= W_Bump_LumpLength()
=
====================
*/
//static uint8_t __attribute__((aligned(32))) bumpbuf[4096];

void W_Bump_ReadLump(int lump, void *dest, int w, int h) // 8002C260
{
	lumpinfo_t *l;

#if RANGECHECK
	if ((lump < 0) || (lump >= bump_numlumps))
		I_Error("W_Bump_ReadLump: lump %i out of range", lump);
#endif
	l = &bump_lumpinfo[lump];
	decode_bumpmap((uint8_t *)(bumpwad + l->filepos), (uint8_t *)dest, w, h);
}

/*
============================================================================

MAP LUMP BASED ROUTINES

============================================================================
*/
#include <errno.h>
/*
====================
=
= W_OpenMapWad
=
= Exclusive Psx Doom / Doom64
====================
*/
void W_OpenMapWad(int mapnum) // 8002C5B0
{
	int infotableofs;
	char name[8];
	//	int lump, size;

	if (mapnum == 0) {
		dbgio_printf("requested map 0, not crashing, giving you 1 instead\n");
		mapnum = 1;
	}

	name[0] = 'M';
	name[1] = 'A';
	name[2] = 'P';
	name[3] = '0' + (char)(mapnum / 10);
	name[4] = '0' + (char)(mapnum % 10);
	name[5] = 0;

	sprintf(fnbuf, "%s/maps/%s.wad", fnpre, name);
	file_t mapfd = fs_open(fnbuf, O_RDONLY);
	if (-1 == mapfd) {
		I_Error("%d Could not open %s for reading.\n", errno, fnbuf);
	}
	size_t mapsize = fs_seek(mapfd, 0, SEEK_END);
	fs_seek(mapfd, 0, SEEK_SET);
	mapfileptr = Z_Alloc(mapsize, PU_STATIC, NULL);
	fs_read(mapfd, mapfileptr, mapsize);
	fs_close(mapfd);

	mapnumlumps = (((wadinfo_t *)mapfileptr)->numlumps);
	infotableofs = (((wadinfo_t *)mapfileptr)->infotableofs);

	maplump = (lumpinfo_t *)(mapfileptr + infotableofs);
}

/*
====================
=
= W_FreeMapLump
=
= Exclusive Doom64
====================
*/

void W_FreeMapLump(void) // 8002C748
{
	Z_Free(mapfileptr);
	mapnumlumps = 0;
}

/*
====================
=
= W_MapLumpLength
=
= Exclusive Psx Doom / Doom64
====================
*/

int W_MapLumpLength(int lump) // 8002C77C
{
#if RANGECHECK
	if (lump >= mapnumlumps)
		I_Error("W_MapLumpLength: %i out of range", lump);
#endif
	return maplump[lump].size;
}

/*
====================
=
= W_MapGetNumForName
=
= Exclusive Psx Doom / Doom64
====================
*/

int W_MapGetNumForName(char *name) // 8002C7D0
{
	char name8[12];
	char c, *tmp;
	int i;
	lumpinfo_t *lump_p;

	/* make the name into two integers for easy compares */

	*(int *)&name8[4] = 0;
	*(int *)&name8[0] = 0;

	tmp = name8;
	while ((c = *name) != 0) {
		*tmp++ = c;

		if ((tmp >= name8 + 8))
			break;

		name++;
	}

	/* scan backwards so patch lump files take precedence */

	lump_p = maplump;
	for (i = 0; i < mapnumlumps; i++) {
		if ((*(int *)&name8[0] ==
		     (*(int *)&lump_p->name[0] & 0x7fffffff)) &&
		    (*(int *)&name8[4] == (*(int *)&lump_p->name[4])))
			return i;

		lump_p++;
	}

	return -1;
}

/*
====================
=
= W_GetMapLump
=
= Exclusive Doom64
====================
*/

void *W_GetMapLump(int lump) // 8002C890
{
#if RANGECHECK
	if (lump >= mapnumlumps)
		I_Error("W_GetMapLump: lump %d out of range", lump);
#endif
	return (void *)((byte *)mapfileptr + maplump[lump].filepos);
}