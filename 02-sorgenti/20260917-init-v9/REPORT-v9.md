# EXPERIMENT 20260917-init-v9 — REPORT

**v9 = v8 + one new read-only child: `/nx679j/relay`.**

The deliverable is `boot_b-init-v9.img` (100663296 bytes, sha256
`615b6aa1b319a4d4bc33683d236cdd31bd94073ebd077f0ab975ebe52089c21f`): the v8
image, byte for byte, with the `/init` of v8 extended by the relay child and
one new entry `/nx679j/relay` added to the archive.  From the phone it turns
the boot journal from *something you dd out of the rawdump partition* into
something you read over the USB link:

```
nc 10.0.0.1 9999
```

## 1. What changed, exactly

| | v8 | v9 |
|---|---|---|
| `/init` | 67312 B, sha256 `6fa91dec…` | 67384 B, sha256 `9f241361…` |
| `/nx679j/worker` | 663536 B, sha256 `ac4a6ff8…` | **identical, not recompiled** |
| `/nx679j/relay` | — | 598152 B, sha256 `271a9bdd…` (new) |
| archive entries | 68 | 69 (68 byte-identical, `/init` replaced, relay added) |
| kernel, cmdline, DTB, container layout | untouched | untouched |

`/init` is the v8 supervisor plus `keep_relay()`: it spawns the relay with the
same `clone(SIGCHLD)+execve` it uses for every worker, **never waits on it**
(`wait4(...,WNOHANG)` only), respawns it if it dies (minimum gap 5 s so a relay
that cannot bind cannot spin the maint loop), and records what happened through
the worker's dump mode.  PID 1 executes **exactly the same eight syscalls as
v8** (clock_gettime, clone, execve, exit_group, kill, nanosleep, reboot,
wait4) — re-derived from the shipped bytes, 27/27 checks
(`verify-candidate-v9.json`).

## 2. The relay

* listens on TCP, `10.0.0.1:9999` on `usb0` (the gadget's NCM interface),
  binds the wildcard address only if `10.0.0.1` is not there yet, and **says
  which one it got** in the dump;
* writes one text snapshot per connection and closes — **there is no request
  protocol**: the client sends nothing, the answer cannot depend on anything
  the client does (proven: three clients, one of them sending HTTP-looking
  junk and one sending binary garbage, get byte-identical sections);
* sections: `[1]` kernel/cmdline/uptime/meminfo, `[2]` interfaces with real
  ioctl addresses, `[3]` UDC/usb_role/configfs gadget, `[4]` block devices +
  mounts + rawdump node, `[5]` pstore, `[6]` loaded modules, `[7]` module
  files, `[8]` **the boot journal itself** (with `hash_fnv1a64_full` over the
  whole file and `hash_fnv1a64_window` over exactly the lines printed), `[9]`
  a self-check (pid/ppid, `/proc/self/ns/mnt` vs `/proc/1/ns/mnt`,
  `/proc/self/mountinfo`, raw errnos), `[10]` end marker;
* bounded by construction: 64 KiB per file, 512 KiB rolling journal window
  (no allocation at all), 250 names per directory, 30 s per client.

**Read-only is a property, checked three ways.**

1. *Static*: every call target reachable from the relay's own code is on a
   whitelist; nothing matching unlink/rename/mkdir/mknod/chmod/chown/truncate/
   creat/fsync/mount/module/symlink/kill/reboot/ptrace/exec/system/popen/
   chroot/fallocate is reachable at all.  The only indirect (PLT/IFUNC) call
   site resolves to `__libc_memmove_ifunc`.
2. *Runtime*: the shipped binary run under `qemu-aarch64 -strace` (68 opens,
   **all** `O_RDONLY`), zero `O_WRONLY/O_RDWR/O_CREAT/O_TRUNC/O_APPEND`, zero
   `unlink/rename/mknod/mkdir/mount/finit_module/...`, zero `SIOCSIFADDR`
   (the network is read, never configured), and **every** `write()` goes to a
   socket fd or to the console fd 1/2 — never to a file, a device or the
   rawdump.
3. *Behavioural*: the fake device tree (30 files) is byte-identical before and
   after serving all the clients.

## 3. Verification actually run

| suite | result | what it proves |
|---|---|---|
| `build-candidate-v9.py` | pass | audited build; the "v9 = v8 + relay" claim is checked structurally on the archives inside the build |
| `verify-candidate-v9.py` | **27/27** | re-derived from the shipped image only: container, archive, PID 1 syscalls, relay whitelist + PLT resolution, worker byte-identical to v8, `/init` changed |
| `localtest-relay.py` | **26/26** | the relay **taken out of the image** answers a real `nc` (BusyBox 1.37.0), the dump is printable ASCII, the journal lines and both FNV hashes match the file on disk, 3 clients + a rude client + the shipped default port 9999, plus the `-strace` read-only proof |
| `qemu-candidate-v9.py` | see `qemu-candidate-v9.json` | full-system: A device kernel (order claims + relay spawned + PID 1 still reboots), B poisoned module (relay still spawned, kill/continue intact), C mainline kernel + e1000 + slirp, **`nc` from the host reads the booting guest's dump** |

Scenario C is the closest offline equivalent of the phone test: the ramdisk is
the LZ4 frame out of the shipped image, PID 1 runs it, the relay is the same
bytes, and the client is the same `nc` one host away — the only substitutions
are the kernel (the device kernel ships no NIC driver for QEMU) and a boot
argument (`ip=…`) that gives the guest an address.  The dump obtained that way
shows the guest's own `uname`, its own `eth0 addr=10.0.2.15`, its real journal
(`nx679j: mounts: done`, `v9-relay-started op=relay rc=0 n=1`,
`PID1-OP-FAILED op=gadget rc=1`), and its mount table with `/proc` and `/sys`
mounted in the namespace shared with PID 1.

## 4. Two real bugs this found (both fixed, both re-verified)

**4.1 "absent" that was not absent.**  The first full-system run showed
`/proc` and `/sys` "absent" in the dump while the worker read both fine.  The
cause was in the relay: the path-prefix helper `P()` filled its buffer **only**
when a test root was set, so on the device (no root prefix) every caller that
used the buffer read an uninitialized stack string and every read failed.  The
qemu-user test could not see it because that test sets `V9_RELAY_ROOT` — a
test that configures the thing under test can hide exactly this class of bug.
Fixed by making `P()` always write the buffer, and the local test now asserts
the real contents (200 journal lines, `/proc` values), not merely that a file
was opened.

**4.2 a bind address that was asked for, not obtained.**  In the device-kernel
guest the relay printed `nc 10.0.0.1 9999 (iface usb0 ip -, wildcard_fallback=0)`
while no interface in that namespace has `10.0.0.1`: `bind()` to a non-local
address had succeeded (that namespace allows it — the sysctl is now part of the
evidence).  The relay was therefore reporting the address it *requested*.
Fixed by asking the kernel: after `bind()` the relay calls `getsockname()` and
publishes what the kernel actually gave it, plus `if_any_has()` (is that
address on any interface here?) and `net.ipv4.ip_nonlocal_bind`, on the console
and in the dump.  A test was added for the invariant this creates: a reported
non-wildcard bind address is only acceptable when the address is present on an
interface or non-local binds are enabled — so this exact defect cannot come
back unnoticed.

## 5. Not verified here (needs the phone)

* that `usb0` exists with `10.0.0.1` on this hardware at that moment — that is
  the same claim v8 already carries (`gadget_ok`), and the relay only reports
  it;
* that the host's NCM driver talks to the gadget;
* anything about the real UDC/module probes.
The relay is deliberately harmless in the negative case: if the gadget never
binds, nothing can reach it, and the dump says `iface usb0 present=0`.
