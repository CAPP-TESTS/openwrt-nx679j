# Diagnosing a silent boot on a Qualcomm Android A/B device

Use this when a flashed image produces *nothing*: no USB of any kind, no pstore, no journal on the unmount-used partition, screen frozen on the bootloader's logo.

## 1. What the host can and cannot see

| Observation | What it proves | What it does NOT prove |
|---|---|---|
| nothing on USB for the whole window | the host saw no transport | kernel entered, init ran, panic, hang — all look identical |
| bootloader logo frozen (one frame) | the bootloader drew its splash | kernel ran; a kernel panic with `CONFIG_PANIC_TIMEOUT=-1` looks the same |
| logo + unlocked warning repeating ~10 s | **the kernel ran your `/init`** | anything about the driver stack under test |
| return to fastboot | the bootloader reported a failure | where it failed |
| `slot-retry-count` unchanged | **no reboot happened** | that the kernel did not run, and not that it did not panic |

A dump partition is not a channel until proven: `logdump` was 512 MiB of zeros on the unit measured here (the bootloader logs nowhere), and `pstore` was empty because the DT's ramoops node carried no `reg`.

## 2. Instrument before you theorise: the heartbeat init

Replace `/init` with a **static** aarch64 binary whose entire behaviour is: log one line to `/dev/kmsg` (with an `mknod` fallback for ramdisks that ship an empty `/dev`), call `reboot(2)`, and never return (no `ret`, no `exit`/`_exit`/`abort` on any path).

- Build it `-static -Os -s`; prove it with `readelf` (`Type=EXEC`, `PT_INTERP` absent, no dynamic section), by diffing `.text` against a fresh unstripped rebuild (all differing bytes confined to ELF header/build-id/section names), and by disassembling `main` to show no `ret` and no branch to `exit`.
- Prove the kernel takes it, offline, with the **device's own kernel** in full-system QEMU and `-no-reboot`: `reboot: Restarting system` plus the probe's own `T1` kmsg lines turns "userspace was reached" into an assertion from real kernel output.
- Flash it twice: once inside the **reference** ramdisk (the content known to boot) and once inside **your** ramdisk. Reference reached + yours reached ⇒ your ramdisk content is sound and the fault is in what your real init does. Reference reached + yours not ⇒ the fault is in the content, and you now have a working readout to bisect it with.

Readout protocol for the human: *logo + unlocked warning repeating every ~10 s = userspace was reached; one frozen logo = it was not.* Say that sentence before the flash, not after.

A reboot loop here does **not** self-terminate: the retry counter is not consumed on every unit, so ask for volume-down → fastboot and plan one such request per cycle. Killing a host-side monitor with `pkill -f` whose pattern also appears in your own command line kills your shell; bracket the pattern or kill by PID.

## 3. Map the platform from the device, not from theory

- `zcat /proc/config.gz` — `CONFIG_RD_*`, `CONFIG_BINFMT_SCRIPT`, `CONFIG_BLK_DEV_INITRD`, `CONFIG_INITRAMFS_SOURCE`, `CONFIG_PANIC_TIMEOUT`, `CONFIG_DEVTMPFS`.
- `cat /proc/cmdline` — the arguments the bootloader actually passes (a copied cmdline is usually harmless: grafting the custom one onto the proven image still booted).
- The **first-stage fstab** in the vendor ramdisk (`first_stage_ramdisk/fstab.qcom`) is the platform map: which partitions are logical inside `super` (mounted as `dm-*` with `first_stage_mount`), which are physical, the filesystem of `/data`, and two absolute paths worth more than any guess — the UFS controller's sysfs node to wait for (`sysfs_path=/sys/devices/platform/soc/…ufshc`) and the USB node where a UDC must appear (`/devices/platform/soc/*.ssusb/*.dwc3/xhci-hcd.*`).
- `/vendor/lib/modules/modules.load` on the running system is the *second-stage* list (hundreds of entries), different from the vendor ramdisk's first-stage `modules.load`. Read both before writing any module list.

## 4. Hypotheses already retired on Qualcomm GKI

Do not re-spend cycles on these; each has one cheap test if it must be re-checked.

| Hypothesis | Cheapest test | Verdict |
|---|---|---|
| cmdline is fatal | graft the custom cmdline into the proven image (bytes 44..1579 only) | booted ⇒ not the cause |
| `signature_size=0` blocks the boot | build the same content with a 4096-byte signature field | booted/failed regardless |
| ramdisk too large | same ramdisk at ~1 MiB | not the cause |
| gzip vs LZ4 legacy | repack the same content to `lz4 -l` | both fail ⇒ not the container |
| script `/init` cannot execute | `CONFIG_BINFMT_SCRIPT=y` + QEMU runs it | not the cause |
| PID 1 exits → panic | static init that cannot return | not the cause |
| missing module softdeps | ship `phy-generic`, `eud`, `qcom_hwspinlock` | necessary for UFS/USB, not sufficient for reaching userspace |
| image not padded to partition length / stale AVB tail | repack in the proven container, full length, original tail at the same offsets, diff asserted outside the ramdisk region | not the cause |
| my packing tooling is broken | repack the **reference content byte for byte** with my own tool | booted ⇒ tooling retired, fault is content |

What survives in this class: the kernel **does** reach userspace, and the fault is either the ramdisk's content or — most often — what the init *does* with it. See the PID-1 rule in SKILL.md.

## 5. Analysis pitfalls that cost real time

- A narrow `strings | grep` looks like "the binary contains no such strings": count first (`strings -n 4 <f> | wc -l`) and broaden the pattern before concluding. Reading a first-stage init this way is how you learn whether it loads modules at all.
- A subagent's build report can cite files that do not exist and pipelines can mask a crash as `EXIT=0`; verify the artifact's presence and hash before flashing, and read the log.
- When two runs disagree, prefer the measurement taken with a readback whose digest was checked before any reboot.

## 6. The endgame: serve the diagnostics over the transport you built

With the gadget up (NCM at `10.0.0.1/24`, pinged from the host 3/3 `ttl=64`), the journal no longer needs a reboot to be read. Add a **read-only** TCP child to the init that writes a text snapshot to every client: banner; relay preconditions (`iface=usb0 present=1 ipv4=…`, `listen_ok=1`, `on_an_interface=yes`, `clients_accepted=…`, `uptime_s=…`); then `[1] KERNEL` (uname, version, cmdline, uptime, loadavg, capped meminfo), interface and UDC state, and `[8] THE BOOT JOURNAL` from the journal file, ending with `END OF DUMP - N bytes`. Measured on hardware: 53 KB in one snapshot, re-readable at will with the phone still running.

Pitfalls, all measured:
- Bind only to an address that is really on an interface. Some kernels accept a non-local `bind(2)` and then nothing can reach the socket (`wanted 10.0.0.1 is NOT on any interface` with the bind still succeeding); check the interface list, fall back to the wildcard, and publish the address `getsockname` returned.
- Give every client a deadline (30 s is plenty for one snapshot) or one stalled reader holds the relay.
- `nc 10.0.0.1 <port> > f` prints nothing and stays connected while the bytes are already on disk: the terminal looks idle because there is no EOF, not because there is no data. Check `wc -c f` / `head f`, and use `nc -N` (or `-w <s>`) to close on EOF. A client that hangs is not a client that received nothing.
- The USB netdev name is derived from the host port path and changes between enumerations: re-detect it each time from `/sys/class/net/*/device/driver` (matching `cdc_ncm`/`cdc_ether`) rather than reusing the previous session's name, and give the user one line that adds the address, brings the link up, fetches and prints the byte count.
- Keep the service in a child so PID 1's syscall set stays at `clock_gettime, clone, execve, exit_group, kill, nanosleep, reboot, wait4` - no `socket`, no `open`, no `read`/`write` - which is what makes "PID 1 cannot block" structural instead of a promise.

## 7. Answered on the unit measured, and what remains

- **What blocks is the call, not a particular module.** `finit_module(2)` runs the module's `probe` in the caller's context, and a probe waiting on a remote processor, a firmware load, an I2C device or a clock blocks the caller for ever. Measured by disassembling the device's own 327 `.ko` and walking the call graph from `init`/`probe`: 75 reach an unbounded wait (`mutex_lock`, `wait_for_completion`, `destroy_workqueue`, `cancel_work_sync`, `flush_workqueue`, `request_firmware`), 20 of them **also inside the curated `modules.load`** — so "load only the curated 98" is necessary and not sufficient. The durable answer is the mechanism: one child per module with a hard deadline, killed on expiry, carrying on, journaling `TIMEOUT-KILLED` next to the per-module rc — proven in QEMU with a FIFO substituted for a `.ko` (the child never returned, PID 1 killed it at 15 s, continued, and still rebooted on schedule).
- **Still open:** whether a `super`-based logical partition can be replaced by a plain ext4 rootfs on a spare partition, and which subset of the vendor modules is genuinely needed now that the storage and USB stacks are known to be built in.
