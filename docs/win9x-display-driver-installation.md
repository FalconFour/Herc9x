# Windows 9x Display Driver Installation: A Deep Dive

How Windows 95/98 discovers, loads, and persists display drivers — and how to
make a non-PnP display adapter (like Hercules/MDA) play nice in this framework.

Research conducted February 2026 from the Windows 95 DDK (Device Driver
Development Kit), the Windows 98 SE MSDISP.INF, MSDET.INF, DETLOG.TXT, and
live registry analysis.

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [The Boot Sequence](#the-boot-sequence)
3. [The Registry: Two Linked Trees](#the-registry-two-linked-trees)
4. [Enumerators and Device Detection](#enumerators-and-device-detection)
5. [The Display DevLoader Chain](#the-display-devloader-chain)
6. [INF Files for Display Drivers](#inf-files-for-display-drivers)
7. [The Non-PnP Display Problem](#the-non-pnp-display-problem)
8. [Case Study: Hercules on Windows 98](#case-study-hercules-on-windows-98)
9. [Strategies for Non-VGA Display Adapters](#strategies-for-non-vga-display-adapters)
10. [Reference: Registry Key Structure](#reference-registry-key-structure)

---

## Architecture Overview

A Windows 9x display driver consists of two components:

| Component | Ring | File | Role |
|-----------|------|------|------|
| Display minidriver | Ring 3 | `*.drv` | 16-bit DLL; translates GDI calls via DIBENG |
| Virtual Display minidriver (mini-VDD) | Ring 0 | `*.vxd` | 32-bit VxD; hardware virtualization, mode switching |

These are managed by two Microsoft-supplied system components:

- **`*vdd`** (Main VDD, `VDD.VXD` inside `VMM32.VXD`) — the Ring 0 DevLoader
  that loads the mini-VDD, manages VGA virtualization, handles DOS sessions
- **`pnpdrvr.drv`** — the Ring 3 "PnP display driver" stub that reads the
  registry to determine which `.drv` file to actually load

When `SYSTEM.INI` says `display.drv=pnpdrvr.drv`, the system uses the full PnP
chain. When it says `display.drv=mydriver.drv` directly, it bypasses PnP
entirely (the "SYSTEM.INI hack").

### The Full PnP Loading Chain

```
SYSTEM.INI: display.drv=pnpdrvr.drv
    |
    v
pnpdrvr.drv reads HKLM\Enum\<enumerator>\<device>\<instance>
    |                  to find Driver=Display\NNNN
    v
HKLM\System\CurrentControlSet\Services\Class\Display\NNNN
    DevLoader=*vdd
    DEFAULT\drv=sample.drv          <-- Ring 3 driver filename
    DEFAULT\minivdd=sample.vxd      <-- Ring 0 mini-VDD filename
    DEFAULT\vdd=*vdd                <-- written to SYSTEM.INI [386Enh]
    DEFAULT\Mode=8,640,480          <-- default bpp,width,height
    MODES\...                       <-- supported mode tree
    |
    +-- Ring 0: *vdd loads sample.vxd, calls MiniVDD_Dynamic_Init
    +-- Ring 3: pnpdrvr.drv loads sample.drv, GDI calls Enable
```

---

## The Boot Sequence

Windows 9x boot has four phases relevant to display:

### Phase 0: BIOS Initialization
The BIOS configures motherboard devices and runs POST. On PnP BIOS machines,
it assigns resources to PnP ISA cards. On Hercules-class machines with no PnP
BIOS, this phase is minimal.

### Phase 1: Real Mode (CONFIG.SYS / AUTOEXEC.BAT)
No automated device enumeration. Drivers must be explicitly specified.

### Phase 2: Real Mode VxD Loader
The system loads VxDs specified in `SYSTEM.INI`. Key events:
- The PnPBIOS enumerator loads (if a PnP BIOS exists)
- Bus-specific enumerators load (ISA, PCI, etc.)
- The **root enumerator** always loads (it is part of Configuration Manager)
- Static VxDs receive `PNP_NEW_DEVNODE` calls

### Phase 3: Protected Mode Initialization
The full Configuration Manager initializes:
1. Root enumerator creates devnodes from `HKLM\Enum\ROOT\*`
2. Bus enumerators create devnodes from detected hardware
3. `*vdd` calls `CONFIGMG_Register_DevLoader` to register itself
4. Configuration Manager walks the devnode tree, finds display devices with
   `DevLoader=*vdd`, and sends `PNP_NEW_DEVNODE` to `*vdd`
5. `*vdd` reads the `minivdd` registry value and dynamically loads the mini-VDD
6. Mini-VDD's `MiniVDD_Dynamic_Init` runs, detects hardware
7. `*vdd` calls `GET_CHIP_ID` to verify chipset matches registry
8. If verification fails, system falls back to standard VGA

---

## The Registry: Two Linked Trees

Device information lives in two linked registry trees. Understanding this
linkage is essential.

### Tree 1: The ENUM Tree (Hardware)

```
HKLM\Enum\<enumerator>\<device-id>\<instance>
```

This tree records *what hardware exists*. Each enumerator gets its own branch:

| Enumerator | Branch | Source |
|------------|--------|--------|
| Root | `HKLM\Enum\ROOT\*` | Legacy devices from detection or manual install |
| BIOS | `HKLM\Enum\BIOS\*` | PnP BIOS-enumerated motherboard devices |
| PCI | `HKLM\Enum\PCI\*` | PCI bus-enumerated devices |
| ISAPNP | `HKLM\Enum\ISAPNP\*` | ISA Plug and Play cards |
| Monitor | `HKLM\Enum\Monitor\*` | DDC/EDID-detected monitors |

Each device instance has values like:
```
DeviceDesc = "My Display Adapter"
Class = Display
Driver = Display\0000          <-- LINKS to the Class tree
HardwareID = Display_MyCard    <-- matched against INF files
ConfigFlags = 00,00,00,00
Mfg = "My Company"
```

The `Driver` value is the critical link — it points to an entry in the Class tree.

### Tree 2: The Class Tree (Software/Driver)

```
HKLM\System\CurrentControlSet\Services\Class\Display\NNNN
```

This tree records *how to load drivers*. Each entry contains:
```
DevLoader = *vdd               <-- which VxD loads this driver
Ver = 4.0                      <-- driver version (4.0 = Win95+)
DriverDesc = "My Display Adapter"
InfPath = MYCARD.INF           <-- which INF installed this
InfSection = MyCard            <-- which INF section was used
MatchingDeviceId = Display_MyCard

DEFAULT\drv = mycard.drv       <-- Ring 3 display driver
DEFAULT\minivdd = mycard.vxd   <-- Ring 0 mini-VDD
DEFAULT\vdd = *vdd             <-- VDD chain (written to SYSTEM.INI)
DEFAULT\Mode = 8,640,480       <-- default video mode
DEFAULT\RefreshRate = -1        <-- -1 = auto from DISPLAYINFO
DEFAULT\DDC = 1                <-- enable DDC monitor detection

MODES\8\640,480                <-- supported modes tree
MODES\8\800,600                <-- (bpp\width,height)
MODES\16\640,480
...
```

### The Linkage

```
ENUM\ROOT\Display_MyCard\0000        ENUM\PCI\VEN_xxxx&DEV_yyyy\...\0000
    Driver = Display\0000                Driver = Display\0001
         |                                    |
         +-----> Class\Display\0000 <---------+  (can't both be active!)
                 Class\Display\0001
```

**Critical rule**: Windows 9x expects exactly ONE active display device. If
multiple ENUM entries point to different Class\Display entries, the PCI-enumerated
one typically wins (it is discovered by a bus enumerator, which takes priority
over the root enumerator).

---

## Enumerators and Device Detection

### The Root Enumerator

From the DDK (PNP.DOC):

> The root enumerator is part of Configuration Manager. This enumerator
> **contains no special detection logic** and **relies on the registry** to
> determine whether a device exists. If there is an entry in the registry,
> the root enumerator **assumes that it exists** and the appropriate drivers
> are loaded. This is the method by which old hardware is supported, since it
> is usually impossible to determine with complete accuracy and safety that a
> particular ISA card is installed.

This is the mechanism for all legacy (non-PnP) hardware: COM ports, sound cards,
game ports, and display adapters that lack PnP IDs.

Once an entry exists under `HKLM\Enum\ROOT\`, it persists across reboots. The
root enumerator trusts the registry blindly. The driver is responsible for
unloading itself if the hardware is not actually present.

### The BIOS Enumerator

On PnP BIOS machines, the BIOS enumerator (`BIOSENUM`) queries the PnP BIOS API
to discover motherboard devices. These appear under `HKLM\Enum\BIOS\*`. Common
entries include `*PNP0000` (PIC), `*PNP0200` (DMA), `*PNP0100` (Timer), etc.

### The PCI Enumerator

Reads PCI configuration space to discover PCI devices. Creates entries under
`HKLM\Enum\PCI\VEN_xxxx&DEV_yyyy\...`. This is how modern display adapters
(S3, ATI, etc.) are found. The PCI enumerator runs on every boot and always
rediscovers PCI devices — you cannot simply delete a PCI ENUM entry and expect
it to stay deleted.

### Hardware Detection Modules (Setup Time Only)

During Windows Setup (not on regular boots), detection modules listed in
`MSDET.INF` probe for legacy hardware. For display, the detection chain in
Windows 98 SE is:

```
DetectMach64 -> DetectMach32 -> DetectMach8 -> DetectCirrusMMapped ->
... (20+ chipset-specific detectors) ...
-> DetectVGA -> DetectAssumeVGA
```

Each detector calls `QueryIOMem` to check if the display I/O ports (`3B0-3BB`,
`3C0-3DF`) are available and then probes for specific chipset signatures.

**Key behavior**: If another enumerator (like the PCI enumerator) has already
claimed the display I/O ports, all detection modules get `rcQuery=2` ("resources
already claimed") and fail. This means **no legacy display device is detected**
when a PCI display card is present.

If no display is detected at all, `MSDISP.INF` provides a default via
`[SysCfgClasses]`:
```
Display,%*PNP0900.DeviceDesc%,ROOT,,%Display.SetupClassName%
```
This creates a fallback `*PNP0900` (Standard VGA) entry under `ENUM\ROOT`.

---

## The Display DevLoader Chain

### `*vdd` — The Standard Display DevLoader

`*vdd` (the Main VDD) is hardwired for **VGA-compatible** hardware:
- It calls the ROM BIOS on the video card (INT 10h)
- It handles VGA 4-plane virtualization for windowed DOS boxes
- It manages VGA register save/restore for screen switching
- It validates the chipset via `GET_CHIP_ID` against the registry

When `*vdd` loads a mini-VDD, the mini-VDD hooks into the VDD dispatch table
via `VDD_Get_Mini_Dispatch_Table` and `MiniVDDDispatch`. The mini-VDD overrides
only the hardware-specific functions it needs.

### `*configmg` — The Fallback DevLoader

For drivers that do NOT use the main VDD (e.g., Windows 3.1 compatibility mode),
the DevLoader can be set to `*configmg`. From the DDK:

> If your driver does not use the main Windows 95 VDD (for example, if it is a
> Windows 3.1 version driver), then change "*vdd" to "*configmg".

The `UNSUPPORTED` section in MSDISP.INF uses this:
```ini
[UNSUPPORTED]
HKR,,DevLoader,,*configmg
```

This is significant for non-VGA adapters where `*vdd`'s VGA assumptions would
cause failures.

### The Ring 3 Side: `pnpdrvr.drv`

`pnpdrvr.drv` is a stub display driver that:
1. Queries Configuration Manager for the active display devnode
2. Reads the `DEFAULT\drv` value from the Class tree
3. Loads the actual `.drv` file (e.g., `hercmini.drv`)
4. Forwards all GDI calls to it

If `SYSTEM.INI` says `display.drv=hercmini.drv` directly (bypassing
`pnpdrvr.drv`), the PnP chain is not involved at all. The driver loads the old
Windows 3.1 way. This works but:
- Device Manager doesn't know about the display adapter
- The display settings dialog may not work correctly
- Re-running Setup may overwrite the SYSTEM.INI hack

---

## INF Files for Display Drivers

### The Manufacturer Section and Device IDs

```ini
[Mfg]
%NonPnP.DeviceDesc%=Driver.Install, Display_MyCard
%PCI.DeviceDesc%=Driver.Install, PCI\VEN_9999&DEV_9999
```

The first line is for **non-PnP** devices (ISA, VLB, Hercules). `Display_MyCard`
is a "dummy ID" — an arbitrary string that becomes the device's identity in the
ENUM tree. From the DDK:

> Dummy IDs are required for non-Plug and Play devices, so that the driver will
> be preserved if the user runs Windows Setup again in the future. Do not use
> "Display_Sample1", rather invent a dummy ID unique to your company and the
> driver, for example, "Display_ACME-123VLB".

The second line is for PCI devices with real PnP IDs.

### The AddReg Section

```ini
[Driver.AddReg]
HKR,,Ver,,4.0                          ; Win95+ driver
HKR,,DevLoader,,*vdd                   ; DevLoader VxD
HKR,DEFAULT,Mode,,"8,640,480"          ; default video mode
HKR,DEFAULT,drv,,mycard.drv            ; Ring 3 driver
HKR,DEFAULT,vdd,,"*vdd,*vflatd"        ; written to SYSTEM.INI [386Enh]
HKR,DEFAULT,minivdd,,mycard.vxd        ; Ring 0 mini-VDD
HKR,DEFAULT,RefreshRate,,-1            ; auto refresh rate
HKR,DEFAULT,DDC,,1                     ; enable DDC detection
HKR,"MODES\8\640,480"                  ; supported modes
HKR,"MODES\8\800,600"
```

`HKR` means "relative to the device's Class key" — i.e.,
`HKLM\System\CurrentControlSet\Services\Class\Display\NNNN`.

### The DelReg Section (Required)

```ini
[Prev.DelReg]
HKR,,Ver
HKR,,DevLoader
HKR,DEFAULT
HKR,MODES
HKR,CURRENT
```

This cleans up old entries when switching from one driver to another. It should
be included "as-is" in all display driver INFs.

### The LogConfig Section (for Legacy Devices)

Legacy devices should declare their resource usage:

```ini
[Herc.LogConfig]
ConfigPriority=HARDWIRED
IOConfig=3B0-3BF
MemConfig=B0000-BFFFF
```

This tells Configuration Manager what I/O ports and memory the device uses,
preventing conflicts with other devices.

---

## The Non-PnP Display Problem

### The Fundamental Challenge

Windows 9x's display subsystem was designed around VGA-compatible hardware.
Every built-in path assumes:
- VGA I/O ports at `3C0-3DF` respond
- A VGA BIOS exists at `C000:0000`
- INT 10h can set video modes
- VGA registers can be saved/restored for screen switching

For a non-VGA adapter like Hercules/MDA:
- I/O ports are at `3B0-3BF` only (no `3C0-3DF`)
- There is no VGA BIOS (the system BIOS draws text directly)
- INT 10h only supports TTY-style text output
- The hardware uses a completely different register set

### Why "Just Edit the Registry" Doesn't Work

A common attempt is to manually create the ENUM and Class entries. This fails
for several reasons:

1. **PCI enumerator wins**: If a PCI display card exists (even one that's not
   the "real" display), it gets enumerated on every boot and its ENUM entry
   takes priority. The display class expects one active display device.

2. **`*vdd` fails on non-VGA hardware**: The Main VDD tries to do VGA things
   during `MiniVDD_Dynamic_Init` and `GET_CHIP_ID`. On non-VGA hardware, these
   operations fail or produce garbage. The PnP subsystem then reports a display
   settings error and falls back to VGA.

3. **Detection re-runs**: If the user ever re-runs Setup or the system detects
   a configuration change, detection modules run again and may overwrite the
   manual entries.

4. **The display class installer**: `SetupX.Dll, Display_ClassInstaller` has
   special logic for the display class. It may enforce constraints about how
   many display devices can be active or validate chipset information.

---

## Case Study: Hercules on Windows 98

### The Test System

- **Emulator**: 86Box with Pentium Pro
- **Display**: Winbond W86855AF (Hercules-compatible) on ISA
- **Also present**: S3 ViRGE PCI (VEN_5333&DEV_5631) — provides VGA BIOS for
  boot/Setup, but is not the actual display output device

### What the Detection Log Shows

From `DETLOG.TXT`:
```
ConfigMG device: PCI\VEN_5333&DEV_5631&SUBSYS_56315333&REV_00\...
RegAvoidRes: VEN_5333&DEV_5631&...\0000
    IO=3b0-3bb(3ff:400:0),3c0-3df(3ff:400:0)
```

The PCI enumerator finds the S3 ViRGE and claims **all** display I/O ports.
Then every display detection module fails with `rcQuery=2`:

```
Checking for: Standard VGA Display Adapter
QueryIOMem: Caller=DETECTVGA, rcQuery=2     <-- BLOCKED
Checking for: Default Standard VGA Display Adapter
QueryIOMem: Caller=DETECTASSUMEVGA, rcQuery=2   <-- BLOCKED
```

Result: **zero display devices detected by detection modules.**

### The Current Registry State

**ENUM tree** — Three competing display entries:

| ENUM Path | Device | Driver Link | How Created |
|-----------|--------|-------------|-------------|
| `ENUM\PCI\VEN_5333&DEV_5631\...\0000` | S3 ViRGE | `Display\0000` | PCI enumerator |
| `ENUM\PCI\VEN_5333&DEV_88F0\...\0000` | S3 Trio (prev.) | `Display\0001` | PCI enumerator |
| `ENUM\Root\Display\0000` | Hercules (ISA) | `Display\0006` | Manual install |

**Class\Display tree** — Seven entries accumulated:

| Class Entry | Driver | InfSection | MatchingDeviceId |
|-------------|--------|------------|-----------------|
| `Display\0000` | hercmini.drv | Herc | `*PNP0900` |
| `Display\0001` | vesamini.drv | VESA | `PCI\CC_0300` |
| `Display\0002` | vesamini.drv | VESA | `PCI\CC_0300` |
| `Display\0003` | vesamini.drv | VESA | `PCI\CC_0300` |
| `Display\0004` | vesamini.drv | VESA | `PCI\CC_0300` |
| `Display\0005` | vga.drv | VGA | `*PNP0900` |
| `Display\0006` | hercmini.drv | Herc | (none) |

**The S3 ViRGE PCI entry** (`Display\0000`) points to `hercmini.drv` with
`MatchingDeviceId=*PNP0900` and has leftover `INFO` values from the S3
(`ChipType=ViRGE`, `VideoMemory=4MB`). This is a Frankenstein entry — the PCI
hardware's devnode was manually re-pointed to Hercules driver data.

**The root-enumerated Hercules** (`Display\0006`) is a clean entry pointing to
hercmini.drv, but it's the *PCI* entry (`Display\0000`) that's actually active
because the PCI enumerator runs first and the S3's devnode has `Driver=Display\0000`.

### Why It Currently Works (With the SYSTEM.INI Hack)

```ini
; SYSTEM.INI [boot]
display.drv=hercmini.drv    ; loaded directly, bypasses PnP
;display.drv=pnpdrvr.drv    ; commented out
```

This completely bypasses `pnpdrvr.drv`, `*vdd`, Configuration Manager, and the
entire PnP display chain. `hercmini.drv` loads the old Windows 3.x way.
`hercmini.vxd` is loaded via a separate SYSTEM.INI `[386Enh]` entry or via the
VxD's own initialization.

---

## Strategies for Non-VGA Display Adapters

### Strategy 1: The SYSTEM.INI Approach (Current — Works But Fragile)

Set `display.drv=hercmini.drv` directly in SYSTEM.INI. Load the VxD via
`[386Enh]` or static VxD loading.

**Pros**: Works reliably. Simple.
**Cons**: Bypasses PnP entirely. Device Manager doesn't know about the display.
Display properties dialog may not work. Re-running Setup can break it.

### Strategy 2: `DevLoader=*configmg` (Promising — Needs Testing)

Use `*configmg` instead of `*vdd` as the DevLoader. This avoids the VGA-centric
Main VDD entirely while still participating in the PnP framework.

```ini
[Herc.AddReg]
HKR,,Ver,,4.0
HKR,,DevLoader,,*configmg          ; bypass *vdd entirely
HKR,DEFAULT,drv,,hercmini.drv
HKR,DEFAULT,Mode,,"1,720,348"
; No minivdd line — *configmg doesn't load mini-VDDs
; The VxD must be loaded via StaticVxD= or [386Enh]
```

This is how Windows 3.1 compatibility drivers work under Win95. The driver
appears in Device Manager, uses the PnP framework for persistence, but the
actual hardware management is handled the old way.

**Open question**: Does `pnpdrvr.drv` work with `DevLoader=*configmg`? The
`DEFAULT\drv` value should still be readable by `pnpdrvr.drv` regardless of the
DevLoader.

### Strategy 3: Disable the PCI Display + Root Enumerate

Disable the S3 PCI devnode (`ConfigFlags` bit 0 = disabled) so it's not the
active display device. Then the root-enumerated Hercules entry can win.

This requires:
1. Setting `ConfigFlags=hex:01,00,00,00` on the S3 PCI ENUM entry
2. Ensuring `ENUM\Root\Display_Hercules\0000` exists with proper values
3. Using `DevLoader=*vdd` or `*configmg` as appropriate

**Risk**: The PCI enumerator still creates the devnode on every boot. If
`ConfigFlags` is not respected for display devices, the S3 entry may still
compete.

### Strategy 4: Custom Setup During Windows Installation

Modify the Windows Setup process to install the Hercules driver from the start:
1. Add `hercmini.drv` and `hercmini.vxd` to the Setup source files
2. Create a custom INF that matches a Hercules-specific device ID
3. Use an `MSBATCH.INF` answer file to pre-select the display driver

This is the cleanest approach but requires the most Setup integration work.

---

## Reference: Registry Key Structure

### ENUM Entry Values

| Value | Type | Description |
|-------|------|-------------|
| `DeviceDesc` | REG_SZ | Human-readable device name |
| `Class` | REG_SZ | Device class (e.g., "Display") |
| `ClassGUID` | REG_SZ | `{4d36e968-e325-11ce-bfc1-08002be10318}` for Display |
| `Driver` | REG_SZ | Points to `Class\Display\NNNN` |
| `HardwareID` | REG_SZ | Comma-separated list of IDs for INF matching |
| `CompatibleIDs` | REG_SZ | Fallback IDs for INF matching |
| `ConfigFlags` | REG_BINARY | Bit 0: disabled, Bit 1: removed |
| `Mfg` | REG_SZ | Manufacturer name |
| `BootConfig` | REG_BINARY | Binary resource data (I/O, IRQ, MEM) |

### Class\Display Entry Values

| Value | Type | Description |
|-------|------|-------------|
| `DevLoader` | REG_SZ | `*vdd` or `*configmg` |
| `Ver` | REG_SZ | `4.0` for Win95+ drivers |
| `DriverDesc` | REG_SZ | Display name |
| `InfPath` | REG_SZ | Source INF filename |
| `InfSection` | REG_SZ | Source INF section |
| `MatchingDeviceId` | REG_SZ | Device ID that matched during install |

### Class\Display\NNNN\DEFAULT Subkey

| Value | Type | Description |
|-------|------|-------------|
| `drv` | REG_SZ | Ring 3 display driver filename |
| `minivdd` | REG_SZ | Ring 0 mini-VDD filename |
| `vdd` | REG_SZ | VDD chain (written to SYSTEM.INI [386Enh]) |
| `Mode` | REG_SZ | Default mode: `bpp,width,height` |
| `RefreshRate` | REG_SZ | `-1` = auto, `0` = default, positive = override |
| `DDC` | REG_SZ | `1` = enable VESA DDC monitor detection |
| `ExtModeSwitch` | REG_SZ | `0` = disable extended mode switching |
| `cardvdd` | REG_SZ | Optional secondary mini-VDD |

### Class\Display\NNNN\MODES Subkey

```
MODES\<bpp>\<width>,<height>
    drv = override_driver.drv    (optional — overrides DEFAULT\drv)
    vdd = override_vdd           (optional — overrides DEFAULT\vdd)
```

Standard VGA modes should always be present:
```
MODES\4\640,480     drv=vga.drv, vdd=*vdd    (required by all drivers)
```

### Display Class Installer

```
HKLM\System\CurrentControlSet\Services\Class\Display
    @="Display adapters"
    Installer="SetupX.Dll, Display_ClassInstaller"
    Icon="-1"
    EnumDriverStack="enumfile.dll,EnumDisplayDriverStack"
```

The display class uses `SetupX.Dll` (not a dedicated class installer DLL).
The `EnumDriverStack` value uses `enumfile.dll` to enumerate display driver
stacks for the Settings dialog.

---

## Sources

- Windows 95 DDK, `DOCS/PNP.DOC` — Plug and Play Driver documentation
- Windows 95 DDK, `DOCS/DISPLAY.DOC` — Display Driver documentation
- Windows 95 DDK, `DISPLAY/SAMPLES/SAMPLE.INF` — Sample display INF
- Windows 95 DDK, `PLUGPLAY/SAMPLES/EXAMENUM/` — Example enumerator (derived
  from DISPENUM, the internal display/monitor enumerator)
- Windows 98 SE `MSDISP.INF` — Display driver INF
- Windows 98 SE `MSDET.INF` — Hardware detection module list
- Windows 98 SE `DETLOG.TXT` — Hardware detection log
- Live registry analysis of a working Hercules/Win98 system
