# EXPERIMENT 20260917-init-v9 — THE ONE HARDWARE TEST

Image: `boot_b-init-v9.img`, **100663296 bytes**, sha256
`615b6aa1b319a4d4bc33683d236cdd31bd94073ebd077f0ab975ebe52089c21f`
(slot B only; same container, cmdline, kernel and DTB as the v7 probe that is
measured to boot and reach userspace, and as the v8 image; the only difference
is our ramdisk).

## The one new thing compared to v8

v8 answers "did the gadget bind?" on the phone's screen.  v9 keeps that and
adds a way to *read the boot while it is happening*:

```
nc 10.0.0.1 9999
```

No login, no request, no write: the relay sends one text snapshot and closes
the connection, so `nc` exits by itself.  Reading it never changes anything on
the phone (verified: the relay opens no file for writing and writes only to
the socket and the console).

## Before flashing

Nothing new.  As in v8: the UFS node watched is
`/sys/devices/platform/soc/1d84000.ufshc`, the UDC is expected under the
`*.ssusb/*.dwc3` path, and the relay reports whatever it finds.

## What the user watches, in order

1. **The screen.**  RedMagic logo STEADY = good sign (a flash cycle means NOT
   successful: 8 s period = UFS up, gadget did not bind; 30 s = no rawdump
   partition; 60 s = PID 1 had to kill an operation).
2. **The host.**  A USB device `18d1:4ee7` should appear within about a
   minute.  If it does, the NCM link is up and the phone is at `10.0.0.1/24`
   on `usb0`.
3. **The relay.**  On the PC, bring the host side of the link into the same
   /24 and read the dump:

   ```bash
   ip -br link                       # find the new usb-ish interface, e.g. enp0s20u1
   sudo ip addr add 10.0.0.2/24 dev enp0s20u1
   sudo ip link set enp0s20u1 up
   nc 10.0.0.1 9999 > /tmp/nx679j-dump.txt
   less /tmp/nx679j-dump.txt
   ```

   (Any netcat works — `busybox nc`, `ncat`, `telnet 10.0.0.1 9999`,
   `socat - TCP:10.0.0.1:9999`.  Reconnecting gives a fresh snapshot; the
   relay never stops listening.)

## How to read the dump

* the first block is the relay itself: `port=9999`, `bind=10.0.0.1` (or
  `bind=0.0.0.0 bind_all=1` if the address was not on the interface yet —
  which still works, and is stated so you are not misled), `iface=usb0
  present=1 ipv4=10.0.0.1`, how many clients it has served, and the client
  deadline (30 s);
* `[1] KERNEL` = cmdline, kernel release, uptime, meminfo (what the phone
  really booted with);
* `[2] NETWORK INTERFACES` = every interface with its real address/MTU/flags
  (`usb0` should be there with `10.0.0.1`);
* `[3] USB GADGET` = `/sys/class/udc` contents (`a600000.dwc3 state=configured
  current_speed=…`), `usb_role` nodes, and the configfs gadget
  (`idVendor=0x18d1 idProduct=0x4ee7 functions=ncm.usb0`).  **This is the v8
  question answered as text instead of as a logo.**
* `[4] STORAGE` = block devices, `/dev/block/by-name`, mounts, the UFS host
  node, and whether `/dev/rd` (the rawdump node) exists;
* `[5] PSTORE`, `[6]` loaded modules, `[7]` module files;
* `[8] THE BOOT JOURNAL` = the whole journal, exactly the text that is also
  written to the rawdump partition (so you can compare the two, and
  `hash_fnv1a64_full` lets you prove they are the same bytes);
* `[9] RELAY SELF-CHECK` = the relay's own pid/ppid, its mount namespace vs
  PID 1's, the mount table, and raw errnos — this is what makes "absent"
  claims in the sections above provable rather than assumed;
* `[10] END OF DUMP` with the byte count: if you do not see it, the transfer
  was cut short (the snapshot is written in one go, so a short dump means the
  connection died, not that the data was wrong).

## What each outcome means

| what you see | what it means |
|---|---|
| logo steady + `18d1:4ee7` + the dump arrives | the success case: the gadget bound, the kernel held, and the journal is readable live.  **This is the result this experiment is built to produce.** |
| logo steady + `18d1:4ee7`, **no dump** | the gadget bound but nothing listens: check the host address (`ip addr` on the usb interface), then whether the dump says `bind=…` at all.  If `nc` says "connection refused", the relay is not in that boot: look for `v9-relay-started` in the journal |
| logo cycles (8 s) | the gadget did not bind (same class as v8).  The journal holds the per-step detail and, once Android is back, can be read from the rawdump partition with `dd` |
| logo cycles, ~30 s | the rawdump partition was not found: the journal states it (`first journal write: NO rawdump partition found`) |
| logo cycles, ~60 s | PID 1 had to kill an operation that overran its deadline (the worst class) |
| logo steady, no USB for 3 min | userspace not reached or the reboot path is blocked; nothing can be concluded from that boot |

## What to capture for the next step

1. the whole dump file (`nc 10.0.0.1 9999 > dump.txt`) — it is the journal,
   the gadget state and the relay's own world in one text file;
2. the PC side: `dmesg | tail -50` (NCM/cdc_ether messages, the USB ids, the
   interface name) and `ip -br addr`;
3. the relay's console lines from the serial console if attached
   (`nx679j-relay-v9: READ-ONLY relay up: nc 10.0.0.1 9999 …`).

To exit a cycle: press and hold Volume Down (the bootloader), as with the v7
probe.  The A/B counters are not consumed on this unit, so the phone will not
stop by itself.
