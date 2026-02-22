# Bare-Metal Installation (Windows 95)

Install Windows 95 on a Hercules-only system - no VGA card needed. This uses the SETUPMOD utility to pre-integrate the Herc9x driver into Windows Setup so you can select "Hercules Graphics Card (ISA)" during installation.

**Requirements:** A DOS boot floppy that can access your CD-ROM drive, a Windows 95 CD, and a hard drive with at least 100 MB free.


## Step 1: Prepare a Boot Floppy

Create a DOS boot floppy with CD-ROM support. You may need to customize the floppy for your hardware - for example, adding drivers for a Backpack parallel-port floppy drive, a proprietary Mitsumi CD-ROM interface card, or other non-standard hardware.

Your floppy should boot to a DOS prompt with:
- Access to the CD-ROM drive (e.g. D:)
- Access to the hard drive (e.g. C:)

**Tip:** Loading XMSMMGR.EXE and SMARTDRV.EXE in your boot floppy's CONFIG.SYS / AUTOEXEC.BAT will dramatically speed up the file copy process (from ~2 minutes down to ~40 seconds).


## Step 2: Copy SETUPMOD Files to Floppy

Copy the contents of the `W9XHERC` folder from the Herc9x release onto your boot floppy in a subfolder:

```
A:\W9XHERC\SETUPMOD.EXE
A:\W9XHERC\HERCULES.DRV
A:\W9XHERC\HERCMINI.DRV
A:\W9XHERC\HERCMINI.VXD
A:\W9XHERC\HERC9X.INF
```


## Step 3: Partition and Format the Hard Drive

Boot from the floppy and prepare your hard drive if you haven't already:

```
FDISK
```

Create a primary DOS partition, reboot, then format:

```
FORMAT C: /S
```

Verify you can write to C: before continuing.


## Step 4: Copy SETUPMOD to the Hard Drive

Copy the W9XHERC folder from the floppy to the hard drive:

```
C:
MKDIR \W9XHERC
CD \W9XHERC
COPY A:\W9XHERC\*.* .
```


## Step 5: Run SETUPMOD

Insert the Windows 95 CD-ROM and run:

```
SETUPMOD.EXE
```

SETUPMOD will:
1. Detect the Windows 95 CD and locate the setup files
2. Copy the setup files (WIN95 folder) to your hard drive
3. Rebuild MINI.CAB, replacing VGA.DRV with HERCULES.DRV (a Hercules-compatible display driver for the first-phase GUI setup)
4. Rebuild PRECOPY1.CAB, patching MSDISP.INF to add the "Hercules Graphics Card (ISA)" option to the display driver selection list
5. Deploy the Herc9x driver files (HERCMINI.DRV, HERCMINI.VXD, HERC9X.INF) to the setup directory

Follow the on-screen prompts. When SETUPMOD finishes, it will tell you how to start Windows Setup.


## Step 6: Run Windows 95 Setup

Run SETUP.EXE from the directory SETUPMOD prepared (typically `C:\WIN95\SETUP.EXE`).

**Important:** When prompted for the setup type, choose **Custom**. This allows you to reach the display driver selection screen where you can change the driver from the default VGA to:

> **Herc9x Project** -> **Hercules Graphics Card (ISA)**

If you accept the default (Typical) setup, Windows will install the VGA driver, which won't work on Hercules hardware.


## Step 7: Complete Setup

Proceed through the rest of Windows Setup as usual. The system will reboot one or more times. You should eventually reach the Windows 95 desktop on your Hercules monitor.


## Step 8: Configure Virtual Resolution (Optional)

The native Hercules resolution (720x348) is quite short vertically. Many dialog boxes and wizards won't fit on screen. You can enable a virtual 720x522 mode that provides more vertical workspace by mapping 3 virtual scanlines to every 2 physical scanlines.

Edit `C:\WINDOWS\SYSTEM.INI` and add:

```ini
[display]
x_resolution=720
y_resolution=522
bpp=1
```

See the main [README](README.md#virtual-720x522-mode) for details.


## Troubleshooting

- **SETUPMOD can't find the CD-ROM:** Make sure your CD-ROM driver is loaded (check CONFIG.SYS for MSCDEX and the appropriate device driver). Try different drive letters if needed.

- **"Not enough disk space":** Windows 95 needs at least 100 MB free. SETUPMOD supports a minimal copy mode for tight setups (around 150 MB drives) that skips optional components.

- **Setup hangs or crashes:** If you're using 86Box or other emulators, make sure Hercules emulation is enabled and no VGA card is configured. On real hardware, verify the Hercules card is at the standard I/O and memory addresses (0x3B4-3BF, 0xB0000).

- **Desktop doesn't appear after setup:** Boot to command prompt (press F8 during startup, select "Command prompt only") and verify that `C:\WINDOWS\SYSTEM.INI` contains `display.drv=pnpdrvr.drv` in `[boot]` and `device=hercmini.vxd` in `[386Enh]`. If not, the driver wasn't installed correctly - you may need to add these entries manually (see the main [README](README.md#setup-procedure) for manual installation steps).


## Limitations

- **Windows 95 only.** Windows 98 bare-metal installation is not yet supported - the display selection page in Win98 Setup differs from Win95.
- **Custom setup required.** You must choose Custom setup to access the display driver selection screen.
