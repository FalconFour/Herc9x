/*****************************************************************************

Copyright (c) 2024 Jaroslav Hensl <emulator@emulace.cz>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*****************************************************************************/

/* const and types for GPU acceleration */

#ifndef __3D_ACCEL_H__
#define __3D_ACCEL_H__

#ifdef __WATCOMC__
#ifndef VXD32
#define FBHDA_SIXTEEN
#endif
#endif

#define API_3DACCEL_VER 20260101

#define ESCAPE_DRV_NT         0x1103 /* (4355) */

/* function codes */
#define OP_FBHDA_SETUP        0x110B /* VXD, DRV, ExtEscape, VxDCall */
#define OP_FBHDA_ACCESS_BEGIN 0x110C /* VXD, DRV, ESCAPE_DRV_NT */
#define OP_FBHDA_ACCESS_END   0x110D /* VXD, DRV, ESCAPE_DRV_NT */
#define OP_FBHDA_SWAP         0x110E /* VXD, DRV, ESCAPE_DRV_NT */
#define OP_FBHDA_CLEAN        0x110F /* VXD, DRV, ESCAPE_DRV_NT */
#define OP_FBHDA_PALETTE_SET  0x1110 /* VXD, DRV, ESCAPE_DRV_NT */
#define OP_FBHDA_PALETTE_GET  0x1111 /* VXD, DRV, ESCAPE_DRV_NT */
#define OP_FBHDA_ACCESS_RECT  0x1112 /* VXD, DRV, ESCAPE_DRV_NT */
#define OP_FBHDA_OVERLAY_SETUP  0x1113 /* VXD, VxDCall, ESCAPE_DRV_NT */
#define OP_FBHDA_OVERLAY_LOCK   0x1114 /* VXD, VxDCall, ESCAPE_DRV_NT */
#define OP_FBHDA_OVERLAY_UNLOCK 0x1115 /* VXD, VxDCall, ESCAPE_DRV_NT */

#define OP_FBHDA_GAMMA_SET    0x1116 /* VXD, DRV, ESCAPE_DRV_NT */
#define OP_FBHDA_GAMMA_GET    0x1117 /* VXD, DRV, ESCAPE_DRV_NT */

#define OP_FBHDA_PAGE_MOD     0x1118 /* VXD */
#define OP_FBHDA_MODE_QUERY   0x1119 /* VXD */
#define OP_FBHDA_REFRESH      0x1120 /* VXD, DRV, ESCAPE_DRV_NT */

#define OP_HERC_VALID         0x5000 /* DRV */
#define OP_HERC_SETMODE       0x5001 /* DRV */
#define OP_HERC_VALIDMODE     0x5002 /* DRV */

#define OP_MOUSE_BUFFER       0x1F00 /* DRV */
#define OP_MOUSE_LOAD         0x1F01 /* DRV */         
#define OP_MOUSE_MOVE         0x1F02 /* DRV */
#define OP_MOUSE_SHOW         0x1F03 /* DRV */
#define OP_MOUSE_HIDE         0x1F04 /* DRV */

/* VXDCall */
#define FBHDA_DEVICE_ID       0x4333
#define FBHDA_SERVICE_TABLE_OFFSET 0x110A
#define FBHDA__GET_VERSION 0
#define FBHDA__SETUP (OP_FBHDA_SETUP-FBHDA_SERVICE_TABLE_OFFSET)
#define FBHDA__OVERLAY_SETUP (OP_FBHDA_OVERLAY_SETUP-FBHDA_SERVICE_TABLE_OFFSET)
#define FBHDA__OVERLAY_LOCK (OP_FBHDA_OVERLAY_LOCK-FBHDA_SERVICE_TABLE_OFFSET)
#define FBHDA__OVERLAY_UNLOCK (OP_FBHDA_OVERLAY_UNLOCK-FBHDA_SERVICE_TABLE_OFFSET)

#pragma pack(push)
#pragma pack(1)

#ifdef FBHDA_SIXTEEN
# define FBPTR __far*
#else
# define FBPTR *
#endif

#define FBHDA_OVERLAYS_MAX 16
//#define FBHDA_ROW_ALIGN 8
#define FBHDA_ROW_ALIGN 4

typedef struct FBHDA_overlay
{
#ifndef FBHDA_SIXTEEN
	void *ptr;
#else
	DWORD ptr32;
#endif
	DWORD size;
} FBHDA_overlay_t;

typedef struct FBHDA
{
	         DWORD cb;
           DWORD flags;
           DWORD version;
	volatile DWORD width;
	volatile DWORD height;
	volatile DWORD bpp;
	volatile DWORD pitch;
	volatile DWORD surface;
	volatile DWORD stride;
	volatile DWORD onflip;
#ifndef FBHDA_SIXTEEN
	         void *vram_pm32; /* frame buffer address */
	         DWORD vram_pm16; 
	         void *vram_phylin; /* frame buffer mapped phy address */
#else
           DWORD       vram_pm32;
           void __far *vram_pm16;
           DWORD       vram_phylin;
#endif
	         DWORD vram_size; /* real r/w memory size */
	         DWORD vram_size_bar; /* PCI region size, may be larger then vram_size */
	         DWORD vram_size_virt; /* virtual memory size (inc. textures) to reported to apps */
	         char vxdname[16]; /* file name or "NT" */
	         DWORD overlay;
	         FBHDA_overlay_t overlays[FBHDA_OVERLAYS_MAX];
	         DWORD overlays_size;
	         DWORD gamma; /* fixed decimal point, 65536 = 1.0 */
	         DWORD system_surface;
	         DWORD palette_update; /* INC by one everytime when the palette is updated */
	         DWORD gamma_update; /* INC by one everytime when the pallete is updated */
	         DWORD gpu_mem_total;
	         DWORD gpu_mem_used;
	         /* heap allocator (removed) */
	         DWORD rem0;
	         DWORD rem1;
	         DWORD rem2;
	         DWORD rem3;
	         DWORD rem4;
	         /* reserved */
	         DWORD res0;
	         DWORD res1;
	         DWORD res2;
	         DWORD res3;
} FBHDA_t;

#define FBHDA_MODE_MAX_REFRESH 16

typedef struct FBHDA_mode
{
	         DWORD cb;
	         DWORD width;
	         DWORD height;
	         DWORD bpp;
	         DWORD refresh[FBHDA_MODE_MAX_REFRESH];
} FBHDA_mode_t;

#define FB_VRAM_HEAP_GRANULARITY (4*32)
/* minimum of vram allocation (32px at 32bpp, or 64px at 16bpp) */

#define FB_SUPPORT_FLIPING     1
#define FB_ACCEL_VIRGE         2
#define FB_ACCEL_CHROMIUM      4
#define FB_ACCEL_QEMU3DFX      8
#define FB_ACCEL_VMSVGA        16
#define FB_ACCEL_VMSVGA3D      32
#define FB_ACCEL_VMSVGA10      64
#define FB_MOUSE_NO_BLIT      128
#define FB_FORCE_SOFTWARE     256
#define FB_ACCEL_VMSVGA10_ST  512 /* not used */
#define FB_BUG_VMWARE_UPDATE 1024
#define FB_ACCEL_GPUMEM      2048
#define FB_VESA_MODES        8192
#define FB_SUPPORT_VSYNC    16384
#define FB_SUPPORT_CLOCK    32768
#define FB_SUPPORT_TRIPLE   65536
#define FB_ACCEL_VIRGL     131072

/* for internal use in RING-0 by VXD only */
BOOL FBHDA_init_hw(); 
void FBHDA_release_hw();

/* for internal use by RING-3 application/driver */
void FBHDA_load();
void FBHDA_free();

#ifdef FBHDA_SIXTEEN
	void FBHDA_setup(FBHDA_t __far* __far* FBHDA, DWORD __far* FBHDA_linear);
#else
	FBHDA_t *FBHDA_setup();
#endif

#define FBHDA_ACCESS_RAW_BUFFERING 1
#define FBHDA_ACCESS_MOUSE_MOVE 2
#define FBHDA_ACCESS_SURFACE_DIRTY 4

#define FBHDA_ACCESS_EXCLUSIVE_BEGIN 8
#define FBHDA_ACCESS_EXCLUSIVE_END   16

#define FBHDA_SWAP_NOWAIT     1
#define FBHDA_SWAP_VTRACE     2
#define FBHDA_SWAP_QUERY      4

void FBHDA_access_begin(DWORD flags);
void FBHDA_access_end(DWORD flags);
void FBHDA_access_rect(DWORD left, DWORD top, DWORD right, DWORD bottom);
BOOL FBHDA_swap(DWORD offset, DWORD flags);
void FBHDA_clean();
void  FBHDA_palette_set(unsigned char index, DWORD rgb);
DWORD FBHDA_palette_get(unsigned char index);

/* return pitch or 0 when failed */
DWORD FBHDA_overlay_setup(DWORD overlay, DWORD width, DWORD height, DWORD bpp);
void  FBHDA_overlay_lock(DWORD left, DWORD top, DWORD right, DWORD bottom);
void  FBHDA_overlay_unlock(DWORD flags);

/* format simitar to WINAPI Set/GetDeviceGammaRamp */
BOOL FBHDA_gamma_get(VOID FBPTR ramp, DWORD buffer_size);
BOOL FBHDA_gamma_set(VOID FBPTR ramp, DWORD buffer_size);

/* set refresh rate (Hz), this not affected monitor refresh rate (only for VESA)
 but internal timer, maximum frequency is about 250 Hz, default is 60 Hz */
void FBHDA_refresh(DWORD refresh_rate);

/* mouse */
#ifdef FBHDA_SIXTEEN
void mouse_buffer(void __far* __far* pBuf, DWORD __far* pLinear);
#else
void *mouse_buffer();
#endif
BOOL mouse_load();
void mouse_move(int x, int y);
void mouse_show();
void mouse_hide();

/* vxd internal */
void mouse_invalidate(); 

#define MOUSE_BUFFER_SIZE 65535

/* helper for some hacks */
BOOL FBHDA_page_modify(DWORD flat_address, DWORD size, const BYTE *new_data);

/* query resulutions + refresh rate (need FB_VESA_MODES flag set) */
BOOL FBHDA_mode_query(DWORD index, FBHDA_mode_t *mode);

/*
 * Hercules Video API
 */
#ifdef HERC

BOOL HERC_init_hw(); /* internal for VXD only */

BOOL HERC_valid();
BOOL HERC_validmode(DWORD w, DWORD h, DWORD bpp);
BOOL HERC_setmode(DWORD w, DWORD h, DWORD bpp);

#endif /* HERC */

#pragma pack(pop)

/* DLL handlers */
#define VMDISP9X_LIB "vmdisp9x.dll"

typedef FBHDA_t *(__cdecl *FBHDA_setup_t)();
typedef void (__cdecl *FBHDA_access_begin_t)(DWORD flags);
typedef void (__cdecl *FBHDA_access_end_t)(DWORD flags);
typedef void (__cdecl *FBHDA_access_rect_t)(DWORD left, DWORD top, DWORD right, DWORD bottom);
typedef BOOL (__cdecl *FBHDA_swap_t)(DWORD offset, DWORD flags);
typedef BOOL (__cdecl *FBHDA_page_modify_t)(DWORD flat_address, DWORD size, const BYTE *new_data);
typedef void (__cdecl *FBHDA_clean_t)(void);
typedef BOOL (__cdecl *FBHDA_mode_query_t)(DWORD index, FBHDA_mode_t *mode);

typedef struct _fbhda_lib_t
{
	HMODULE lib;
	LONG lock;
	FBHDA_setup_t pFBHDA_setup;
	FBHDA_access_begin_t pFBHDA_access_begin;
	FBHDA_access_end_t pFBHDA_access_end;
	FBHDA_access_rect_t pFBHDA_access_rect;
	FBHDA_swap_t pFBHDA_swap;
	FBHDA_page_modify_t pFBHDA_page_modify;
	FBHDA_clean_t pFBHDA_clean;
	FBHDA_mode_query_t pFBHDA_mode_query;
} fbhda_lib_t;

#endif /* __3D_ACCEL_H__ */
