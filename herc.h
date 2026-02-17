/*
 * Hercules Graphics Card Hardware Definitions
 * For use with Herc9x Windows 95/98 display driver
 */

#ifndef __HERC_H__INCLUDED__
#define __HERC_H__INCLUDED__

/* Hercules Graphics Mode Specifications */
#define HERC_WIDTH      720
#define HERC_HEIGHT     348     /* Physical scanlines */
#define HERC_BPP        1
#define HERC_HW_PITCH   90      /* 720 pixels / 8 bits per byte = hardware pitch */
#define HERC_DIB_PITCH  92      /* DWORD-aligned pitch for DIB engine: ((720+31)/32)*4 */
#define HERC_STRIDE     (HERC_HEIGHT * HERC_DIB_PITCH)

/* Virtual mode: 720x522 -> 720x348, skip every 3rd line (3:2 ratio) */
#define HERC_VIRT_HEIGHT  522
#define HERC_VIRT_STRIDE  (HERC_VIRT_HEIGHT * HERC_DIB_PITCH)

/* Framebuffer Physical Address */
#define HERC_VRAM_BASE  0xB0000UL
#define HERC_VRAM_SIZE  0x10000UL  /* 64KB */

/* Planar Memory Layout - 4-way interleave */
#define HERC_PLANE_SIZE 0x2000   /* 8KB per plane */
#define HERC_PLANE_0    0x0000   /* Lines 0, 4, 8, 12... */
#define HERC_PLANE_1    0x2000   /* Lines 1, 5, 9, 13... */
#define HERC_PLANE_2    0x4000   /* Lines 2, 6, 10, 14... */
#define HERC_PLANE_3    0x6000   /* Lines 3, 7, 11, 15... */

/* I/O Port Definitions - 6845 CRTC */
#define HERC_CRTC_INDEX 0x3B4    /* Index register */
#define HERC_CRTC_DATA  0x3B5    /* Data register */
#define HERC_MODE_CTRL  0x3B8    /* Mode control register */
#define HERC_STATUS     0x3BA    /* Status register (read-only) */
#define HERC_CONFIG_SW  0x3BF    /* Configuration switch */

/* Status Register Bits (0x3BA, read-only) */
#define HERC_STATUS_HSYNC   0x01  /* Horizontal retrace active */
#define HERC_STATUS_VIDEO   0x08  /* Pixel being drawn (video signal) */
#define HERC_STATUS_VSYNC   0x80  /* Vertical sync (0=active, 1=inactive) */

/* Mode Control Register Bits (0x3B8) */
#define HERC_MODE_GRAPHICS  0x02  /* Graphics mode (vs text) */
#define HERC_MODE_ENABLE    0x08  /* Video enable */
#define HERC_MODE_BLINK     0x20  /* Enable blink */
#define HERC_MODE_PAGE1     0x80  /* Page 1 select */

/* Configuration Switch Register (0x3BF) */
#define HERC_CFG_GRAPHICS   0x01  /* Graphics mode allowed */
#define HERC_CFG_PAGE1      0x02  /* Page 1 allowed */

/* 6845 CRTC Register Count */
#define HERC_CRTC_REG_COUNT 16

#endif /* __HERC_H__INCLUDED__ */
