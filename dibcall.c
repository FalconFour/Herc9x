/*****************************************************************************

Copyright (c) 2022  Michal Necasek
              2023  Jaroslav Hensl

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

#include "winhack.h"
#include <gdidefs.h>
#include <dibeng.h>
#include <minivdd.h>
#include <string.h>

#include "minidrv.h"
#include "cursor.h"

#include "3d_accel.h"

DWORD mouse_buf_lin = 0;
void __far* mouse_buf = NULL;
BOOL mouse_vxd = FALSE;

/*
 * What's this all about? Most of the required exported driver functions can
 * be passed straight to the DIB Engine. The DIB Engine in some cases requires
 * an additional parameter.
 *
 * See the dibthunk.asm module for functions that can be handled easily. Note
 * that although the logic could be implemented in C, it can be done more
 * efficiently in assembly. Forwarders turn into simple jumps, and functions
 * with an extra parameter can be implemented with very small overhead, saving
 * stack space and extra copying.
 *
 * This module deals with the very few functions that need additional logic but
 * are not hardware specific.
 */

#pragma code_seg( _TEXT )

void WINAPI __loadds MoveCursor(int absX, int absY)
{	
	if(wEnabled)
	{
		if(mouse_vxd)
		{
			mouse_move(absX, absY);
		}
		else
		{
			DIB_MoveCursorExt(absX, absY, lpDriverPDevice);
		}
	}
}

static size_t mouse_cursor_size(CURSORSHAPE __far *lpCursor)
{
	size_t s = 0;
	s = ((lpCursor->cx + 7)/8) * lpCursor->cy; // size of AND mask
	
	if(s == 0)
		return 0;	
	
	if(lpCursor->BitsPixel == 1)
	{
		s += ((lpCursor->cx + 7)/8) * lpCursor->cy; // size of XOR mask 1bpp
	}
	else
	{
		s += ((lpCursor->BitsPixel + 7)/8) * lpCursor->cx * lpCursor->cy; // size of XOR mask (mode bpp)
	}
	
	s += sizeof(CURSORSHAPE);
	
	return s;
}

WORD WINAPI __loadds SetCursor_driver(CURSORSHAPE __far *lpCursor)
{
	if(wEnabled)
	{
		if(mouse_vxd)
		{
			if(lpCursor != NULL)
			{
				size_t ms = mouse_cursor_size(lpCursor);
				if(ms > 0)
				{
					_fmemcpy(mouse_buf, lpCursor, ms);
					if(mouse_load())
					{
						return 1;
					}
					return 0;
				}
			}
			mouse_hide();
			return 1;
		}
		else
		{
			DIB_SetCursorExt(lpCursor, lpDriverPDevice);
			return 1;
		}
		
	}
	
	return 0;
}

/* Exported as DISPLAY.104 */
void WINAPI __loadds CheckCursor( void )
{
	if(wEnabled)
	{
		if(!mouse_vxd)
		{
			DIB_CheckCursorExt(lpDriverPDevice);
		}
	}
}

/* Custom BitBlt implementation.
 *
 * The DIB engine has a bug in 1bpp mode: when blitting between the screen
 * surface (DIBENGINE, deType=TYPE_DIBENG) and a monochrome memory bitmap
 * (PBITMAP, bmType=0), it applies color conversion using DRAWMODE's
 * TextColor/bkColor even though both surfaces are 1bpp. This produces
 * incorrect (all-black) output, causing black artifacts when dragging
 * windows, opening menus, etc.
 *
 * We intercept SRCCOPY blits where both surfaces are 1bpp and perform
 * a direct bit copy, bypassing the broken color conversion.
 */
#ifdef HWBLT

#define SRCCOPY_ROP  0x00CC0020L

extern BOOL WINAPI (* BitBltDevProc)( LPDIBENGINE, WORD, WORD, LPPDEVICE, WORD, WORD,
                                      WORD, WORD, DWORD, LPBRUSH, LPDRAWMODE );

/* Get the bits-per-pixel for a device, which may be DIBENGINE or PBITMAP. */
static BYTE get_dev_bpp( LPPDEVICE lpDev )
{
    /* Both DIBENGINE and BITMAP have deBitsPixel/bmBitsPixel at offset +9. */
    return ((LPDIBENGINE)lpDev)->deBitsPixel;
}

/* Get a far pointer to the bitmap bits for a device. */
static BYTE FAR * get_dev_bits( LPPDEVICE lpDev )
{
    LPDIBENGINE lpEng = (LPDIBENGINE)lpDev;
    if( lpEng->deType == TYPE_DIBENG ) {
        /* DIBENGINE: construct far pointer from selector + offset.
         * deBitsSelector:deBitsOffset forms a 48-bit pointer.
         * For our Hercules driver, the offset is within 64KB so fits in 16 bits.
         */
        return (BYTE FAR *)( ((DWORD)lpEng->deBitsSelector << 16)
                           | (WORD)lpEng->deBitsOffset );
    } else {
        /* PBITMAP (deType == 0): use bmBits field. */
        return ((LPBITMAP)lpDev)->bmBits;
    }
}

/* Get the scanline stride in bytes. */
static WORD get_dev_stride( LPPDEVICE lpDev )
{
    /* Both structs have this at offset +6 (deWidthBytes / bmWidthBytes). */
    return ((LPDIBENGINE)lpDev)->deWidthBytes;
}

/* Get device width in pixels. */
static WORD get_dev_width( LPPDEVICE lpDev )
{
    /* Both DIBENGINE and BITMAP have width at offset +2. */
    return ((LPDIBENGINE)lpDev)->deWidth;
}

/* Get device height in pixels. */
static WORD get_dev_height( LPPDEVICE lpDev )
{
    /* Both DIBENGINE and BITMAP have height at offset +4. */
    return ((LPDIBENGINE)lpDev)->deHeight;
}

/* Check if a device is the screen (VRAM-backed DIBENGINE). */
static BOOL is_screen_dev( LPPDEVICE lpDev )
{
    LPDIBENGINE lpEng = (LPDIBENGINE)lpDev;
    return (lpEng->deType == TYPE_DIBENG) && (lpEng->deFlags & VRAM);
}

/* Copy one scanline of 1bpp data between surfaces with same bit alignment.
 * srcRow and dstRow point to the byte containing the first pixel.
 * bitOff is the bit offset within that first byte (0 = MSB).
 * Uses _fmemmove for the bulk copy to handle overlapping regions.
 */
static void blit_row_aligned( BYTE FAR *dstRow, BYTE FAR *srcRow,
                               BYTE bitOff, WORD width )
{
    WORD totalBits = width;
    WORD byteIdx = 0;
    BYTE mask;

    if( bitOff != 0 ) {
        BYTE bitsInFirst = 8 - bitOff;
        if( bitsInFirst > totalBits )
            bitsInFirst = (BYTE)totalBits;
        mask = (BYTE)((0xFF >> bitOff) & (0xFF << (8 - bitOff - bitsInFirst)));
        dstRow[0] = (dstRow[0] & ~mask) | (srcRow[0] & mask);
        totalBits -= bitsInFirst;
        byteIdx = 1;
    }

    if( totalBits >= 8 ) {
        WORD fullBytes = totalBits >> 3;
        _fmemmove( dstRow + byteIdx, srcRow + byteIdx, fullBytes );
        byteIdx += fullBytes;
        totalBits -= fullBytes << 3;
    }

    if( totalBits > 0 ) {
        mask = (BYTE)(0xFF << (8 - totalBits));
        dstRow[byteIdx] = (dstRow[byteIdx] & ~mask) | (srcRow[byteIdx] & mask);
    }
}

/* Copy one scanline of 1bpp data between surfaces with different bit alignment.
 * Reads the full source row into a local buffer first, so overlapping
 * source/destination regions are handled safely regardless of direction.
 */
static void blit_row_unaligned( BYTE FAR *dstRow, BYTE FAR *srcRow,
                                 BYTE dstBitOff, BYTE srcBitOff, WORD width )
{
    /* Local buffer: max 720 pixels = 90 bytes + 1 for partial byte overshoot. */
    BYTE    srcBuf[92];
    WORD    srcBytes;
    WORD    px;
    WORD    dstByte, srcByte;
    BYTE    dBit, sBit;
    BYTE    dVal, sVal, dMask;

    /* Calculate how many source bytes we need to read. */
    srcBytes = (WORD)(srcBitOff + width + 7) >> 3;
    if( srcBytes > sizeof(srcBuf) )
        srcBytes = sizeof(srcBuf);

    /* Copy source bytes to local buffer to eliminate overlap hazard. */
    _fmemcpy( srcBuf, srcRow, srcBytes );

    dstByte = 0;
    dBit = dstBitOff;
    srcByte = 0;
    sBit = srcBitOff;

    dVal = dstRow[0];

    for( px = 0; px < width; px++ ) {
        sVal = (srcBuf[srcByte] >> (7 - sBit)) & 1;

        if( sVal )
            dVal |= (BYTE)(0x80 >> dBit);
        else
            dVal &= ~(BYTE)(0x80 >> dBit);

        sBit++;
        if( sBit >= 8 ) {
            sBit = 0;
            srcByte++;
        }

        dBit++;
        if( dBit >= 8 ) {
            dstRow[dstByte] = dVal;
            dstByte++;
            dBit = 0;
            dVal = dstRow[dstByte];
        }
    }

    if( dBit != 0 ) {
        dMask = (BYTE)(0xFF << (8 - dBit));
        dstRow[dstByte] = (dstRow[dstByte] & ~dMask) | (dVal & dMask);
    }
}

/* Direct 1bpp bit-block copy. Handles arbitrary bit alignment and overlap.
 * Both source and destination are 1bpp packed-pixel surfaces (MSB = leftmost).
 * Coordinates and extents must already be clipped to surface bounds.
 */
static void blit_1bpp( BYTE FAR *dstBits, WORD dstStride, WORD dstX, WORD dstY,
                       BYTE FAR *srcBits, WORD srcStride, WORD srcX, WORD srcY,
                       WORD width, WORD height )
{
    WORD    srcByteOff, dstByteOff;
    BYTE    srcBitOff, dstBitOff;
    int     y, yStart, yEnd, yStep;
    BYTE FAR *srcRow;
    BYTE FAR *dstRow;

    if( width == 0 || height == 0 )
        return;

    srcBitOff = (BYTE)(srcX & 7);
    dstBitOff = (BYTE)(dstX & 7);
    srcByteOff = srcX >> 3;
    dstByteOff = dstX >> 3;

    /* Determine copy direction for overlap safety (screen-to-screen blits). */
    if( srcBits == dstBits && (dstY > srcY || (dstY == srcY && dstX > srcX)) ) {
        yStart = (int)height - 1;
        yEnd = -1;
        yStep = -1;
    } else {
        yStart = 0;
        yEnd = (int)height;
        yStep = 1;
    }

    for( y = yStart; y != yEnd; y += yStep ) {
        srcRow = srcBits + (DWORD)(srcY + y) * srcStride + srcByteOff;
        dstRow = dstBits + (DWORD)(dstY + y) * dstStride + dstByteOff;

        if( srcBitOff == dstBitOff ) {
            blit_row_aligned( dstRow, srcRow, dstBitOff, width );
        } else {
            blit_row_unaligned( dstRow, srcRow, dstBitOff, srcBitOff, width );
        }
    }
}

BOOL WINAPI __loadds BitBlt( LPDIBENGINE lpDestDev, WORD wDestX, WORD wDestY, LPPDEVICE lpSrcDev,
                    WORD wSrcX, WORD wSrcY, WORD wXext, WORD wYext, DWORD dwRop3,
                    LPBRUSH lpPBrush, LPDRAWMODE lpDrawMode )
{
    /* Intercept 1bpp SRCCOPY to work around DIB engine color conversion bug.
     * The DIB engine incorrectly applies TextColor/bkColor conversion when
     * blitting between DIBENGINE (screen) and PBITMAP (memory) surfaces,
     * even though both are 1bpp and no conversion is needed.
     */
    if( dwRop3 == SRCCOPY_ROP && lpSrcDev != NULL && wBpp == 1 ) {
        BYTE srcBpp = get_dev_bpp( lpSrcDev );
        BYTE dstBpp = get_dev_bpp( (LPPDEVICE)lpDestDev );

        if( srcBpp == 1 && dstBpp == 1 && wXext > 0 && wYext > 0 ) {
            BYTE FAR *srcBits = get_dev_bits( lpSrcDev );
            BYTE FAR *dstBits = get_dev_bits( (LPPDEVICE)lpDestDev );
            WORD srcStride = get_dev_stride( lpSrcDev );
            WORD dstStride = get_dev_stride( (LPPDEVICE)lpDestDev );

            if( srcBits != NULL && dstBits != NULL ) {
                WORD srcW = get_dev_width( lpSrcDev );
                WORD srcH = get_dev_height( lpSrcDev );
                WORD dstW = get_dev_width( (LPPDEVICE)lpDestDev );
                WORD dstH = get_dev_height( (LPPDEVICE)lpDestDev );
                int sx = (int)wSrcX, sy = (int)wSrcY;
                int dx = (int)wDestX, dy = (int)wDestY;
                int w = (int)wXext, h = (int)wYext;

                /* Handle negative source coordinates: clip off the
                 * off-surface portion and adjust dest to match.
                 */
                if( sx < 0 ) { w += sx; dx -= sx; sx = 0; }
                if( sy < 0 ) { h += sy; dy -= sy; sy = 0; }

                /* Handle negative dest coordinates: clip off the
                 * off-surface portion and adjust source to match.
                 */
                if( dx < 0 ) { w += dx; sx -= dx; dx = 0; }
                if( dy < 0 ) { h += dy; sy -= dy; dy = 0; }

                /* Bail if everything clipped away. */
                if( w <= 0 || h <= 0 )
                    return TRUE;

                /* Clip to source right/bottom edge. */
                if( sx + w > (int)srcW )
                    w = (int)srcW - sx;
                if( sy + h > (int)srcH )
                    h = (int)srcH - sy;

                /* Clip to dest right/bottom edge. */
                if( dx + w > (int)dstW )
                    w = (int)dstW - dx;
                if( dy + h > (int)dstH )
                    h = (int)dstH - dy;

                if( w <= 0 || h <= 0 )
                    return TRUE;

                blit_1bpp( dstBits, dstStride, (WORD)dx, (WORD)dy,
                           srcBits, srcStride, (WORD)sx, (WORD)sy,
                           (WORD)w, (WORD)h );

                /* If the destination is the screen, mark the region dirty
                 * so the async timer picks it up for WRAM -> VRAM conversion.
                 */
                if( is_screen_dev( (LPPDEVICE)lpDestDev ) ) {
                    FBHDA_access_rect( (WORD)dx, (WORD)dy,
                                       (DWORD)dx + w,
                                       (DWORD)dy + h );
                }
                return TRUE;
            }
        }
    }

    /* For all other cases, fall through to the DIB engine. */
    return( DIB_BitBlt( lpDestDev, wDestX, wDestY, lpSrcDev, wSrcX, wSrcY, wXext, wYext, dwRop3, lpPBrush, lpDrawMode ) );
}

#endif

#ifndef ETO_GLYPH_INDEX
#define ETO_GLYPH_INDEX 0x0010
#endif

DWORD WINAPI __loadds ExtTextOut( LPDIBENGINE lpDestDev, WORD wDestXOrg, WORD wDestYOrg, LPRECT lpClipRect,
                                        LPSTR lpString, int wCount, LPFONTINFO lpFontInfo, LPDRAWMODE lpDrawMode,
                                        LPTEXTXFORM lpTextXForm, LPSHORT lpCharWidths, LPRECT lpOpaqueRect, WORD wOptions )
{
	return DIB_ExtTextOut(lpDestDev, wDestXOrg, wDestYOrg, lpClipRect, lpString, wCount, lpFontInfo, lpDrawMode, lpTextXForm, lpCharWidths, lpOpaqueRect, wOptions);
}

BOOL WINAPI __loadds DDIGammaRamp(LPDIBENGINE lpDev, BOOL fGetSet, LPARAM lpGammaRamp)
{
	BOOL rc;
	if(fGetSet)
	{
		rc = FBHDA_gamma_set((VOID FBPTR)lpGammaRamp, sizeof(WORD)*256*3);
		dbg_printf("set the ramp: %d\n", rc);
	}
	else
	{
		rc = FBHDA_gamma_get((VOID FBPTR)lpGammaRamp, sizeof(WORD)*256*3);
		dbg_printf("get the ramp\n");
	}
	
	return rc;
}
