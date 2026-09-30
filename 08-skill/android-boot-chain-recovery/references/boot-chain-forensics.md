# Boot-chain forensics reference

## Evidence table

| Observation | What it proves | What it does not prove |
|---|---|---|
| `adb` reaches Android and `su` is root | Current Android userspace and root channel work | Slot B contents or previous boot path |
| `fastboot flash` returns `OKAY` | Bootloader accepted a transfer | ABL accepted the image for boot |
| RedMagic/logo appears | Some bootloader/display path ran | Linux entry |
| USB disappears and fastboot returns | A bounded boot attempt failed persistently | AVB rejection, watchdog, panic, or kernel entry |
| pstore is empty | No retained pstore record is available now | That Linux never ran |
| `procd` in QEMU | Kernel unpacked the tested ramdisk and userspace ran in QEMU | Qualcomm hardware drivers or real-device boot |
| `secure:yes`, `unlocked:yes` | Fastboot reports secure boot enabled and unlocked state | Exact AVB policy or signature acceptance |
| `vendor_boot_a != vendor_boot_b` | Slot vendor boot images differ | The difference is the sole boot cause |
| `slot-retry-count` unchanged after a silent attempt | No reboot happened, so there was no panic loop and the bootloader was never re-entered | That the kernel never ran, or how far the chain got |
| Total silence with the counter untouched, while the same image class returns to fastboot with the target slot's `dtbo` neutralised | The bootloader accepted the image and the stop is downstream of its decision | Which layer downstream (kernel handoff, init, driver stack) |

## Android readback recipe

Use a persistent directory and a fixed manifest. For each partition:

```sh
block=$(adb shell "su -c 'readlink -f /dev/block/by-name/$part'" | tr -d '\r\n')
sectors=$(adb shell "su -c 'cat /sys/class/block/${block##*/}/size'" | tr -d '\r\n')
bytes=$((sectors * 512))
adb exec-out "su -c 'exec dd if=/dev/block/by-name/$part bs=4194304 2>/dev/null'" > "$part.img"
test "$(stat -c %s "$part.img")" = "$bytes"
sha256sum "$part.img"
adb shell "su -c 'toybox sha256sum /dev/block/by-name/$part'"
```

Keep `dd` diagnostics off binary stdout: otherwise the remote `dd` summary can append bytes to the image. Reject and quarantine short or oversized files; do not silently trim them.

## Image checks

```sh
unpack_bootimg --boot_img boot.img --out unpacked
avbtool info_image --image vbmeta.img
file unpacked/kernel unpacked/ramdisk
```

For Android boot v4, record: kernel size/hash, ramdisk size/hash, header version/size, cmdline, OS version/patch, signature size, and any AVB footer. Parse `vendor_boot` independently and record DTB container size/hash, vendor ramdisk/table, bootconfig, and vendor cmdline. Parse DTBO with an Android DTBO/FDT parser, not `unpackbootimg`.

## DT/DTBO control

Map the runtime indices to exact offsets and selectors before editing:

- `ro.boot.dtb_idx` identifies a selected FDT entry in the vendor DTB container.
- `ro.boot.dtbo_idx` identifies a selected DTBO entry in the DTBO image.
- Verify model, compatible, `qcom,msm-id`, `board-id`, PMIC IDs, UFS, DWC3/PHY role, reserved memory, and ramoops.
- Do not apply a stock overlay to an upstream/custom tree unless symbols and selector metadata are compatible; `FDT_ERR_NOTFOUND` or `FDT_ERR_BADOFFSET` is evidence of incompatibility, not a reason to force the merge.
- If a slot's vendor DTB container is malformed or relocated incorrectly, restore an intact vendor boot before testing kernel/userspace changes.

### vendor_boot v4: measure the layout, do not transcribe it

Measured on one SM8450 device (`vendor_boot_a`, a 96 MiB full-partition readback, header v4, `header_size=2128`, `page_size=4096`), offsets found by scanning the bytes for signatures rather than by assuming them:

```text
@0          header v4
@4096       vendor ramdisk, 10 041 284 B, LZ4 legacy   (found by scanning for 02 21 4c 18)
@10 047 488 dtb area: NINE FDTs, ~300-500 KB each     (found by scanning for d00dfeed)
            names seen: Waipio, Waipio v2, WaipioP, WaipioP v2, plus a variant with a
            different board id
@13 930 496 bootconfig (header declares 85 B; its bytes are not text at that offset)
```

Field map (u64 shown where it matters): `0` magic `VNDRBOOT`, `8` header_version, `12` page_size, `16` kernel_addr, `20` ramdisk_addr, `24` vendor_ramdisk_size, `28` cmdline[2048], `2076` tags_addr, `2080` name[16], `2096` header_size, `2100` dtb_size, `2104` dtb_addr (**u64**), `2112` vendor_ramdisk_table_size, `2116` vendor_ramdisk_table_entry_num, `2120` vendor_ramdisk_table_entry_size, `2124` bootconfig_size.

- A declared field is not proof of content: the table the header described was zeros at the offset the header implied, so read the bytes before decoding a structure out of them.
- The vendor cmdline is real content, not decoration: measured `video=vfb:640x400,bpp=32,memsize=3072000 qcom-dload-mode.download_mode=1 bootconfig` on this unit, and those arguments appear in the live `/proc/cmdline` because the kernel concatenates the boot image cmdline, the vendor boot cmdline and the bootconfig section.
- With several candidate FDTs, the bootloader selects one by board identity (`qcom,msm-id` / `qcom,board-id`). Read `ro.boot.dtb_idx` / `ro.boot.dtbo_idx` to learn which was selected, and compare a custom DTB against the running FDT before testing it.

### DTBO image layout, and what the overlays actually touch

```text
@0    header (32 B): magic 0xd7b7ab1e, total_size, header_size=32,
                     dt_entry_size=32, dt_entry_count, dt_entries_offset, page_size, version
@32   entries (32 B each): dt_size, dt_offset, id, rev, custom[4]
```

Measured: 44 entries and 9 447 224 B of real content inside a 25 MiB partition, with `dtbo_a` identical to `dtbo_b`. Scan the overlay blobs for node labels and compatible strings before touching anything: these contain `ufshc`, `dwc3`, `ssusb` and `a600000`, so the overlays patch exactly the storage and USB nodes. The DT the working kernel runs with is therefore *the selected base DTB plus these overlays*, which is why neutralising the target slot's `dtbo` changes a custom image's failure mode, and why it is a diagnostic probe rather than a fix.

### How the chain that boots hands over

- The boot ramdisk's `/init` is a **first-stage** init. On a Magisk-patched image it is `magiskinit`, recognisable from its strings (`/system_root`, `Cannot mount root partition, abort`, `magiskinit::ffi::MagiskInit::mount_overlay`, `First Stage Init`): it mounts according to the **vendor** ramdisk's `first_stage_ramdisk/fstab.qcom`, then switch_roots and execs the real init. It loads **no** modules, so it is not a model for a custom init - only its handover shape is.
- That fstab is the map of the working layout: `system`, `system_ext`, `product`, `vendor`, `vendor_dlkm`, `odm` are logical partitions inside `super` (device-mapper `dm-N`), `/metadata` and `misc` are plain partitions, `userdata` is f2fs with file encryption, and it names the two controller paths a custom userspace may need: `sysfs_path=/sys/devices/platform/soc/1d84000.ufshc` for UFS and `/devices/platform/soc/*.ssusb/*.dwc3/*` for USB.
- Who brings USB up, from the live `dmesg` of a good boot: the controller is `a600000.dwc3`, the vendor's OTG state machine drives it to `drd_state = peripheral`, and a *service* then builds the gadget. Loading the driver is not sufficient - the role switch and the gadget configuration are separate steps the custom init must perform itself.
- Module loading is internal to `init` (the vendor `.rc` files contain no `modprobe`/`insmod` directive), it reads `modules.load`, `modules.dep` and `modules.softdep` and runs in a child process with timeouts; the running system has ~500 entries in `/sys/module`. The difference from a hanging custom init is the mechanism, not the module list.

## EDL and fastboot safety

- Treat EDL/Firehose command success as untrusted until output, expected length, and readback digest agree. A transfer can reach 100% and still fail on the final protocol acknowledgement.
- If named partition enumeration is empty, do not guess partition names or write raw sectors. First acquire a fresh raw GPT read using the installed client's documented decimal `rs START COUNT FILE` syntax, validate GPT CRCs and LUN mapping, and stop if the raw read also blocks.
- Do not use blanket `rl`/`rf` for recovery snapshots. Full-LUN reads can exceed available disk and may include sensitive userdata.
- Do not use `/dev/zero` as a bounded EDL write source; its length is zero. Create an explicitly sized payload and preserve the original `misc` image.
- Do not use a fabricated GPT repair script or cross-LUN assumptions. Derive LBA, sector size, partition bounds, and both GPT copies from a fresh read.
- Transport for `05c6:9008`: it is a USB bulk device, not a serial line. The kernel driver that claims the interface (e.g. `qcserial`) blocks raw access, so exclude it before any protocol work. `sudo modprobe -r qcserial usb_wwan` is not durable (udev reloads it on the next enumeration) and neither is a `blacklist` line (it only suppresses alias autoload - the module came back): use `install qcserial /bin/true` plus the same for `usb_wwan` in `/etc/modprobe.d/`, and confirm with `lsmod | grep -E 'qcserial|usb_wwan'` immediately before the run, because a test started while the driver is bound fails with `Resource busy` before it measures anything. The `/dev/ttyUSB*` node the same driver creates is a read-side diagnostic fallback at best: it re-frames and buffers, so it cannot be trusted to deliver one write as one transfer. Packet-level detail for this boot ROM lives with the protocol-recovery skill, not here.
- A freshly entered EDL can look mute. The boot ROM announces itself at USB enumeration time, so a reader opened seconds later has already missed it. Get the interface unbind done first, then be listening *while* the device enumerates (replug, or a fresh `adb reboot edl`) with the reader already running and re-globbing the device path. Also confirm nothing on the host holds the port — a modem manager daemon that opens the tty produces the same silence from a healthy device. Expect transport asymmetry on the same device: it can read as silent over raw libusb and as chatty over the driver's tty, because announcements are replies plus a burst the kernel buffer may already have drained; check whether a read was pending at enumeration before concluding the device changed.
- Do not try to un-wedge a session by poking packets. A session that errored without completing answers every packet, whatever it is, with the same refusal, and per-packet retries just burn cycles. Treat any client that reports success without printing the raw reply as untrustworthy. Measured on a device wedged this way: zeroing the BCB command field in `misc` (readback-verified), `RESET_REQ`, leaving and re-entering EDL, and switching between the tty and raw libusb all left the identical answer. So a fresh entry into EDL is not evidence that the state was cleared. Measured: the identical refusal also survives a genuine power-off cold boot, which retires the "RAM that survives warm resets" explanation - a message that invariant is that ROM's normal greeting to a host it does not consider a session partner. Stop treating it as residue, record the avenue as blocked, and say so plainly instead of probing further: nothing the host can read or write changes the outcome, and an exploit delivered inside the session is blocked by the same wall.
- The fastboot route into EDL is not universal: `fastboot oem edl`, `fastboot oem reboot-edl`, `fastboot oem enable-edl` and `fastboot reboot edl` are all absent on some ABL builds (they answer `unknown command` / `unknown reboot target`). The working route is then Android with `adb reboot edl`. Probe these bounded and with the active slot already set to the bootable one, so that a normal reboot cannot land on the slot under test.
- Read slot state with `timeout fastboot getvar current-slot`, `slot-successful:a|b`, `slot-unbootable:a|b`, `slot-retry-count:a|b`, plus `unlocked` and `secure`, before and after every attempt. A retry counter still at maximum with A successful means the bootloader has recorded no failure for B, so an earlier failed attempt did not necessarily consume a retry — do not present the counter as proof that nothing was tried.

## Persistent evidence channels

Choose the channel before the experiment, and verify the channel works before trusting a negative result from it.

| Channel | Works when | Check first |
|---|---|---|
| `pstore` / ramoops | The DT node has a fixed `reg` that actually reserves memory | `ls -la /sys/fs/pstore` on the booted Android; a node with `size`/`pmsg-size` but no `reg` reserves nothing |
| `rawdump` / `logdump` | The bootloader wrote them on this unit | Hash or scan them: both can be entirely zero, leaving no XBL/ABL log to read |
| Breadcrumb journal in a spare partition | The kernel reaches the storage driver and the custom init can write | Identify the partition by size under `/sys/class/block/*/size`, back it up, then `mknod` and `dd conv=notrunc`; flush the journal in RAM until the device node exists, since storage comes up after the first breadcrumbs |
| USB timeline on the host | Always | Poll `lsusb` and the interface list with timestamps; the phone's VID/PID changes with its mode (Android, fastboot, EDL, gadget), so match the ID you expect per phase |

A failed attempt with no evidence at all is still a result, but only if the absence is explained: state which channel was checked, what it contained, and whether the payload could even reach it.

## Ramdisk forensics: containers, header fields, inventory

### Container magics (record the first 4 bytes of every ramdisk)

| First bytes | Format | How to produce it |
|---|---|---|
| `1f 8b 08 00` | gzip | `gzip -9` |
| `02 21 4c 18` | LZ4 **legacy** (`0x184C2102`) | `lz4 -l` — what Android's boot ramdisk and Qualcomm's vendor ramdisk use |
| `04 22 4d 18` | LZ4 **frame** (`0x184D2204`) | `lz4` (CLI default) |
| `30 37 30 37` (`070701`) | uncompressed cpio newc | none |

Legacy and frame are different magics: "it is LZ4" does not say which. Decompress either with `lz4 -d -f <ram> <out.cpio>`. A gzip initramfs is a legitimate kernel format (the kernel unpacks it in QEMU with the same kernel binary), so a format mismatch is a hypothesis to test by repacking, never a verdict on its own: a candidate repacked to the device's exact format can still fail, and that result retires the container and moves the fault into the ramdisk's contents.

### Header field map for a single-field patch (boot v4, page 4096)

| Offset | Size | Field |
|---|---|---|
| `0x00` | 8 | magic `ANDROID!` |
| `0x08` | 4 | kernel_size |
| `0x0C` | 4 | ramdisk_size |
| `0x10` | 4 | os_version |
| `0x14` | 4 | header_size (1584) |
| `0x28` | 4 | header_version (4) |
| `0x2C` | 1536 | cmdline, NUL-padded (`44..1579`) |
| `1580` | 4 | signature_size |

Layout: header padded up to the page size, then kernel, ramdisk, signature block — each padded up to `page_size`. To isolate one field, copy the image that boots, overwrite only that region, then assert the diff: print the differing byte count and offset range, and compare the kernel region separately. A surgical edit on a proven base makes the outcome a single-variable fact; a rebuild does not.

### Why the reference ramdisk's `/init` may be unreachable as a model

Android's boot ramdisk `/init` is a **static AArch64 ELF** (hundreds of KB) and the ramdisk carries no `/bin/sh`, no busybox and no libc. A candidate whose `/init` is a `#!/bin/sh` script depends on an interpreter chain the reference does not have at all — a real structural difference, and worth verifying (shebang, `/bin/sh` target, loader path, every `NEEDED`) before blaming it, since qemu-user can prove the chain end to end without booting anything.

### The exhaustive inventory, and what to do with it

Parse both archives completely and split into: only in the reference, only in the candidate, same path with different content, same path with different metadata. Then rank by impact on the kernel's boot path:

1. `/init` and its interpreter/library chain — decides whether the kernel obtains a PID 1 at all.
2. Device nodes and init tooling (`/dev/console`, `/dev/kmsg`, `/dev/pmsg0`, module loaders) — decides whether the init can talk to anything.
3. Paths only Android's own init reads (Magisk scaffolding `.backup/` and `overlay.d/`, `first_stage_ramdisk/`, `system/etc/ramdisk/`, `debug_ramdisk/`, `metadata/`, `mnt/`) — the kernel never looks at these, so they cannot be the cause of a failed boot.
4. The candidate's own additions (extra modules, static device nodes, busybox, libraries) — inert with respect to the kernel; note that a reference Android ramdisk may not ship *any* device nodes.

### What a Qualcomm vendor ramdisk actually contains

Measured on an SM8450 Android 12 device — `vendor_ramdisk00` unpacks to 341 entries:

```text
lib/modules/*.ko                327 flat modules (no version subdirectory)
lib/modules/modules.load         98 entries - the first-stage list Android itself uses
lib/modules/modules.load.recovery 329 entries - the larger variant for a boot that is
                                  not a normal Android boot; this is the one a custom
                                  ramdisk should follow
lib/modules/modules.dep, modules.alias, modules.softdep, modules.blocklist
first_stage_ramdisk/fstab.qcom
avb/*.avbpubkey
```

No `/init` and no `usb_f_*`/`libcomposite` (those are built in, so a configfs gadget needs no module of its own). The files are present at runtime in the merged initramfs, so a custom init can read the device's own dependency data instead of carrying a hand-written module list.

`modules.softdep` is the trap. Measured lines:

```text
softdep smem      pre: qcom_hwspinlock
softdep dwc3_msm  pre: phy-generic phy-msm-snps-hs phy-msm-ssusb-qmp eud
```

A curated list that omits `qcom_hwspinlock`, `phy-generic` and `eud` cannot load `smem` (and UFS then never comes up) or `dwc3_msm` (and the USB gadget never comes up) — while the init itself runs and reports no error. Diagnostic signature of exactly that state: no USB for the whole window, no panic (helpfully the init does not die), no return to fastboot, no retry consumed, and an empty breadcrumb because the UFS the journal needs is the subsystem that failed. In QEMU it is invisible, because QEMU has no UFS and no UDC. Resolve the order from `modules.dep` plus `modules.softdep` (or hand the softdeps to the loader if it understands them), and journal each module's `rc` on the first page of the breadcrumb so the load outcome is evidence rather than an assumption.

## The single-variable ledger: variables already retired

Keep this per device class, not per session: it is the list of hypotheses that are spent, and it is what stops a later session from re-spending them.

| Variable | Single-variable test | Outcome |
|---|---|---|
| cmdline injected into a working image | proven image, only bytes 44..1579 replaced | boots - retired |
| `signature_size=0` | proven image + a 4096-byte signature block, everything else identical | fails - retired |
| ramdisk size (11 MiB, then 1 MiB, i.e. below the reference's own) | minimal ramdisk | fails - retired |
| ramdisk compression (gzip, then LZ4 frame) | repack to the device's LZ4 legacy | fails - retired |
| image length and trailing AVB bytes | candidate in the proven container at full partition length with the original tail at the same offsets | fails - retired |
| the candidate's whole ramdisk *content* | reference content unchanged, repacked by the candidate's own tool | boots - packing retired, fault is the content |
| `/init` as a shell script with an interpreter chain | static no-interpreter PID 1 that never returns | fails - retired as the primary cause |

An empty evidence channel is a result, and belongs in the ledger: on one unit `logdump` (512 MiB) and `rawdump` (256 MiB) measured entirely zero with no extractable strings and `pstore` was empty, so no bootloader or kernel log exists to read there and every conclusion has to come from the host-visible USB timeline or from something the custom init writes itself.

## Proving your own packing pipeline (content vs tooling)

When every image that fails was built by your own packing code and the only image that boots was built by someone else's tooling, content and pipeline are confounded: no content edit can distinguish them. Retire the pipeline first — it is cheap and upstream of everything else.

1. Take the reference *content* byte for byte: the cpio of the image that boots, including its `/init`.
2. Repack it with your own tool — your cpio writer, your compressor at the device's container format.
3. Rebuild in the proven container: same kernel, same signature block, full partition length, original trailing bytes preserved at the same absolute offsets.
4. Assert before flashing: decompress your output and compare to the input; verify the kernel region is byte-identical and the total length equals the partition.
5. Boot it. Boots => the pipeline is sound and the fault is in the content. Fails => the fault is in the packing, and every earlier content experiment is uninterpretable.

A ramdisk the target kernel unpacked under QEMU does not substitute for this: QEMU proves the kernel accepts your archive, not that your image is accepted on the device.

## Experiment record

For each hardware attempt, save:

```text
hypothesis:
source image sha256:
target partitions:
readback sha256:
current/active slot:
slot retry/success/unbootable state:
BCB/misc first bytes:
USB timeline:
pstore/rawdump/logdump result:
reset/recovery result:
new evidence:
next falsifiable hypothesis:
```
