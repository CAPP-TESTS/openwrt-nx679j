
WHAT WAS VERIFIED, WITH THE REAL NUMBERS (appended after the fixes above)

localtest-worker.py: 11/11 checks passed.  Evidence: the FIFO victim
altmode-glink.ko produced "MOD altmode_glink TIMEOUT-KILLED after 15000 ms",
the next module (clk-qcom.ko) was then attempted, the cost was 15.3 s, the
chain still exited 0, and "loaded" and "TIMEOUT-KILLED" are distinct strings
in the same journal.  This is the requirement "kill the child, lose the
driver, not the phone" measured on the shipped bytes.

qemu-candidate-v8.py run A (clean, device kernel, shipped image): 12/12 checks
passed, including the two ORDER claims checked by byte offset in the boot log:
  FIRST JOURNAL WRITE at byte 20111
  gadget: start       at byte 23243
  first ATTEMPT finit_module at byte 28741
so the journal write and the gadget attempt both precede any module load.  The
run also shows the honest QEMU answer for the durable channel:
"first journal write: NO rawdump partition found" (QEMU has no UFS), and
"gadget: NO UDC after 12 s" (QEMU has no dwc3), and PID 1 rebooting at 58 s.

qemu-candidate-v8.py run B (one .ko replaced by a FIFO, so the child that
opens it blocks in open(2) for ever): the full-system log shows
  ATTEMPT finit_module .../phy-qcom-emu.ko
  MOD phy_qcom_emu  TIMEOUT-KILLED after 15000 ms (stuck in finit_module, ...)
  chain: done in 17538 ms: ok=46 timeout-killed=1 failed/missing=0 skipped=0
and PID 1 still rebooted at 72 s.  A child that can never return cost 15 s and
one driver, and PID 1 kept its ability to report.

TWO TEST BUGS FOUND AND FIXED (the failures were real and are not hidden)
  1. The A/F checks first searched for the tag "nx679j-v8:" while the worker
     writes "nx679j:".  Five checks failed on that alone.  Test corrected.
  2. The FIFO poisoning zeroed the size field but left the payload, so the
     kernel rejected the whole ramdisk: "rootfs image is not initramfs (junk
     within compressed archive)".  Run B then measured nothing at all (0
     journal lines) while still showing a reboot at 32 s, which is exactly how
     a broken test can look like a result.  The poisoning now re-serialises a
     valid archive (verified with cpio -it: 68 entries, the victim listed as
     "prw-------" = FIFO).  Only after that fix does run B show the kill.
