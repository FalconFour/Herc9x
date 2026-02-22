HERC9X - BARE-METAL WINDOWS 95 INSTALLATION
=============================================

This folder contains the files needed to install Windows 95 on a
system with only a Hercules graphics card (no VGA).

SETUPMOD.EXE pre-integrates the Herc9x display driver into the
Windows 95 setup files so you can select "Hercules Graphics Card
(ISA)" during installation instead of VGA.


FILES
-----
  SETUPMOD.EXE  - Setup modifier utility (run this)
  HERCULES.DRV  - Windows 3.1 Hercules driver (used during setup)
  HERCMINI.DRV  - Herc9x display driver
  HERCMINI.VXD  - Herc9x VxD (Ring 0 driver)
  HERC9X.INF    - Device installation INF file


QUICK START
-----------
1. Boot from a DOS floppy with CD-ROM support.

2. Partition and format your hard drive (FDISK, FORMAT C: /S).

3. Copy this folder to the hard drive:
     C:
     MKDIR \W9XHERC
     CD \W9XHERC
     COPY A:\W9XHERC\*.* .

4. Insert the Windows 95 CD-ROM.

5. Run SETUPMOD.EXE and follow the prompts.

6. Run Windows Setup. Choose CUSTOM setup when prompted.

7. At the display driver selection, choose:
     Herc9x Project -> Hercules Graphics Card (ISA)

8. Complete setup as usual. You should reach the desktop.


AFTER INSTALLATION
------------------
To get more vertical screen space, edit C:\WINDOWS\SYSTEM.INI and
add these lines:

  [display]
  x_resolution=720
  y_resolution=522
  bpp=1

This enables a virtual 720x522 mode (the native resolution is
720x348).


TIPS
----
- Load XMSMMGR and SMARTDRV on your boot floppy to speed up the
  file copy process significantly.

- Windows 95 needs at least 100 MB of free disk space.

- Windows 98 is not yet supported for bare-metal installation.

For full documentation, visit: https://github.com/FalconFour/Herc9x
