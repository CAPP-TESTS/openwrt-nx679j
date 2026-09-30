
PART 2 - EVERY BLOCKING POINT IN v6 (candidate-init-v6.c, line numbers exact)

1. line 675  fd = open(path, O_RDONLY)   the .ko file itself: initramfs RAM, safe.
2. line 681  syscall(SYS_finit_module, fd, "", 0)
   THE ONE. Runs the driver probe inside PID 1 (see mechanism above).
   QEMU: probe never called, rc=0 instantly. Hardware: 327 modules are offered
   to this loop (modules.load.recovery, 329 lines); measured 75 of them have an
   unbounded wait reachable from init/probe (objdump call graph, see PART 4).
   line 693 syscall(SYS_init_module, ...) is the fallback, same class.
3. line 749 and line 994  gadget_cycle(900, 5) / gadget_cycle(901, 5)
   Not a hang but worse than some: a fifteen minute wait for a UDC with no
   journal write in between. journal_flush() is only reached after the module
   chain (line 1257), so the rawdump stays all zeros and the screen stays on
   the logo for 15 minutes. This alone explains the measured "no journal, no
   USB, frozen logo" without needing any kernel-level hang.
4. line 1071 fd = open("/dev/rd", O_WRONLY) and line 1088 write(fd, buf, 4096)
   journal_flush(): real block I/O. If ufs_qcom probed but the link is dead,
   the request never completes (SCSI error handler retries). QEMU: /dev/rd
   does not exist, open fails instantly, nothing happens.
5. line 1099 do_mount() -> line 1104 mount(src, tgt, type, 0, NULL)
   lines 1221-1224 mount proc/sysfs/configfs/pstore. Pseudo filesystems, no
   hardware underneath, so this class is the same in QEMU and on hardware -
   it is audited, not accused. (devtmpfs is not mounted: no CONFIG_DEVTMPFS.)
6. line 1198-1200 open /dev/kmsg, /dev/console (O_NONBLOCK) and every note()
   call that follows. O_NONBLOCK does not protect a tty whose write path
   busy-waits, and printk goes through console_unlock() in the caller's
   context. The device console is ttyMSM0 = msm_geni_serial, and
   msm_geni_serial is one of the 98 modules this same run loads: the moment it
   probes, PID 1's own logging writes start going through device hardware.
7. line 396-415 rf("/sys/class/udc/%s/state|current_speed") - reads that go
   down into the USB controller (MMIO) with the driver possibly stuck.
8. line 302-330 socket(AF_INET) + ioctl(SIOCSIFFLAGS|SIOCSIFADDR) on usb0 ->
   dev_open -> ndo_open -> gadget set_alt / endpoint enable. QEMU: no usb0,
   instant error.
9. safe but listed for completeness: line 251/274 opendir(/sys/class/udc,
   /sys/class/usb_role), line 527 opendir(/lib/modules), line 1023-1044
   opendir(/sys/class/block) + reads of size/dev.
10. line 1238 read /proc/cmdline, line 1136 read /proc/devices: kernel
   generated, safe.

THE EVIDENCE CHANNEL PROBLEM (this is half the diagnosis)
The only durable channel is journal_flush() to rawdump, and v6 calls it only
at line 1257 (after load_modules() returns), 1270, 1275 and 1287. Everything
before that exists only in RAM or in /dev/kmsg, and there is no console on the
phone. Therefore: any block before line 1257 produces exactly "rawdump all
zeros, logo frozen, no USB" - indistinguishable from a panic, which with
CONFIG_PANIC_TIMEOUT=-1 leaves no trace either. v6 could not have told us
anything, even if it had survived.
