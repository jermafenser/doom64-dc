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
pvr_poly_hdr_t __attribute__((aligned(32))) pvr_sprite_hdr;
pvr_poly_hdr_t __attribute__((aligned(32))) pvr_sprite_hdr_nofilter;

pvr_poly_hdr_t __attribute__((aligned(32))) pvr_sprite_hdr_bump;
pvr_poly_hdr_t __attribute__((aligned(32))) pvr_sprite_hdr_nofilter_bump;

void *pwepnbump;
pvr_ptr_t wepnbump_txr = 0;
pvr_poly_cxt_t wepnbump_cxt;
pvr_poly_hdr_t __attribute__((aligned(32))) wepnbump_hdr;

pvr_ptr_t wepndecs_txr;
pvr_poly_cxt_t wepndecs_cxt;
pvr_poly_hdr_t __attribute__((aligned(32))) wepndecs_hdr;
pvr_poly_hdr_t __attribute__((aligned(32))) wepndecs_hdr_nofilter;

// see doomdef.h
const char *fnpre = STORAGE_PREFIX;

char fnbuf[256];

uint16_t *printtex;
pvr_ptr_t dlstex = 0;

extern pvr_dr_state_t dr_state;

void W_DrawLoadScreen(char *what, int current, int total)
{
	pvr_poly_cxt_t load_cxt;
	pvr_poly_hdr_t __attribute__((aligned(32))) load_hdr;
	pvr_poly_cxt_t load2_cxt;
	pvr_poly_hdr_t __attribute__((aligned(32))) load2_hdr;

	printtex = (uint16_t *)malloc(256 * 32 * sizeof(uint16_t));
	if (!printtex) {
		I_Error("OOM for status bar texture");
	}
	memset(printtex, 0, 256 * 32 * sizeof(uint16_t));

	if (dlstex) {
		pvr_mem_free(dlstex);
		dlstex = 0;
	}
	dlstex = pvr_mem_malloc(256 * 32 * sizeof(uint16_t));
	if (!dlstex) {
		I_Error("PVR OOM for status bar texture");
	}

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

	sprintf(fnbuf, "%s/tex/wepn_decs.raw", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s", fnbuf);
	}
	wepndecs_txr = pvr_mem_malloc(64*64);
	if (!wepndecs_txr) {
		I_Error("PVR OOM for muzzle flash texture");
	}
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


	sprintf(fnbuf, "%s/tex/sawg_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s", fnbuf);
	}
	all_comp_wepn_bumps[0] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[0]) {
		I_Error("Could not allocate wepnbump 0");
	}
	memcpy(all_comp_wepn_bumps[0], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/pung_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s", fnbuf);
	}
	all_comp_wepn_bumps[1] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[1]) {
		I_Error("Could not allocate wepnbump 1");
	}
	memcpy(all_comp_wepn_bumps[1], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/pisg_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s", fnbuf);
	}
	all_comp_wepn_bumps[2] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[2]) {
		I_Error("Could not allocate wepnbump 2");
	}
	memcpy(all_comp_wepn_bumps[2], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/sht1_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s", fnbuf);
	}
	all_comp_wepn_bumps[3] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[3]) {
		I_Error("Could not allocate wepnbump 3");
	}
	memcpy(all_comp_wepn_bumps[3], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/sht2_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s", fnbuf);
	}
	all_comp_wepn_bumps[4] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[4]) {
		I_Error("Could not allocate wepnbump 4");
	}
	memcpy(all_comp_wepn_bumps[4], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/chgg_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s", fnbuf);
	}
	all_comp_wepn_bumps[5] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[5]) {
		I_Error("Could not allocate wepnbump 5");
	}
	memcpy(all_comp_wepn_bumps[5], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/rock_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s", fnbuf);
	}
	all_comp_wepn_bumps[6] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[6]) {
		I_Error("Could not allocate wepnbump 6");
	}
	memcpy(all_comp_wepn_bumps[6], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/plas_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s", fnbuf);
	}
	all_comp_wepn_bumps[7] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[7]) {
		I_Error("Could not allocate wepnbump 7");
	}
	memcpy(all_comp_wepn_bumps[7], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/bfgg_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s", fnbuf);
	}
	all_comp_wepn_bumps[8] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[8]) {
		I_Error("Could not allocate wepnbump 8");
	}
	memcpy(all_comp_wepn_bumps[8], pwepnbump, vqsize);
	free(pwepnbump);

	sprintf(fnbuf, "%s/tex/lasr_nrm.cmp", fnpre);
	vqsize = fs_load(fnbuf, &pwepnbump);
	if (vqsize == -1) {
		I_Error("Could not load %s", fnbuf);
	}
	all_comp_wepn_bumps[9] = memalign(32,vqsize);
	if (!all_comp_wepn_bumps[9]) {
		I_Error("Could not allocate wepnbump 9");
	}
	memcpy(all_comp_wepn_bumps[9], pwepnbump, vqsize);
	free(pwepnbump);
}

extern void P_FlushSprites(void);
extern void __attribute__((noinline)) P_FlushAllCached(void);
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
	if (wepnbump_txr) {
		pvr_mem_free(wepnbump_txr);
		wepnbump_txr = 0;
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

	if (w*h*2 > pvr_mem_available()) {
//		dbgio_printf("very low on vram\n");
		P_FlushAllCached();
	}

	wepnbump_txr = pvr_mem_malloc(w*h*2);
	if (!wepnbump_txr) {
		I_Error("PVR OOM for weapon normal map texture");
	}
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

int extra_episodes = 0;

uint8_t md5_lostlevel[7][16] = {
{0xc0,0xb6,0x50,0x82,0x8e,0x55,0x2e,0x4c,0x75,0xa9,0x3c,0xcb,0x39,0x4c,0x89,0x75},
{0x11,0x3e,0x8c,0x80,0x46,0x9b,0x53,0xd6,0xab,0x92,0xcd,0x47,0x71,0x53,0x52,0x0a},
{0x54,0x59,0xda,0x76,0xa3,0xdf,0x1d,0xfa,0x0a,0x32,0x60,0x02,0xe4,0xe9,0x04,0xbe},
{0x62,0x34,0xa3,0xad,0x54,0x2f,0x44,0x41,0xc9,0xf9,0x1e,0xab,0x77,0x42,0xaf,0xc6},
{0x60,0x5b,0x30,0x72,0x6b,0x9b,0x0d,0xb6,0x8d,0x8e,0x62,0x6b,0x42,0x33,0x7a,0xe9},
{0x63,0xf6,0x9f,0x1b,0x9a,0xdf,0xf7,0xb8,0x94,0x2b,0x70,0x4d,0x89,0x41,0x32,0xec},
{0xf4,0x2b,0x74,0xf5,0xa5,0xa6,0xa7,0xa4,0x62,0xc0,0xcf,0xdb,0xfe,0x4a,0xd5,0xe7},
};

int size_lostlevel[7] = {
253568,
360456,
311768,
395260,
361192,
215276,
61996
};

#include "md5.h"
uint8 md5sum[16];
MD5_CTX ctx;

int kneedeep_only = 0;

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

	size_t loadsize;
	unsigned char *chunk = NULL;

	extra_episodes = -6;

	chunk = malloc(65536);
	if (!chunk) {
		extra_episodes = 0;
		goto skip_ee_check;
	}

	for (int i = 34; i <= 40; i++) {
		sprintf(fnbuf, "%s/maps/map%d.wad", fnpre, i);
		file_t mapfd = fs_open(fnbuf, O_RDONLY);
		if (-1 != mapfd) {
			extra_episodes++;
			MD5Init(&ctx);

			size_t mapsize = size_lostlevel[i - 34];
			while (mapsize > 65536) {
				fs_read(mapfd, (void *)chunk, 65536);
				mapsize -= 65536;
				MD5Update(&ctx, chunk, 65536);
			}
			memset(chunk, 0, 65536);
			fs_read(mapfd, (void *)chunk, mapsize);
			fs_close(mapfd);
			MD5Update(&ctx, chunk, mapsize);
			MD5Final(md5sum, &ctx);
			if (memcmp(md5sum, md5_lostlevel[i - 34], 16))
			{
				extra_episodes = 0;
				break;
			}
		} else {
			extra_episodes = 0;
			break;
		}
	}

	sprintf(fnbuf, "%s/maps/map%d.wad", fnpre, 41);
	file_t mapfd = fs_open(fnbuf, O_RDONLY);
	if (-1 != mapfd) {
		if (extra_episodes == 0) {
			kneedeep_only = 1;
		}
		dbgio_printf("found map 41\n");
		extra_episodes++;
		fs_close(mapfd);
	}

skip_ee_check:
	if (chunk)
		free(chunk);

	W_DrawLoadScreen("Palettes", 33, 100);
	sprintf(fnbuf, "%s/doom64monster.pal", fnpre);
	loadsize = fs_load(fnbuf, (void **)&pal1);
	if (-1 == loadsize) {
		I_Error("Could not load %s", fnbuf);
	}

	W_DrawLoadScreen("Palettes", 66, 100);
	sprintf(fnbuf, "%s/doom64nonenemy.pal", fnpre);
	loadsize = fs_load(fnbuf, (void **)&pal2);
	if (-1 == loadsize) {
		I_Error("Could not load %s", fnbuf);
	}

	W_DrawLoadScreen("Palettes", 100, 100);

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

	pvr_ptr_t back_tex = 0;
	back_tex = pvr_mem_malloc(512 * 512 * 2);
	memset(back_tex, 0xff, 512 * 512 * 2);
	void *backbuf = NULL;
	sprintf(fnbuf, startupfile, fnpre);
	fs_load(fnbuf, &backbuf);

	if (backbuf) {
		pvr_txr_load(backbuf, back_tex, 512 * 512 * 2);
		MD5Init(&ctx);
		MD5Update(&ctx, backbuf, 512*512*2);
		MD5Final(backres, &ctx);
		if (memcmp(backres, backcheck, 16)) {
			I_Error(waderrstr);
		}
	}
	else {
		I_Error(waderrstr);
	}

	pvr_wait_ready();
	pvr_poly_cxt_t backcxt;
	pvr_poly_hdr_t __attribute__((aligned(32))) backhdr;
	pvr_poly_cxt_txr(&backcxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565, 512, 512, back_tex, PVR_FILTER_BILINEAR);
	pvr_poly_compile(&backhdr, &backcxt);
	pvr_vertex_t *backvert;

	for (int i = 0; i < 300; i++) {
		vid_waitvbl();
		pvr_scene_begin();
		pvr_list_begin(PVR_LIST_OP_POLY);
		pvr_dr_init(&dr_state);

		pvr_vertex_t *hdr1 = pvr_dr_target(dr_state);
		memcpy(hdr1, &backhdr, sizeof(pvr_poly_hdr_t));
		pvr_dr_commit(hdr1);

		backvert = pvr_dr_target(dr_state);
		backvert->argb = 0xffffffff;
		backvert->oargb = 0;
		backvert->flags = PVR_CMD_VERTEX;
		backvert->x = 0.0f;
		backvert->y = 0.0f;
		backvert->z = 1.0f;
		backvert->u = 0.0f;
		backvert->v = 0.0f;
		pvr_dr_commit(backvert);

		backvert = pvr_dr_target(dr_state);
		backvert->argb = 0xffffffff;
		backvert->oargb = 0;
		backvert->flags = PVR_CMD_VERTEX;
		backvert->x = 640.0f;
		backvert->y = 0.0f;
		backvert->z = 1.0f;
		backvert->u = 1.0f;//320.0f / 512.0f;
		backvert->v = 0.0f;
		pvr_dr_commit(backvert);

		backvert = pvr_dr_target(dr_state);
		backvert->flags = PVR_CMD_VERTEX;
		backvert->x = 0.0f;
		backvert->y = 480.0f;
		backvert->z = 1.0f;
		backvert->u = 0.0f;
		backvert->v = 1.0f;//240.0f / 512.0f;
		backvert->argb = 0xffffffff;
		backvert->oargb = 0;
		pvr_dr_commit(backvert);

		backvert = pvr_dr_target(dr_state);
		backvert->argb = 0xffffffff;
		backvert->oargb = 0;
		backvert->flags = PVR_CMD_VERTEX_EOL;
		backvert->x = 640.0f;
		backvert->y = 480.0f;
		backvert->z = 1.0f;
		backvert->u = 1.0f;//320.0f / 512.0f;
		backvert->v = 1.0f;//240.0f / 512.0f;
		pvr_dr_commit(backvert);

		pvr_list_finish();
		pvr_scene_finish();

		pvr_wait_ready();
	}

	if (back_tex)
		pvr_mem_free(back_tex);
	if (backbuf)
		free(backbuf);

	// weapon bumpmaps
	load_all_comp_wepn_bumps();	

	// all non-enemy sprites are in an uncompressed, pretwiddled 8bpp 1024^2 sheet texture
	W_DrawLoadScreen("Item Tex", 50, 100);
	sprintf(fnbuf, "%s/tex/non_enemy.tex", fnpre);
	loadsize = fs_load(fnbuf, &pnon_enemy);
	if (-1 == loadsize) {
		I_Error("Could not load %s", fnbuf);
	}

	W_DrawLoadScreen("Item Tex", 50, 100);
	dbgio_printf("non_enemy loaded size is %d\n", loadsize);
	pvr_non_enemy = pvr_mem_malloc(loadsize);
	if (!pvr_non_enemy) {
		I_Error("PVR OOM for non-enemy texture");
	}
	pvr_txr_load(pnon_enemy, pvr_non_enemy, loadsize);
	free(pnon_enemy);

	W_DrawLoadScreen("Item Tex", 100, 100);

	dbgio_printf("PVR mem free after non_enemy: %lu\n",
		     pvr_mem_available());

	// doom64 wad
	dbgio_printf("W_Init: Loading IWAD into RAM...\n");

	wadfileptr = (wadinfo_t *)Z_Alloc(sizeof(wadinfo_t), PU_STATIC, NULL);
	sprintf(fnbuf, "%s/pow2.wad", fnpre); // doom64.wad
	wad_file = fs_open(fnbuf, O_RDONLY);
	if (-1 == wad_file) {
		I_Error("Could not open %s for reading.", fnbuf);
	}

	size_t full_wad_size = fs_seek(wad_file, 0, SEEK_END);
	size_t wad_rem_size = full_wad_size;
	fullwad = malloc(wad_rem_size);
	if (!fullwad) {
		I_Error("OOM for %s", fnbuf);
	}
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
		I_Error("Could not open %s for reading.", fnbuf);
	}
	size_t alt_wad_size = fs_seek(s2_file, 0, SEEK_END);
	wad_rem_size = alt_wad_size;
	s2wad = malloc(wad_rem_size);
	if (!s2wad) {
		I_Error("OOM for %s", fnbuf);
	}
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
		I_Error("Could not open %s for reading.", fnbuf);
	}
	size_t bump_wad_size = fs_seek(bump_file, 0, SEEK_END);
	wad_rem_size = bump_wad_size;
	bumpwad = malloc(wad_rem_size);
	if (!bumpwad) {
		I_Error("OOM for %s", fnbuf);
	}
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

static uint32_t Swap32(uint32_t val)
{
	return ((((val)&0xff000000) >> 24) | (((val)&0x00ff0000) >> 8) |
		(((val)&0x0000ff00) << 8) | (((val)&0x000000ff) << 24));
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
	char __attribute__((aligned(8))) name8[8];
	char c;
	char *tmp;
	int i;
	lumpinfo_t *lump_p;

	// end-swap the masks instead of having to do it to all of the names
	hibit1 = Swap32(hibit1);
	hibit2 = Swap32(hibit2);

	/* make the name into two integers for easy compares */
	*(int *)&name8[4] = 0;
	*(int *)&name8[0] = 0;

	tmp = name8;
	while ((c = *name) != 0) {
		*tmp++ = c;
		if ((tmp >= name8+8))
			break;
		name++;
	}

	/* scan backwards so patch lump files take precedence */

	lump_p = lumpinfo;
	for (i = 0; i < numlumps; i++) {
		if ((*(int *)&name8[0] == (*(int *)&lump_p->name[0] & hibit1)) &&
			(*(int *)&name8[4] == (*(int *)&lump_p->name[4] & hibit2))) {
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

int W_GetNumForName(char *name)
{
#if RANGECHECK
	int i;

	i = W_CheckNumForName(name, 0x7fffffff, 0xFFFFFFFF);
	if (i != -1)
		return i;

	I_Error("W_GetNumForName: %s not found!", name);
	return -1;
#endif
	return W_CheckNumForName(name, 0x7fffffff, 0xffffffff);
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

int W_LumpLength(int lump)
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

void W_ReadLump(int lump, void *dest, decodetype dectype)
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

void *W_CacheLumpNum(int lump, int tag, decodetype dectype)
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

void *W_CacheLumpName(char *name, int tag, decodetype dectype)
{
	return W_CacheLumpNum(W_GetNumForName(name), tag, dectype);
}

#define MIN(a,b) ((a) < (b) ? (a) : (b))

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
int W_S2_CheckNumForName(char *name)
{
	lumpinfo_t *lump_p;
	char __attribute__((aligned(8))) name8[8];
	char __attribute__((aligned(8))) lumpname[8];

	int n_len = MIN(8, strlen(name));

	memset(name8, 0, 8);
	memcpy(name8, name, n_len);

	lump_p = s2_lumpinfo;

	for (int i = 0; i < s2_numlumps; i++) {
		int ln_len = MIN(8, strlen(lump_p->name));
		memset(lumpname, 0, 8);
		memcpy(lumpname, lump_p->name, ln_len);
		// always jag compressed
		lumpname[0] &= 0x7f;
		if (!memcmp(name8, lumpname, 8)) {
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

int W_S2_GetNumForName(char *name)
{
#if RANGECHECK
	int i;

	i = W_S2_CheckNumForName(name);
	if (i != -1)
		return i;

	//I_Error("W_S2_GetNumForName: %s not found!", name);
	return -1;
#endif

	return W_S2_CheckNumForName(name);
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

int W_S2_LumpLength(int lump)
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
void W_S2_ReadLump(int lump, void *dest, decodetype dectype)
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

void *W_S2_CacheLumpNum(int lump, int tag, decodetype dectype)
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

void *W_S2_CacheLumpName(char *name, int tag, decodetype dectype)
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
= Returns -1 if name not found or lump is 0 size
= 0 size lump means bumpmap didnt exist at generation time
=
====================
*/
int W_Bump_CheckNumForName(char *name)
{
	lumpinfo_t *lump_p;
	char __attribute__((aligned(8))) name8[8];
	char __attribute__((aligned(8))) lumpname[8];

	int n_len = MIN(8, strlen(name));

	memset(name8, 0, 8);
	memcpy(name8, name, n_len);

	lump_p = bump_lumpinfo;

	for (int i = 0; i < bump_numlumps; i++) {
		int ln_len = MIN(8, strlen(lump_p->name));

		memset(lumpname, 0, 8);
		memcpy(lumpname, lump_p->name, ln_len);

		if (!memcmp(name8, lumpname, 8)) {
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

int W_Bump_GetNumForName(char *name)
{
#if RANGECHECK
	int i;

	i = W_Bump_CheckNumForName(name);
	if (i != -1)
		return i;

#if 0 //RANGECHECK
	I_Error("W_Bump_GetNumForName: %s not found!", name);
#endif
	return -1;
#endif

	return W_Bump_CheckNumForName(name);
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

int W_Bump_LumpLength(int lump)
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

void W_Bump_ReadLump(int lump, void *dest, int w, int h)
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
void W_OpenMapWad(int mapnum)
{
	int infotableofs;
	char name[8];
	//	int lump, size;

	if (mapnum == 0) {
		dbgio_printf("requested map 0, not crashing, giving you 1 instead\n");
		mapnum = 1;
	}

	name[0] = 'm';
	name[1] = 'a';
	name[2] = 'p';
	name[3] = '0' + (char)(mapnum / 10);
	name[4] = '0' + (char)(mapnum % 10);
	name[5] = 0;

	sprintf(fnbuf, "%s/maps/%s.wad", fnpre, name);
	file_t mapfd = fs_open(fnbuf, O_RDONLY);
	if (-1 == mapfd) {
		I_Error("Could not open %s for reading.", fnbuf);
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

void W_FreeMapLump(void)
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

int W_MapLumpLength(int lump)
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

int W_MapGetNumForName(char *name)
{
	char name8[8];
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
