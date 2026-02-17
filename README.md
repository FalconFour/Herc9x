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

Tested on 86Box (Hercules emulation) and intended for real Hercules-class hardware.


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

- **Stability.** The driver is not fully stable. Random crashes and hangs occur both during rapid drawing activity and near-idle states. This is the primary issue still under investigation.

- **INF installation not supported.** The driver must be installed manually via SYSTEM.INI. Normal Add New Hardware / INF-based installation does not work because the driver breaks too many VGA and PnP conventions. Display Properties and Device Manager will not show the driver.

- **DOS box corruption.** Opening a DOS prompt (Command Prompt) shows a "This program cannot run in a window" message. The driver blocks full-screen DOS switching since there is no VGA hardware to switch to. DOS/console applications are not currently usable.

- **Black rectangles.** Black boxes sometimes appear behind removed on-screen objects (ghost artifacts). This appears to be a limitation of the Windows 9x DIB engine itself when operating in 1-bit color mode - the same behavior occurs with Microsoft's own `framebuf.drv` in monochrome mode.

- **ISA bus contention.** Heavy drawing activity generates substantial ISA bus traffic from the async blit timer. This can cause stuttering on ISA sound cards sharing the bus.

- **No power management.** DPMS (Display Power Management Signaling) is not supported. The Hercules card has no power management capability. Screen savers are recommended instead.


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

Requires [Open Watcom C/C++ 1.9](http://openwatcom.org/ftp/install/) with Windows 3.1x target support (for Win16 headers).

```
set WATCOM=C:\WATCOM
set PATH=%WATCOM%\BINNT;%WATCOM%\BINW;%PATH%
set INCLUDE=%WATCOM%\H;%WATCOM%\H\WIN
wmake
```

This produces `hercmini.drv` and `hercmini.vxd`.

Debug logging is enabled by default (`DBGPRINT=1` in the makefile). Serial output goes to COM2 (16-bit driver) and COM1 (32-bit VxD). Disable by commenting out the `DBGPRINT` line in the makefile.


## License

MIT License - see individual source files for copyright notices. Original VMDisp9x code is Copyright (c) Jaroslav Hensl and Michal Necasek.
