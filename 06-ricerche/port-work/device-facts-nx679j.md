# NX679J (RedMagic 7 / SM8450 / Waipio) — Phase 3 Device Facts

Date: 2026-07-10  
Status: facts only — no re-flash until next controlled `boot_b` test  
Baseline: phone restored to known-good Magisk/Android on slot B after first UEFI handoff → `05c6:900e`

## 1. Identity / stock baseline

| Item | Value |
|------|--------|
| Product | NX679J / NX679J-UN (REDMAGIC 7) |
| SoC | SM8450 (Waipio / taro) |
| Platform prop | `ro.board.platform=taro`, `ro.soc.model=SM8450` |
| Stock build | `SKQ1.211113.001` / `NX679J_UNCommon_V3.11` / internal `NX679J_Z69_UN_ZML0S_V311` |
| Kernel | `5.10.66-android12-9-…` |
| Serial | `3dbd****` (also USB iSerial on 900e path) |
| Storage | UFS `soc/1d84000.ufshc` (`ro.boot.bootdevice` / `boot_devices`) |
| USB controller | `a600000.dwc3` |
| AVB | unlocked / orange (`ro.boot.flash.locked=0`, `verifiedbootstate=orange`) |
| A/B | yes (`ro.build.ab_update=true`); dynamic partitions |
| Active slot observed in dumps | `_a` at capture time; **test/restore target is slot B** |
| Phase0 backup dir | `/home/user/nx679j-stock/phase0-backups/2026-07-02-slotb-preproto/` |

Phase0 slot-B images (hash-checked): `boot_b`, `vendor_boot_b`, `dtbo_b`, `vbmeta_b`, `vbmeta_system_b`, `recovery_b`.

Note: `restore_slot_b.sh` currently runs `fastboot set_active a` — for slot-B restore use `set_active b` (as done in recovery).

## 2. Boot chain layout (Option A constraints)

### 2.1 Stock firmware partitions (from `full_extracted_v311/`)

Early boot / TEE (do **not** flash in Phase 3–4):

- `xbl.img`, `xbl_config.img`, `xbl_ramdump.img`
- `abl.img` — **stock ABL kept; no `abl_b` flash until proven necessary**
- `aop.img`, `aop_config.img`, `tz.img`, `hyp.img`
- `devcfg.img`, `qupfw.img`, `shrm.img`, `cpucp.img`, `keymaster.img`
- `uefi.img`, `uefisecapp.img`, `imagefv.img`, `multiimgqti.img`, `multiimgoem.img`
- `modem.img`, `bluetooth.img`, `dsp.img`, `featenabler.img`, `qweslicstore.img`

Android boot / HLOS:

- `boot.img` / slot `boot_a|boot_b`
- `vendor_boot.img` / `vendor_boot_a|b`
- `dtbo.img` / `dtbo_a|b`
- `recovery.img` / `recovery_a|b`
- `vbmeta.img`, `vbmeta_system.img` (+ slot variants)
- Super/dynamic: `system`, `system_ext`, `product`, `vendor`, `odm`, `vendor_dlkm`

### 2.2 Intended custom path (mu_aloha Option A)

```
PBL → XBL → stock ABL → load Android boot.img from active slot
     → BootShim + gzip(SM8450_EFI.fd) as "kernel" payload
     → mu_aloha UEFI (LinuxLoader / BDS menu)
```

First handoff result:

- Flashing custom UEFI only to `boot_b` → left ADB/fastboot → USB `05c6:900e` (QUSB_BULK).
- Interpretation: **stock ABL accepted and started the payload; payload failed early** (no menu/display/serial proof yet).
- Rollback: restore phase0 slot-B images + `set_active b` → Android/ADB OK. ABL/XBL/TZ untouched.

### 2.3 Custom artifact packaging

Source: `Platforms/WaipioPkg/PythonLibs/PostBuild.py`

- Payload = `BootShim.bin` || `SM8450_EFI.fd`, then **gzip -9**
- Packed as Android **boot header v3**, pagesize 4096, base `0x0`, empty cmdline, **no ramdisk**
- Output images under `port-work/nx679j-boot-assets/uefi/`:
  - `nubia-nx679j-uefi-debug-boot.img`
  - `nubia-nx679j-uefi-debug-menu-boot.img`
  - `nubia-nx679j-SM8450_EFI-debug*.fd`

Stock Android boot layout (for comparison; vendor_boot v4):

```
--header_version 4 --pagesize 0x1000 --base 0x0
--kernel_offset 0x8000 --ramdisk_offset 0x01000000
--tags_offset 0x100 --dtb_offset 0x1f00000
vendor_cmdline: video=vfb:640x400,bpp=32,memsize=3072000
  qcom-dload-mode.download_mode=1 bootconfig
```

Stock kernel cmdline highlights (from live logcat):

- console: `ttyMSM0,115200n8`
- panel: `msm_drm.dsi_display0=qcom,mdss_dsi_r6130_1080_2400_amoled_cmd:`
- dload: `qcom-dload-mode.download_mode=1`
- Machine model string in FDT: `Qualcomm Technologies, Inc. Waipio MTP with PM8010`
- DTB index at boot: `ro.boot.dtb_idx=5`, `ro.boot.dtbo_idx=35`

## 3. Memory map (UEFI / mu_aloha NX679J)

Primary sources (identical map to qcom-hdk8450 Waipio reference):

- `Device/nubia-nx679j/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.c`
- `Device/nubia-nx679j/Binaries/RawFiles/uefiplat.cfg`
- Build PCDs: `nubia-nx679j-build-report-debug.txt`

### 3.1 System / FD PCDs

| PCD | Value |
|-----|--------|
| `PcdSystemMemoryBase` | `0x80000000` |
| `PcdSystemMemorySize` | `0x300000000` (12 GiB window as coded) |
| `PcdFdBaseAddress` / FDF base | **`0xFFC00000`** |
| `PcdFdSize` / `PcdFvSize` | `0x00400000` (4 MiB) |
| `PcdCpuVectorBaseAddress` | `0xA7600000` |
| GIC distributor | `0x17100000` |
| GIC redistributors | `0x17180000` |
| UART serial base | `0x99c000` |
| Display FB PCD | 1080×2400, 32 bpp |
| Cores | 8 active / MaxCoreCnt 8 / EarlyInitCoreCnt 2 |

### 3.2 Dual FD address (critical for handoff)

| Role | Address | Size | Notes |
|------|---------|------|--------|
| FDF / link-time FD base | `0xFFC00000` | 4 MiB | `Waipio.fdf` `BaseAddress`; map entry `"UEFI FD"` |
| Runtime FD reserve (stock ABL / BootShim path) | `0xA7000000` | 4 MiB | `"FD Reserved I"` / uefiplat `"UEFI FD"` |
| FD reserve II | `0xA7400000` | 2 MiB | vectors/heaps follow |
| Kernel carveout | `0xA8000000` | 256 MiB | HLOS kernel load region |

PrePi looks up map area name **`"UEFI FD"`**. In `PlatformMemoryMapLib.c` that name is at **`0xFFC00000`**, while **`0xA7000000` is labeled `"FD Reserved I"`**. In `uefiplat.cfg` the `0xA7000000` row is labeled `"UEFI FD"`. BootShim is expected to relocate/decompress into the ABL-provided region near `0xA7000000`. First 900e is consistent with **early payload failure** (wrong place, decompression, MMU, or assert) after ABL jumped into the boot.img kernel slot.

### 3.3 High-value DDR regions (from PlatformMemoryMapLib)

| Name | Base | Size |
|------|------|------|
| HYP | `0x80000000` | 6 MiB (unusable DDR at beginning per uefiplat) |
| DT BLOB | `0x80600000` | 256 KiB |
| XBL RAMDUMP | `0x80640000` | 1.75 MiB |
| AOP / AOP CMD DB | `0x80800000` | 0.6+0.4 MiB |
| SMEM | `0x80900000` | 2 MiB |
| CUCP | `0x80B00000` | 1 MiB |
| UEFI Stack (early) | `0x80C00000` | 4 MiB |
| DXE Heap (map) | `0x81000000` | 66 MiB *(uefiplat also lists DXE Heap at `0xF8000000` — keep both in mind)* |
| PIL Reserved | `0x85200000` | ~450 MiB |
| Display Demura | `0xA1400000` | 43 MiB |
| DBI Dump | `0xA6000000` | 15 MiB |
| FD Reserved I / runtime FD | `0xA7000000` | 4 MiB |
| FD Reserved II | `0xA7400000` | 2 MiB |
| CPU Vectors | `0xA7600000` | 4 KiB |
| Info Blk / MMU / Log / stacks / heaps / FV | `0xA7601000` … `0xA7FFC000` | (through UEFI RESV) |
| Kernel | `0xA8000000` | 256 MiB |
| Display Reserved | `0xB8000000` | 43 MiB |
| OEM VM | `0xBB000000` | 80 MiB |
| MTE Reserved | `0xC0000000` | 512 MiB |
| TZ STATS / TZApps | `0xE8800000` / `0xEA000000` | 16 / 116 MiB |
| UEFI FD (link) | `0xFFC00000` | 4 MiB |
| HYP Reserved (high) | `0x830000000` | 256 MiB |

### 3.4 IMEM / cookies / dump

| Item | Address | Notes |
|------|---------|--------|
| IMEM Base | `0x14680000` | 0x2A000 |
| Shared IMEM / Cookie Base | `0x146AA000` | `SharedIMEMBaseAddr` |
| PIL dbg cookie | `0x146AA6DC` = `0x53444247` (`SDBG`) |
| DloadCookieAddr | `0x01FD3000` | TCSR cookie |
| DloadCookieValue | `0x10` | full dump (`0x20` = minidump) |

Stock kernel also enables `qcom-dload-mode.download_mode=1` → early crash often surfaces as **900e** Sahara dump mode (as observed).

### 3.5 Stock kernel reserved/CMA (from logcat; cross-check only)

Live Android (not UEFI) early memory nodes span `0x80000000`–`0xbffffffff` with CMA pools including:

- non_secure_display @ `0xd5c00000` (164 MiB)
- audio_cma @ `0xfe000000` (28 MiB)
- ramoops @ `0xbfc600000` size `0x200000`

Total RAM reported ~18.5 GiB physical available in that boot — map coded as 12 GiB UEFI window + high HYP region; do not assume map covers all DRAM without dynamic partitions.

## 4. DTB / display / panel

### 4.1 Stock DTBs

- Split stock DTB pack: `port-work/vboot-dtb-swap/stock_dtbs/dtb_00.dtb` … `dtb_08.dtb`
- Boot used index **5** (`ro.boot.dtb_idx=5`), DTBO index **35**
- Combined vendor_boot DTB blob also under `vboot-dtb-swap/unpack*/dtb`

### 4.2 mu_aloha device DTBs

- `Device/nubia-nx679j/DeviceTreeBlob/Android/android-nx679j.dtb`
- `Device/nubia-nx679j/DeviceTreeBlob/Linux/linux-nx679j.dtb`

(Copied/skeleton; not yet proven on hardware.)

### 4.3 Panel / framebuffer

| Source | Value |
|--------|--------|
| Stock kernel panel | `qcom,mdss_dsi_r6130_1080_2400_amoled_cmd` |
| UEFI PCDs | 1080×2400, 32 bpp |
| Display Reserved | `0xB8000000` size `0x02B00000` |
| Display Demura | `0xA1400000` size `0x02B00000` |
| Config | `EnableDisplayImageFv=1`, `EnableDisplayThread=0` (DeviceConfigurationMap) |
| Panel XML set | many BOE/R66451/NT* under `Binaries/RawFiles/` — **stock panel is R6130 AMOLED; likely needs panel binding work** before menu is visible |

### 4.4 BDS / menu

- `DefaultBDSBootApp = LinuxLoader` (`uefiplat.cfg`)
- `EnableShell = 0x1`
- Retail hotkey detection **disabled** (`DetectRetailUserAttentionHotkey = 0x00`); code `0x17` = SCAN_ESC if enabled
- `BDS_Menu.cfg`: Exit, Secure Boot toggle, Debug Policy, Shell, Boot USB First, MassStorage, Reboot, Shutdown, CLOCK/USB/PMIC/EUD menus, **EDL Mode**, UEFI Menu

Visibility without serial: depend on DisplayDxe + correct panel; first success criterion remains **any** UEFI menu / LinuxLoader / USB gadget / non-900e life sign.

## 5. Platform config map (NX679J)

From `DeviceConfigurationMap.h` / `PlatformConfigurationMapLib.c` (key entries):

- `DloadCookieAddr=0x01FD3000`, `DloadCookieValue=0x10`
- `SharedIMEMBaseAddr=0x146AA000`
- `SecurityFlag=0xC4`
- `EnableUfsIOC=1`, `UfsSmmuConfigForOtherBootDev=1`
- `EnableShell=1`
- `EnableMultiThreading=0`, `EnableMultiCoreFvDecompression=0` (safer single-thread early bring-up)
- `PcdABLProduct="waipio"`

## 6. Stock vs port tree paths

| Purpose | Path |
|---------|------|
| Port tree | `/home/user/nx679j-stock/port-work/mu_aloha_platforms` |
| Device dir | `…/Platforms/WaipioPkg/Device/nubia-nx679j` |
| UEFI artifacts | `/home/user/nx679j-stock/port-work/nx679j-boot-assets/uefi/` |
| Phase0 backups | `/home/user/nx679j-stock/phase0-backups/2026-07-02-slotb-preproto/` |
| Full extract | `/home/user/nx679j-stock/full_extracted_v311/` |
| Stock DTBs | `/home/user/nx679j-stock/port-work/vboot-dtb-swap/stock_dtbs/` |
| Magisk baseline img | `/home/user/nx679j-stock/magisk_patched-30700_Zx2eF.img` |
| edl / qdl | `/tmp/edl`, `/tmp/qdl-src/build/qdl` |
| Dump workdir (real disk) | `/home/user/nx679j-stock/port-work/ramdump-900e-minimal` — **never dump full RAM to /tmp** |

## 7. Implications for next work (Phase 4)

1. **Keep flashing only `boot_b`.** Never `abl_b` until stock-ABL-loaded UEFI is proven impossible.
2. **900e means ABL handoff happened.** BootShim **intentionally** relocates FD to `0xFFC00000` (not a bug vs `0xA7000000` reserve). Focus on early SEC/PrePi death + post-mortem logs.
3. **Phase 4 change**: `USE_MEMORY_FOR_SERIAL_OUTPUT=1` + PStore @ `0x800000000` (4 MiB) for in-RAM DEBUG. See `port-work/phase4-minimal-payload.md`.
4. **Display is unproven** for NX679J panel (`r6130`); treat menu visibility as stretch.
5. **DTB**: stock uses idx 5; port embeds android/linux nx679j DTBs — verify content vs `dtb_05` before expecting LinuxLoader → Linux.
6. **Recovery is proven**: phase0 flash + `set_active b` + reboot.
7. **Fix later**: `restore_slot_b.sh` `set_active a` bug; dual DXE heap locations in map vs uefiplat.

## 8. Safe next test (not executed yet)

Only after reviewing this facts file:

1. Confirm ADB on known-good Android, slot B active or set active B.
2. Optional: rebuild DEBUG with maximum early serial/heartbeat if available.
3. `fastboot flash boot_b <uefi-debug-or-menu-boot.img>` only.
4. Observe: black screen vs menu vs 900e vs fastboot.
5. On 900e: prefer key combo to fastboot; if using edl, **filtered** dump on real disk only, then restore phase0.

---

Sources: NX679J PlatformMemoryMapLib, uefiplat.cfg, DeviceConfigurationMap, PcdsFixedAtBuild, Waipio.fdf, PostBuild.py, stock getprop/logcat dumps, mkbootimg_args, phase0 SHA256SUMS, full_extracted partition list, first handoff notes (conversation).
