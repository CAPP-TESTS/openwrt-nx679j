
EXPERIMENT 20260917-init-v8 - THE ONE HARDWARE TEST (image boot_b-init-v8.img)

What is flashed: boot_b-init-v8.img, 100663296 bytes, slot B only. It is the
SAME container, cmdline, kernel and ramdisk content as the v7 image that is
measured to boot and to reach userspace with our content; the only difference
is /init (now the freestanding supervisor) plus the new /nx679j/worker. The
kernel bytes are untouched and that is verified byte for byte.

BEFORE FLASHING, two facts from the device are used by this image:
  - UFS host node watched: /sys/devices/platform/soc/1d84000.ufshc
    (from the first stage fstab sysfs_path).
  - the UDC is expected under the *.ssusb/*.dwc3 path; the worker waits for
    /sys/class/udc/* (skipping dummy_udc) and logs what it finds.

WHAT THE IMAGE DOES, IN ORDER (this is the design, and it is the reason a hang
can no longer erase the evidence -- UFS and USB are built into this kernel):

  1. mounts /proc /sys /configfs
  2. THE FIRST JOURNAL WRITE, before any module is loaded: the worker looks
     for the rawdump partition (by-name first, then by size in
     /sys/class/block), makes /dev/rd, and writes the journal there. No module
     is loaded before this line exists.
  3. THE FIRST GADGET ATTEMPT, still before any module: configfs NCM. If the
     UDC (a600000.dwc3) is not there yet the worker says so.
  4. the modules, each finit_module(2) in its own child with a 15 s deadline:
     on overrun the child is killed and the journal gets
     "MOD <name> TIMEOUT-KILLED"; the walk continues.
  5. the gadget attempt again, now that the PHY/redriver/role switch may be up.

WHAT THE USER WATCHES - two things only, both obvious:
  1. the screen: does the RedMagic logo KEEP COMING BACK (flash) or does it
     stay put?
  2. the host: does a USB device 18d1:4ee7 appear within about a minute?

HOW TO READ THE RESULT (this is the whole protocol):

  logo STEADY and USB 18d1:4ee7 present
      SUCCESS: the gadget bound (step 3 or 5). The interface is up at
      10.0.0.1/24 on usb0.

  logo CYCLES (flashes, comes back, keeps flashing)
      IT DID NOT FINISH, and PID 1 is alive and telling us so.  The journal is
      already on the rawdump partition, including everything up to the point of
      failure.  The flash interval is a coarse class hint (the walk re-runs
      between flashes, so read it as "fast / about half a minute / about a
      minute"):
        ~8 s period  -> the rawdump write worked, the gadget did not bind
        ~30 s period -> the rawdump partition was NOT found (check the journal
                        line "first journal write: NO rawdump partition found")
        ~60 s period -> PID 1 itself killed an operation that overran its
                        deadline (the worst class)


  logo STEADY and NO USB, for three minutes
      The bad case: either userspace was not reached, or PID 1's own reboot
      call was blocked. Nothing was written and nothing can be concluded from
      this boot alone; report it as "fermo, niente USB".

To exit a cycle: press and hold Volume Down (the bootloader), same as with the
v7 sonda. The A/B counters are not consumed on this unit, so the phone will not
stop by itself.

WHERE THE JOURNAL IS (read after the test):
  1. /nx679j-journal inside the boot - only during that boot, RAM.
  2. rawdump partition, offset 0, as many 4 KiB sectors as there are journal
     bytes (the worker finds the partition by size, the same 256 KiB-size
     heuristic v5/v6 used, and writes it in-place). Read it from Android with
     dd and look for lines starting with "nx679j: ".
  3. /sys/fs/pstore after the next Android boot, if ramoops is registered.
     The worker logs "pmsg unavailable (no ramoops this boot)" when it is not,
     so the absence is stated, not silently ignored.
  4. /dev/kmsg of the running boot (the measured QEMU logs show the mirror).

WHAT THIS IMAGE PROVES EVEN IF IT FAILS ON HARDWARE: that PID 1 cannot be the
thing that hangs. Whatever happens to the driver probes, PID 1 keeps running
its own loop and keeps reporting.
