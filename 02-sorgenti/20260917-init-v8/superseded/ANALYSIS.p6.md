
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
