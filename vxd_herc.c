/*****************************************************************************

Hercules Graphics Card VXD Hardware Abstraction
For Herc9x Windows 95/98 display driver

*****************************************************************************/

#include "winhack.h"
#include "vmm.h"
#include "vxd.h"

#include "wram.h"
#include "async.h"

#include "vxd_lib.h"
#include "3d_accel.h"

#include "herc.h"

#define IO_IN8
#define IO_OUT8
#include "io32.h"

#include "code32.h"

extern FBHDA_t *hda;
extern DWORD ThisVM;

static char herc_vxd_name[] = "hercmini.vxd";

/* 6845 CRTC register values for Hercules graphics mode (720x348) */
static const BYTE herc_crtc_values[HERC_CRTC_REG_COUNT] = {
	0x35, 0x2D, 0x2E, 0x07, 0x5B, 0x02, 0x57, 0x57,
	0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static BOOL herc_valid = FALSE;
static BOOL timer_set = FALSE;
static BOOL vga_mode = TRUE;
static void *herc_vram_lin = NULL;  /* Linear mapping of 0xB0000 */
static DWORD virt_height = HERC_HEIGHT;  /* Current virtual height */

static blit_t wblit;

/* Function pointer dispatch - set during mode switch, no per-line checks */
typedef void (*herc_draw_fn)(blit_t *blit);
static herc_draw_fn active_draw_fn = NULL;

static void HERC_draw(blit_t *blit);
static void HERC_draw_native(blit_t *blit);
static void HERC_draw_virtual(blit_t *blit);

/* vxd_mouse.c */
BOOL mouse_get_rect(DWORD *ptr_left, DWORD *ptr_top,
	DWORD *ptr_right, DWORD *ptr_bottom);

/*
 * HERC_init_hw - Called during Device_Init
 *
 * Software-only initialization:
 * - Allocate wram shadow buffer
 * - Init FBHDA structure
 * - Map Hercules VRAM to linear address
 * - Start async blit timer
 * - DO NOT touch I/O ports or hardware VRAM!
 */
BOOL HERC_init_hw()
{
	dbg_printf("HERC_init_hw begin...\n");

	/* Map Hercules VRAM at physical 0xB0000 */
	herc_vram_lin = (void*)_MapPhysToLinear(HERC_VRAM_BASE, HERC_VRAM_SIZE, 0);
	if(herc_vram_lin == INVALID_FLAT_ADDRESS || herc_vram_lin == NULL)
	{
		dbg_printf("HERC: _MapPhysToLinear(0x%lX) FAILED\n", HERC_VRAM_BASE);
		return FALSE;
	}
	dbg_printf("HERC: VRAM mapped at linear %lX\n", (DWORD)herc_vram_lin);

	/* Allocate wram shadow buffer (64KB) */
	if(!wram_init(HERC_VRAM_SIZE))
	{
		dbg_printf("HERC: wram_init FAILED\n");
		return FALSE;
	}

	/* Initialize FBHDA structure (lives inside wram->extradata) */
	if(!FBHDA_init_hw())
	{
		dbg_printf("HERC: FBHDA_init_hw FAILED\n");
		return FALSE;
	}

	memcpy(hda->vxdname, herc_vxd_name, sizeof(herc_vxd_name));

	hda->vram_size     = HERC_VRAM_SIZE;
	hda->vram_size_bar = HERC_VRAM_SIZE;
	hda->vram_phylin   = herc_vram_lin;
	hda->vram_pm32     = (BYTE*)wram + wram->regs.s.fbmin;
	hda->vram_size_virt = wram->regs.s.fbmax;
	hda->flags         = 0;

	dbg_printf("HERC: vram_phylin=%lX vram_pm32=%lX\n",
		(DWORD)hda->vram_phylin, (DWORD)hda->vram_pm32);

	/* Don't start async blit timer here - start it in HERC_setmode
	 * when we actually have a mode set. Starting it during Device_Init
	 * can cause Windows Protection Error if the timer fires before
	 * the system is fully initialized.
	 */
	wblit.num_changes = 0;

	herc_valid = TRUE;

	dbg_printf("HERC_init_hw = TRUE\n");
	return TRUE;
}

/*
 * HERC_valid - Check if Hercules hardware is available
 */
BOOL HERC_valid()
{
	return herc_valid;
}

/*
 * HERC_validmode - Check if requested mode is supported
 */
BOOL HERC_validmode(DWORD w, DWORD h, DWORD bpp)
{
	if(w != HERC_WIDTH || bpp != HERC_BPP) return FALSE;
	return (h == HERC_HEIGHT || h == HERC_VIRT_HEIGHT);
}

/*
 * HERC_setmode - Program the 6845 CRTC and switch to graphics mode
 *
 * Called from DRV via SetDisplayMode when GDI enables the driver.
 */
BOOL HERC_setmode(DWORD w, DWORD h, DWORD bpp)
{
	int i;
	BOOL virtual_mode;

	dbg_printf("HERC_setmode(%ld, %ld, %ld)\n", w, h, bpp);

	if(!HERC_validmode(w, h, bpp))
	{
		dbg_printf("HERC_setmode: invalid mode\n");
		return FALSE;
	}

	virtual_mode = (h == HERC_VIRT_HEIGHT);
	virt_height = h;

	/* Enable graphics mode on configuration switch */
	outp(HERC_CONFIG_SW, HERC_CFG_GRAPHICS | HERC_CFG_PAGE1);

	/* Disable video while programming CRTC */
	outp(HERC_MODE_CTRL, 0);

	/* Program all 16 CRTC registers (always 720x348 physical) */
	for(i = 0; i < HERC_CRTC_REG_COUNT; i++)
	{
		outp(HERC_CRTC_INDEX, (unsigned char)i);
		outp(HERC_CRTC_DATA, herc_crtc_values[i]);
	}

	/* Enable graphics mode + video output */
	outp(HERC_MODE_CTRL, HERC_MODE_GRAPHICS | HERC_MODE_ENABLE);

	/* Clear hardware framebuffer */
	memset(herc_vram_lin, 0, HERC_VRAM_SIZE);

	/* Update FBHDA fields - GDI sees virtual dimensions */
	hda->width   = HERC_WIDTH;
	hda->height  = h;
	hda->bpp     = HERC_BPP;
	hda->pitch   = HERC_DIB_PITCH;
	hda->stride  = h * HERC_DIB_PITCH;
	hda->surface = 0;
	hda->system_surface = 0;

	/* Update wram registers - GDI draws at virtual resolution */
	wram->regs.s.width  = HERC_WIDTH;
	wram->regs.s.height = h;
	wram->regs.s.mode   = MODE_8;  /* Closest - we handle 1bpp specially */
	wram->regs.s.pitch  = HERC_DIB_PITCH;

	/* Select draw function - no per-line checks needed */
	active_draw_fn = virtual_mode ? HERC_draw_virtual : HERC_draw_native;

	dbg_printf("HERC: %s mode, virt=%ldx%ld, DIB pitch=%ld, HW pitch=%d\n",
		virtual_mode ? "VIRTUAL" : "NATIVE",
		(DWORD)HERC_WIDTH, (DWORD)h, (DWORD)HERC_DIB_PITCH, HERC_HW_PITCH);

	/* Set blit destination info (not used for wram_blit, but needed for wram_changes) */
	wblit.base.ptr      = herc_vram_lin;
	wblit.dst[0].flat.ptr = herc_vram_lin;
	wblit.dst[1].flat.ptr = NULL;
	wblit.dst[2].flat.ptr = NULL;
	wblit.dst_w         = HERC_WIDTH;
	wblit.dst_h         = HERC_HEIGHT;
	wblit.dst_pitch     = HERC_HW_PITCH;
	wblit.dst_mode      = MODE_8;
	wblit.dst_scans     = 1;
	wblit.dst_padx      = 0;
	wblit.dst_pady      = 0;

	/* Start async blit timer on first mode set */
	if(!timer_set)
	{
		timer_set = async_blit_init(&wblit, HERC_draw);
		dbg_printf("HERC: timer = %d\n", timer_set);
	}

	vga_mode = FALSE;

	mouse_invalidate();

	dbg_printf("HERC_setmode: success\n");
	return TRUE;
}

/*
 * HERC_draw - Async blit dispatcher
 *
 * Calls the active draw function (native or virtual) via function pointer.
 * Set during HERC_setmode - zero overhead, no per-line mode checks.
 */
static void HERC_draw(blit_t *blit)
{
	if(vga_mode || !herc_vram_lin || !active_draw_fn) return;
	if(blit->num_changes == 0) return;

	active_draw_fn(blit);
	blit->num_changes = 0;
}

/*
 * Composite software cursor onto a scanline (shared by both draw functions).
 * src_y is the virtual scanline index (used for cursor hit testing).
 */
static void herc_cursor_composite(BYTE *scanline,
	LONG cur_x, LONG cur_y, LONG cur_w, LONG cur_h, DWORD src_y)
{
	LONG cx_start, cx_end, cx;
	DWORD cursor_row = src_y - cur_y;

	cx_start = cur_x;
	cx_end = cur_x + cur_w;
	if(cx_start < 0) cx_start = 0;
	if(cx_end > HERC_WIDTH) cx_end = HERC_WIDTH;

	for(cx = cx_start; cx < cx_end; cx++)
	{
		DWORD cp = cursor_row * CURSOR_DMAX + (cx - cur_x);
		DWORD and_val = wram->cursor.andmask[cp];
		DWORD xor_val = wram->cursor.xormask[cp];
		LONG byte_idx = cx >> 3;
		BYTE bit_mask = 0x80 >> (cx & 7);

		if(and_val == 0)
			scanline[byte_idx] &= ~bit_mask;
		if(xor_val != 0)
			scanline[byte_idx] ^= bit_mask;
	}
}

/*
 * HERC_draw_native - 720x348 -> 720x348 (1:1 mapping)
 *
 * Uses dirty region for partial updates.
 * wram layout: Line Y at offset (fbstart + Y * HERC_DIB_PITCH).
 * Hercules VRAM: Line Y at plane (Y&3) * 0x2000 + (Y/4) * 90.
 */
static void HERC_draw_native(blit_t *blit)
{
	DWORD y, sy, ey;
	BYTE *src_base = (BYTE*)wram + wram->regs.s.fbstart;
	BYTE *dst_base = (BYTE*)herc_vram_lin;
	BYTE scanline[HERC_HW_PITCH];
	LONG cur_x, cur_y, cur_w, cur_h;
	BOOL has_cursor;

	/* Bounds-check dirty region */
	sy = blit->src_sy;
	ey = blit->src_ey;
	if(sy >= HERC_HEIGHT) sy = 0;
	if(ey > HERC_HEIGHT) ey = HERC_HEIGHT;
	if(sy >= ey) return;

	has_cursor = wram->regs.s.cursor_visible;
	if(has_cursor)
	{
		cur_x = wram->regs.s.cursor_x;
		cur_y = wram->regs.s.cursor_y;
		cur_w = wram->regs.s.cursor_w;
		cur_h = wram->regs.s.cursor_h;
		if(cur_w > CURSOR_DMAX) cur_w = CURSOR_DMAX;
		if(cur_h > CURSOR_DMAX) cur_h = CURSOR_DMAX;
	}

	for(y = sy; y < ey; y++)
	{
		BYTE *src_line = src_base + (y * HERC_DIB_PITCH);
		DWORD plane = y & 3;
		DWORD plane_offset = (y >> 2) * HERC_HW_PITCH;
		BYTE *dst = dst_base + (plane * HERC_PLANE_SIZE) + plane_offset;

		if(has_cursor &&
			(LONG)y >= cur_y && (LONG)y < (cur_y + cur_h))
		{
			memcpy(scanline, src_line, HERC_HW_PITCH);
			herc_cursor_composite(scanline, cur_x, cur_y, cur_w, cur_h, y);
			memcpy(dst, scanline, HERC_HW_PITCH);
		}
		else
		{
			memcpy(dst, src_line, HERC_HW_PITCH);
		}
	}
}

/*
 * HERC_draw_virtual - 720x522 -> 720x348 (skip every 3rd source line)
 *
 * For destination line d, source line s = d + d/2 (integer division).
 * Maps groups of 3 virtual lines to 2 physical lines (skip the 3rd).
 * Dirty region is converted from virtual to physical coordinates.
 * Cursor coordinates are in virtual space - compositing uses src_y.
 */
static void HERC_draw_virtual(blit_t *blit)
{
	DWORD d, sy, ey, phy_sy, phy_ey;
	BYTE *src_base = (BYTE*)wram + wram->regs.s.fbstart;
	BYTE *dst_base = (BYTE*)herc_vram_lin;
	BYTE scanline[HERC_HW_PITCH];
	LONG cur_x, cur_y, cur_w, cur_h;
	BOOL has_cursor;

	/* Convert virtual dirty range to physical destination range */
	sy = blit->src_sy;
	ey = blit->src_ey;
	if(sy >= HERC_VIRT_HEIGHT) sy = 0;
	if(ey > HERC_VIRT_HEIGHT) ey = HERC_VIRT_HEIGHT;
	if(sy >= ey) return;

	phy_sy = (sy * 2) / 3;
	phy_ey = ((ey - 1) * 2) / 3 + 1;
	if(phy_ey > HERC_HEIGHT) phy_ey = HERC_HEIGHT;
	if(phy_sy >= phy_ey) return;

	has_cursor = wram->regs.s.cursor_visible;
	if(has_cursor)
	{
		cur_x = wram->regs.s.cursor_x;
		cur_y = wram->regs.s.cursor_y;
		cur_w = wram->regs.s.cursor_w;
		cur_h = wram->regs.s.cursor_h;
		if(cur_w > CURSOR_DMAX) cur_w = CURSOR_DMAX;
		if(cur_h > CURSOR_DMAX) cur_h = CURSOR_DMAX;
	}

	for(d = phy_sy; d < phy_ey; d++)
	{
		DWORD src_y = d + (d >> 1);  /* d + d/2 = source virtual line */
		BYTE *src_line = src_base + (src_y * HERC_DIB_PITCH);
		DWORD plane = d & 3;
		DWORD plane_offset = (d >> 2) * HERC_HW_PITCH;
		BYTE *dst = dst_base + (plane * HERC_PLANE_SIZE) + plane_offset;

		if(has_cursor &&
			(LONG)src_y >= cur_y && (LONG)src_y < (cur_y + cur_h))
		{
			memcpy(scanline, src_line, HERC_HW_PITCH);
			herc_cursor_composite(scanline, cur_x, cur_y, cur_w, cur_h, src_y);
			memcpy(dst, scanline, HERC_HW_PITCH);
		}
		else
		{
			memcpy(dst, src_line, HERC_HW_PITCH);
		}
	}
}


/*
 * FBHDA interface functions - Hercules implementations
 */

void FBHDA_palette_set(unsigned char index, DWORD rgb)
{
	/* Monochrome - store in wram palette for completeness but no HW DAC */
	wram->palette[index].dw = rgb;
}

DWORD FBHDA_palette_get(unsigned char index)
{
	return wram->palette[index].dw;
}

BOOL FBHDA_swap(DWORD offset, DWORD flags)
{
	void *new_fb;
	if((flags & FBHDA_SWAP_QUERY) != 0)
	{
		return TRUE;
	}

	new_fb = ((BYTE*)hda->vram_pm32) + offset;
	if(wram_swap(new_fb, &wblit))
	{
		if(!timer_set)
		{
			HERC_draw(&wblit);
		}
		else
		{
			hda->onflip = 1;
		}
		hda->surface = offset;
		return TRUE;
	}
	return TRUE;
}

void FBHDA_access_begin(DWORD flags)
{
	FBHDA_lock();

	if(flags & FBHDA_ACCESS_MOUSE_MOVE)
	{
		DWORD left, top, right, bottom;
		if(mouse_get_rect(&left, &top, &right, &bottom))
		{
			wram_changes(&wblit, left, top, right, bottom);
		}
	}
	else
	{
		wram_changes(&wblit, 0, 0, hda->width, hda->height);
	}

	FBHDA_unlock();
}

void FBHDA_access_rect(DWORD left, DWORD top, DWORD right, DWORD bottom)
{
	/* No semaphore here - DWORD writes are atomic on x86, bounds checking
	 * in HERC_draw catches wild values, and periodic refresh catches missed
	 * updates. Removing the semaphore eliminates blocking during rapid GDI
	 * updates (minimize/restore animation) that was causing crashes. */
	wram_changes(&wblit, left, top, right, bottom);
}

void FBHDA_access_end(DWORD flags)
{
	FBHDA_lock();

	if(flags & FBHDA_ACCESS_MOUSE_MOVE)
	{
		DWORD left, top, right, bottom;
		if(mouse_get_rect(&left, &top, &right, &bottom))
		{
			wram_changes(&wblit, left, top, right, bottom);
		}
	}

	if(!timer_set)
	{
		HERC_draw(&wblit);
	}

	FBHDA_unlock();
}

DWORD FBHDA_overlay_setup(DWORD overlay, DWORD width, DWORD height, DWORD bpp)
{
	return 0;
}

void FBHDA_overlay_lock(DWORD left, DWORD top, DWORD right, DWORD bottom)
{
}

void FBHDA_overlay_unlock(DWORD flags)
{
}

BOOL FBHDA_mode_query(DWORD index, FBHDA_mode_t *mode)
{
	DWORD height;

	switch(index)
	{
		case 0: height = HERC_HEIGHT; break;      /* 720x348 native */
		case 1: height = HERC_VIRT_HEIGHT; break;  /* 720x522 virtual */
		default: return FALSE;
	}

	if(mode)
	{
		int i;
		mode->cb     = sizeof(FBHDA_mode_t);
		mode->width  = HERC_WIDTH;
		mode->height = height;
		mode->bpp    = HERC_BPP;
		for(i = 0; i < FBHDA_MODE_MAX_REFRESH; i++)
		{
			mode->refresh[i] = 0;
		}
	}

	return TRUE;
}

/* Screen switching support - restore Hercules graphics mode.
 * Re-programs the 6845 CRTC without clearing VRAM (desktop is still
 * in the wram shadow buffer and will be redrawn by the blit timer).
 */
void HERC_HIRES_enable()
{
	int i;
	dbg_printf("HERC_HIRES_enable\n");

	if(!herc_vram_lin || !herc_valid) return;

	/* Re-program 6845 CRTC for graphics mode */
	outp(HERC_CONFIG_SW, HERC_CFG_GRAPHICS | HERC_CFG_PAGE1);
	outp(HERC_MODE_CTRL, 0);  /* Disable video while programming */
	for(i = 0; i < HERC_CRTC_REG_COUNT; i++)
	{
		outp(HERC_CRTC_INDEX, (unsigned char)i);
		outp(HERC_CRTC_DATA, herc_crtc_values[i]);
	}
	outp(HERC_MODE_CTRL, HERC_MODE_GRAPHICS | HERC_MODE_ENABLE);

	vga_mode = FALSE;

	/* Force full screen redraw from wram shadow buffer */
	wblit.src_sy = 0;
	wblit.src_ey = virt_height;
	if(wblit.num_changes == 0) wblit.num_changes = 1;
}

void HERC_HIRES_disable()
{
	dbg_printf("HERC_HIRES_disable\n");
	vga_mode = TRUE;
}
