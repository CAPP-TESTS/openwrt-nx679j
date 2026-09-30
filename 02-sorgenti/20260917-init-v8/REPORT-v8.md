REPORT - experiment 20260917-init-v8
Outcome first, then the evidence, then what is NOT proven.

WHAT WAS ASKED
  (A) read our init v5/v6 and list every place PID 1 can block for ever
  (B) design and build an init v8 that cannot block, with the three claims
      that were added later: journal first, gadget attempt first, modules last
  (C) keep the hardware tests to a minimum and make the outcome readable
  (D) verify everything locally and say clearly what is not proven

(A) THE ANALYSIS - see ANALYSIS-BLOCKING-V5-V6.md (254 lines, 7 parts)
  The structural fact, verified from the bytes on this machine: the /init that
  works on this device is magiskinit (sha256 8e26e33c...), and it contains ZERO
  occurrences of finit_module, init_module, modules.load, modules.dep,
  /lib/modules, insmod, modprobe.  Our v6 contains and uses all of them.
  The call that can hang for ever is finit_module(2): in kernel/module.c it runs
  load_module() AND do_init_module() in the CALLER's context, and the driver's
  probe() runs synchronously inside it (platform_driver_register -> ... ->
  really_probe, unless the driver asks for async probing, which these do not).
  Measured from the device's own 327 .ko: 75 of them have an unbounded wait
  reachable from init/probe (mutex_lock, wait_for_completion, destroy_workqueue,
  cancel_work_sync, flush_workqueue, request_firmware), 20 of those are even in
  the 98-module modules.load set; 70 rendezvous with other processors.  Among
  the 231 recovery-only modules are cnss2 (wait_for_completion + mhi_register_
  controller), icnss2 (qmi_txn_wait), cdsprm (wait_for_completion + rpmsg_send),
  mhi_* and the touch drivers.  v6 loads all of them inside PID 1, in a
  blocking call, and writes its journal only at the end (journal_flush at line
  1062, called at 1257/1270/1275); hence: logo frozen, no USB, rawdump all
  zeros, no panic - exactly what was measured.  v6 also had a 900 s silent wait
  (gadget_cycle(900,5) at line 749 and (901,5) at 994) inside the chain.

(B) v8 AS SHIPPED - what it is
  Two programs in the ramdisk, built from this directory:
    /init            nx679j-init-v8.c   67312 bytes, freestanding, no libc
    /nx679j/worker   nx679j-worker.c    663536 bytes, static, does all the
                                          risky work
  /init is PID 1 and nothing else.  Its complete syscall set, read back from
  the SHIPPED bytes by disassembling them (build-candidate-v8.py step 4):
    clock_gettime, clone, execve, exit_group, kill, nanosleep, reboot, wait4
  No open, read, write, mount, ioctl, socket, mknod, finit_module: PID 1 has no
  code path that can block on a driver, because it has no code path that talks
  to a driver at all.  Every operation is a child: /init forks, execs
  /nx679j/worker, waits with wait4(WNOHANG) in a loop and kills the child when
  its deadline passes.  It never uses a blocking waitpid either: a child stuck
  in an uninterruptible sleep would never become waitable.

  The order of operations is the requirement (nx679j-init-v8.c):
    line 241  #define OP_MOUNTS  0    /proc /sys /configfs
    line 242  #define OP_JOURNAL 1    the FIRST journal write
    line 243  #define OP_GADGET  2    configfs NCM attempt
    line 244  #define OP_CHAIN   3    modules, each in a child with a deadline
    line 245  #define OP_GADGET2 4    gadget retry after the modules
    line 366  rc = run_op(OP_MOUNTS, 0);
    line 371  rc = run_op(OP_JOURNAL, "first-journal-write-before-any-module");
    line 377  rc = run_op(OP_GADGET, "before-modules");
    line 388  rc = run_op(OP_CHAIN, 0);          <- the only op that can call
                                                   finit_module
    line 393  rc = run_op(OP_GADGET2, "after-modules");

  CLAIM 1 (journal): no module is loaded before the first journal write.
    Proof, source: OP_JOURNAL is 1 and OP_CHAIN is 3 (lines 242 and 244), and
    _start execs them in that order (lines 371 and 388).  OP_CHAIN is the only
    operation whose worker mode can call finit_module; nothing before line 371
    can load a module.  The worker side is mode_journal() in nx679j-worker.c
    (line 938 onward): it does not fork, does not load, does not wait for
    anything - it looks for the rawdump partition (by-name first, then by size),
    mknod's it, and writes.  Its journal line, line 942: "FIRST JOURNAL WRITE,
    no module loaded yet (device tree nodes UFS=... USB=...)".
  CLAIM 2 (gadget): no module is loaded before the first gadget attempt.
    Proof, source: OP_GADGET is 2 (line 243) and is exec'd at line 377, before
    OP_CHAIN at line 388.  libcomposite, configfs, dwc3 and the NCM function
    are built in (=y), so the attempt does not depend on a module; if the UDC
    is not there yet the worker says "gadget: NO UDC after N s" and the retry
    is OP_GADGET2 after the modules.  A retry is not a dependency: the first
    attempt already happened.
  CLAIM 3 (modules last, each in a child with a deadline): the chain worker
    forks a grandchild per module (load_guarded, nx679j-worker.c), the child
    writes "ATTEMPT finit_module <path>" BEFORE the call, and the parent kills
    it after MOD_TIMEOUT_MS = 15000 and writes "MOD <name> TIMEOUT-KILLED after
    15000 ms".  On a timeout the walk continues: the driver is lost, not the
    boot.  modules.load (98 names) is the starting set; modules.load.recovery
    is never read (nx679j-worker.c build_chain, and no "recovery" string in the
    binary - checked in the build).

(C) WHAT WAS VERIFIED LOCALLY, WITH THE REAL RESULTS
  1. localtest-worker.py - the shipped worker under qemu-aarch64, with a FIFO
     in place of one .ko so that open() can never return: 11/11 checks passed.
     The striking ones: "MOD altmode_glink TIMEOUT-KILLED after 15000 ms"
     appears, the walk CONTINUED (the next module is attempted), the cost was
     15.3 s, and the chain still exited 0.  "loaded" and "TIMEOUT-KILLED" are
     different strings in the same journal.
  2. qemu-candidate-v8.py - two full-system boots of the device kernel with the
     shipped image (A clean, B with a poisoned .ko).  Results in
     qemu-run.log / qemu-candidate-v8.json and printed at the end of this file.
     The order claims are asserted on the boot log by byte offset:
     FIRST JOURNAL WRITE at 20111, gadget: start at 23243, first
     ATTEMPT finit_module at 28741 - journal and gadget both before any module.
  3. build-candidate-v8.py - the container is the one measured to boot, the
     kernel bytes are untouched, the ramdisk is lz4 -l legacy and round trips
     byte for byte, and the image is unpacked again and checked file by file
     (/init and /nx679j/worker present, executable, hashes equal to the built
     binaries).

WHAT FAILED FIRST AND WHAT I CHANGED (not hidden)
  * The first QEMU run failed 5 of 8 checks.  Cause: the test looked for the
    tag "nx679j-v8:" while the worker writes "nx679j:".  That was a TEST bug
    and the test was corrected; no init behaviour was weakened.
  * Reading that run showed a REAL defect in my worker: the journal mirror
    printed only 3 lines while the journal file was complete.  Cause: the mirror
    only knew this process's RAM tail, and the per-module children are separate
    processes, so their ATTEMPT/MOD/TIMEOUT-KILLED lines never got there.  Fixed
    in nx679j-worker.c: the mirror now reads the journal FILE; a first attempt
    with a watermark was buggy (overlapping memcpy) and was replaced by a plain
    whole-file mirror, which also made the byte count visible in the log.
  * The very first A run after that fix printed NO journal line at all, only an
    empty line.  Cause: the mirror was gated behind open("/dev/kmsg") and
    silenced itself when it failed.  Fixed: fd 1 (/dev/console, which the kernel
    gives to PID 1 and which the child inherits) is now the first channel, and
    the journal content goes to it.  The kmsg mirror is secondary: a multi-line
    write to /dev/kmsg did NOT reach the console in this environment while the
    header line did - that is recorded here as an observation, not explained.
  * The first image built had no "nx679j" directory entry next to the file
    "nx679j/worker", so the kernel's initramfs unpacker would have silently
    dropped the worker.  Found by unpacking the built ramdisk (build step 7b,
    added because of it).

WHAT IS NOT PROVEN - read this before flashing
  * Nothing here proves the hardware hang is FIXED.  QEMU has no UFS and no
    dwc3, so "first journal write: NO rawdump partition found" and
    "gadget: NO UDC" are the honest QEMU results.  Whether the real rawdump
    partition is found and written at op 2, and whether the UDC a600000.dwc3
    appears, can only be measured on the device.
  * The reboot heartbeat assumes reboot(2) still works while another task is
    stuck.  Not provable in QEMU (nothing there is genuinely stuck).  If a
    module probe wedges a device and the kernel's shutdown path then blocks in
    PID 1's reboot, the screen shows a steady logo - which is itself the
    diagnostic, but the reboot-first design would be wrong.
  * pstore/ramoops is not present in the QEMU kernel; the worker says so
    ("pmsg unavailable (no ramoops this boot)").  On the device it may or may
    not exist; the journal does not depend on it.
  * The worker's own risk class is not zero: it writes to /dev/kmsg and to fd 1
    (console) and to a block device.  All of that happens in a child with a
    deadline, so a hang there costs that child and is journalled - it cannot
    stop PID 1, but it can stop that particular operation.

FINAL STATE OF THE VERIFICATION (this is what the artifacts say now)
  qemu-candidate-v8.py  : 18/18 checks PASSED, exit 0 (two full-system
                          boots of the device kernel with the shipped
                          image; A clean, B with a FIFO .ko)
  localtest-worker.py   : 11/11 checks PASSED (shipped worker, qemu-aarch64,
                          FIFO victim: kill at 15 s, walk continues)
  verify-candidate-v8.py: 13/13 checks PASSED (re-derived from
                          boot_b-init-v8.img only)

  image   boot_b-init-v8.img  100663296 bytes  sha256 04e7f9b2be9430977f48ce7091ada2edcad63c8edd11b976fce11262a45d1dbd
  ramdisk lz4                 1076975 bytes  sha256 2a97f1efaf64ea1982957ba3f4361a07a0121ce49207bad40ca88a00db80aab8
  /init                       67312 bytes  sha256 6fa91dec86dceafc01cbd69caea71dbff1be27d26c815802a23bd914be2bd6b1
  /nx679j/worker              663536 bytes  sha256 ac4a6ff8069bbce36d77d566f76ac2e247e4dd9308edcf38920a5daed487be29
  PID 1 syscalls (from the shipped bytes): clock_gettime, clone, execve, exit_group, kill, nanosleep, reboot, wait4

