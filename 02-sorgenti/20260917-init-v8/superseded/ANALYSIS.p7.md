
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
