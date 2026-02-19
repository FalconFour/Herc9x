/*
 * BSPLASH - Boot Splash for Hercules Graphics Card
 *
 * Displays a 1-bit Windows BMP on a Hercules card, or restores text mode.
 *
 * Usage:
 *   bsplash              - Restore MDA text mode and exit
 *   bsplash image.bmp    - Display BMP in Hercules graphics mode and exit
 *
 * The BMP must be exactly 720x348 pixels, 1-bit monochrome, uncompressed.
 * The program exits leaving the display in whichever mode was set.
 *
 * Typical boot integration (AUTOEXEC.BAT):
 *   bsplash C:\LOGO.BMP
 *   ... (Windows loads, takes over Hercules) ...
 *   bsplash
 *
 * Build: wmake bsplash.exe  (requires Open Watcom C)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>

/* Hercules display constants */
#define HERC_WIDTH       720
#define HERC_HEIGHT      348
#define HERC_PITCH       90      /* 720 pixels / 8 bits = 90 bytes/line */
#define HERC_PLANE_SIZE  0x2000  /* 8KB per interleave plane */
#define HERC_VRAM_SEG    0xB000

/* I/O ports */
#define CRTC_INDEX  0x3B4
#define CRTC_DATA   0x3B5
#define MODE_CTRL   0x3B8
#define CONFIG_SW   0x3BF

/* 6845 CRTC register values - Hercules graphics mode (720x348) */
static const unsigned char crtc_graph[16] = {
	0x35, 0x2D, 0x2E, 0x07, 0x5B, 0x02, 0x57, 0x57,
	0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* 6845 CRTC register values - MDA text mode (80x25) */
static const unsigned char crtc_text[16] = {
	0x61, 0x50, 0x52, 0x0F, 0x19, 0x06, 0x19, 0x19,
	0x02, 0x0D, 0x0B, 0x0C, 0x00, 0x00, 0x00, 0x00
};

/* BMP file structures */
#pragma pack(push, 1)
typedef struct {
	unsigned short bfType;
	unsigned long  bfSize;
	unsigned short bfReserved1;
	unsigned short bfReserved2;
	unsigned long  bfOffBits;
} BMPFILEHDR;

typedef struct {
	unsigned long  biSize;
	long           biWidth;
	long           biHeight;
	unsigned short biPlanes;
	unsigned short biBitCount;
	unsigned long  biCompression;
	unsigned long  biSizeImage;
	long           biXPelsPerMeter;
	long           biYPelsPerMeter;
	unsigned long  biClrUsed;
	unsigned long  biClrImportant;
} BMPINFOHDR;

typedef struct {
	unsigned char b, g, r, x;
} RGBQUAD;
#pragma pack(pop)

/*
 * Program all 16 CRTC registers from a table.
 */
static void program_crtc(const unsigned char *vals)
{
	int i;
	for (i = 0; i < 16; i++) {
		outp(CRTC_INDEX, i);
		outp(CRTC_DATA, vals[i]);
	}
}

/*
 * Restore MDA text mode. Does not clear the text buffer.
 */
static void set_text_mode(void)
{
	outp(CONFIG_SW, 0x00);   /* text mode only */
	outp(MODE_CTRL, 0x20);  /* text + blink, screen off */
	program_crtc(crtc_text);
	outp(MODE_CTRL, 0x28);  /* text + blink, screen on */
}

/*
 * Switch to Hercules 720x348 graphics mode.
 */
static void set_graphics_mode(void)
{
	outp(CONFIG_SW, 0x03);   /* allow graphics + page 1 */
	outp(MODE_CTRL, 0x00);  /* screen off while programming */
	program_crtc(crtc_graph);
	outp(MODE_CTRL, 0x0A);  /* graphics + video enable */
}

/*
 * Clear all visible VRAM (32KB covering all 4 interleave planes).
 */
static void clear_vram(void)
{
	unsigned char far *vram = MK_FP(HERC_VRAM_SEG, 0);
	_fmemset(vram, 0, 0x8000U);
}

/*
 * Write one scanline to Hercules VRAM using 4-way planar interleave.
 *   Line Y -> plane (Y & 3), row-in-plane (Y >> 2)
 *   Address = plane * 0x2000 + row * 90
 */
static void write_scanline(int y, const unsigned char *data)
{
	unsigned int plane = y & 3;
	unsigned int row   = (unsigned int)y >> 2;
	unsigned int off   = plane * HERC_PLANE_SIZE + row * HERC_PITCH;
	unsigned char far *dst = MK_FP(HERC_VRAM_SEG, off);

	_fmemcpy(dst, data, HERC_PITCH);
}

int main(int argc, char *argv[])
{
	FILE *fp;
	BMPFILEHDR fh;
	BMPINFOHDR ih;
	RGBQUAD pal[2];
	unsigned char row[96];  /* max BMP stride for 720px + safety margin */
	int bmp_stride;
	int invert, topdown;
	long bmp_h, k;
	int dest_y, i;

	if (argc < 2) {
		fprintf(stderr,
			"BSPLASH - Hercules Boot Splash v1.0\n"
			"Displays a 1-bit BMP on Hercules 720x348\n\n"
			"Usage:\n"
			"  bsplash <file.bmp>  Display BMP and exit\n"
			"  bsplash /t          Restore text mode and exit\n");
		return 0;
	}

	if (argv[1][0] == '/' || argv[1][0] == '-') {
		if (argv[1][1] == 't' || argv[1][1] == 'T') {
			set_text_mode();
			return 0;
		}
		fprintf(stderr,
			"BSPLASH - Hercules Boot Splash v1.0\n"
			"Unknown option: %s\n", argv[1]);
		return 1;
	}

	/* --- Validate BMP before touching hardware --- */

	fprintf(stderr, "BSPLASH - Hercules Boot Splash v1.0\n");

	fp = fopen(argv[1], "rb");
	if (!fp) {
		fprintf(stderr, "Error: cannot open %s\n", argv[1]);
		return 1;
	}

	if (fread(&fh, sizeof(fh), 1, fp) != 1 || fh.bfType != 0x4D42) {
		fprintf(stderr, "Error: not a BMP file\n");
		fclose(fp);
		return 1;
	}

	if (fread(&ih, sizeof(ih), 1, fp) != 1) {
		fprintf(stderr, "Error: truncated header\n");
		fclose(fp);
		return 1;
	}

	if (ih.biBitCount != 1 || ih.biPlanes != 1) {
		fprintf(stderr, "Error: not a 1-bit monochrome BMP\n");
		fclose(fp);
		return 1;
	}

	if (ih.biCompression != 0) {
		fprintf(stderr, "Error: compressed BMPs not supported\n");
		fclose(fp);
		return 1;
	}

	topdown = (ih.biHeight < 0);
	bmp_h = topdown ? -ih.biHeight : ih.biHeight;

	if (ih.biWidth != HERC_WIDTH || bmp_h != HERC_HEIGHT) {
		fprintf(stderr, "Error: BMP is %ldx%ld, need exactly %dx%d\n",
		        ih.biWidth, bmp_h, HERC_WIDTH, HERC_HEIGHT);
		fclose(fp);
		return 1;
	}

	/* Determine BMP row stride.
	 * Standard: DWORD-aligned -> ((720+31)/32)*4 = 92 bytes.
	 * Some tools write unpadded 90-byte rows (non-conformant but common
	 * for 1-bit BMPs). Use biSizeImage to detect actual stride, falling
	 * back to the DWORD-aligned formula.
	 */
	bmp_stride = (int)(((ih.biWidth + 31) / 32) * 4);
	if (ih.biSizeImage >= (unsigned long)HERC_PITCH * bmp_h) {
		int actual = (int)(ih.biSizeImage / (unsigned long)bmp_h);
		if (actual >= HERC_PITCH && actual <= bmp_stride) {
			bmp_stride = actual;
		}
	}

	/* Read palette (seek past any extended info header) */
	fseek(fp, 14L + (long)ih.biSize, SEEK_SET);
	if (fread(pal, sizeof(RGBQUAD), 2, fp) != 2) {
		fprintf(stderr, "bsplash: cannot read palette\n");
		fclose(fp);
		return 1;
	}

	/*
	 * Determine if bits need inversion.
	 * Hercules: bit 1 = bright pixel, bit 0 = dark pixel.
	 * BMP: bit value indexes into palette.
	 * If palette[0] is brighter than palette[1], invert all bits.
	 */
	invert = ((int)pal[0].r + pal[0].g + pal[0].b >
	          (int)pal[1].r + pal[1].g + pal[1].b);

	/* --- Validation passed, switch to graphics mode --- */

	set_graphics_mode();
	clear_vram();

	/* Seek to pixel data */
	fseek(fp, (long)fh.bfOffBits, SEEK_SET);

	/* Blit BMP rows to Hercules VRAM */
	for (k = 0; k < HERC_HEIGHT; k++) {
		if (fread(row, (size_t)bmp_stride, 1, fp) != 1) break;

		if (topdown)
			dest_y = (int)k;
		else
			dest_y = HERC_HEIGHT - 1 - (int)k;

		if (invert) {
			for (i = 0; i < HERC_PITCH; i++)
				row[i] = ~row[i];
		}

		write_scanline(dest_y, row);
	}

	fclose(fp);
	return 0;
}
