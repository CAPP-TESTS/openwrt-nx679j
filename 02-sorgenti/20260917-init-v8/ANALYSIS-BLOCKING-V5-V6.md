
PART 1 - THE STRUCTURAL FACT (verified locally from the running system's own bytes)

The /init that works on this device is not Android's init: it is magiskinit, a
first stage that mounts partitions and hands over. Extracted from the Magisk
android ramdisk we already have locally
(probe-v7-sonda/inputs/magisk-android-ramdisk.cpio, member "init", sha256
8e26e33c3db284823f5d083588df26781d032ecd9818b8a116490d2f488f75dd,
statically linked, stripped) the string counts are:

    finit_module 0        init_module 0        modules.load 0
    modules.dep 0        /lib/modules 0        insmod 0     modprobe 0

and the strings it does contain are the ones already quoted from the device:
/system_root, Mounting system_root, Cannot mount root partition, abort,
/first_stage_ramdisk, /first_stage_ramdisk/sdcard.

Our init v6 contains and uses exactly the opposite set: /lib/modules/
modules.load, modules.load.recovery, modules.softdep, modules.blocklist and
finit_module/init_module. v5 uses busybox insmod (which is finit_module).

So the working boot never runs a module load inside PID 1. That is the
structural difference, and it is the one that matters, because of how
finit_module(2) works on a GKI 5.10 kernel:

THE MECHANISM (why one bad probe is enough)

finit_module(2) does not just map code. In kernel/module.c it runs
load_module() and then do_init_module(), and do_init_module() calls
do_one_initcall(mod->init) before the syscall returns. For a GKI module whose
init is module_platform_driver(...), mod->init is
platform_driver_register(), which inside the same call chain reaches
driver_register -> bus_add_driver -> driver_attach -> __driver_attach ->
driver_probe_device. driver_probe_device() only schedules an asynchronous
probe if the driver asked for it (PROBE_PREFER_ASYNCHRONOUS, or the
driver_async_probe= kernel parameter); otherwise it calls really_probe() and
the driver's probe() function directly, in the caller's context.

The caller's context here is PID 1, inside the finit_module(2) syscall. The
driver probe therefore runs with PID 1 as its thread, and any wait inside it
that never completes is a permanent, silent freeze of PID 1: no panic (so with
CONFIG_PANIC_TIMEOUT=-1 there is no restart), no journal, no USB, nothing on
the screen except the still logo. That matches every measurement on hardware.

WHY QEMU NEVER SHOWS IT: under qemu-system-aarch64 -machine virt the device
tree has none of this platform's compatibles (qcom,ufshc, qcom,dwc-usb3-msm,
qcom,glink-smem, ...), so the platform driver never matches a device and
probe() is never called at all. finit_module returns rc=0 in microseconds.
That is exactly what our own QEMU run of v6 showed: 294 of 304 modules
"loaded" with rc=0. On hardware the same call runs the matching probe.

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

PART 3 - EVERY BLOCKING POINT IN v5 (candidate-init-v5.sh, line numbers exact)

v5 is a shell script, so PID 1 is busybox sh. It blocks in a second way: the
shell waits (wait4) for every command it starts, so a child that is stuck in
the kernel is a stuck PID 1 too.

1. line 161  out=$($INSMOD "$f" 2>&1)  inside the loop of line 118-123
   busybox insmod = finit_module(2). Same mechanism as v6 item 2, same class
   of driver probe, and it is the *first* thing that can freeze this init.
2. line 87   dd if="$J" of="$RD" bs=4096 count=1 conv=notrunc
   line 88   $BB sync
   Block device I/O to rawdump: same failure class as v6 item 4.
3. line 57-60  log(): printf >> $J, then printf > /dev/kmsg, > /dev/pmsg0,
   > /dev/console. Three device opens/writes per line, on the console path.
   /dev/pmsg0 is a node shipped in our ramdisk with a fixed major:minor; if
   ramoops is not registered the open fails silently and nothing survives.
4. line 93-105  six mounts: tmpfs, proc, sysfs, devtmpfs, configfs, pstore.
   devtmpfs fails instantly (the kernel has no CONFIG_DEVTMPFS) - which is why
   the whole design needed the mknod path at line 69-76.
5. line 69-76  find_rawdump: cat $d/size, cat $d/dev, $BB mknod.
6. work done by a child the shell waits for, each one a possible freeze:
   line 107 cat /proc/cmdline, line 108 which insmod, line 174 cat
   /proc/partitions, line 191 $BB basename in a loop over /sys/class/udc,
   line 231/235 cat $G/UDC and cat state, line 244 ip -o addr show usb0,
   line 260 cat /proc/uptime and /proc/modules.
7. line 211-244  printf into sysfs/configfs attributes, including line 233
   printf '%s' "$UDC" > $G/UDC. A sysfs write runs the provider's callback in
   the caller's context; for the UDC attribute that is the gadget bind
   (endpoint enable, PHY, role switch).
8. bounded, listed for completeness: line 171 sleep 1, line 198 sleep 1,
   line 239 sleep 3, line 264 sleep 15.
9. journal: same problem as v6. The rawdump flush needs UFS (line 87), and
   before that everything is only in /dev/kmsg and a hardcoded /dev/pmsg0.

PART 4 - MEASURED: WHICH MODULES CAN DO IT (not guessed - disassembled)

Tool: module-risk-analysis.py, run over the 327 .ko of the device's own vendor
ramdisk (port-work/stock_vendor_ramdisk/lib/modules). It builds the exact load
order v6's emit() produces, then walks each module's call graph from
init_module and from every probe/notify function using objdump -dr, and reports
the external calls reached. Output: module-risk.json, module-risk-sweep.log.

    modules in v6's chain                                 327
    have an unbounded wait reachable from init/probe       75
    reference a peer-subsystem rendezvous symbol           70
    of the 75, in modules.load (the 98 working set)        20
    of the 75, recovery-only (never loaded in PID 1)       55

Unbounded waits reached, by symbol:
    wait_for_completion            qcom_scm (qcom_scm_call path), gh_rm_drv
                                   (gh_rm_call), cnss2 (also _interruptible
                                   and _killable), cdsprm
    mutex_lock / down_write        61 modules, including ufs_qcom, arm_smmu,
                                   pmic_glink, ucsi_glink, altmode_glink,
                                   pdr_interface, qmi_helpers, atmel_mxt_ts
    cancel_work_sync/flush_workqueue/destroy_workqueue   cfg80211, mac80211,
                                   msm_drm, dwc3_msm, mem_buf, qmi_helpers,
                                   pmic_glink, altmode_glink, cnss2, icnss2,
                                   cdsprm, mhi_dev_net
    request_firmware               msm_drm

The modules named from the device read the day before, all recovery-only
(not in modules.load): cnss2 (wait_for_completion, mhi_register_controller),
icnss2 (qmi_txn_wait), cdsp-loader, cdsprm (wait_for_completion, rpmsg_send),
adsp_sleepmon, mhi_cntrl_qcom (mhi_arch_*), mhi_dev_drv, mhi_dev_net,
atmel_mxt_ts (mutex_lock), aw9620x, fsa4480_i2c, qcom_q6v5_pas (rproc_*).

Also worth knowing: 20 of the 98 modules.load members are in the 75. The 98
are safer by precedent (that is the set the working first stage loads on this
device), not because they are risk free. That is why v8 keeps a timeout even
for them.

PART 5 - CONCLUSION: what the single strongest suspect is

Not "the module X". The strongest, and the only one the evidence supports
without guessing, is the CALL, not the module:

    our init performs, as PID 1, an unbounded number of finit_module(2) calls
    on an input set that the working boot never loads in PID 1, chosen by
    modules.load.recovery (329 names) rather than modules.load (98); every one
    of those calls can enter a driver probe that waits for hardware that is not
    up yet, and the first one that does so freezes PID 1 for ever, silently,
    because CONFIG_PANIC_TIMEOUT=-1 turns a panic into nothing and the journal
    was only ever written after the whole loop.

Aggravating factors measured, in order of how much each explains:
  a) 231 of the 329 names are recovery-only: the working boot does not run them
     in PID 1 at all (and rc=0 in QEMU proves nothing about them, because no
     probe runs there);
  b) 75 of them have an unbounded wait reachable from init/probe, including
     wait_for_completion in qcom_scm, gh_rm_drv, cnss2 (x3 variants) and cdsprm;
  c) v6 lines 749 and 994 wait up to 900 s for a UDC before it ever writes the
     journal, so even a benign slow boot looks exactly like a hang;
  d) the journal is written only after the chain (line 1257+), so a freeze
     leaves rawdump all zeros - which is what was measured;
  e) v5 has the same shape through busybox insmod plus a shell that waits on
     every child, so a stuck child is a stuck PID 1 there too.

WHAT v8 DOES ABOUT EACH OF THEM (implemented, see nx679j-init-v8.c and
nx679j-worker.c):
  - PID 1 is freestanding (-nostdlib, no libc) and the build proves from the
    shipped bytes that the complete set of syscalls it can execute is:
    clone, execve, wait4(WNOHANG), nanosleep, kill, reboot, exit_group,
    clock_gettime. No open, no read, no write, no mount, no finit_module,
    no socket, no ioctl. Attribute (README-V8) and verify-v8 report this.
  - Every risky action runs in a separate program (/nx679j/worker) started by
    PID 1 as a child, with a deadline enforced by PID 1 itself (wait4 WNOHANG
    in a loop + CLOCK_MONOTONIC; a blocking wait4 would be exactly the bug).
    This closes (a), (b) and (c).
  - Inside the chain step, each module load is AGAIN a separate child process
    with its own 15 s deadline: open+finit_module happen in the grandchild, and
    on expiry the grandchild is killed and the walk continues with the next
    module. One driver that never returns costs one module, not the phone.
  - The journal is appended to /nx679j-journal line by line BEFORE each risky
    call ("ATTEMPT finit_module <path>") and after it ("MOD <name> loaded rc=0"
    or "MOD <name> finit_module rc=-E errno=N"), and is mirrored to /dev/kmsg,
    /dev/pmsg0 and rawdump at every dump point. "loaded" and "TIMEOUT-KILLED"
    are different strings, so the record says which happened. This closes (d).
  - The start set is modules.load (the 98 that this device's own first stage
    loads), with the gadget closure added after it; modules.load.recovery is
    never read. QEMU has no metadata at all, so it uses fallback.order - the
    device path and the QEMU path are logged differently and the log says which.
  - The reboot heartbeat is kept, but only as the last resort, after the dump
    op has been attempted. It is not the evidence channel; it is the "I could
    not finish" signal, and its period encodes the failure class.

PART 6 - WHAT THE LOCAL VERIFICATION CANNOT PROVE

Proven locally: PID 1 cannot block (from the shipped bytes: the only syscalls
it can execute are clone, execve, wait4, nanosleep, kill, reboot, clock_gettime
and exit_group in the child that failed to execve); a child stuck for ever is
killed on the deadline, journalled as TIMEOUT-KILLED and the walk continues
(localtest-worker.json, 11/11); the kernel runs the image, /init runs as PID 1,
the ops run in order and PID 1 reaches reboot(2) (QEMU, device kernel).

NOT proven, and not provable without the phone:
- that a real driver probe in this kernel hangs; the argument is structural
  (the measured 75/327 modules with an unbounded wait reachable from init or
  probe) plus the fact that the working boot never runs a load in PID 1.
- that reboot(2) still completes while another task is stuck in D state.
  device_shutdown() in kernel_restart() only calls shutdown handlers of BOUND
  drivers, so a task stuck inside a probe should not hold it, but this is a
  reasoning step, not a measurement. If it does block, the screen shows a
  frozen logo after the first heartbeat, which is itself one of the four
  documented outcomes.
- that the rawdump partition really is the 524288-sector one, and that writing
  to it works on hardware (QEMU has no UFS at all).
- that pstore/ramoops exists (measured: pmsg unavailable in QEMU; the QEMU
  kernel is the device kernel but the ramoops platform device needs the device
  DT, so this says nothing about the phone).
- that the UDC appears under 1d84000.ufshc for UFS and under the ssusb/dwc3
  path for USB: those two paths come from the device's own fstab, as reported,
  and are used as wait hints only. If they are wrong, the step reports a
  timeout and the heartbeat says so, instead of hanging.
