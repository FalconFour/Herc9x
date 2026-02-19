# Herc9x

Windows 95/98 display driver for the Hercules Graphics Card (and compatibles).

Renders a full Windows desktop on monochrome Hercules hardware: 720x348 pixels, 1 bit per pixel, through a 6845 CRTC driving a long-persistence phosphor CRT.

## Origin

This driver is built on [VMDisp9x](https://github.com/JHRobotics/vmdisp9x) by Jaroslav Hensl, which is itself based on [Michal Necasek's VirtualBox display minidriver](http://www.os2museum.com/wp/windows-9x-video-minidriver-hd/). The original project supports Bochs VBE, VMware SVGA-II, VBox SVGA, and VESA 2.0/3.0 adapters in virtual machines.

Herc9x strips out all virtual GPU and 3D acceleration support, replacing it with direct 6845 CRTC programming and a planar framebuffer blit engine for the Hercules card's interleaved memory layout.


## Hardware

- **Resolution:** 720x348 (native), 720x522 (virtual mode with line skipping)
- **Color depth:** 1 bpp (monochrome)
- **Framebuffer:** 0xB0000-0xBFFFF (64 KB), 4-way planar interleave
- **I/O ports:** 0x3B4/3B5 (6845 CRTC), 0x3B8 (mode control), 0x3BF (config switch)
- **Bus:** 8-bit ISA

Tested on 86Box (Hercules emulation) and real Hercules-class hardware (single-chip Winbond W86855AF) on Pentium Pro. Not yet tested on an original/OG Hercules card.

## Installation

### Prerequisites

A working Windows 95 or Windows 98 installation. The system must already be set up and bootable - the driver cannot be used during initial Windows setup from CD/floppy because the installer requires VGA.

**Planned:** Bare-metal installation support (setting up Windows directly from Hercules without ever using VGA). Not yet implemented.

### Setup procedure

1. **Boot to command prompt.** Press F8 during startup and select "Command prompt only."

2. **Copy the driver files.** Place `hercmini.drv` and `hercmini.vxd` into your Windows system directory:
   ```
   COPY hercmini.drv C:\WINDOWS\SYSTEM
   COPY hercmini.vxd C:\WINDOWS\SYSTEM
   ```

3. **Edit SYSTEM.INI.** Open `C:\WINDOWS\SYSTEM.INI` in a text editor (e.g. `EDIT C:\WINDOWS\SYSTEM.INI`).

   In the `[boot]` section, comment out the existing display driver and add ours:
   ```ini
   [boot]
   ;display.drv=pnpdrvr.drv
   display.drv=hercmini.drv
   ```

   In the `[386Enh]` section, add the VxD:
   ```ini
   [386Enh]
   device=hercmini.vxd
   ```

4. **Boot Windows.** Restart the system. The desktop should appear on the Hercules monitor.

### Virtual 720x522 mode

The native Hercules resolution (720x348) is quite short vertically. The driver supports a virtual 720x522 mode that gives Windows more vertical workspace by mapping 3 virtual scanlines to every 2 physical scanlines (skipping every 3rd line).

To enable it, add a `[display]` section to `SYSTEM.INI`:
```ini
[display]
x_resolution=720
y_resolution=522
bpp=1
```

### Uninstalling

Reverse the SYSTEM.INI changes: remove the `display.drv=hercmini.drv` line, uncomment the original `display.drv=` line, and remove the `device=hercmini.vxd` line from `[386Enh]`. Boot to safe mode or command prompt if needed.


## Known issues

- **DOS box corruption.** Opening a DOS prompt (Command Prompt) shows a "This program cannot run in a window" message. The driver blocks full-screen DOS switching since there is no VGA hardware to switch to. DOS/console applications are not currently usable.

- **Black rectangles.** Black boxes sometimes appear behind removed on-screen objects (ghost artifacts). This appears to be a limitation of the Windows 9x DIB engine itself when operating in 1-bit color mode - the same behavior occurs with Microsoft's own `framebuf.drv` in monochrome mode.

- **BSOD-free!** Hercules Monochrome is incapable of producing the blue color inherent in the Blue Screen of Death, thus it has been unimplemented. **KIDDING!** No, but for real though - switching to/from text-mode to display the BSOD is not working, thus Windows just hangs/crashes in case of a BSOD ("Fatal Exception 0E" etc etc).

- **INF installation not supported.** The driver must be installed manually via SYSTEM.INI. Normal Add New Hardware / INF-based installation does not work because the driver breaks too many VGA and PnP conventions. Display Properties and Device Manager will not show the driver.

- **ISA bus contention.** Heavy drawing activity generates substantial ISA bus traffic from the async blit timer. This can cause stuttering on ISA sound cards sharing the bus.

- **No power management.** DPMS (Display Power Management Signaling) is not supported. The Hercules card has no power management capability. Screen savers are recommended instead.


## Boot Splash Utility (bsplash)

Included is `bsplash.exe`, a standalone DOS program that displays a 1-bit BMP image on the Hercules card before Windows loads. This gives you a graphical boot splash instead of a text-mode DOS prompt during startup.

### Usage

```
bsplash <file.bmp>    Display BMP in Hercules graphics mode and exit
bsplash /t            Restore text mode and exit
bsplash               Show usage
```

### Requirements

The BMP must be exactly **720x348 pixels, 1-bit (monochrome), uncompressed**. Both standard (palette[0]=black) and inverted (palette[0]=white) palettes are handled automatically.

To convert an image with ImageMagick:
```
magick input.png -resize 720x348! -dither FloydSteinberg -monochrome BMP3:splash.bmp
```

### Boot integration

Add to your `AUTOEXEC.BAT`, before `WIN`:
```batch
@echo off
C:\HERC9X\BSPLASH.EXE C:\HERC9X\SPLASH.BMP
WIN
```

The `@echo off` is important - without it, DOS echoes the commands to the screen as text, which corrupts the graphics framebuffer.

The splash image stays on screen until the Herc9x display driver takes over during Windows startup. After Windows shuts down, call `BSPLASH /T` to restore text mode, or just let the DOS prompt overwrite the graphics buffer naturally.


## Architecture

The driver consists of two components:

- **hercmini.drv** - 16-bit display driver (Ring 3). Interfaces with GDI and the DIB engine. Handles mode validation, palette stubs, and screen switch hooks.

- **hercmini.vxd** - 32-bit VxD mini-driver (Ring 0). Programs the 6845 CRTC, maps the hardware framebuffer, manages the wram shadow buffer, and runs the async blit timer that converts linear 1bpp pixel data to the Hercules planar memory layout.

Drawing flow:
```
GDI/DIB Engine -> wram shadow buffer (linear 1bpp)
                    |
              async blit timer (Ring 0)
                    |
                    v
           Hercules VRAM (4-way planar interleave at 0xB0000)
```

The DIB engine renders into a linear shadow buffer. A periodic timer in the VxD converts dirty regions from linear layout to the Hercules 4-plane interleave and writes them to hardware VRAM. Software cursor compositing happens during the blit.


## Building from source

Requires [Open Watcom C/C++ 1.9](http://openwatcom.org/ftp/install/).

During Watcom installation, enable at minimum:
- **16-bit compiler and tools** (for the .drv and bsplash.exe)
- **32-bit compiler and tools** (for the .vxd)
- **Windows 3.1 16-bit target** headers/libraries (Win16 API for the .drv)
- **16-bit DOS target** headers/libraries (for bsplash.exe)

```
set WATCOM=C:\WATCOM
set PATH=%WATCOM%\BINNT;%WATCOM%\BINW;%PATH%
set INCLUDE=%WATCOM%\H;%WATCOM%\H\WIN
wmake
```

This produces `hercmini.drv`, `hercmini.vxd`, and `bsplash.exe`.

Debug logging is enabled by default (`DBGPRINT=1` in the makefile). Serial output goes to COM2 (16-bit driver) and COM1 (32-bit VxD). For a release build, comment out the `DBGPRINT = 1` line in the makefile and run `wmake clean` before building.


## License

MIT License - see individual source files for copyright notices. Original VMDisp9x code is Copyright (c) Jaroslav Hensl and Michal Necasek.
