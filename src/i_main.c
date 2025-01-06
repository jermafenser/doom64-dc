#include "i_main.h"
#include "doomdef.h"
#include "st_main.h"
#include <kos.h>
#include <kos/thread.h>
#include <dc/asic.h>
#include <sys/time.h>
#include <dc/vblank.h>
#include <dc/video.h>
#include <arch/irq.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <sys/param.h>
#include <dc/maple/keyboard.h>
#include "dc/perf_monitor.h"

void wav_shutdown(void);

pvr_init_params_t pvr_params = { { PVR_BINSIZE_16, 0, PVR_BINSIZE_16, 0, 0 },
				768 * 1024 /*VERTBUF_SIZE*/,
				1, // dma enabled
				0, // fsaa
				0, // 1 is autosort disabled
				2 };

uint8_t __attribute__((aligned(32))) tr_buf[TR_VERTBUF_SIZE];

int side = 0;

extern int globallump;
extern int globalcm;

//----------
kthread_t *main_thread;
kthread_attr_t main_attr;

kthread_t *sys_ticker_thread;
kthread_attr_t sys_ticker_attr;

boolean disabledrawing = false;

mutex_t vbi2mtx;
condvar_t vbi2cv;

volatile int vbi2msg = 0;
atomic_int rdpmsg;
volatile s32 vsync = 0;
volatile s32 drawsync2 = 0;
volatile s32 drawsync1 = 0;

u32 NextFrameIdx = 0;

s32 ControllerPakStatus = 1;
s32 gamepad_system_busy = 0;
s32 FilesUsed = -1;
u32 SystemTickerStatus = 0;

void vblfunc(uint32_t c, void *d)
{
	vsync++;
}
extern pvr_dr_state_t dr_state;

int __attribute__((noreturn)) main(int argc, char **argv)
{
	dbgio_dev_select("serial");

	global_render_state.quality = 2;
	global_render_state.fps_uncap = 1;

	vid_set_enabled(0);
	vid_set_mode(DM_640x480, PM_RGB565);
	pvr_init(&pvr_params);
	vblank_handler_add(&vblfunc, NULL);
	vid_set_enabled(1);

	pvr_set_vertbuf(PVR_LIST_TR_POLY, tr_buf, TR_VERTBUF_SIZE);

	mutex_init(&vbi2mtx, MUTEX_TYPE_NORMAL);
	cond_init(&vbi2cv);

	main_attr.create_detached = 0;
	main_attr.stack_size = 65536;
	main_attr.stack_ptr = NULL;
	main_attr.prio = 10;
	main_attr.label = "I_Main";

	main_thread = thd_create_ex(&main_attr, I_Main, NULL);
	dbgio_printf("started main thread\n");

	thd_join(main_thread, NULL);
	wav_shutdown();
	while (true) {
		thd_sleep(25); // don't care anymore
	}
}

void *I_Main(void *arg);
void *I_SystemTicker(void *arg);

void *I_Main(void *arg)
{
	D_DoomMain();
	return 0;
}

uint64_t running = 0;

void *I_SystemTicker(void *arg)
{
	while (!running) {
		thd_pass();
	}

	while (true) {
		if (rdpmsg) {
			rdpmsg = 0;

			SystemTickerStatus |= 16;
			thd_pass();
			continue;
		}

		if (SystemTickerStatus & 16) {
			if (demoplayback || !global_render_state.fps_uncap) {
			if ((u32)(vsync - drawsync2) < 2) {
				thd_pass();
				continue;
			}
			}

			SystemTickerStatus &= ~16;

			if (demoplayback) {
				vsync = drawsync2 + 2;
			}

			drawsync1 = vsync - drawsync2;
			drawsync2 = vsync;

			mutex_lock(&vbi2mtx);
			vbi2msg = 1;
			cond_signal(&vbi2cv);
			mutex_unlock(&vbi2mtx);
		}

		thd_pass();
	}

	return 0;
}

extern void S_Init(void);

void I_Init(void)
{
	sys_ticker_attr.create_detached = 0;
	sys_ticker_attr.stack_size = 32768;
	sys_ticker_attr.stack_ptr = NULL;
	sys_ticker_attr.prio = 9;
	sys_ticker_attr.label = "I_SystemTicker";

	sys_ticker_thread =
		thd_create_ex(&sys_ticker_attr, I_SystemTicker, NULL);

	/* osJamMesg(&sys_msgque_vbi2, (OSMesg)VID_MSG_KICKSTART, OS_MESG_NOBLOCK); */
	// initial value must be 1 or everything deadlocks
	vbi2msg = 1;
	rdpmsg = 0;

	dbgio_printf("I_Init: started system ticker thread\n");
}

#include "stdarg.h"

int early_error = 1;

void __attribute__((noreturn)) I_Error(char *error, ...)
{
	char buffer[256];
	va_list args;
	va_start(args, error);
	vsprintf(buffer, error, args);
	va_end(args);

	pvr_scene_finish();
	pvr_wait_ready();

	if (early_error) {
		dbgio_dev_select("fb");
		dbgio_printf("I_Error [%s]\n", buffer);
		while (true) {
			;
		}
	} else {
		dbgio_dev_select("serial");
		dbgio_printf("I_Error [%s]\n", buffer);
		while (true) {
			vid_waitvbl();
			pvr_scene_begin();
			pvr_list_begin(PVR_LIST_OP_POLY);
			pvr_list_finish();
			I_ClearFrame();
			ST_Message(err_text_x, err_text_y, buffer, 0xffffffff, 1);
			I_DrawFrame();
			pvr_scene_finish();
			pvr_wait_ready();
		}
	}
}

typedef struct {
	int pad_data;
} pad_t;

int last_joyx;
int last_joyy;
int last_Ltrig;
int last_Rtrig;

int I_GetControllerData(void)
{
	maple_device_t *controller;
	cont_state_t *cont;
	kbd_state_t *kbd;
	mouse_state_t *mouse;
	int ret = 0;

	int tries;

	tries = 0;
	while((!(controller = maple_enum_type(0, MAPLE_FUNC_CONTROLLER))) && (tries < 10)) {
		dbgio_printf("could not get controller in %d tries\n", tries++);
	}

	if (tries == 10) {
		dbgio_printf("never got controller\n");
	}

	if (controller) {
		cont = maple_dev_status(controller);

		// used for analog stick movement
		// see am_main.c, p_user.c
		last_joyx = ((cont->joyx * 3) / 4);
		last_joyy = -((cont->joyy * 3) / 4);
		// used for analog strafing, see p_user.c
		last_Ltrig = cont->ltrig;
		last_Rtrig = cont->rtrig;
		
		// second analog
		if (cont->joy2y > 10 || cont->joy2y < -10) {
			last_joyy = -cont->joy2y * 2;
		}
		
		if (cont->joy2x > 10) {
			last_Rtrig = MAX(last_Rtrig, cont->joy2x * 4);
			ret |= PAD_R_TRIG;
		} else if (cont->joy2x < -10) {
			last_Ltrig = MAX(last_Ltrig, -cont->joy2x * 4);
			ret |= PAD_L_TRIG;
		}
		
		ret |= (last_joyy & 0xff);
		ret |= ((last_joyx & 0xff) << 8);

		// ATTACK
		ret |= (cont->buttons & (CONT_A | CONT_C)) ? PAD_Z_TRIG : 0;
		// USE
		ret |= (cont->buttons & (CONT_B | CONT_Z)) ? PAD_RIGHT_C : 0;

		// AUTOMAP is x+y together
		if ((cont->buttons & CONT_X) &&
		    (cont->buttons & CONT_Y)) {
			ret |= PAD_UP_C;
		} else {
			// WEAPON BACKWARD
			ret |= (cont->buttons & CONT_X) ? PAD_A : 0;
			// WEAPON FORWARD
			ret |= (cont->buttons & CONT_Y) ? PAD_B : 0;
		}
		
		// AUTOMAP select/back on 3rd paty controllers (usb4maple)
		ret |= (cont->buttons & CONT_D) ? PAD_UP_C : 0;
		
		// MOVE
		ret |= (cont->buttons & CONT_DPAD_RIGHT) ? PAD_RIGHT : 0;
		ret |= (cont->buttons & CONT_DPAD_LEFT) ? PAD_LEFT : 0;
		ret |= (cont->buttons & CONT_DPAD_DOWN) ? PAD_DOWN : 0;
		ret |= (cont->buttons & CONT_DPAD_UP) ? PAD_UP : 0;

		// START
		ret |= (cont->buttons & CONT_START) ? PAD_START : 0;

		if (cont->ltrig) {
			ret |= PAD_L_TRIG;
		} else if (cont->rtrig) {
			ret |= PAD_R_TRIG;
		}
	}
	
	controller = maple_enum_type(0, MAPLE_FUNC_KEYBOARD);
	
	if (controller) {
		kbd = maple_dev_status(controller);
		
		// ATTACK
		if (kbd->cond.modifiers & (KBD_MOD_LCTRL | KBD_MOD_RCTRL)) {
			ret |= PAD_Z_TRIG;
		}
		
		// USE
		if (kbd->cond.modifiers & (KBD_MOD_LSHIFT | KBD_MOD_RSHIFT)) {
			ret |= PAD_RIGHT_C;
		}
		
		for (int i = 0; i < MAX_PRESSED_KEYS; i++) {
			if (!kbd->cond.keys[i] || kbd->cond.keys[i] == KBD_KEY_ERROR) {
				break;
			}
			
			switch (kbd->cond.keys[i]) {
				// ATTACK
				case KBD_KEY_SPACE:
					ret |= PAD_Z_TRIG;
					break;
				
				// USE
				case KBD_KEY_F:
					ret |= PAD_RIGHT_C;
					break;
				
				// WEAPON BACKWARD
				case KBD_KEY_PGDOWN:
				case KBD_KEY_PAD_MINUS:
					ret |= PAD_A;
					break;
				
				// WEAPON FORWARD
				case KBD_KEY_PGUP:
				case KBD_KEY_PAD_PLUS:
					ret |= PAD_B;
					break;
				
				// MOVE
				case KBD_KEY_D: // RIGHT
				case KBD_KEY_RIGHT:
					ret |= PAD_RIGHT;
					break;
				
				case KBD_KEY_A: // LEFT
				case KBD_KEY_LEFT:
					ret |= PAD_LEFT;
					break;
				
				case KBD_KEY_S: // DOWN
				case KBD_KEY_DOWN:
					ret |= PAD_DOWN;
					break;
				
				case KBD_KEY_W: // UP
				case KBD_KEY_UP:
					ret |= PAD_UP;
					break;
				
				// START
				case KBD_KEY_ESCAPE:
				case KBD_KEY_ENTER:
				case KBD_KEY_PAD_ENTER:
					ret |= PAD_START;
					break;
				
				// MAP
				case KBD_KEY_TAB:
					ret |= PAD_UP_C;
					break;
				
				// STRAFE
				case KBD_KEY_COMMA: // L
					ret |= PAD_L_TRIG;
					last_Ltrig = 255;
					break;
				
				case KBD_KEY_PERIOD: // R
					ret |= PAD_R_TRIG;
					last_Rtrig = 255;
					break;
				
				case KBD_KEY_BACKSPACE:
					ret |= PAD_LEFT_C;
					break;
				
				default:
			}
		}
	}
	
	controller = maple_enum_type(0, MAPLE_FUNC_MOUSE);
	
	if (controller) {
		mouse = maple_dev_status(controller);
		
		// ATTACK
		if (mouse->buttons & MOUSE_LEFTBUTTON) {
			ret |= PAD_Z_TRIG;
		}
		
		// USE
		if (mouse->buttons & MOUSE_RIGHTBUTTON) {
			ret |= PAD_RIGHT_C;
		}
		
		// START
		if (mouse->buttons & MOUSE_SIDEBUTTON) {
			ret |= PAD_START;
		}
		
		// STRAFE 
		// Only five buttons mouse, supported by usb4maple
		if (mouse->buttons & (1 << 5)) { // BACKWARD
			ret |= PAD_L_TRIG;
			last_Ltrig = 255;
		}
		if (mouse->buttons & (1 << 4)) { // FORWARD
			ret |= PAD_R_TRIG;
			last_Rtrig = 255;
		}
		
		// WEAPON
		if (mouse->dz < 0) { 
			ret |= PAD_A;
		} else if (mouse->dz > 0) {
			ret |= PAD_B;
		}
		
		if (mouse->dx)
		{
			last_joyx = mouse->dx*4;
			
			if (last_joyx > 127) {
				last_joyx = 127;
			} else if(last_joyx < -128) {
				last_joyx = -128;
			}
			
			ret = (ret & ~0xFF00) | ((last_joyx & 0xff) << 8);
		}
		
		if (mouse->dy)
		{
			last_joyy = -mouse->dy*4;
			
			if (last_joyy > 127) {
				last_joyy = 127;
			} else if(last_joyy < -128) {
				last_joyy = -128;
			}
			
			ret = (ret & ~0xFF)   |  (last_joyy & 0xff);
		}
	}
	
	return ret;
}

void I_ClearFrame(void) // 8000637C
{
	NextFrameIdx += 1;

	globallump = -1;
	globalcm = -2;
}

void I_DrawFrame(void) // 80006570
{
	running++;

	mutex_lock(&vbi2mtx);

	while (!vbi2msg) {
		cond_wait(&vbi2cv, &vbi2mtx);
	}

	vbi2msg = 0;

	mutex_unlock(&vbi2mtx);
}

short SwapShort(short dat)
{
	return ((((dat << 8) | (dat >> 8 & 0xff)) << 16) >> 16);
}

void I_MoveDisplay(int x, int y)
{
}

#define MELTALPHA2 0.00392f
#define FB_TEX_W 512
#define FB_TEX_H 256
#define FB_TEX_SIZE (FB_TEX_W * FB_TEX_H * sizeof(uint16_t))

extern float empty_table[129];
extern void  __attribute__((noinline)) P_FlushAllCached(void);

static pvr_vertex_t __attribute__((aligned(32))) wipeverts[8];


void I_WIPE_MeltScreen(void)
{
	pvr_poly_cxt_t wipecxt;
	pvr_poly_hdr_t __attribute__((aligned(32))) wipehdr;
	pvr_ptr_t pvrfb = 0;
	pvr_vertex_t *vert;

	float x0, y0, x1, y1;
	float y0a, y1a;
	float u0, v0, u1, v1;
	float v1a;

	uint32_t save;
	uint16_t *fb = (uint16_t *)Z_Malloc(FB_TEX_SIZE, PU_STATIC, NULL);

	pvr_wait_ready();

	P_FlushAllCached();
	pvrfb = pvr_mem_malloc(FB_TEX_SIZE);
	if (!pvrfb) {
		I_Error("PVR OOM for melt fb");
	}

	memset(fb, 0, FB_TEX_SIZE);

	save = irq_disable();
	for (uint32_t y = 0; y < 480; y += 2) {
		for (uint32_t x = 0; x < 640; x += 2) {
			// (y/2) * 512 == y << 8
			// y*640 == (y<<9) + (y<<7)
			fb[(y << 8) + (x >> 1)] =
				vram_s[((y << 9) + (y << 7)) + x];
		}
	}
	irq_restore(save);

	pvr_txr_load(fb, pvrfb, FB_TEX_SIZE);
	pvr_poly_cxt_txr(&wipecxt, PVR_LIST_TR_POLY,
			 PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED, FB_TEX_W,
			 FB_TEX_H, pvrfb, PVR_FILTER_NONE);
	wipecxt.blend.src = PVR_BLEND_ONE;
	wipecxt.blend.dst = PVR_BLEND_ONE;
	pvr_poly_compile(&wipehdr, &wipecxt);

	// Fill borders with black
	pvr_set_bg_color(0, 0, 0);
	pvr_fog_table_color(0.0f, 0.0f, 0.0f, 0.0f);
	pvr_fog_table_custom(empty_table);

	u0 = 0.0f;
	u1 = 0.625f; // 320.0f / 512.0f;
	v0 = 0.0f;
	v1 = 0.9375f; // 240.0f / 256.0f;
	x0 = 0.0f;
	y0 = 0.0f;
	x1 = 640;
	y1 = 480;
	y0a = y0;
	y1a = y1;

	for (int vn = 0; vn < 4; vn++) {
		wipeverts[vn].flags = PVR_CMD_VERTEX;
		wipeverts[vn].z = 5.0f;
		wipeverts[vn].argb = 0xffff0000; // red, alpha 255/255
	}
	wipeverts[3].flags = PVR_CMD_VERTEX_EOL;

	for (int vn = 4; vn < 8; vn++) {
		wipeverts[vn].flags = PVR_CMD_VERTEX;
		wipeverts[vn].z = 5.01f;
		wipeverts[vn].argb = 0x10080808; // almost black, alpha 16/255
	}
	wipeverts[7].flags = PVR_CMD_VERTEX_EOL;

	for (int i = 0; i < 160; i += 2) {
		pvr_scene_begin();
		pvr_list_begin(PVR_LIST_OP_POLY);
		pvr_list_finish();
		vert = wipeverts;
		vert->x = x0;
		vert->y = y1;
		vert->u = u0;
		vert->v = v1;
		vert++;

		vert->x = x0;
		vert->y = y0;
		vert->u = u0;
		vert->v = v0;
		vert++;

		vert->x = x1;
		vert->y = y1;
		vert->u = u1;
		vert->v = v1;
		vert++;

		vert->x = x1;
		vert->y = y0;
		vert->u = u1;
		vert->v = v0;
		vert++;

#if 1
		// I'm not sure if I need this but leaving it for now
		if (y1a > y1 + 31) {
			double ydiff = y1a - 480;
			y1a = 480;
			v1a = (240.0f - ydiff) / 256.0f;
		} else {
			v1a = v1;
		}
#endif

		vert->x = x0;
		vert->y = y1a;
		vert->u = u0;
		vert->v = v1a;
		vert++;

		vert->x = x0;
		vert->y = y0a;
		vert->u = u0;
		vert->v = v0;
		vert++;

		vert->x = x1;
		vert->y = y1a;
		vert->u = u1;
		vert->v = v1a;
		vert++;

		vert->x = x1;
		vert->y = y0a;
		vert->u = u1;
		vert->v = v0;

		pvr_list_prim(PVR_LIST_TR_POLY, &wipehdr,
			      sizeof(pvr_poly_hdr_t));
		pvr_list_prim(PVR_LIST_TR_POLY, wipeverts,
			      8 * sizeof(pvr_vertex_t));
		pvr_scene_finish();
		pvr_wait_ready();

		// after PVR changes, have to submit twice
		// or it flickers
		pvr_scene_begin();
		pvr_list_begin(PVR_LIST_OP_POLY);
		pvr_list_finish();
		pvr_list_prim(PVR_LIST_TR_POLY, &wipehdr,
			      sizeof(pvr_poly_hdr_t));
		pvr_list_prim(PVR_LIST_TR_POLY, wipeverts,
			      8 * sizeof(pvr_vertex_t));
		pvr_scene_finish();
		pvr_wait_ready();

		if (i < 158) {
			save = irq_disable();
			for (uint32_t y = 0; y < 480; y += 2) {
				for (uint32_t x = 0; x < 640; x += 2) {
					// (y/2) * 512 == y << 8
					// y*640 == (y<<9) + (y<<7)
					fb[(y << 8) + (x >> 1)] =
						vram_s[((y << 9) + (y << 7)) +
						       x];
				}
			}
			irq_restore(save);

			pvr_txr_load(fb, pvrfb, FB_TEX_SIZE);

			y0a += 1.0f;
			y1a += 1.0f;
		}
	}

	pvr_mem_free(pvrfb);

	Z_Free(fb);
	I_WIPE_FadeOutScreen();
	return;
}

void I_WIPE_FadeOutScreen(void)
{
	pvr_poly_cxt_t wipecxt;
	pvr_poly_hdr_t __attribute__((aligned(32))) wipehdr;
	pvr_ptr_t pvrfb = 0;
	pvr_vertex_t *vert;

	float x0, y0, x1, y1;
	float u0, v0, u1, v1;

	uint32_t save;
	uint16_t *fb = (uint16_t *)Z_Malloc(FB_TEX_SIZE, PU_STATIC, NULL);

	pvr_wait_ready();

	P_FlushAllCached();
	pvrfb = pvr_mem_malloc(FB_TEX_SIZE);
	if (!pvrfb) {
		I_Error("PVR OOM for melt fb");
	}

	memset(fb, 0, FB_TEX_SIZE);

	save = irq_disable();
	for (uint32_t y = 0; y < 480; y += 2) {
		for (uint32_t x = 0; x < 640; x += 2) {
			// (y/2) * 512 == y << 8
			// y*640 == (y<<9) + (y<<7)
			fb[(y << 8) + (x >> 1)] =
				vram_s[((y << 9) + (y << 7)) + x];
		}
	}
	irq_restore(save);

	pvr_txr_load(fb, pvrfb, FB_TEX_SIZE);
	pvr_poly_cxt_txr(&wipecxt, PVR_LIST_TR_POLY,
			 PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED, FB_TEX_W,
			 FB_TEX_H, pvrfb, PVR_FILTER_NONE);
	wipecxt.blend.src = PVR_BLEND_ONE;
	wipecxt.blend.dst = PVR_BLEND_ONE;
	pvr_poly_compile(&wipehdr, &wipecxt);

	pvr_set_bg_color(0, 0, 0);
	pvr_fog_table_color(0.0f, 0.0f, 0.0f, 0.0f);

	u0 = 0.0f;
	u1 = 0.625f; // 320.0f / 512.0f;
	v0 = 0.0f;
	v1 = 0.9375f; // 240.0f / 256.0f;
	x0 = 0.0f;
	y0 = 0.0f;
	x1 = 640;
	y1 = 480;
	for (int vn = 0; vn < 4; vn++) {
		wipeverts[vn].flags = PVR_CMD_VERTEX;
		wipeverts[vn].z = 5.0f;
	}
	wipeverts[3].flags = PVR_CMD_VERTEX_EOL;

	for (int i = 248; i >= 0; i -= 8) {
		uint8_t ui = (uint8_t)(i & 0xff);
		uint32_t fcol = 0xff000000 | (ui << 16) | (ui << 8) | ui;

		pvr_scene_begin();
		pvr_list_begin(PVR_LIST_OP_POLY);
		pvr_list_finish();
		vert = wipeverts;
		vert->x = x0;
		vert->y = y1;
		vert->u = u0;
		vert->v = v1;
		vert->argb = fcol;
		vert++;

		vert->x = x0;
		vert->y = y0;
		vert->u = u0;
		vert->v = v0;
		vert->argb = fcol;
		vert++;

		vert->x = x1;
		vert->y = y1;
		vert->u = u1;
		vert->v = v1;
		vert->argb = fcol;
		vert++;

		vert->x = x1;
		vert->y = y0;
		vert->u = u1;
		vert->v = v0;
		vert->argb = fcol;

		pvr_list_prim(PVR_LIST_TR_POLY, &wipehdr,
			      sizeof(pvr_poly_hdr_t));
		pvr_list_prim(PVR_LIST_TR_POLY, wipeverts, 4 * sizeof(pvr_vertex_t));
		pvr_scene_finish();
		pvr_wait_ready();
	}

	pvr_mem_free(pvrfb);

	Z_Free(fb);
	return;
}

#include <dc/vmu_pkg.h>

s32 Pak_Memory = 0;
s32 Pak_Size = 0;
u8 *Pak_Data;

unsigned short vmu_icon_pal[] = {
0xF404, 0xF303, 0xF202, 0xF101, 0xFF62, 0xFE41, 0xFB42, 0xF732, 0xFC31, 0xF610, 0xF721, 0xF921, 0xF842, 0xFA21, 0xFA55, 0xF510, };

unsigned char vmu_icon_img[] = {
0x23,0x32,0x21,0x22,0x2B,0x8B,0x31,0x22,0x22,0x23,0xAA,0xF3,0x33,0x22,0x23,0x21,
0x23,0x93,0x32,0x2F,0xBD,0xA3,0x33,0x33,0x33,0x33,0xFD,0x8A,0xF3,0x23,0x3F,0x31,
0x23,0xF7,0x73,0x2A,0xDD,0x33,0xCC,0xAA,0xAA,0x73,0x33,0x8D,0x93,0x3F,0x9F,0x31,
0x23,0xFF,0xA7,0x78,0xDA,0xCC,0xAF,0xFF,0xFF,0x9C,0x79,0xB8,0x8F,0xF9,0xAF,0x21,
0x22,0x3F,0xA7,0x9D,0xDA,0x99,0xFF,0x9F,0xFF,0x9F,0xF9,0x78,0x8A,0x99,0xF3,0x20,
0x21,0x23,0x99,0xA8,0xDF,0x9F,0xFF,0x33,0x33,0x99,0x99,0x98,0x56,0x7A,0xF2,0x00,
0x21,0x13,0xF9,0x9B,0xBF,0xF3,0x33,0x33,0x33,0x33,0x39,0xFB,0x8B,0x99,0x32,0x11,
0x21,0x12,0x37,0xA8,0xDF,0xFF,0x33,0x39,0xF3,0x33,0x97,0x98,0x8B,0x99,0x31,0x11,
0x22,0x12,0x33,0xCA,0x8A,0xFA,0x9F,0x3E,0xD3,0xFC,0x7A,0xA5,0xB9,0xA2,0x21,0x11,
0x22,0x23,0xE7,0xAD,0xAD,0xFF,0x99,0xFD,0xDA,0xC7,0x9A,0xDC,0xB7,0x79,0x21,0x11,
0x12,0x33,0xCA,0xF8,0x5A,0xF9,0x99,0xFD,0xD9,0xAA,0xAA,0x74,0x89,0x77,0x22,0x12,
0x12,0x3C,0x7A,0xFD,0x88,0xA9,0xAA,0xFB,0xDF,0xAB,0xAC,0x68,0x8A,0x77,0x92,0x22,
0x12,0x3E,0x7A,0x39,0xA5,0x8A,0xBB,0x9B,0xD9,0xBB,0xB8,0x4B,0xB2,0x77,0xA2,0x22,
0x12,0x3E,0x79,0x3F,0xA5,0xDB,0xBD,0x98,0xD9,0xDD,0xD6,0x4B,0x92,0xE7,0xA2,0x12,
0x22,0x3E,0xA9,0x33,0x98,0x8A,0xBB,0x7B,0x8F,0xB6,0xC8,0x4B,0x22,0xE7,0xB2,0x01,
0x23,0x3C,0x79,0x33,0x9A,0x58,0xDA,0xA8,0x5F,0x76,0x44,0x6A,0x22,0xE7,0xB2,0x01,
0x32,0x37,0x79,0x37,0xCF,0x54,0x5D,0xB5,0x5A,0x84,0x44,0xAA,0x62,0xE7,0x72,0x11,
0x22,0x37,0x77,0xC6,0xBC,0x98,0x44,0x85,0x48,0x44,0x48,0xA6,0xBE,0xC7,0xA2,0x01,
0x22,0x3E,0x77,0xBD,0x8C,0xFF,0xB4,0x45,0x54,0x45,0xFF,0xB5,0x66,0xE7,0xA2,0x10,
0x23,0xEE,0x7B,0xB8,0x8A,0x9A,0x9B,0x48,0x54,0x89,0xAF,0x85,0x8C,0xC7,0x7E,0x21,
0x3E,0xE7,0x7A,0xAA,0xAA,0xA8,0xAF,0xD8,0xD8,0xFD,0x8D,0x66,0x6B,0x77,0x7C,0xE2,
0xAB,0x9A,0x9B,0xB9,0xF9,0x9C,0xDD,0x8F,0xA5,0x88,0xA9,0x99,0x76,0xB9,0x99,0x9C,
0x33,0x33,0x3A,0xDD,0x63,0x3C,0xAB,0x95,0x4A,0xDA,0xF2,0x2C,0x66,0xA2,0x21,0x22,
0x11,0x11,0x22,0xA8,0x8C,0x33,0xC9,0xDD,0xD5,0xAB,0x22,0x74,0x5D,0x21,0x00,0x00,
0x01,0x11,0x01,0x2D,0x55,0xE7,0x89,0xF9,0xA9,0xA8,0x9C,0x45,0xD2,0x10,0x00,0x00,
0x00,0x10,0x00,0x12,0x85,0x58,0xB5,0x8A,0xBD,0x5A,0x45,0x48,0x21,0x01,0x11,0x00,
0x00,0x00,0x00,0x12,0x2D,0x54,0x8A,0x5B,0xD5,0x88,0x45,0xD3,0x21,0x11,0x11,0x00,
0x00,0x00,0x00,0x00,0x12,0x2D,0x8A,0x5D,0x85,0xB8,0xD3,0x32,0x22,0x21,0x11,0x11,
0x10,0x11,0x11,0x00,0x01,0x23,0x33,0xAD,0xDA,0x33,0x32,0x22,0x33,0x21,0x01,0x22,
0x11,0x11,0x11,0x00,0x10,0x12,0x12,0xF9,0x9F,0x31,0x12,0x22,0x22,0x11,0x01,0x22,
0x10,0x10,0x00,0x00,0x00,0x11,0x12,0x39,0x93,0x32,0x22,0x22,0x22,0x22,0x22,0x22,
0x10,0x00,0x00,0x00,0x00,0x00,0x11,0x3F,0xF3,0x22,0x32,0x22,0x22,0x22,0x32,0x22,
};

int I_CheckControllerPak(void)
{
	maple_device_t *vmudev = NULL;

	ControllerPakStatus = 0;
	FilesUsed = -1;

	vmudev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
	if (!vmudev) return PFS_ERR_NOPACK;

	file_t d;
	dirent_t *de;

	d = fs_open("/vmu/a1", O_RDONLY | O_DIR);
	if(!d) return PFS_ERR_ID_FATAL;

	Pak_Memory = 200;

	FilesUsed = 0;

	while((de = fs_readdir(d))) {
		Pak_Memory -= (de->size / 512);
		FilesUsed = 1;
	}

	fs_close(d);

	ControllerPakStatus = 1;

	return 0;
}

int I_DeletePakFile(int filenumb)
{
	maple_device_t *vmudev = NULL;

	ControllerPakStatus = 0;

	vmudev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
	if (!vmudev) return PFS_ERR_NOPACK;

	int rv = fs_unlink("/vmu/a1/doom64");
	if(!rv) return PFS_ERR_ID_FATAL;

	return 0;
}

int I_SavePakSettings(void)
{
	vmu_pkg_t pkg;
	uint8 *pkg_out;
	int pkg_size;
	maple_device_t *vmudev = NULL;

	vmudev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
	if (!vmudev) return PFS_ERR_NOPACK;

	file_t d = fs_open("/vmu/a1/doom64stg", O_WRONLY | O_CREAT);
	if(!d) return PFS_ERR_ID_FATAL;

	memset(&pkg, 0, sizeof(vmu_pkg_t));
	strcpy(pkg.desc_short,"D64 settings");
	strcpy(pkg.desc_long, "Doom 64 settings data");
	strcpy(pkg.app_id, "Doom 64");
	pkg.icon_cnt = 1;
	pkg.icon_data = vmu_icon_img;
	memcpy(pkg.icon_pal, vmu_icon_pal, sizeof(vmu_icon_pal));
	pkg.data_len = 384;

	vmu_pkg_build(&pkg, &pkg_out, &pkg_size);

	if(!pkg_out || pkg_size <= 0) {
		return PFS_ERR_ID_FATAL;
	}

	memcpy(&pkg_out[640], &menu_settings, sizeof(doom64_settings_t));

	size_t rv = fs_write(d, pkg_out, pkg_size);
	fs_close(d);
	free(pkg_out);

	if (rv == pkg_size) {
		return 0;
	} else {
		return PFS_ERR_ID_FATAL;
	}
}

int I_SavePakFile(int filenumb, int flag, byte *data, int size)
{
	vmu_pkg_t pkg;
	uint8 *pkg_out;
	int pkg_size;
	maple_device_t *vmudev = NULL;

	ControllerPakStatus = 0;

	vmudev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
	if (!vmudev) return PFS_ERR_NOPACK;

	file_t d = fs_open("/vmu/a1/doom64", O_WRONLY);
	if(!d) return PFS_ERR_ID_FATAL;

	memset(&pkg, 0, sizeof(vmu_pkg_t));
	strcpy(pkg.desc_short,"Doom 64 saves");
	strcpy(pkg.desc_long, "Doom 64 save data");
	strcpy(pkg.app_id, "Doom 64");
	pkg.icon_cnt = 1;
	pkg.icon_data = vmu_icon_img;
	memcpy(pkg.icon_pal, vmu_icon_pal, sizeof(vmu_icon_pal));
	pkg.data_len = 512;
	if (!Pak_Data) {
		return PFS_ERR_ID_FATAL;
	}
	pkg.data = Pak_Data;

	vmu_pkg_build(&pkg, &pkg_out, &pkg_size);
	if(!pkg_out || pkg_size <= 0) {
		return PFS_ERR_ID_FATAL;
	}
	size_t rv = fs_write(d, pkg_out, pkg_size);
	fs_close(d);
	free(pkg_out);

	if (rv == pkg_size) {
		ControllerPakStatus = 1;
		return 0;
	} else {
	    return PFS_ERR_ID_FATAL;
	}
}

#define COMPANY_CODE 0x3544 // 5D
#define GAME_CODE 0x4e444d45 // NDME

int I_ReadPakSettings(void)
{
	size_t size;
	vmu_pkg_t pkg;
	maple_device_t *vmudev = NULL;
	uint8_t *data;

	vmudev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
	if (!vmudev) return PFS_ERR_NOPACK;

	file_t d = fs_open("/vmu/a1/doom64stg", O_RDONLY);
	if(!d) return PFS_ERR_ID_FATAL;

	size = fs_total(d);
	data = calloc(1, size);

	if(!data) {
		fs_close(d);
		return PFS_ERR_ID_FATAL;
	}

	memset(&pkg, 0, sizeof(pkg));
	size_t res = fs_read(d, data, size);
	fs_close(d);

	if (res <= 0) {
		free(data);
		return PFS_ERR_ID_FATAL;
	}

	memcpy(&menu_settings, &data[640], sizeof(doom64_settings_t));

	menu_settings.runintroduction = false;
	global_render_state.quality = menu_settings.Quality;
	global_render_state.fps_uncap = menu_settings.FpsUncap;

	free(data);

	return 0;
}

int I_ReadPakFile(void)
{
	size_t size;
	vmu_pkg_t pkg;
	maple_device_t *vmudev = NULL;
	uint8_t *data;

	ControllerPakStatus = 0;

	vmudev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
	if (!vmudev) return PFS_ERR_NOPACK;

	Pak_Data = NULL;
	Pak_Size = 0;

	file_t d = fs_open("/vmu/a1/doom64", O_RDONLY);
	if(!d) return PFS_ERR_ID_FATAL;

	size = fs_total(d);
	data = calloc(1, size);

	if(!data) {
		fs_close(d);
		return PFS_ERR_ID_FATAL;
	}

	memset(&pkg, 0, sizeof(pkg));
	size_t res = fs_read(d, data, size);
	fs_close(d);

	if (res <= 0) {
		free(data);
		return PFS_ERR_ID_FATAL;
	}

	if(vmu_pkg_parse(data, &pkg) < 0) {
		free(data);
		return PFS_ERR_ID_FATAL;
	}

	Pak_Size = 512;
	Pak_Data = (byte *)Z_Malloc(Pak_Size, PU_STATIC, NULL);
	memcpy(Pak_Data, pkg.data, Pak_Size);
	ControllerPakStatus = 1;

	free(data);

	return 0;
}

int I_CreatePakFile(void)
{
	maple_device_t *vmudev = NULL;

	ControllerPakStatus = 0;

	vmudev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
	if (!vmudev) return PFS_ERR_NOPACK;

	Pak_Size = 512;
	Pak_Data = (byte *)Z_Malloc(Pak_Size, PU_STATIC, NULL);
	D_memset(Pak_Data, 0, Pak_Size);

	file_t d = fs_open("/vmu/a1/doom64", O_RDWR | O_CREAT);
	if(!d) return PFS_ERR_ID_FATAL;
	fs_close(d);

	ControllerPakStatus = 1;

	return 0;
}
