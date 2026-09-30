VERIFY THIS IMAGE BEFORE FLASHING (all commands are local, no device needed)

  cd /home/user/nx679j-stock/experiments/20260917-init-v8

  python3 build-candidate-v8.py      # compiles, audits PID 1's syscalls from the
                                     # shipped bytes, rebuilds the ramdisk, the
                                     # lz4 -l frame, unpacks it again and checks
                                     # the two programs, grafts the image
  python3 verify-candidate-v8.py     # independent re-check from the IMAGE only
  python3 localtest-worker.py        # shipped worker under qemu-aarch64, FIFO
                                     # victim: kill + continue, 11 checks
  python3 qemu-candidate-v8.py       # two full-system boots with the device
                                     # kernel: order claims + poisoned module

The facts to look at, in order:
  manifest.json            everything that was built and its sha256
  qemu-run.log             the two full-system runs, PASS/FAIL as printed
  localtest-run.log        the FIFO kill test
  qemu/A-clean.log         the whole journal of run A, in the boot order
  qemu/B-poisoned.log      run B: ATTEMPT, TIMEOUT-KILLED, and the walk going on

WHAT IS NOT VERIFIED AND CANNOT BE, LOCALLY
  the real rawdump partition (QEMU has no UFS), the real UDC a600000.dwc3
  (QEMU has no dwc3), whether the image is accepted by ABL/AVB, and whether a
  module probe that wedges a device still lets reboot(2) through on hardware.
