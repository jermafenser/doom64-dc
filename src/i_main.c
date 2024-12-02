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

pvr_init_params_t pvr_params = { { PVR_BINSIZE_16, 0, PVR_BINSIZE_16, 0, 0 },
				 512 * 1024 /*VERTBUF_SIZE*/,
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

int __attribute__((noreturn)) main(int argc, char **argv)
{
	dbgio_dev_select("serial");

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

	while (true) {
		thd_pass(); // don't care anymore
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
			if ((u32)(vsync - drawsync2) < 2) {
				thd_pass();
				continue;
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
int lowmem_rgb565_screen_shot(const char *fn);

int I_GetControllerData(void)
{
	maple_device_t *controller;
	cont_state_t *cont;
	kbd_state_t *kbd;
	mouse_state_t *mouse;
	int ret = 0;

	controller = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);

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
					break;
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
			} else if(last_joyy > -128) {
				last_joyy = -128;
			}

			ret = (ret & ~0xFF)   |  (last_joyy & 0xff);
		}
	}

	return ret;
}

void I_ClearFrame(void)
{
	NextFrameIdx += 1;

	globallump = -1;
	globalcm = -2;
}

void I_DrawFrame(void)
{
	running++;

	mutex_lock(&vbi2mtx);

	while (!vbi2msg) {
		cond_wait(&vbi2cv, &vbi2mtx);
	}

	vbi2msg = 0;

	mutex_unlock(&vbi2mtx);
}

long LongSwap(long dat)
{
	return dat;
}

short LittleShort(short dat)
{
	return (dat << 16) >> 16;
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

void I_WIPE_MeltScreen(void)
{
	pvr_vertex_t __attribute__((aligned(32))) verts[8];
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

	pvrfb = pvr_mem_malloc(FB_TEX_SIZE);
	if (!pvrfb) {
		I_Error("PVR OOM for melt fb");
	}

	memset(fb, 0, FB_TEX_SIZE);

	pvr_wait_ready();

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
		verts[vn].flags = PVR_CMD_VERTEX;
		verts[vn].z = 5.0f;
		verts[vn].argb = 0xffff0000; // red, alpha 255/255
	}
	verts[3].flags = PVR_CMD_VERTEX_EOL;

	for (int vn = 4; vn < 8; vn++) {
		verts[vn].flags = PVR_CMD_VERTEX;
		verts[vn].z = 5.01f;
		verts[vn].argb = 0x10080808; // almost black, alpha 16/255
	}
	verts[7].flags = PVR_CMD_VERTEX_EOL;

	for (int i = 0; i < 160; i += 2) {
		pvr_scene_begin();
		pvr_list_begin(PVR_LIST_OP_POLY);
		pvr_list_finish();
		vert = verts;
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
		pvr_list_prim(PVR_LIST_TR_POLY, &verts[0],
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
		pvr_list_prim(PVR_LIST_TR_POLY, &verts[0],
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
	pvr_vertex_t __attribute__((aligned(32))) verts[4];
	pvr_poly_cxt_t wipecxt;
	pvr_poly_hdr_t __attribute__((aligned(32))) wipehdr;
	pvr_ptr_t pvrfb = 0;
	pvr_vertex_t *vert;

	float x0, y0, x1, y1;
	float u0, v0, u1, v1;

	uint32_t save;
	uint16_t *fb = (uint16_t *)Z_Malloc(FB_TEX_SIZE, PU_STATIC, NULL);

	pvrfb = pvr_mem_malloc(FB_TEX_SIZE);
	if (!pvrfb) {
		I_Error("PVR OOM for fade fb");
	}

	memset(fb, 0, FB_TEX_SIZE);

	pvr_wait_ready();

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
		verts[vn].flags = PVR_CMD_VERTEX;
		verts[vn].z = 5.0f;
	}
	verts[3].flags = PVR_CMD_VERTEX_EOL;

	for (int i = 248; i >= 0; i -= 8) {
		uint8_t ui = (uint8_t)(i & 0xff);
		uint32_t fcol = 0xff000000 | (ui << 16) | (ui << 8) | ui;

		pvr_scene_begin();
		pvr_list_begin(PVR_LIST_OP_POLY);
		pvr_list_finish();
		vert = verts;
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
		pvr_list_prim(PVR_LIST_TR_POLY, &verts, sizeof(verts));
		pvr_scene_finish();
		pvr_wait_ready();
	}

	pvr_mem_free(pvrfb);

	Z_Free(fb);
	return;
}

int lowmem_rgb565_screen_shot(const char *destfn) {
	file_t f;
	char *header;
	size_t header_size;
	char *line;
	size_t line_size;
	uint32_t save;
	f = fs_open(destfn, O_WRONLY | O_TRUNC);
	if (!f) {
		dbgio_printf("lowmem_rgb565_screen_shot: "
			"can't open output file '%s'\n", destfn);
		return -1;
	}
	header_size = snprintf(NULL, 0, "P6\n#KallistiOS Screen Shot\n"
					"%d %d\n255\n", vid_mode->width, vid_mode->height) + 1;
	header = (char *)alloca(header_size);
	line = (char *)alloca(vid_mode->width * 3);
	sprintf(header, "P6\n#KallistiOS Screen Shot\n%d %d\n255\n",
		vid_mode->width, vid_mode->height);
	header_size = strlen(header);
	if (fs_write(f, header, header_size) != (ssize_t)(header_size)) {
		dbgio_printf("lowmem_rgb565_screen_shot: "
			"can't write header to output file '%s'\n", destfn);
		fs_close(f);
		return -1;
	}
	line_size = vid_mode->width * 3;
	save = irq_disable();
	for (int y = 0; y < vid_mode->height; y ++) {
		for (int x = 0; x < vid_mode->width; x ++) {
			uint16_t pixel1 = vram_s[(y * vid_mode->width)+ x];
			line[(x*3) + 0] = (((pixel1 >> 11) & 0x1f) << 3);
			line[(x*3) + 1] = (((pixel1 >> 5) & 0x3f) << 2);
			line[(x*3) + 2] = (((pixel1 >> 0) & 0x1f) << 3); 
		}
		if(fs_write(f, line, line_size) != (ssize_t)line_size) {
			irq_restore(save);
			dbgio_printf("lowmem_rgb565_screen_shot: "
				"can't write line data to output file '%s'\n", destfn);
			fs_close(f);
			return -1;
		}
	}
	irq_restore(save);
	fs_close(f);
	dbgio_printf("lowmem_rgb565_screen_shot: "
		"written to output file '%s'\n", destfn);
	return 0;
}


int I_CheckControllerPak(void)
{
	return 0;
}

int I_DeletePakFile(int filenumb)
{
	return 0;
}

int I_SavePakFile(int filenumb, int flag, byte *data, int size)
{
	return 0;
}

#define COMPANY_CODE 0x3544 // 5D
#define GAME_CODE 0x4e444d45 // NDME

int I_ReadPakFile(void)
{
	return 0;
}

int I_CreatePakFile(void)
{
	return 0;
}
