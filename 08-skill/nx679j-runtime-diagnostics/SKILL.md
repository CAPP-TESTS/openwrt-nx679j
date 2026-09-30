---
name: nx679j-runtime-diagnostics
description: "Use when capturing NX679J logs. Stream read-only to host."
version: 0.1.0
author: "user, Hermes Agent"
license: MIT
platforms: [linux]
metadata:
  hermes:
    tags: [nx679j, openwrt, drm, kernel-logs]
    related_skills: [nx679j-openwrt, linux-kernel-crash-debug]
---

# NX679J Read-Only Runtime Diagnostics

Use this workflow to preserve kernel evidence for short KMS experiments and inspect current DRM framebuffer state. It is a passive capture procedure: it does not start, stop, or reconfigure the display stack.

## When to Use

- Capture kernel messages before a brief KMS test that may reboot the phone.
- Read current DRM plane/framebuffer format and pitch without changing scanout.
- Establish whether a reboot left pstore evidence.

Do not use it to initiate a modeset, kill a DRM master, enable tracing, change debugfs knobs, or trigger a reboot.

## Prerequisites

- Read the NX679J `STATO-ATTUALE.md` before each device step; use the latest display hold/master state, not old chat context.
- Use the project's disabled host-key check only for the isolated USB gadget because Dropbear rotates its key per boot; do not reuse it for general hosts.
- Store captures on the host. The device rawdump and rootfs are volatile and rawdump is cleared at boot.
- Last recorded runtime baseline: vendor Linux 5.10.66, OpenWrt 25.12.5, BusyBox 1.37.0. Confirm if the image changes.

## Procedure

1. **Create a host capture directory and baseline.** Run from the host; every redirection below is local:
   ```sh
   RUN="$HOME/nx679j-diag-$(date -u +%Y%m%dT%H%M%SZ)"
   mkdir -p "$RUN"
   ssh -T -i ~/.ssh/nx679j_key -o IdentitiesOnly=yes \
     -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
     root@10.0.0.1 'printf "boot_id="; cat /proc/sys/kernel/random/boot_id; cat /proc/uptime; dmesg' \
     >"$RUN/before.txt" 2>"$RUN/before.ssh.err"
   ```
   Plain BusyBox `dmesg` is a non-clearing snapshot. Never add `-c`.

2. **Start the non-clearing kernel stream before the test.** In a host terminal, let this run; start the already-approved test from another terminal. Stop the reader with Ctrl-C on the host after a successful test. If the phone reboots, SSH should close and the host file remains:
   ```sh
   ssh -T -i ~/.ssh/nx679j_key -o IdentitiesOnly=yes \
     -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
     root@10.0.0.1 'cat /dev/kmsg' \
     >"$RUN/kmsg.raw" 2>"$RUN/kmsg.ssh.err"
   ```
   `/dev/kmsg` begins with the oldest retained record and then blocks for new records. Its records include sequence number and monotonic timestamp; allow the initial backlog to drain before marking the test start. Add host wall-clock start/end markers in a separate file. Do not inject markers by writing to `/dev/kmsg`.

3. **Optionally capture OpenWrt's human-readable log stream in a second host terminal.** `logread -f` belongs to OpenWrt `ubox`/`logd`, not BusyBox `dmesg`:
   ```sh
   ssh -T -i ~/.ssh/nx679j_key -o IdentitiesOnly=yes \
     -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
     root@10.0.0.1 'logread -f' \
     >"$RUN/logread.txt" 2>"$RUN/logread.ssh.err"
   ```
   In a second host terminal, set `RUN` to the same host directory before starting the optional logread command. OpenWrt `logd` already holds the single-reader `/proc/kmsg` path; avoid a second reader there. Prefer `/dev/kmsg` for an independent kernel reader.

4. **Take read-only DRM snapshots only when debugfs is already mounted.** Save a pre-test and a post-test copy on the host:
   ```sh
   ssh -T -i ~/.ssh/nx679j_key -o IdentitiesOnly=yes \
     -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
     root@10.0.0.1 'for f in /sys/kernel/debug/dri/*/state /sys/kernel/debug/dri/*/framebuffer; do [ -r "$f" ] || continue; printf "\n=== %s ===\n" "$f"; cat "$f"; done' \
     >"$RUN/drm-before.txt" 2>"$RUN/drm-before.ssh.err"
   ```
   In `state`, identify the active plane's `fb=<id>`; use `framebuffer[<id>]` and its `format`, `modifier`, `pitch[]`, and `offset[]`. The framebuffer list alone includes inactive objects. If `state` is absent, do not infer which listed framebuffer is active. Debugfs is not a stable ABI and vendor kernels can omit files.

5. **After a reboot, reconnect and preserve the new boot's metadata and any existing pstore records.** Do not delete pstore files:
   ```sh
   ssh -T -i ~/.ssh/nx679j_key -o IdentitiesOnly=yes \
     -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
     root@10.0.0.1 'printf "boot_id="; cat /proc/sys/kernel/random/boot_id; cat /proc/uptime; dmesg; for f in /sys/fs/pstore/*; do [ -r "$f" ] || continue; printf "\n=== %s ===\n" "$f"; cat "$f"; done' \
     >"$RUN/after-reboot.txt" 2>"$RUN/after-reboot.ssh.err"
   ```
   A changed boot ID and reset uptime confirm a new boot; post-boot `dmesg` cannot recover the previous boot's lost ring.

## Pitfalls

- The project BusyBox 1.37.0 `dmesg` supports `-c`, `-r`, `-n`, and `-s`; it has no `-w`/`-W`. `-c` explicitly clears after printing. The util-linux manual documents `-w`/`-W` for its separate implementation: https://man7.org/linux/man-pages/man1/dmesg.1.html.
- `/dev/kmsg` reads are independent and non-clearing, but an overrun returns `EPIPE`; sequence gaps mean the capture is incomplete. A simple stream reader may stop on a read error, so preserve stderr and SSH exit status.
- OpenWrt `logread -f` follows logd's separate bounded RAM buffer; it is not durable until redirected on the host.
- `/proc/kmsg` is a single-reader stream. Do not open it directly beside logd.
- DRM `state` is a point-in-time dump and takes DRM modeset locks. Do not tight-poll it during a 16 ms flip loop; a read can perturb timing. Do not write debugfs controls or start tracing in this workflow.
- Pstore/ramoops survives only if preconfigured with persistent RAM. Read existing files; do not configure or unlink anything. An abrupt hardware reset may leave no pstore panic record.
- Never stop the currently held DRM master or launch a second master for diagnostics. Keep touch and the existing display setup unchanged.

## Verification

- Confirm host files contain the pre-test snapshot, live raw `/dev/kmsg`, host start/end markers, SSH stderr/exit status, and DRM snapshots.
- Check `/dev/kmsg` sequence numbers for gaps and retain monotonic timestamps; device wall clock may be unsynchronized.
- After reboot, compare boot ID/uptime and read any existing pstore data before another reboot.
- If the stream ends without a panic/oops or reset-reason record, report the reboot cause as undetermined; an SSH disconnect alone is not causal evidence.

## Sources

- OpenWrt v25.12.5 BusyBox package: https://raw.githubusercontent.com/openwrt/openwrt/v25.12.5/package/utils/busybox/Makefile
- BusyBox 1.37.0 source archive: https://busybox.net/downloads/busybox-1.37.0.tar.bz2
- Linux v5.10 `/dev/kmsg` ABI: https://raw.githubusercontent.com/torvalds/linux/v5.10/Documentation/ABI/testing/dev-kmsg
- OpenWrt v25.12.5 ubox source pin: https://raw.githubusercontent.com/openwrt/openwrt/v25.12.5/package/system/ubox/Makefile
- OpenWrt ubox `logread`/`logd`: https://raw.githubusercontent.com/openwrt/ubox/6f78fa496bf36c55864a41e353df7d13f04b1077/log/logread.c and https://raw.githubusercontent.com/openwrt/ubox/6f78fa496bf36c55864a41e353df7d13f04b1077/log/syslog.c
- Linux v5.10 DRM atomic/framebuffer debugfs: https://raw.githubusercontent.com/torvalds/linux/v5.10/drivers/gpu/drm/drm_atomic.c and https://raw.githubusercontent.com/torvalds/linux/v5.10/drivers/gpu/drm/drm_framebuffer.c
- Debugfs ABI status: https://docs.kernel.org/filesystems/debugfs.html
- Linux v5.10 ramoops: https://kernel.org/doc/html/v5.10/admin-guide/ramoops.html
