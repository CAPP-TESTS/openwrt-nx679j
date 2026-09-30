# NX679J OpenWrt / Qualcomm Boot-Chain Reverse-Engineering Handoff

## Document purpose

This document is the persistent technical handoff for an AI agent or engineer continuing the OpenWrt-on-Nubia-RedMagic-7 investigation.

The target is a Nubia RedMagic 7, model NX679J, based on Qualcomm SM8450/Snapdragon 8 Gen 1. The objective is to boot a reproducible OpenWrt userspace on the device, obtain a persistent control channel over the single USB-C port, and retain a reliable recovery path through the known-good Android slot or Qualcomm EDL.

The document deliberately separates:

- verified observations;
- artifacts that can be independently rechecked;
- hypotheses that are still unproven;
- operations that are safe to perform before a device connection is available;
- operations that must not be performed blindly.

Do not treat historical conclusions as facts unless they are explicitly marked **verified** below.

## Current executive status

### Device state

At the latest bounded check:

- `adb devices`: no device;
- `fastboot devices`: no device;
- `lsusb`: no Qualcomm 9008, Android fastboot, or Android ADB device;
- only ordinary host USB devices were visible.

The phone is therefore not currently available for a new flash or readback operation.

### Known-good path

The Android boot path on slot A was restored and rooted with Magisk 30.7 during the earlier work. Runtime evidence shows a functioning Android userspace, root access through `su`, Magisk mounts, ADB FunctionFS, pstore, and the Qualcomm DWC3 UDC.

The known-good recovery image is the Magisk-patched Android boot image:

`/home/user/nx679j-stock/magisk_patched-30700_Zx2eF.img`

The corresponding EDL readback/recovery artifact is:

`/home/user/nx679j-stock/recovery-v311-magisk-20260711/boot_a-magisk-30700.img`

Do not overwrite the known-good A-side image until a complete readback backup and SHA-256 record have been made.

### OpenWrt result

An OpenWrt Android boot image was written to slot B. Fastboot accepted the image, the RedMagic logo appeared, USB disappeared during the boot attempt, and the phone returned to fastboot or fell back to slot A after approximately 15 seconds.

The most useful monitor is:

`/home/user/nx679j-stock/native-openwrt-usb-build/headless-upstream/diagnostic-v2/boot-attempt-1-monitor.log`

It records:

- fastboot reboot succeeded;
- fastboot disappeared;
- no new network or USB gadget interface appeared;
- fastboot returned at approximately 14.83 seconds.

This proves a failed persistent boot attempt. It does **not** prove:

- that Linux reached `/init`;
- that the OpenWrt ramdisk ran;
- that a kernel panic occurred;
- that the Qualcomm watchdog caused the reset;
- that the failure was caused by AVB, the DTB, UFS, DWC3, or ABL.

## Hardware and boot-chain facts

### Target

- Device: Nubia RedMagic 7
- Model: NX679J
- SoC family: Qualcomm SM8450 / Snapdragon 8 Gen 1 / Waipio
- Storage: UFS
- Boot architecture: Android boot image v4, vendor boot, DTBO, AVB/vbmeta, A/B slots
- USB connector: one USB-C port
- Bootloader: unlocked
- Secure boot: enabled
- Slot count: 2
- Boot partition size: `0x6000000` bytes (96 MiB)

### Boot partitions in the recovered GPT

The recovered GPT is documented in:

`/home/user/nx679j-stock/edl-recovery/recon-20260717-183330/printgpt.txt`

Relevant entries include:

- `boot_a`
- `boot_b`
- `vendor_boot_a`
- `vendor_boot_b`
- `dtbo_a`
- `dtbo_b`
- `vbmeta_a`
- `vbmeta_b`
- `misc`
- `abl_a`
- `abl_b`
- `xbl_a`
- `xbl_b`
- `rawdump`
- `logdump`
- `xbl_ramdump_a`
- `xbl_ramdump_b`

In the captured GPT:

- A-side boot-chain entries are active;
- B-side entries are inactive;
- B-side boot flags differed from the ordinary successful A-side state;
- `boot_b` was not marked unbootable in the relevant fastboot observations;
- retry/success state changed during the failed boot experiments.

Do not reconstruct the entire GPT from `patch-gpt-active-bit.py` without comparing it to a fresh device readback. That script hardcodes geometry and creates a new disk GUID; it is not a generic safe repair tool.

## EDL and Firehose status

### Earlier incorrect conclusion

Early sessions suggested that secure boot permanently prevented Firehose access. This was later disproved.

### Verified later state

The loader was accepted in a later session:

- Sahara communication succeeded;
- the device entered Firehose mode;
- GPT parsing succeeded;
- partition reads succeeded;
- partition writes and verification succeeded;
- a known-good Magisk boot image was restored to A;
- the readback SHA-256 matched the source;
- the `bootonce-bootloader` BCB entry was cleared from `misc`.

Loader:

`/home/user/nx679j-stock/edl-recovery/prog_firehose_ddr.melf`

EDL recon report:

`/home/user/nx679j-stock/edl-recovery/recon-20260717-183330/analysis/REPORT.md`

GPT capture:

`/home/user/nx679j-stock/edl-recovery/recon-20260717-183330/printgpt.txt`

EDL executable used historically:

`/home/user/venvs/edk2/bin/edl`

Use the loader and memory type exactly as recorded in the scripts unless a new agent independently verifies a better loader:

`--loader=/home/user/nx679j-stock/edl-recovery/prog_firehose_ddr.melf --memory=ufs`

## Android boot-image analysis

The earlier statement that the stock boot image was corrupted was caused by parsing header fields at incorrect offsets and conflating different files. The valid stock images are Android boot header v4 images containing valid ARM64 Linux Images.

### Stock Android boot image

Primary artifact:

`/home/user/nx679j-stock/extracted/boot.img`

EDL readback analyzed in:

`/home/user/nx679j-stock/edl-recovery/recon-20260717-183330/boot_a.bin`

Verified fields:

- magic: `ANDROID!`
- header version: 4
- page size: 4096
- header size: 1584
- kernel size: 49,108,324 bytes
- ramdisk size: 1,380,095 bytes
- signature size: 4096 bytes
- kernel SHA-256:
  `f0aa949c95d1421381850cb1982bd2a505e96fcd7565543ec28d5b1f7d81a0cc`
- ramdisk SHA-256:
  `5ca714aada08c2f7275869dd77b5bbe59f7391af83f51cceb422e4c9d1442d76`
- kernel type:
  `Linux kernel ARM64 boot executable Image, little-endian, 4K pages`

### Magisk-patched Android boot image

Artifact:

`/home/user/nx679j-stock/magisk_patched-30700_Zx2eF.img`

Verified fields:

- magic: `ANDROID!`
- header version: 4
- kernel identical to stock A;
- ramdisk size: 2,200,983 bytes;
- signature size: 4096 bytes;
- Magisk 30.7 content present;
- full-image SHA-256:
  `0cd94d5627501f3c65c94542093ad18b1ec6c1c215dc6a7ecab4f941057e8364`

The artifact named `stock_boot.img` is confusing: it is byte-identical to the Magisk-patched image in the current artifact set and should not automatically be treated as an untouched stock image.

### OpenWrt boot image found on B

EDL readback:

`/home/user/nx679j-stock/edl-recovery/recon-20260717-183330/boot_b.bin`

Also represented historically by:

`/home/user/stock-boot/boot_a_pulled.img`

Verified fields:

- magic: `ANDROID!`
- header version: 4
- header size: 1584
- kernel size: 38,781,440 bytes
- ramdisk size: 4,929,692 bytes
- kernel: Linux `6.12.0-00001-gfad4d497aa70-dirty`
- signature size: 0
- kernel SHA-256:
  `0760e628803e19d2259eab3b4b2aa661b919d36520f6c9c61d913c9db4276f56`
- ramdisk SHA-256:
  `7d7f52963499860f5d1e6c3a14727ef95804c4bae6dacdd78fb0575e604a66b5`

Command line:

`console=ttyMSM0,115200n8 console=tty0 loglevel=8 ignore_loglevel initcall_debug fbcon=map:0 panic=10 g_ncm.dev_addr=xx:xx:xx:xx:xx:xx g_ncm.host_addr=xx:xx:xx:xx:xx:xx`

Important boot-chain difference:

- stock and Magisk boot images have a 4096-byte signature;
- the OpenWrt B image has no boot-image signature.

This is a serious AVB/ABL compatibility variable. Fastboot accepting the write and showing the logo does not prove that ABL accepted the image as a valid signed boot payload.

### Other OpenWrt build variant

The v1 validation artifact reports:

`/home/user/nx679j-stock/native-openwrt-usb-build/headless-upstream/v1/validate-final/boot-info.txt`

Fields:

- header version: 4
- kernel size: 38,648,320 bytes
- ramdisk size: 4,150,073 bytes
- signature size: 0
- command line:
  `console=ttyMSM0,115200n8 console=tty0 loglevel=7 earlycon`

This is not byte-identical to the 38,781,440-byte kernel image read from B. Do not use measurements from one build variant to describe another.

## Vendor boot and device tree

### Vendor DTB container

Artifact:

`/home/user/nx679j-stock/vendor_boot_unpacked/dtb`

Verified:

- size: 3,879,360 bytes;
- nine concatenated valid FDT blobs;
- totalsizes:
  - 431,264
  - 431,252
  - 347,467
  - 298,995
  - 387,498
  - 496,030
  - 495,598
  - 495,630
  - 495,626 bytes.

The DTBO files were previously misclassified by applying an Android boot-image parser. DTBO is a different format and must be parsed as DTBO/flat device-tree data.

### Running Android device tree

Artifact:

`/home/user/nx679j-stock/runtime-evidence-20260711/running_fdt.dts`

Top-level identity:

- model:
  `Qualcomm Technologies, Inc. Waipio MTP with PM8010`
- compatible:
  `qcom,waipio-mtp`, `qcom,waipio`, `qcom,mtp`
- `qcom,msm-id` includes `0x1c9` / `0x1e2` variants;
- `qcom,board-id = <0x10008 0x00>`.

Important chosen bootargs include:

- `console=ttyMSM0,115200n8`
- `loglevel=6`
- `kpti=0`
- `swiotlb=noforce`
- `pcie_ports=compat`
- `allow_mismatched_32bit_el0`
- `cpufreq.default_governor=performance`
- `ftrace_dump_on_oops`
- `pstore.compress=none`
- `video=vfb:640x400,bpp=32,memsize=3072000`
- `qcom-dload-mode.download_mode=1`
- `bootconfig`
- `rootwait`
- `ro`
- `init=/init`.

The DT includes Qualcomm resources for UFS, USB/DWC3, PHY, GIC, PCIe, PMICs, display, memory reservations, and watchdog/hypervisor support.

The existence of a valid DTB is verified. The correct DTB selection for an OpenWrt boot is not yet experimentally proven.

## OpenWrt build system and image construction

### Headless kernel configuration

Primary config:

`/home/user/nx679j-stock/native-openwrt-usb-build/headless-upstream/gunyah-wdt-v7/nx679j-headless.config`

The file explicitly enables boot-critical built-in features:

- `CONFIG_USB=y`
- `CONFIG_USB_GADGET=y`
- `CONFIG_USB_DWC3=y`
- `CONFIG_USB_DWC3_GADGET=y`
- `CONFIG_USB_DWC3_QCOM=y`
- `CONFIG_USB_LIBCOMPOSITE=y`
- `CONFIG_USB_U_ETHER=y`
- `CONFIG_USB_F_NCM=y`
- `CONFIG_USB_G_NCM=y`
- `CONFIG_USB_CONFIGFS=y`
- `CONFIG_USB_CONFIGFS_NCM=y`
- `CONFIG_PHY_QCOM_USB_SNPS_FEMTO_V2=y`
- `CONFIG_SCSI=y`
- `CONFIG_SCSI_UFSHCD=y`
- `CONFIG_SCSI_UFSHCD_PLATFORM=y`
- `CONFIG_SCSI_UFS_QCOM=y`
- `CONFIG_PHY_QCOM_QMP=y`
- `CONFIG_PHY_QCOM_QMP_UFS=y`
- `CONFIG_PSTORE=y`
- `CONFIG_PSTORE_CONSOLE=y`
- `CONFIG_PSTORE_PMSG=y`
- `CONFIG_PSTORE_RAM=y`
- `CONFIG_GUNYAH_WATCHDOG=y`
- `CONFIG_NET=y`
- `CONFIG_PACKET=y`
- `CONFIG_INET=y`.

The configuration intentionally does not enable DWC3 host or dual-role mode:

- `CONFIG_USB_DWC3_HOST` is not set;
- `CONFIG_USB_DWC3_DUAL_ROLE` is not set.

The config comment assumes USB peripheral mode and an external ramdisk that creates `usb0`.

Important: a kernel `.config` only proves that support was requested. It does not prove that:

- the exact flashed kernel was built from this config;
- the DWC3 controller probed;
- the USB PHY initialized;
- the DT selected peripheral mode;
- the gadget init script ran;
- the host saw a gadget.

### Build script

`/home/user/nx679j-stock/native-openwrt-usb-build/headless-upstream/v1/build-headless-openwrt.sh`

The script:

1. selects a kernel `Image`;
2. selects `sm8450-nx679j.dtb`;
3. creates a reproducible gzip cpio ramdisk from the OpenWrt rootfs;
4. builds an Android boot header v4 image;
5. builds a vendor boot image v4;
6. reuses stock vendor bootconfig and `vendor_ramdisk00`;
7. embeds the selected DTB in vendor boot;
8. writes SHA-256 sums.

Relevant default inputs:

- rootfs:
  `$WORKDIR/../../rootfs`
- kernel build directory:
  `/home/user/nx679j-kernel-test.IYMJNo`
- kernel:
  `$BUILD_DIR/arch/arm64/boot/Image`
- DTB:
  `$BUILD_DIR/arch/arm64/boot/dts/qcom/sm8450-nx679j.dtb`
- stock boot ramdisk:
  `v1/boot-stock/ramdisk`
- stock vendor DTB:
  `v1/vendor-boot-stock/dtb`
- stock vendor bootconfig:
  `v1/vendor-boot-stock/bootconfig`
- stock vendor ramdisk fragment:
  `v1/vendor-boot-stock/vendor_ramdisk00`.

The boot image command line generated by this script is:

`console=ttyMSM0,115200n8 loglevel=7 earlycon panic=10`

The vendor command line is:

`video=vfb:640x400,bpp=32,memsize=3072000 qcom-dload-mode.download_mode=1 bootconfig`

### Slot-B flashing script

`/home/user/nx679j-stock/native-openwrt-usb-build/headless-upstream/abl-compat-v12/scripts/flash-slotb-openwrt.sh`

The intended sequence is:

1. require Qualcomm 9008;
2. print GPT;
3. clear `misc`;
4. set active slot B;
5. write `boot_b`;
6. write `vendor_boot_b`;
7. write `dtbo_b`;
8. preserve or replace `vbmeta_b`;
9. read back boot and vendor boot;
10. compare SHA-256;
11. read back `misc`;
12. reset.

The script has dangerous properties that must be reviewed before reuse:

- it may attempt to write `/dev/zero` to `misc`;
- it has `|| true` around some operations;
- it writes a DTBO artifact whose existence and exact format must be independently checked;
- it may substitute `vbmeta_a` for `vbmeta_b`;
- its logs default to `/tmp`, which is not persistent across a system reboot.

Do not run it unchanged on a newly recovered device. First replace it with a bounded, persistent-log, readback-first procedure.

## Ramdisk and userspace entry point

The OpenWrt ramdisk was decompressed and its `init` script was inspected.

The script is designed to:

1. mount or prepare minimal virtual filesystems;
2. configure the USB gadget;
3. expose a network/serial control channel;
4. execute `/sbin/init`.

The script’s existence does not establish that it executed during the failed boot. There is no direct console, pstore record, ADB connection, NCM interface, or kernel log proving entry into userspace.

## Android runtime baseline

Runtime evidence was collected while Android/Magisk was functioning:

`/home/user/nx679j-stock/runtime-evidence-20260711/runtime_state.txt`

Useful verified baseline facts:

- pstore is mounted at `/sys/fs/pstore`;
- Android ADB FunctionFS is mounted;
- diag, MTP, and PTP FunctionFS endpoints exist;
- UDC `a600000.dwc3` exists;
- Android exposes a Qualcomm DWC3 controller;
- the Android runtime has a working display stack;
- an OpenWrt rootfs was previously mounted under `/data/local/openwrt-rootfs`;
- Magisk runtime mounts are present.

This baseline proves that the hardware and stock Android device tree can initialize UFS, DWC3, USB gadget infrastructure, and pstore. It does not prove that the OpenWrt kernel’s driver binding and DT interpretation are equivalent.

## Watchdog investigation

### Evidence

The watchdog hypothesis is plausible because:

- the failed boot returns after a repeatable short interval;
- Android logs/modules include Qualcomm/Gunyah watchdog components;
- the DT includes watchdog/hypervisor resources;
- `CONFIG_GUNYAH_WATCHDOG=y` is enabled in the OpenWrt test configuration;
- a Qualcomm watchdog MMIO region around `0x17410000` was identified during prior reverse engineering.

### Unproven status

No direct OpenWrt watchdog bite log exists.

The kernel-side patch in:

`/home/user/nx679j-stock/kernel-patch-test/linux-6.12/arch/arm64/kernel/head.S`

was discussed and prepared, but there is no evidence that a patched image was flashed and successfully tested.

The Android helper:

`/home/user/nx679j-stock/hypervisor-bypass/watchdog_pet.sh`

attempts to write to:

- `/sys/devices/platform/soc/soc:qcom,gh-virt-wdt/watchdog/watchdog0`;
- `/dev/watchdog0`.

Its effectiveness on this platform is unverified.

Do not write arbitrary MMIO values to a live device as a first experiment. First establish the exact register map, clock/reset semantics, access permissions, and whether the watchdog is owned by Gunyah, firmware, ABL, or Linux.

## Fastboot behavior and transport observations

Observed:

- `fastboot getvar all` and some flashes completed;
- `fastboot boot` and some long-running transfers could hang when USB disappeared;
- the failed OpenWrt boot caused a disappearance and later fastboot return;
- commands without a timeout made diagnosis ambiguous.

Operational rules:

- use `timeout` around every fastboot/ADB query;
- never assume a reboot leaves the device on the same USB identity;
- poll USB state independently from fastboot state;
- do not issue a second write after a reset until the device is rediscovered;
- save all logs to a persistent project directory, not `/tmp`;
- after every write, perform an EDL or fastboot readback and hash comparison.

## Facts versus hypotheses

### Verified

1. The device is an NX679J/SM8450-class Qualcomm Android phone.
2. The bootloader is unlocked and secure boot is enabled.
3. Firehose was accepted in later sessions.
4. GPT was parsed and partitions were read.
5. Stock and Magisk boot images are valid Android boot v4 images.
6. The stock ARM64 kernel is valid.
7. The OpenWrt B boot image is a valid Android boot v4 container.
8. The OpenWrt B boot image has no signature.
9. The OpenWrt B kernel is Linux 6.12.0 dirty.
10. OpenWrt boot B was written/read back during the experiment.
11. The RedMagic logo appeared during an OpenWrt boot attempt.
12. USB disappeared and fastboot returned at approximately 15 seconds.
13. The stock vendor DTB container contains nine valid FDT blobs.
14. The Android runtime device tree contains Waipio/SM8450 resources.
15. The OpenWrt headless config requests built-in DWC3 gadget, QCOM USB, NCM, UFS, pstore, and Gunyah watchdog support.
16. Current ADB, fastboot, and EDL connectivity is absent.

### Plausible but unproven

1. ABL/AVB rejects the unsigned OpenWrt boot image.
2. ABL accepts the image but the kernel fails before console output.
3. The Gunyah/Qualcomm watchdog resets the system.
4. The selected DTB/vendor boot combination is wrong.
5. UFS or another required early Qualcomm driver is not initialized.
6. DWC3 or the USB PHY does not probe in the selected peripheral-only configuration.
7. The OpenWrt image reaches the kernel but not `/init`.
8. Slot retry/success metadata causes fallback independently of the kernel failure.
9. The DTB memory map or reserved-memory layout is incompatible with the new kernel.
10. `vbmeta_b` or `dtbo_b` does not match the boot/vendor boot pair.

### Explicitly disproven or corrected

1. “The stock boot image is corrupted.” False; the parser used wrong offsets and mixed artifacts.
2. “The DTB is missing.” False; vendor boot contains nine valid FDTs and a runtime DTS exists.
3. “The OpenWrt kernel has no USB gadget support.” False; the relevant config explicitly enables it.
4. “Firehose is permanently unavailable.” False; later recon sessions entered Firehose.
5. “A 15-second fallback proves a watchdog bite.” False; it is consistent with a watchdog but not diagnostic.

## Safe continuation procedure

### Phase 0: no-device operations

Safe operations while the phone is absent:

1. Inspect all image headers with format-appropriate tools.
2. Hash all source and recovery artifacts.
3. Compare the OpenWrt source image to the EDL readback.
4. Split and decompile all nine FDTs.
5. Parse DTBO with an Android DTBO/FDT parser, not `unpackbootimg`.
6. Inspect AVB descriptors and vbmeta chain.
7. Diff stock and OpenWrt vendor boot metadata.
8. Verify the exact kernel `.config` embedded or saved beside each build.
9. Prepare a persistent experiment directory and logs.

Do not rebuild or flash until the exact source artifact for the prior failed attempt is identified.

### Phase 1: bounded transport detection

Use only bounded, non-writing probes:

```sh
timeout 10 adb devices
timeout 10 fastboot devices
timeout 10 lsusb -nn
timeout 10 lsusb -d 05c6:9008
```

If fastboot appears, collect `getvar` output with a timeout and save it under a timestamped persistent directory.

If 9008 appears, do not immediately write. First:

1. save `printgpt`;
2. read `misc`;
3. read `boot_a` and `boot_b`;
4. read `vendor_boot_a` and `vendor_boot_b`;
5. read `vbmeta_a`, `vbmeta_b`, `dtbo_a`, and `dtbo_b`;
6. hash every readback;
7. compare against the previous known-good artifacts.

### Phase 2: recovery snapshot

Before any new test, create a persistent directory such as:

`/home/user/nx679j-stock/experiments/YYYYMMDD-HHMMSS/`

Store there:

- command transcripts;
- GPT output;
- source image hashes;
- readback image hashes;
- fastboot variables;
- USB enumeration logs;
- experiment metadata;
- the exact test hypothesis.

Never use `/tmp` as the only copy of a recovery log or image.

### Phase 3: isolate boot-chain acceptance

The first question is whether ABL/AVB accepts the boot chain independently from whether Linux boots.

Use a controlled matrix, changing one variable at a time:

1. stock boot A content on the B partition with the B-side vendor boot and metadata;
2. OpenWrt kernel/ramdisk with stock-compatible vendor boot;
3. OpenWrt boot plus the exact vendor boot generated by the same build;
4. controlled `vbmeta_b` variants;
5. controlled DTB/DTBO variants.

After each boot attempt, record:

- whether the logo appears;
- duration until USB disappearance;
- whether fastboot returns;
- whether slot metadata changes;
- whether rawdump/logdump/pstore data changes.

Do not interpret the logo alone as proof that Linux was entered; it may be displayed by the bootloader.

### Phase 4: obtain early kernel evidence

Prioritize evidence that survives a reset:

- pstore/ramoops with a valid reserved-memory region in the DT;
- Qualcomm rawdump and logdump;
- ABL/XBL boot logs;
- a kernel image with an unmistakable early marker;
- the framebuffer console if already initialized;
- USB gadget initialization as early as safely possible.

`CONFIG_PSTORE=y` alone is insufficient. The DT must reserve a correctly sized and correctly addressed pstore/ramoops region, and the reset path must preserve it.

The kernel command line should retain:

- `console=ttyMSM0,115200n8`;
- `earlycon` if the UART mapping is correct;
- `panic=10`;
- a high log level;
- pstore options compatible with the reserved memory.

### Phase 5: watchdog experiment

Only after a baseline diagnostic image exists:

1. determine whether Gunyah watchdog probing occurs;
2. inspect the stock Android watchdog driver and DT resources;
3. identify who owns the watchdog;
4. instrument or disable it using the correct driver/firmware interface;
5. build a uniquely hashed image;
6. flash only B;
7. verify readback;
8. compare boot duration and persistent logs.

Do not label the result “watchdog fixed” unless the same image boots past the previous failure point and the evidence distinguishes watchdog behavior from all other resets.

## High-priority reverse-engineering questions

1. Does ABL enforce the Android boot-image signature on this unlocked-but-secure device?
2. Is the OpenWrt image rejected before Linux entry because `signature_size=0`?
3. Which of the nine vendor FDTs corresponds to the NX679J board ID `0x10008`?
4. Is the custom `sm8450-nx679j.dtb` equivalent to the runtime DTB in memory layout, reserved memory, USB role, and UFS configuration?
5. Does the vendor boot image contain the correct vendor ramdisk fragment and bootconfig for this ABL build?
6. Is `dtbo_b` a valid Android DTBO image with the expected entries and board matching?
7. Does `vbmeta_b` authenticate or reference the OpenWrt boot/vendor boot content?
8. Does the kernel reach `start_kernel`, `console_init`, `ufshcd`, `dwc3`, and `populate_rootfs`?
9. Does the Gunyah watchdog driver start before the 15-second reset?
10. Is the reset a firmware watchdog bite, an ABL boot-attempt timeout, a kernel panic, a power/PMIC reset, or an AVB fallback?
11. Does the bootloader update BCB retry/success metadata before returning to fastboot?
12. Can rawdump/logdump be retrieved after the failed attempt through EDL?

## Artifact index

### EDL and GPT

- `edl_config.json`
- `edl-recovery/prog_firehose_ddr.melf`
- `edl-recovery/recon-20260717-183330/printgpt.txt`
- `edl-recovery/recon-20260717-183330/dump.log`
- `edl-recovery/recon-20260717-183330/analysis/REPORT.md`
- `edl-recovery/recon-20260717-183330/analysis/boot_headers.json`
- `patch-gpt-active-bit.py`
- `gpt_dumps/`

### Android and Magisk

- `extracted/boot.img`
- `stock_extracted/`
- `boot_unpacked/`
- `magisk_patched-30700_Zx2eF.img`
- `magisk_boot.img`
- `magisk_boot_verify/`
- `magisk_extract/`
- `recovery-v311-magisk-20260711/`
- `recovery-v411-bootchain-20260711/`
- `runtime-evidence-20260711/runtime_state.txt`
- `runtime-evidence-20260711/running_fdt.dts`

### Vendor boot and DT

- `vendor_boot_unpacked/`
- `vendor_boot_repack/`
- `vendor_ramdisk_proper.lz4`
- `vendor_ramdisk_custom.cpio`
- `vendor_rd_build/`
- `port-work/vboot-dtb-swap/`

### OpenWrt and kernel

- `native-openwrt-usb-build/`
- `native-openwrt-usb-build/headless-upstream/gunyah-wdt-v7/nx679j-headless.config`
- `native-openwrt-usb-build/headless-upstream/v1/build-headless-openwrt.sh`
- `native-openwrt-usb-build/headless-upstream/v1/validate-final/boot-info.txt`
- `native-openwrt-usb-build/headless-upstream/abl-compat-v12/scripts/flash-slotb-openwrt.sh`
- `native-openwrt-usb-build/headless-upstream/abl-compat-v12/scripts/run-slotb-openwrt.sh`
- `native-openwrt-usb-build/headless-upstream/diagnostic-v2/boot-attempt-1-monitor.log`
- `port-work/openwrt-phase4-proto1-rootfsfirst/`
- `kernel-patch-test/`
- `hypervisor-bypass/`

### Recovery and raw evidence

- `phase0-backups/`
- `phase2-failure-analysis/`
- `full_extracted_v311/`
- `logs/`
- `log.txt`
- `out/`
- `analysis/`
- `rawdump` and `logdump` artifacts under the EDL recon/build areas.

## Known hash and size anchors

These values are anchors for identifying artifacts, not substitutes for a fresh readback:

| Artifact | Size | SHA-256 or identifying value |
|---|---:|---|
| `extracted/boot.img` | 100,663,296 bytes | full-image hash recorded locally as `fbf2e30b4a8aa7f2648fcde2bffbbe4fd33e60a4c4d2dbd7ba68b89c980ec57b` |
| `magisk_patched-30700_Zx2eF.img` | 100,663,296 bytes | `0cd94d5627501f3c65c94542093ad18b1ec6c1c215dc6a7ecab4f941057e8364` |
| stock A kernel | 49,108,324 bytes | `f0aa949c95d1421381850cb1982bd2a505e96fcd7565543ec28d5b1f7d81a0cc` |
| stock A ramdisk | 1,380,095 bytes | `5ca714aada08c2f7275869dd77b5bbe59f7391af83f51cceb422e4c9d1442d76` |
| OpenWrt B kernel | 38,781,440 bytes | `0760e628803e19d2259eab3b4b2aa661b919d36520f6c9c61d913c9db4276f56` |
| OpenWrt B ramdisk | 4,929,692 bytes | `7d7f52963499860f5d1e6c3a14727ef95804c4bae6dacdd78fb0575e604a66b5` |
| vendor DTB container | 3,879,360 bytes | `b67334af3954fc47bc07cefb8d7fc1c7a59ce8258c2b4c8a55bed92fee5c88ad` |
| running FDT DTS | 1,185,841 bytes | `32f7b3e885d355c3f861c0db009cd7e5bfafff368c09f44c091fd83a5ee18123` |

## Handoff instructions for the next AI agent

The next agent should:

1. Read this document completely before proposing a flash.
2. Treat every “plausible but unproven” item as an experiment hypothesis.
3. Recompute hashes before relying on any image path.
4. Identify the exact boot image variant used in each historical experiment.
5. Preserve A/Magisk and all EDL backups.
6. Avoid unbounded fastboot commands.
7. Avoid `fastboot boot` until the current transport is known to be stable.
8. Avoid rewriting GPT unless a fresh full GPT read and a byte-level comparison justify it.
9. Do not assume the logo means Linux reached the kernel.
10. Do not assume USB gadget config means a gadget was initialized.
11. Do not assume a 15-second reset is a watchdog bite.
12. Prefer readback, pstore, rawdump, logdump, and ABL evidence over inference.
13. Change one boot-chain variable per experiment.
14. Write all logs and artifacts below `/home/user/nx679j-stock/` in persistent directories.
15. End each experiment with a concise record of:
    - source image hashes;
    - target partitions;
    - readback hashes;
    - slot and BCB state;
    - USB observations;
    - reset time;
    - new evidence;
    - next falsifiable hypothesis.

## Success criteria

OpenWrt is not considered working until all of the following are true:

1. The image boots without returning to fastboot.
2. The kernel reaches `/init`.
3. A USB gadget is enumerated.
4. NCM, ACM, ADB, or another persistent control channel is usable.
5. The boot sequence is reproducible from a documented image set.
6. The slot/GPT/BCB state is documented.
7. Android A/Magisk or EDL recovery remains available.
8. At least one persistent boot or kernel log can be retrieved after a failure.
