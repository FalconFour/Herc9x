INCS = -I$(%WATCOM)\h\win -Iddk

VER_BUILD = 120

FLAGS = -DDRV_VER_BUILD=$(VER_BUILD)

# Fixer utility for NE/VXD output
FIXLINK_EXE = fixlink.exe
FIXLINK_CC  = wcl386 -q fixlink\fixlink.c -fe=$(FIXLINK_EXE)

# Debug logging: run "wmake DBGPRINT=1" to enable, or plain "wmake" for release.
# DBGPRINT = 1

!ifdef DBGPRINT
FLAGS += -DDBGPRINT
DBGFILE = file dbgprint.obj
DBGFILE32 = file dbgprint32.obj
!else
DBGFILE =
DBGFILE32 =
!endif

CFLAGS = -q -wx -s -zu -zls
CFLAGS32 = -q -wx -s -zls -mf -DVXD32 -fpi87 -ei -oeatxhn
CC = wcc
CC32 = wcc386

CFLAGS   += -6 -fp6
CFLAGS32 += -6s -fp6

!ifdef DBGPRINT
CFLAGS   += -DCOM2
CFLAGS32 += -DCOM1
!else
CFLAGS32 += -d0
!endif

HERCMINI_DRV_RC = wrc -q hercmini.res $@ && .\$(FIXLINK_EXE) -40 $@

all : hercmini.drv hercmini.vxd bsplash.exe

# Object files: PM16 RING-3
dbgprint.obj : dbgprint.c .autodepend
	$(CC) $(CFLAGS) -zW $(FLAGS) $<

dibcall.obj : dibcall.c .autodepend
	$(CC) $(CFLAGS) -zW $(INCS) $(FLAGS) $<

dibthunk.obj : dibthunk.asm
	wasm -q $(FLAGS) $<

dddrv.obj : dddrv.c .autodepend
	$(CC) $(CFLAGS) -zW $(INCS) $(FLAGS) $<

drvlib.obj: drvlib.c .autodepend
	$(CC) $(CFLAGS) -zW $(INCS) $(FLAGS) $<

enable.obj : enable.c .autodepend
	$(CC) $(CFLAGS) -zW $(INCS) $(FLAGS) $<

init.obj : init.c .autodepend
	$(CC) $(CFLAGS) -zW $(INCS) $(FLAGS) $<

control.obj : control.c .autodepend
	$(CC) $(CFLAGS) -zW $(INCS) $(FLAGS) $<

pm16_calls_herc.obj : pm16_calls_herc.c .autodepend
	$(CC) $(CFLAGS) -zW $(INCS) $(FLAGS) $<

palette.obj : palette.c .autodepend
	$(CC) $(CFLAGS) -zW $(INCS) $(FLAGS) $<

sswhook.obj : sswhook.asm
	wasm -q $(FLAGS) $<

modes_herc.obj : modes_herc.c .autodepend
	$(CC) $(CFLAGS) -zW $(INCS) $(FLAGS) $<

scrsw_herc.obj : scrsw_herc.c .autodepend
	$(CC) $(CFLAGS) -zW $(INCS) $(FLAGS) $<

# Boot splash utility (DOS real-mode)
bsplash.obj : bsplash.c .autodepend
	$(CC) -q -wx -s -ms -6 -fp6 $<

bsplash.exe : bsplash.obj
	wlink op quiet system dos name bsplash.exe file bsplash.obj

# Object files: PM32 RING-0

dbgprint32.obj : dbgprint32.c .autodepend
	$(CC32) $(CFLAGS32) $(FLAGS) $<

vxd_fbhda.obj : vxd_fbhda.c .autodepend
	$(CC32) $(CFLAGS32) $(INCS) $(FLAGS) $<

vxd_lib.obj : vxd_lib.c .autodepend
	$(CC32) $(CFLAGS32) $(INCS) $(FLAGS) $<

vxd_main_herc.obj : vxd_main_herc.c .autodepend
	$(CC32) $(CFLAGS32) $(INCS) $(FLAGS) $<

vxd_herc.obj : vxd_herc.c .autodepend
	$(CC32) $(CFLAGS32) $(INCS) $(FLAGS) $<

vxd_vdd_herc.obj : vxd_vdd_herc.c .autodepend
	$(CC32) $(CFLAGS32) $(INCS) $(FLAGS) $<

vxd_mouse.obj : vxd_mouse.c .autodepend
	$(CC32) $(CFLAGS32) $(INCS) $(FLAGS) $<

vxd_halloc.obj : vxd_halloc.c .autodepend
	$(CC32) $(CFLAGS32) $(INCS) $(FLAGS) $<

vxd_wram.obj : vxd_wram.c .autodepend
	$(CC32) $(CFLAGS32) $(INCS) $(FLAGS) $<

vxd_async.obj : vxd_async.c .autodepend
	$(CC32) $(CFLAGS32) $(INCS) $(FLAGS) $<

# Resources
hercmini.res : res/hercmini.rc res/colortab.bin res/config.bin res/fonts.bin res/fonts120.bin .autodepend
	wrc -q -r -ad -bt=windows -fo=$@ -Ires -I$(%WATCOM)/h/win $(FLAGS) res/hercmini.rc

res/colortab.bin : res/colortab.c
	wcc -q $(INCS) $<
	wlink op quiet disable 1014, 1023 name $@ sys dos output raw file colortab.obj

res/config.bin : res/config.c
	wcc -q $(INCS) $<
	wlink op quiet disable 1014, 1023 name $@ sys dos output raw file config.obj

res/fonts.bin : res/fonts.c .autodepend
	wcc -q $(INCS) $<
	wlink op quiet disable 1014, 1023 name $@ sys dos output raw file fonts.obj

res/fonts120.bin : res/fonts120.c .autodepend
	wcc -q $(INCS) $<
	wlink op quiet disable 1014, 1023 name $@ sys dos output raw file fonts120.obj

# Libraries
dibeng.lib : ddk/dibeng.lbc
	wlib -b -q -n -fo -ii @$< $@

# Fixer
$(FIXLINK_EXE): drvfix.c
	$(FIXLINK_CC)

hercmini.drv : hercmini.res dibeng.lib $(FIXLINK_EXE) dbgprint.obj &
               dibcall.obj dibthunk.obj dddrv.obj drvlib.obj enable.obj &
               init.obj control.obj pm16_calls_herc.obj palette.obj &
               sswhook.obj modes_herc.obj scrsw_herc.obj
	wlink op quiet, start=DriverInit_ disable 2055 $(DBGFILE) @<<hercmini.lnk
system windows dll initglobal
file dibcall.obj
file dibthunk.obj
file dddrv.obj
file drvlib.obj
file enable.obj
file init.obj
file control.obj
file pm16_calls_herc.obj
file palette.obj
file sswhook.obj
file modes_herc.obj
file scrsw_herc.obj
name hercmini.drv
option map=hercmini.map
library dibeng.lib
library clibs.lib
option modname=DISPLAY
option description 'DISPLAY : 100, 96, 96 : DIB Engine based Mini display driver.'
option oneautodata
segment type data preload fixed
segment '_TEXT'  preload shared
segment '_INIT'  preload moveable
export BitBlt.1
export ColorInfo.2
export Control.3
export Disable.4
export Enable.5
export EnumDFonts.6
export EnumObj.7
export Output.8
export Pixel.9
export RealizeObject.10
export StrBlt.11
export ScanLR.12
export DeviceMode.13
export ExtTextOut.14
export GetCharWidth.15
export DeviceBitmap.16
export FastBorder.17
export SetAttribute.18
export DibBlt.19
export CreateDIBitmap.20
export DibToDevice.21
export SetPalette.22
export GetPalette.23
export SetPaletteTranslate.24
export GetPaletteTranslate.25
export UpdateColors.26
export StretchBlt.27
export StretchDIBits.28
export SelectBitmap.29
export BitmapBits.30
export ReEnable.31
export DDIGammaRamp.32
export Inquire.101
export SetCursor.102
export MoveCursor.103
export CheckCursor.104
export GetDriverResourceID.450
export UserRepaintDisable.500
export ValidateMode.700
import GlobalSmartPageLock  KERNEL.230
<<
	$(HERCMINI_DRV_RC)

hercmini.vxd : $(FIXLINK_EXE) dbgprint32.obj &
               vxd_main_herc.obj vxd_fbhda.obj vxd_lib.obj vxd_herc.obj &
               vxd_vdd_herc.obj vxd_mouse.obj vxd_halloc.obj &
               vxd_wram.obj vxd_async.obj
	wlink op quiet $(DBGFILE32) @<<hercmini.lnk
system win_vxd dynamic
option map=hercmini_vxd.map
option nodefaultlibs
name hercmini.vxd
file vxd_main_herc.obj
file vxd_fbhda.obj
file vxd_lib.obj
file vxd_herc.obj
file vxd_vdd_herc.obj
file vxd_mouse.obj
file vxd_halloc.obj
file vxd_wram.obj
file vxd_async.obj
segment '_TEXT'  PRELOAD NONDISCARDABLE
segment '_DATA'  PRELOAD NONDISCARDABLE
segment 'CONST'  PRELOAD NONDISCARDABLE
segment 'CONST2' PRELOAD NONDISCARDABLE
segment '_BSS'   PRELOAD NONDISCARDABLE
export VXD_DDB.1
<<
	.\$(FIXLINK_EXE) -vxd32 $@

# Cleanup
clean : .symbolic
    rm *.obj
    rm *.err
    rm *.lib
    rm *.drv
    rm *.vxd
    rm *.map
    rm *.res
    rm res/*.obj
    rm res/*.bin
    -rm -f bsplash.exe
    -rm -f $(FIXLINK_EXE)
