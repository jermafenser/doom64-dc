#include "doomdef.h"
#include "r_local.h"
#include "p_local.h"
#include "sheets.h"

int firsttex;
int lasttex;
int numtextures;
int firstswx;
int *textures;

int firstsprite;
int lastsprite;
int numsprites;

int skytexture;

void R_InitTextures(void);
void R_InitSprites(void);
/*===========================================================================*/

#define PI_VAL 3.141592653589793
extern uint32_t next_pow2(uint32_t v);

/*
================
=
= R_InitData
=
= Locates all the lumps that will be used by all views
= Must be called after W_Init
=================
*/
#include <math.h>
#include <dc/pvr.h>

void R_InitStatus(void);
void R_InitFont(void);
void R_InitSymbols(void);

void R_InitData(void)
{
	// with single precision float
	// this table is not accurate enough for demos to sync
	// so I generated it offline on PC and put it in tables.c
#if 0
	int i;
	int val = 0;

	for(i = 0; i < (5*FINEANGLES/4); i++)
	{
		finesine[i] = (fixed_t) (sinf((((float) val * (float) PI_VAL) / 8192.0f)) * 65536.0f);
		val += 2;
	}
#endif
	R_InitStatus();
	R_InitFont();
	R_InitSymbols();
	R_InitTextures();
	R_InitSprites();
}

/*
==================
=
= R_InitTextures
=
= Initializes the texture list with the textures from the world map
=
==================
*/

pvr_ptr_t *bump_txr_ptr;
pvr_poly_cxt_t **bump_cxt;
pvr_poly_hdr_t **bump_hdrs;

pvr_ptr_t **pvr_texture_ptrs;
pvr_poly_cxt_t **txr_cxt_bump;
pvr_poly_cxt_t **txr_cxt_nobump;

pvr_poly_hdr_t **txr_hdr_bump;
pvr_poly_hdr_t **txr_hdr_nobump;

uint16_t tmp_8bpp_pal[256];

uint8_t *num_pal;

pvr_ptr_t pvrstatus;

extern pvr_sprite_hdr_t status_shdr;
extern pvr_sprite_cxt_t status_scxt;
extern pvr_sprite_txr_t status_stxr;

pvr_poly_cxt_t laser_cxt;
pvr_poly_hdr_t  __attribute__((aligned(32))) laser_hdr;

void R_InitStatus(void)
{
	uint16_t *status16;
	status16 = malloc(128 * 16 * sizeof(uint16_t));
	if (!status16) {
		I_Error("OOM for STATUS lump texture");
	}
	pvrstatus = pvr_mem_malloc(128 * 16 * 2);
	if (!pvrstatus) {
		I_Error("PVR OOM for STATUS lump texture");
	}
	// 1 tile, not compressed
	void *data = (byte *)W_CacheLumpName("STATUS", PU_CACHE, dec_jag);
	int width = (SwapShort(((spriteN64_t *)data)->width) + 7) & ~7;
	int height = SwapShort(((spriteN64_t *)data)->height);
	byte *src = data + sizeof(spriteN64_t);
	byte *offset = src + SwapShort(((spriteN64_t *)data)->cmpsize);
	// palette
	tmp_8bpp_pal[0] = 0;
	short *p = (short *)offset;
	p++;
	for (int j = 1; j < 256; j++) {
		short val = *p;
		p++;
		val = SwapShort(val);
		// Unpack and expand to 8bpp, then flip from BGR to RGB.
		u8 b = (val & 0x003E) << 2;
		u8 g = (val & 0x07C0) >> 3;
		u8 r = (val & 0xF800) >> 8;
		u8 a = 0xff;
		tmp_8bpp_pal[j] = get_color_argb1555(r, g, b, a);
	}

	int i = 0;
	int x1;
	int x2;

	for (int h = 1; h < 16; h += 2) {
		for (i = 0; i < width / 4; i += 2) {
			int *tmpSrc = (int *)(src + (h * 80));
			x1 = *(int *)(tmpSrc + i);
			x2 = *(int *)(tmpSrc + i + 1);

			*(int *)(tmpSrc + i) = x2;
			*(int *)(tmpSrc + i + 1) = x1;
		}
	}

	for (int h = 0; h < height; h++) {
		for (int w = 0; w < width; w++) {
			status16[w + (h * 128)] =
				tmp_8bpp_pal[src[w + (h * width)]];
		}
	}

	pvr_txr_load_ex(status16, pvrstatus, 128, 16, PVR_TXRLOAD_16BPP);

	pvr_sprite_cxt_txr(&status_scxt, PVR_LIST_TR_POLY,
						D64_TARGB,
						128, 16, pvrstatus,
						PVR_FILTER_NONE);
	pvr_sprite_compile(&status_shdr, &status_scxt);

	free(status16);
}

extern pvr_ptr_t pvrfont;
extern pvr_sprite_hdr_t font_shdr;
extern pvr_sprite_cxt_t font_scxt;
extern pvr_sprite_txr_t font_stxr;

void R_InitFont(void)
{
	uint8_t *font8;
	uint16_t *font16;
	int fontlump = W_GetNumForName("SFONT");
	pvrfont = pvr_mem_malloc(256 * 16 * 2);
	if (!pvrfont) {
		I_Error("PVR OOM for SFONT lump texture");
	}
	void *data = W_CacheLumpNum(fontlump, PU_CACHE, dec_jag);
	int width = SwapShort(((spriteN64_t *)data)->width);
	int height = SwapShort(((spriteN64_t *)data)->height);
	byte *src = data + sizeof(spriteN64_t);
	byte *offset = src + 0x800;

	font16 = (uint16_t *)malloc(256 * 16 * sizeof(uint16_t));
	if (!font16) {
		I_Error("OOM for indexed font data");
	}

	// palette
	short *p = (short *)offset;
	tmp_8bpp_pal[0] = 0;
	p++;
	for (int j = 1; j < 16; j++) {
		short val = *p;
		p++;
		val = SwapShort(val);
		// Unpack and expand to 8bpp, then flip from BGR to RGB.
		u8 b = (val & 0x003E) << 2;
		u8 g = (val & 0x07C0) >> 3;
		u8 r = (val & 0xF800) >> 8;
		u8 a = 0xff;
		tmp_8bpp_pal[j] = get_color_argb1555(r, g, b, a);
	}
	tmp_8bpp_pal[0] = 0;

	int size = (width * height) / 2;

	font8 = src;
	int mask = 32; //256 / 8;
	// Flip nibbles per byte
	for (int k = 0; k < size; k++) {
		byte tmp = font8[k];
		font8[k] = (tmp >> 4);
		font8[k] |= ((tmp & 0xf) << 4);
	}
	int *font32 = (int *)(src);
	// Flip each sets of dwords based on texture width
	for (int k = 0; k < size / 4; k += 2) {
		int x1;
		int x2;
		if (k & mask) {
			x1 = *(int *)(font32 + k);
			x2 = *(int *)(font32 + k + 1);
			*(int *)(font32 + k) = x2;
			*(int *)(font32 + k + 1) = x1;
		}
	}

	for (int j = 0; j < (width * height); j += 2) {
		uint8_t sps = font8[j >> 1];
		font16[j] = tmp_8bpp_pal[sps & 0xf];
		font16[j + 1] = tmp_8bpp_pal[(sps >> 4) & 0xf];
	}

	pvr_txr_load_ex(font16, pvrfont, 256, 16, PVR_TXRLOAD_16BPP);

	pvr_sprite_cxt_txr(&font_scxt, PVR_LIST_TR_POLY,
						D64_TARGB,
						256, 16, pvrfont,
						PVR_FILTER_NONE);
	pvr_sprite_compile(&font_shdr, &font_scxt);

	free(font16);
}

uint16_t *symbols16;
extern pvr_ptr_t pvr_symbols;
int symbols16size = 0;
int symbols16_w;
int symbols16_h;
int rawsymbol_w;
int rawsymbol_h;

extern pvr_sprite_hdr_t symbols_shdr;
extern pvr_sprite_cxt_t symbols_scxt;
extern pvr_sprite_txr_t symbols_stxr;

void R_InitSymbols(void)
{
	void *data;
	sprintf(fnbuf, "%s/symbols.raw", fnpre);
	fs_load(fnbuf, &data);
	byte *src = data + sizeof(gfxN64_t);

	int width = SwapShort(((gfxN64_t *)data)->width);
	int height = SwapShort(((gfxN64_t *)data)->height);

	symbols16_w = next_pow2(width);
	symbols16_h = next_pow2(height);
	symbols16size = (symbols16_w * symbols16_h * 2);

	rawsymbol_w = width;
	rawsymbol_h = height;

	pvr_symbols = pvr_mem_malloc(symbols16_w * symbols16_h * 2);
	if (!pvr_symbols) {
		I_Error("PVR OOM for SYMBOLS lump texture");
	}

	symbols16 = (uint16_t *)malloc(symbols16size);
	if (!symbols16) {
		I_Error("OOM for STATUS lump texture");
	}

	// Load Palette Data
	int offset = SwapShort(((gfxN64_t *)data)->width) *
		     SwapShort(((gfxN64_t *)data)->height);
	offset = (offset + 7) & ~7;
	// palette
	short *p = data + offset + sizeof(gfxN64_t);
	for (int j = 0; j < 256; j++) {
		short val = *p;
		p++;
		val = SwapShort(val);
		// Unpack and expand to 8bpp, then flip from BGR to RGB.
		u8 b = (val & 0x003E) << 2;
		u8 g = (val & 0x07C0) >> 3;
		u8 r = (val & 0xF800) >> 8;
		u8 a = (val & 1);
		tmp_8bpp_pal[j] = get_color_argb1555(r, g, b, a);
	}
	tmp_8bpp_pal[0] = 0;

	for (int h = 0; h < height; h++) {
		for (int w = 0; w < width; w++) {
			symbols16[w + (h * symbols16_w)] =
				tmp_8bpp_pal[src[w + (h * width)]];
		}
	}

	pvr_txr_load_ex(symbols16, pvr_symbols, symbols16_w, symbols16_h,
			PVR_TXRLOAD_16BPP);
	free(symbols16);

	pvr_sprite_cxt_txr(&symbols_scxt, PVR_LIST_TR_POLY,
						D64_TARGB,
						symbols16_w, symbols16_h, pvr_symbols,
						PVR_FILTER_NONE);
	pvr_sprite_compile(&symbols_shdr, &symbols_scxt);
}

uint8_t *pt;
pvr_poly_cxt_t flush_cxt;
pvr_poly_hdr_t __attribute__((aligned(32))) flush_hdr;

extern int lump_frame[575 + 310];
extern int used_lumps[575 + 310];

void R_InitTextures(void)
{
	int swx, i;

	firsttex = W_GetNumForName("T_START") + 1;
	lasttex = W_GetNumForName("T_END") - 1;
	numtextures = (lasttex - firsttex) + 1;
	pvr_texture_ptrs = (pvr_ptr_t **)malloc(numtextures * sizeof(pvr_ptr_t *));
	txr_cxt_bump = (pvr_poly_cxt_t **)malloc(numtextures *
					 sizeof(pvr_poly_cxt_t *));
	if (!txr_cxt_bump) {
		I_Error("R_InitTextures: could not malloc txr_cxt_bump* array");
	}
	txr_cxt_nobump = (pvr_poly_cxt_t **)malloc(numtextures *
						 sizeof(pvr_poly_cxt_t *));
	if (!txr_cxt_nobump) {
		I_Error("R_InitTextures: could not malloc txr_cxt_nobump* array");
	}
	txr_hdr_bump = (pvr_poly_hdr_t **)malloc(numtextures *
					 sizeof(pvr_poly_hdr_t *));
	if (!txr_hdr_bump) {
		I_Error("R_InitTextures: could not malloc txr_hdr_bump* array");
	}
	txr_hdr_nobump = (pvr_poly_hdr_t **)malloc(numtextures *
						 sizeof(pvr_poly_hdr_t *));
	if (!txr_hdr_nobump) {
		I_Error("R_InitTextures: could not malloc txr_hdr_nobump* array");
	}

#define ALL_SPRITES_INDEX (575 + 310)
	memset(used_lumps, 0xff, sizeof(int) * ALL_SPRITES_INDEX);
	memset(lump_frame, 0xff, sizeof(int) * ALL_SPRITES_INDEX);

	num_pal = (uint8_t *)malloc(numtextures);
	pt = (uint8_t *)malloc(numtextures);
	memset(pvr_texture_ptrs, 0, sizeof(pvr_ptr_t *) * numtextures);
	memset(txr_cxt_bump, 0, sizeof(pvr_poly_cxt_t *) * numtextures);
	memset(txr_cxt_nobump, 0, sizeof(pvr_poly_cxt_t *) * numtextures);

	memset(txr_hdr_bump, 0, sizeof(pvr_poly_hdr_t *) * numtextures);
	memset(txr_hdr_nobump, 0, sizeof(pvr_poly_hdr_t *) * numtextures);

	memset(num_pal, 0, numtextures);
	memset(pt, 0, numtextures);

	bump_txr_ptr = (pvr_ptr_t *)malloc(numtextures * sizeof(pvr_ptr_t));
	bump_cxt =
		(pvr_poly_cxt_t **)malloc(numtextures * sizeof(pvr_poly_cxt_t*));
	if (!bump_cxt) {
		I_Error("R_InitTextures: could not malloc bump_cxt array");
	}
	bump_hdrs =
		(pvr_poly_hdr_t **)malloc(numtextures * sizeof(pvr_poly_hdr_t*));
	if (!bump_hdrs) {
		I_Error("R_InitTextures: could not malloc bump_hdr array");
	}
	memset(bump_txr_ptr, 0, sizeof(pvr_ptr_t) * numtextures);
	memset(bump_cxt, 0, sizeof(pvr_poly_cxt_t*) * numtextures);
	memset(bump_hdrs, 0, sizeof(pvr_poly_hdr_t*) * numtextures);

	textures = Z_Malloc(numtextures * sizeof(int), PU_STATIC, NULL);

	pvr_poly_cxt_col(&flush_cxt, PVR_LIST_TR_POLY);
	flush_cxt.blend.src_enable = 1;
	flush_cxt.blend.dst_enable = 0;
	flush_cxt.blend.src = PVR_BLEND_SRCALPHA;
	flush_cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
	pvr_poly_compile(&flush_hdr, &flush_cxt);

	pvr_poly_cxt_col(&laser_cxt, PVR_LIST_OP_POLY);
	pvr_poly_compile(&laser_hdr, &laser_cxt);

	for (i = 0; i < numtextures; i++) {
		int texture = (i + firsttex) << 4;
		textures[i] = texture;
	}
	swx = W_CheckNumForName("SWX", 0x7fffff00, 0);
	firstswx = (swx - firsttex);
}

/*
================
=
= R_InitSprites
=
=================
*/

void R_InitSprites(void)
{
	firstsprite = W_GetNumForName("S_START") + 1;
	lastsprite = W_GetNumForName("S_END") - 1;
	numsprites = (lastsprite - firstsprite) + 1;

	setup_sprite_headers();
}
