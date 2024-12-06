
//Renderer phase 2 - Sky Rendering Routines

#include "doomdef.h"
#include "r_local.h"

#define FIRESKY_WIDTH 64
#define FIRESKY_HEIGHT 64

extern short SwapShort(short dat);

extern pvr_dr_state_t dr_state;

typedef enum {
	SKF_CLOUD = 1,
	SKF_THUNDER = 2,
	SKF_MOUNTAIN = 4,
} skyflags_e;

fixed_t FogNear;
int FogColor;
skyfunc_t R_RenderSKY;
// Fire data double buffer
byte *SkyFireData[2];
byte *SkyCloudData;
int Skyfadeback;
int FireSide;
int SkyCloudOffsetX;
int SkyCloudOffsetY;
int ThunderCounter;
int LightningCounter;
int SkyPicSpace;
int SkyPicMount;
int SkyCloudColor;
int SkyVoidColor;
int SkyFlags;

void R_RenderSpaceSky(void);
void R_RenderCloudSky(void);
void R_RenderVoidSky(void);
void R_RenderEvilSky(void);
void R_RenderClouds(void);
void R_RenderSkyPic(int lump, int yoffset, int callno);
void R_RenderFireSky(void);
void R_CloudThunder(void);

uint32_t FireSkyColor1;
uint32_t FireSkyColor2;

uint32_t skycloudv0col;
uint32_t skycloudv2col;

pvr_ptr_t pvrcloud;
pvr_poly_cxt_t cloudcxt;
pvr_poly_hdr_t __attribute__((aligned(32))) pvrcloudhdr;
extern int clipped;
extern int has_bump;
int CloudOffsetX, CloudOffsetY;
void R_SetupSky(void)
{
	byte *data;

	if (!pvrcloud) {
		pvrcloud = pvr_mem_malloc(64 * 64 * 2);
		if (!pvrcloud) {
			I_Error("PVR OOM for cloud texture");
		}
		pvr_poly_cxt_txr(&cloudcxt, PVR_LIST_OP_POLY,
				 D64_TARGB, 64, 64, pvrcloud,
				 PVR_FILTER_BILINEAR);
		cloudcxt.gen.specular = PVR_SPECULAR_ENABLE;
		cloudcxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		pvr_poly_compile(&pvrcloudhdr, &cloudcxt);
		uint8_t *dccloud = malloc(64 * 64);
		if (!dccloud) {
			I_Error("OOM for raw cloud data");
		}
		uint16_t *thecloud = (uint16_t *)malloc(64 * 64 * 2);
		if (!thecloud) {
			I_Error("OOM for indexed cloud data");
		}
		SkyCloudData =
			(byte *)W_CacheLumpName("CLOUD", PU_CACHE, dec_jag);
		memcpy(dccloud, SkyCloudData + 8, 4096);
		int *tmpSrc = (int *)dccloud;
		int i = 0;
		int mask = 64 / 4;
		// Flip each sets of dwords based on texture width
		for (i = 0; i < 4096 / 4; i += 2) {
			int x1;
			int x2;
			if (i & mask) {
				x1 = *(int *)(tmpSrc + i);
				x2 = *(int *)(tmpSrc + i + 1);
				*(int *)(tmpSrc + i) = x2;
				*(int *)(tmpSrc + i + 1) = x1;
			}
		}
		for (int i = 0; i < 64 * 64; i++) {
			uint8_t tmpp = dccloud[i];
			thecloud[i] = get_color_argb1555(tmpp, tmpp, tmpp, 1);
		}
		free(dccloud);
		pvr_txr_load_ex(thecloud, pvrcloud, 64, 64, PVR_TXRLOAD_16BPP);
		free(thecloud);
	}

	FogNear = 985;
	FogColor = PACKRGBA(0, 0, 0, 0);
	R_RenderSKY = NULL;
	SkyFlags = 0;
	SkyCloudOffsetX = 0;
	CloudOffsetX = 0;
	SkyCloudOffsetY = 0;
	CloudOffsetY = 0;
	ThunderCounter = 180;
	LightningCounter = 0;
	FireSide = 0;
	Skyfadeback = 0;
	SkyPicSpace = W_GetNumForName("SPACE");
	pvr_set_bg_color(0, 0, 0);

	switch (skytexture) {
	case 1:
	case 10: {
		SkyFlags = (SKF_CLOUD | SKF_THUNDER);
		R_RenderSKY = R_RenderCloudSky;

		SkyCloudColor = PACKRGBA(176, 128, 255, 255);

		if (skytexture == 10) {
			SkyPicMount = W_GetNumForName("MOUNTC");
			SkyFlags |= SKF_MOUNTAIN;
		}

		skycloudv0col = PACKRGBA(0, 0, 0, 255);
		skycloudv2col = PACKRGBA(0, 0, 21, 255);
	} break;

	case 2: {
		SkyFlags = SKF_CLOUD;
		R_RenderSKY = R_RenderCloudSky;

		SkyCloudColor = PACKRGBA(255, 48, 48, 255);

		skycloudv0col = PACKRGBA(16, 0, 0, 255);
		skycloudv2col = PACKRGBA(16, 0, 0, 255);
	} break;

	case 3:
	case 5: {
		SkyFlags = SKF_CLOUD;
		R_RenderSKY = R_RenderCloudSky;

		SkyCloudColor = PACKRGBA(208, 112, 64, 255);

		if (skytexture == 3) {
			SkyPicMount = W_GetNumForName("MOUNTB");
			SkyFlags |= SKF_MOUNTAIN;
		} else {
			FogNear = 975;
			FogColor = PACKRGBA(48, 16, 8, 0);
		}

		skycloudv0col = PACKRGBA(0, 0, 0, 255);
		skycloudv2col = PACKRGBA(64, 16, 0, 255);
	} break;

	case 4:
	case 9: {
		R_RenderSKY = R_RenderFireSky;

		data = W_CacheLumpName("FIRE", PU_LEVEL, dec_jag);
		SkyFireData[0] = (data + 8);
		SkyFireData[1] = Z_Malloc((FIRESKY_WIDTH * FIRESKY_HEIGHT),
					  PU_LEVEL, NULL);

		D_memcpy(SkyFireData[1], SkyFireData[0],
			 (FIRESKY_WIDTH * FIRESKY_HEIGHT));

		if (skytexture == 4) {
			FireSkyColor1 = PACKRGBA(255, 0, 0, 255);
			FireSkyColor2 = PACKRGBA(255, 96, 0, 255);
		} else {
			FireSkyColor1 = PACKRGBA(0, 255, 0, 255);
			FireSkyColor2 = PACKRGBA(112, 112, 0, 255);
		}
	} break;

	case 6:
		R_RenderSKY = R_RenderSpaceSky;
		break;

	case 7:
		FogNear = 995;
		R_RenderSKY = R_RenderEvilSky;
		break;

	case 8:
		R_RenderSKY = R_RenderVoidSky;
		FogNear = 975;
		FogColor = PACKRGBA(0, 64, 64, 0);
		SkyVoidColor = PACKRGBA(0, 56, 56, 0);
		break;

	case 11:
		R_RenderSKY = R_RenderSpaceSky;
		SkyPicMount = W_GetNumForName("MOUNTA");
		SkyFlags |= SKF_MOUNTAIN;
		break;
	}
}

void R_RenderSpaceSky(void)
{
	R_RenderSkyPic(SkyPicSpace, 128, 0);

	if (SkyFlags & SKF_MOUNTAIN)
		R_RenderSkyPic(SkyPicMount, 170, 1);
}

void R_RenderCloudSky(void)
{
	if (SkyFlags & SKF_CLOUD)
		R_RenderClouds();

	if (SkyFlags & SKF_THUNDER)
		R_CloudThunder();

	if (SkyFlags & SKF_MOUNTAIN)
		R_RenderSkyPic(SkyPicMount, 170, 0);
}

void R_RenderVoidSky(void)
{
	pvr_set_bg_color((float)UNPACK_R(SkyVoidColor) / 255.0f,
			 (float)UNPACK_G(SkyVoidColor) / 255.0f,
			 (float)UNPACK_B(SkyVoidColor) / 255.0f);
}

void R_RenderEvilSky(void)
{
	int color;

	if (Skyfadeback) {
		Skyfadeback += 4;

		if (Skyfadeback > 255)
			Skyfadeback = 255;

		if (Skyfadeback > 128)
			color = 128;
		else
			color = Skyfadeback;
	}

	if (Skyfadeback < 255) {
		R_RenderSkyPic(SkyPicSpace, 128, 0);
		R_RenderSkyPic(SkyPicSpace, 240, 1);
	}

	if (Skyfadeback) {
		M_DrawBackground(63, 25, color, "EVIL", 0.00015f, 0);
	}
}

int dont_color = 0;
extern Matrix R_ProjectionMatrix;
extern Matrix R_ModelMatrix;

void R_RenderClouds(void)
{
	// implementation borrowed from Doom 64 EX
	float pos = (TRUEANGLES(viewangle) * 0.005556f);/// 360.0f) * 2.0f;
	float u0, v0, u1, v1;

	if (!gamepaused) {
		SkyCloudOffsetX = (SkyCloudOffsetX - (viewcos >> 14)) & 16383;
		SkyCloudOffsetY = (SkyCloudOffsetY + (viewsin >> 13)) & 16383;
		CloudOffsetX -=
			(finecosine[viewangle >> ANGLETOFINESHIFT] >> 10);
		CloudOffsetY += (finesine[viewangle >> ANGLETOFINESHIFT] >> 9);
	}

	u0 = ((float)CloudOffsetX * recip64k) - pos;
	u1 = (((float)CloudOffsetX * recip64k) + 1.5f) - pos;
	v0 = ((float)CloudOffsetY * recip64k);
	v1 = ((float)CloudOffsetY * recip64k) + 2.0f;

	pvr_vertex_t __attribute__((aligned(32))) VTX[4];
	// transformed/screen projected coords
	VTX[0].flags = PVR_CMD_VERTEX;
	VTX[0].x = 920.000000;
	VTX[0].y = 0.000000;
	VTX[0].z = 0.006250;
	VTX[0].u = u1;
	VTX[0].v = v0;
	VTX[0].argb = D64_PVR_REPACK_COLOR(SkyCloudColor);
	VTX[0].oargb = D64_PVR_REPACK_COLOR(skycloudv0col);

	VTX[1].flags = PVR_CMD_VERTEX;
	VTX[1].x = 640.000000;
	VTX[1].y = 240.000000;
	VTX[1].z = 0.003333;
	VTX[1].u = u1;
	VTX[1].v = v1;
	VTX[1].argb = D64_PVR_REPACK_COLOR(SkyCloudColor);
	VTX[1].oargb = D64_PVR_REPACK_COLOR(skycloudv2col);

	VTX[2].flags = PVR_CMD_VERTEX;
	VTX[2].x = -280.000000;
	VTX[2].y = 0.000000;
	VTX[2].z = 0.006250;
	VTX[2].u = u0;
	VTX[2].v = v0;
	VTX[2].argb = D64_PVR_REPACK_COLOR(SkyCloudColor);
	VTX[2].oargb = D64_PVR_REPACK_COLOR(skycloudv0col);

	VTX[3].flags = PVR_CMD_VERTEX_EOL;
	VTX[3].x = 0.000000;
	VTX[3].y = 240.000000;
	VTX[3].z = 0.003333;
	VTX[3].u = u0;
	VTX[3].v = v1;
	VTX[3].argb = D64_PVR_REPACK_COLOR(SkyCloudColor);
	VTX[3].oargb = D64_PVR_REPACK_COLOR(skycloudv2col);

	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), &pvrcloudhdr, 1);	
	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), VTX, 4);

	dont_color = 0;
	has_bump = 0;
	context_change = 1;
}

extern uint16_t bgpal[256];
extern uint16_t __attribute__((aligned(32))) biggest_bg[512 * 256];

pvr_poly_cxt_t pvrskycxt[2];
pvr_poly_hdr_t __attribute__((aligned(32))) pvrskyhdr[2];
pvr_ptr_t pvrsky[2];
int lastlump[2] = { -1, -1 };

void R_RenderSkyPic(int lump, int yoffset, int callno) // 80025BDC
{
	byte *data;
	byte *src;
	byte *paldata;

	int width;
	int height;

	int yl;
	int ang;
	data = W_CacheLumpNum(lump, PU_CACHE, dec_jag);

	width = (SwapShort(((spriteN64_t *)data)->width) + 7) & ~7;
	height = SwapShort(((spriteN64_t *)data)->height);

	if (!pvrsky[callno]) {
		pvrsky[callno] = pvr_mem_malloc(256 * 256 * sizeof(uint16_t));
		if (!pvrsky[callno]) {
			I_Error("PVR OOM for sky texture %d [%d]", lump, callno);
		}
		pvr_poly_cxt_txr(&pvrskycxt[callno], PVR_LIST_TR_POLY,
				 D64_TARGB, 256, 256,
				 pvrsky[callno], PVR_FILTER_BILINEAR);
		pvrskycxt[callno].depth.write = PVR_DEPTHWRITE_DISABLE;
		pvrskycxt[callno].txr.uv_flip = PVR_UVFLIP_NONE;
		pvr_poly_compile(&pvrskyhdr[callno], &pvrskycxt[callno]);
	}

	ang = 0 - ((viewangle >> 22) & 255);

	src = data + sizeof(spriteN64_t);
	paldata = (src + (width * height));
	short *palsrc = (short *)paldata;
	if (lump != lastlump[callno]) {
		lastlump[callno] = lump;
		for (int j = 0; j < 256; j++) {
			short val = *palsrc;
			palsrc++;
			val = SwapShort(val);
			// Unpack and expand to 8bpp, then flip from BGR to RGB.
			u8 b = (val & 0x003E) << 2;
			u8 g = (val & 0x07C0) >> 3;
			u8 r = (val & 0xF800) >> 8;
			u8 a = 0xff; // Alpha is always 255..
			if (j == 0 && r == 0 && g == 0 && b == 0) {
				bgpal[j] = get_color_argb1555(0, 0, 0, 0);
			} else {
				bgpal[j] = get_color_argb1555(r, g, b, a);
			}
		}
		int *tmpSrc = (int *)src;
		int i = 0;
		int mask = 256 / 4;
		// Flip each sets of dwords based on texture width
		for (i = 0; i < (256 * height) / 4; i += 2) {
			int x1;
			int x2;
			if (i & mask) {
				x1 = *(int *)(tmpSrc + i);
				x2 = *(int *)(tmpSrc + i + 1);
				*(int *)(tmpSrc + i) = x2;
				*(int *)(tmpSrc + i + 1) = x1;
			}
		}
		for (int h = 0; h < height; h++) {
			for (int w = 0; w < 256; w++) {
				biggest_bg[w + (h * 256)] =
					bgpal[src[w + (h * 256)]];
			}
		}
		pvr_txr_load_ex(biggest_bg, pvrsky[callno], 256, 256,
				PVR_TXRLOAD_16BPP);
	}

	float u0, v0, u1, v1;
	pvr_vertex_t __attribute__((aligned(32))) verts[4];
	u0 = (float)ang * 0.00390625f; // / 256.0f;
	u1 = u0 + 1.0f;
	v0 = 0.0f;
	v1 = (float)height * 0.00390625f; // / 256.0f;

	yl = (yoffset - SwapShort(((spriteN64_t *)data)->height));

	for (int vn = 0; vn < 4; vn++) {
		verts[vn].flags = PVR_CMD_VERTEX;
		verts[vn].z = (0.00014f + (callno ? 0.000005 : 0.0));
		verts[vn].argb = 0xffffffff;
		verts[vn].oargb = 0;
	}
	verts[3].flags = PVR_CMD_VERTEX_EOL;

	pvr_vertex_t *vert = verts;

	vert->x = 0.0f;
	vert->y = (float)(yl + height) * (float)RES_RATIO;
	vert->u = u0;
	vert->v = v1;
	vert++;

	vert->x = 0.0f;
	vert->y = (float)yl * (float)RES_RATIO;
	vert->u = u0;
	vert->v = v0;
	vert++;

	vert->x = 640.0f;
	vert->y = (float)(yl + height) * (float)RES_RATIO;
	vert->u = u1;
	vert->v = v1;
	vert++;

	vert->x = 640.0f;
	vert->y = (float)yl * (float)RES_RATIO;
	vert->u = u1;
	vert->v = v0;

	pvr_list_prim(PVR_LIST_TR_POLY, &pvrskyhdr[callno],
		      sizeof(pvr_poly_hdr_t));
	pvr_list_prim(PVR_LIST_TR_POLY, &verts, sizeof(verts));
}

uint16_t tmpfire[64 * 64];
uint8_t dcfire[4096];

pvr_ptr_t pvrfire;
pvr_poly_cxt_t pvrfirecxt;
pvr_poly_hdr_t __attribute__((aligned(32))) pvrfirehdr;

void R_RenderFireSky(void)
{
	byte *buff;
	byte *src, *srcoffset, *tmpSrc;
	int width, height, rand;
	int pixel, randIdx;
	int ang;

	if (!pvrfire) {
		pvrfire = pvr_mem_malloc(64 * 64 * sizeof(uint16_t));
		if (!pvrfire) {
			I_Error("PVR OOM for fire texture");
		}
		pvr_poly_cxt_txr(&pvrfirecxt, PVR_LIST_OP_POLY,
				 D64_TARGB, 64, 64, pvrfire, PVR_FILTER_BILINEAR);
		pvrfirecxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		pvr_poly_compile(&pvrfirehdr, &pvrfirecxt);
	}

	if (((gamevbls < gametic) && (gametic & 1)) && (!gamepaused)) {
		buff = SkyFireData[FireSide];
		D_memcpy(buff, SkyFireData[FireSide ^ 1],
			 (FIRESKY_WIDTH * FIRESKY_HEIGHT));

		rand = (M_Random() & 0xff);
		width = 0;
		src = (buff + FIRESKY_WIDTH);

		do { // width
			height = 2;
			srcoffset = (src + width);

			// R_SpreadFire
			pixel = *(byte *)srcoffset;
			if (pixel != 0) {
				randIdx = rndtable[rand];
				rand = ((rand + 2) & 0xff);

				tmpSrc = (src + (((width - (randIdx & 3)) + 1) &
						 (FIRESKY_WIDTH - 1)));
				*(byte *)(tmpSrc - FIRESKY_WIDTH) =
					pixel - ((randIdx & 1) << 4);
			} else {
				*(byte *)(srcoffset - FIRESKY_WIDTH) = 0;
			}

			src += FIRESKY_WIDTH;
			srcoffset += FIRESKY_WIDTH;

			do { // height
				height += 2;

				// R_SpreadFire
				pixel = *(byte *)srcoffset;
				if (pixel != 0) {
					randIdx = rndtable[rand];
					rand = ((rand + 2) & 0xff);

					tmpSrc = (src +
						  (((width - (randIdx & 3)) +
						    1) &
						   (FIRESKY_WIDTH - 1)));
					*(byte *)(tmpSrc - FIRESKY_WIDTH) =
						pixel - ((randIdx & 1) << 4);
				} else {
					*(byte *)(srcoffset - FIRESKY_WIDTH) =
						0;
				}

				src += FIRESKY_WIDTH;
				srcoffset += FIRESKY_WIDTH;

				// R_SpreadFire
				pixel = *(byte *)srcoffset;
				if (pixel != 0) {
					randIdx = rndtable[rand];
					rand = ((rand + 2) & 0xff);

					tmpSrc = (src +
						  (((width - (randIdx & 3)) +
						    1) &
						   (FIRESKY_WIDTH - 1)));
					*(byte *)(tmpSrc - FIRESKY_WIDTH) =
						pixel - ((randIdx & 1) << 4);
				} else {
					*(byte *)(srcoffset - FIRESKY_WIDTH) =
						0;
				}

				src += FIRESKY_WIDTH;
				srcoffset += FIRESKY_WIDTH;

			} while (height != FIRESKY_HEIGHT);

			src -= ((FIRESKY_WIDTH * FIRESKY_HEIGHT) -
				FIRESKY_WIDTH);
			width++;

		} while (width != FIRESKY_WIDTH);

		FireSide ^= 1;
	} else {
		buff = SkyFireData[FireSide ^ 1];
	}

	memcpy(dcfire, buff, 4096);
	int *tmpDcSrc = (int *)dcfire;
	int i = 0;
	int mask = 64 / 4;
	// Flip each sets of dwords based on texture width
	for (i = 0; i < 4096 / 4; i += 2) {
		int x1;
		int x2;

		if (i & mask) {
			x1 = *(int *)(tmpDcSrc + i);
			x2 = *(int *)(tmpDcSrc + i + 1);

			*(int *)(tmpDcSrc + i) = x2;
			*(int *)(tmpDcSrc + i + 1) = x1;
		}
	}

	for (int i = 0; i < 64 * 64; i++) {
		uint8_t tmpp = dcfire[i];
		tmpfire[i] = get_color_argb1555(tmpp, tmpp, tmpp, 1);
	}
	pvr_txr_load_ex(tmpfire, pvrfire, 64, 64, PVR_TXRLOAD_16BPP);

	ang = 0 - ((viewangle >> 22) & 255);
	float u0 = (float)ang * 0.00390625f; // / 256.0f;
	float u1 = u0 + 5.0f;
	float v0 = 0.0035f;
	float v1 = 1.0f;

	pvr_vertex_t __attribute__((aligned(32))) VTX[4];

	// transformed/screen projected coords
	VTX[0].flags = PVR_CMD_VERTEX;
	VTX[0].x = 640.0f;
	VTX[0].y = 0.0f;
	VTX[0].z = 0.00625f;
	VTX[0].u = u1;
	VTX[0].v = v0;
	VTX[0].argb = D64_PVR_REPACK_COLOR(FireSkyColor1);
	VTX[0].oargb = 0xff000000;

	VTX[1].flags = PVR_CMD_VERTEX;
	VTX[1].x = 640.0f;
	VTX[1].y = 240.0f;
	VTX[1].z = 0.00625f;
	VTX[1].u = u1;
	VTX[1].v = v1;
	VTX[1].argb = D64_PVR_REPACK_COLOR(FireSkyColor2);
	VTX[1].oargb = 0xff000000;

	VTX[2].flags = PVR_CMD_VERTEX;
	VTX[2].x = 0.0f;
	VTX[2].y = 0.0f;
	VTX[2].z = 0.00625f;
	VTX[2].u = u0;
	VTX[2].v = v0;
	VTX[2].argb = D64_PVR_REPACK_COLOR(FireSkyColor1);
	VTX[2].oargb = 0xff000000;

	VTX[3].flags = PVR_CMD_VERTEX_EOL;
	VTX[3].x = 0.0f;
	VTX[3].y = 240.0f;
	VTX[3].z = 0.00625f;
	VTX[3].u = u0;
	VTX[3].v = v1;
	VTX[3].argb = D64_PVR_REPACK_COLOR(FireSkyColor2);
	VTX[3].oargb = 0xff000000;

	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), &pvrfirehdr, 1);	
	sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), VTX, 4);

	has_bump = 0;
	dont_color = 0;
	context_change = 1;
}

int add_lightning = 0;

void R_CloudThunder(void) // 80026418
{
	int rand;
	if (!(gamepaused)) {
		ThunderCounter -= vblsinframe[0];

		if (ThunderCounter <= 0) {
			if (LightningCounter == 0) {
				S_StartSound(NULL,
					     sfx_thndrlow + (M_Random() & 1));
			} else if (!(LightningCounter < 6)) {
				// Reset loop after 6 lightning flickers
				rand = (M_Random() & 7);
				ThunderCounter =
					((((rand << 4) - rand) << 2) + 60) << 1;
				//(x*16 - x)*4 == x*15*4 = 60*x
				//ThunderCounter = (rand * 60) + 60;
				LightningCounter = 0;
				return;
			}

			if ((LightningCounter & 1) == 0) {
				skycloudv0col += 0x11111100;
				skycloudv2col += 0x11111100;
				add_lightning = 1;
			} else {
				skycloudv0col -= 0x11111100;
				skycloudv2col -= 0x11111100;
				add_lightning = 0;
			}
			// Do short delay loops for lightning flickers
			ThunderCounter = ((M_Random() & 7) + 1) << 1; 
			LightningCounter += 1;
		}
	}
}
