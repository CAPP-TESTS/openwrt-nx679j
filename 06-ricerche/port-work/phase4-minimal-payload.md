# NX679J Phase 4 — minimal payload / debug path

Date: 2026-07-10  
Status: packaging + FD-base + BootShim text_offset work done. **Root cause written in `ROOT-CAUSE-900e.md`.** Stop blind flashing; next is DualBoot hybrid (stock kernel + FD). Phone may still be on 900e until restored.

## 0. Boot packaging + FD base tests (NX679J stock ABL)
| Build | FD/BootShim base | Header | Payload | USB result |
|-------|------------------|--------|---------|------------|
| stock phase0 | n/a | v4 | raw ARM64 Image + ramdisk | Android OK |
| mu_aloha default | **0xFFC00000** | v3 | gzip BootShim\|\|FD | **900e** |
| experimental | 0xFFC00000 | v4 | raw BootShim\|\|FD | **fastboot** |
| experimental | 0xFFC00000 | v3 | raw BootShim\|\|FD | **fastboot** |
| rebased stock-like | **0xA7000000** | v4 | raw BootShim\|\|FD | **fastboot** |
| rebased stock-like | **0xA7000000** | v3 | gzip BootShim\|\|FD | **900e** |

Source changes kept (correct for stock NX679J):
- `build_cfg/sm8450.json` `UEFI_BASE=0xA7000000`
- `Waipio.fdf` `BaseAddress=0xA7000000`
- NX679J/HDK/MTP maps: single `"UEFI FD"` at `0xA7000000` (removed phantom `0xFFC00000` entry)

Artifacts:
- A7 raw v4: `nx679j-boot-assets/uefi/nubia-nx679j-uefi-debug-a7-raw-v4-boot.img`
- A7 gzip v3: `.../nubia-nx679j-uefi-debug-a7-gzip-v3-boot.img`
- older FFC experiments still under same uefi/ dir

Interpretation:
1. Stock ABL always accepts custom `boot_b`.
2. **gzip → 900e**, **raw → fastboot**, independent of header v3/v4 and of FFC vs A7 base.
3. Stock `uefi.img` / uefiplat use **`UEFI FD @ 0xA7000000`** — FFC was wrong for this device; A7 rebase is still required even though it does not alone yield menu.
4. Next focus: why BootShim/SEC dies before stay-alive (raw path) vs hard crash (gzip path). Likely load address / EL2 / MmuDetach / missing stock ABL handoff context — not packaging header version.

Next concrete steps:
- Restore phase0 if still 900e.
- Prefer raw A7 packaging for safer iteration (no Sahara).
- Instrument early BootShim or SEC (LED/USB cookie) or capture PStore on gzip path with `sudo edl peek`.
- Inspect stock ABL kernel entry / whether it expects EFI stub MZ vs pure Image.

## 1. BootShim / FD path (updated for NX679J stock)

Stock NX679J `uefiplat.cfg` / `uefi.img` use **`UEFI FD @ 0xA7000000`**. Tree was rebased from wrong Waipio default `0xFFC00000`:

1. Stock ABL loads Android boot.img (v3 or v4) kernel payload.
2. BootShim relocates FD to **`UEFI_BASE = 0xA7000000`**, size **`0x00400000`**.
3. Jump → SEC/PrePi; map name **`"UEFI FD"`** now matches `0xA7000000`.
4. Early stack still `0x80C00000`; vectors `0xA7600000` (unchanged).

900e still means: **ABL accepted boot_b; payload died early**. Do not replace ABL yet.

## 2. NX679J vs reference Waipio

| Item | NX679J | qcom-hdk8450 |
|------|--------|--------------|
| PlatformMemoryMapLib | FD @ 0xA7000000 (rebased) | was FFC; rebased with NX679J |
| BootShim base/size | **0xA7000000** / 4MiB | same after rebase |
| Config map | same cookies/IMEM | same |
| Display PCDs | 1080×2400, brand nubia | reference HDK |
| Panel | stock kernel `r6130` AMOLED | generic XML set |

No map mismatch found that uniquely explains 900e vs HDK.

## 3. Source changes applied (Phase 4)

### 3.1 Enable in-memory early DEBUG log (PStore)

Default Waipio build used `SerialPortLibNull` → **no early logs**.

Changed:

- `Platforms/WaipioPkg/WaipioNoSb.dsc`: `USE_MEMORY_FOR_SERIAL_OUTPUT = 1`
- Uncommented PStore region in memory maps:
  - `Device/nubia-nx679j/.../PlatformMemoryMapLib.c`
  - `Device/qcom-hdk8450/.../PlatformMemoryMapLib.c` (parity)
  - `Device/qcom-mtp8450/.../PlatformMemoryMapLib.c` (parity)

PStore region:

| Name | Base | Size |
|------|------|------|
| PStore | `0x800000000` | 4 MiB |

`InMemorySerialPortLib` writes DEBUG text into this ring; after a 900e crash, dump that range from RAM (edl filtered dump / memorydump) to recover SEC/PrePi strings.

`USE_MEMORY_FOR_SERIAL_OUTPUT` is already passed as `-DUSE_MEMORY_FOR_SERIAL_OUTPUT=…` via `Andromeda.dsc.inc` BuildOptions.

### 3.2 Not changed (deliberately)

- BootShim base remains `0xFFC00000` (correct).
- No `abl_b` path.
- No panel/R6130 binding yet (menu visibility is later).
- `Waipio.dsc` (secure-boot package) left at serial=0; we build **NoSb** (`-s 0`).

## 4. Rebuild (run on host)

Shell from agent session was hung; run these yourself:

```bash
cd /home/user/nx679j-stock/port-work/mu_aloha_platforms
# use project venv if present
.venv/bin/python build_uefi.py -d nubia-nx679j -p WaipioPkg -s 0 -t DEBUG
```

Expected outputs:

- `BootShim/BootShim.bin` (built with `UEFI_BASE=0xFFC00000`)
- `Build/WaipioPkg/nubia-nx679j.img` (boot v3 image)
- `Build/WaipioPkg/DEBUG_CLANGPDB/FV/SM8450_EFI.fd` (or similar tool-chain dir)

Copy into assets:

```bash
ASSETS=/home/user/nx679j-stock/port-work/nx679j-boot-assets/uefi
cp -a Build/WaipioPkg/nubia-nx679j.img \
  "$ASSETS/nubia-nx679j-uefi-debug-pstore-boot.img"
cp -a Build/WaipioPkg/DEBUG_CLANGPDB/FV/SM8450_EFI.fd \
  "$ASSETS/nubia-nx679j-SM8450_EFI-debug-pstore.fd" 2>/dev/null || \
  cp -a Build/WaipioPkg/*/*/FV/SM8450_EFI.fd \
  "$ASSETS/nubia-nx679j-SM8450_EFI-debug-pstore.fd"
```

## 5. Safe flash test (boot_b only)

Prereq: phone on known-good Magisk/Android, ADB OK, phase0 backups present.

```bash
# optional verify
adb devices
adb shell getprop ro.boot.slot_suffix

adb reboot bootloader
fastboot flash boot_b /home/user/nx679j-stock/port-work/nx679j-boot-assets/uefi/nubia-nx679j-uefi-debug-pstore-boot.img
fastboot set_active b
fastboot reboot
```

Observe:

| Outcome | Meaning | Action |
|---------|---------|--------|
| Menu / logo / shell | success path | stop; document |
| Black screen, no 900e | may be alive without display | wait; try USB modes |
| `05c6:900e` again | early crash | key-combo → fastboot, restore; then PStore dump if possible |
| Stuck fastboot | ABL did not leave | check image/header |

### Restore (proven)

```bash
cd /home/user/nx679j-stock/phase0-backups/2026-07-02-slotb-preproto
fastboot flash boot_b boot_b.img
fastboot flash vendor_boot_b vendor_boot_b.img
fastboot flash dtbo_b dtbo_b.img
fastboot flash vbmeta_b vbmeta_b.img
fastboot flash vbmeta_system_b vbmeta_system_b.img
# NOTE: restore_slot_b.sh wrongly uses set_active a — use b:
fastboot set_active b
fastboot reboot
```

### If 900e: capture PStore (real disk only)

Prefer force fastboot. If using edl, **do not full-RAM dump to /tmp**.

Target region: **`0x800000000` length `0x400000`**.

Example (adjust to your edl/qdl tooling once on Sahara):

```text
dump phys 0x800000000 size 0x400000 -> /home/user/nx679j-stock/port-work/ramdump-900e-minimal/pstore.bin
```

Then:

```bash
strings -n 8 pstore.bin | head -200
# look for: "UEFI firmware", "Error:", ASSERT, PrePi, Project Mu
```

Empty PStore ⇒ died **before** SerialPortInitialize / first DEBUG (BootShim copy, early ModuleEntry, or MMU before log).

## 6. Next debug order if still 900e after PStore build

1. Confirm BootShim magic/`UEFI_BASE` in built `BootShim.bin` (`ARM\x64` header + quad `0xFFC00000`).
2. Inspect PStore contents after crash.
3. If PStore empty: suspect BootShim/EL1 entry or ABL gzip load size; try `fastboot boot` the img once without permanent flash.
4. If PStore has ASSERT on map name: fix map.
5. If logs progress past PrePi then crash in DXE: trim APRIORI (Display/USB) for minimal payload.
6. Only if stock ABL cannot load any payload: consider `abl_b` — **not yet**.

## 7. Constraints (unchanged)

- Flash **only `boot_b`** for tests.
- Preserve stock ABL/XBL/TZ.
- Always keep phase0 rollback path.
- First success = any non-900e UEFI life sign (log/menu/USB), not full OpenWrt.
