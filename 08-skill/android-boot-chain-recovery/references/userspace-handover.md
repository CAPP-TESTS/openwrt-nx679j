# Handing the machine to a foreign userspace (native port on top of a working boot chain)

Use when the bring-up already works (the custom init mounts, journals, brings up storage and a USB gadget) and the goal is the *real* target userspace — an OpenWrt/router rootfs, a mainline distribution — running natively, not hosted inside Android.

The split is the whole design: keep the proven init as the bring-up stage (journal first, gadget, modules), and let the guest own the root afterwards. Nothing about the bring-up is rewritten, and nothing about the guest is guessed.

## Recipe

1. **Acquire the guest rootfs from upstream, by listing the index, not by assuming names.** Distribution version numbers and target names change between releases (measured: `armvirt/64` is now `armsr/armv8`, and the release series that is current is not the one you remember). List `.../releases/` and then `.../releases/<ver>/targets/<t>/` and pick the real filename. Download and verify before use: `tar tzf` for the entry count, `file` on `bin/busybox` and `sbin/init` for the architecture **and interpreter** (a musl `aarch64_generic` rootfs matches a kernel whose userspace expectations are musl), and read `/etc/openwrt_release` (or the equivalent) for the identity.
2. **Stage, then merge.** Put the guest tree under one directory in the ramdisk (`/owrt`, never at the root — it would collide with `/bin`, `/lib`, `/sbin`, `/etc`), add the config files below, and build the new ramdisk by concatenating `<proven ramdisk cpio> + <cpio of the new files>`, with uid/gid rewritten to 0 in the new archive. See the `newc` field rules in SKILL.md.
3. **One line in the working init, in the success path.** After the bring-up succeeded:

   ```c
   static char *sav[4], *senv[3];
   sav[0] = (char *)"/owrt/bin/busybox";
   sav[1] = (char *)"sh";
   sav[2] = (char *)"/nx679j/switch.sh";
   sav[3] = 0;
   senv[0] = (char *)"PATH=/owrt/bin:/owrt/sbin:/owrt/usr/bin:/owrt/usr/sbin";
   senv[1] = (char *)"HOME=/";
   senv[2] = 0;
   (void)run_op(OP_DUMP, "handing over to /nx679j/switch.sh AS A CHILD");
   sp = spawn_bg("/nx679j/switch.sh");   /* fork + execve: PID 1 stays alive */
   (void)run_op(OP_DUMP, sp > 0 ? "switch child started" : "spawn_bg FAILED");
   ```

   Starting it as a **child** is the point, not a compromise. `execve` only returns on failure, so once it succeeds the maintenance loop below it is unreachable: a guest program that dies at once (missing shebang, loader problem, its own fatal path) takes PID 1 with it, and the kernel panics — with `panic=<n>` in the cmdline that reads as a boot repeating every ~(bring-up + n) seconds, with no error anywhere. With a child, the same death costs nothing and shows up as the child's own journal lines stopping at a named step.

   `spawn_bg` passes no arguments, so starting the script *by path* makes the kernel's shebang handling and the script's own execute bit load-bearing — two dependencies worth dropping. Prefer handing the work to a process that is not PID 1 at all and already has file access and an exec path (the worker that loads modules does): `execve("/owrt/bin/busybox", {"/owrt/bin/busybox", "sh", "/nx679j/switch.sh"}, env)` with argv written **explicitly**. Nothing then depends on the shebang or on the script being executable, the worker reports the raw `-errno` in the journal, and a dying guest costs a journal line instead of a panic. Keep the errno branch from the exec version — as that process's own diagnostic — because `ENOENT`/`ENOEXEC`/`EACCES` still name three different repairs.

The rule behind both shapes: **PID 1 surveys, it does not execute.** Every design that has PID 1 itself run the guest converts a guest bug into a kernel panic; a child, or better a worker, converts the same bug into a journal line.

A child's **exit status cannot name the failure** when the spawn helper has a fallback exit code. `child exit 127` reads like a verdict but 127 is the helper's own hardcoded path for *any* failed exec — and the shell's "command not found" convention besides — so it does not separate `EACCES` from `ENOENT`. Take the errno from a process that can print it, or stat/access the paths from one that has file access; never promote a synthetic exit code into a diagnosis.

   Build the init with the same freestanding flags as always (`-static -nostdlib -ffreestanding -fno-stack-protector`); a libc build fails with `multiple definition of _start` and `undefined reference to main`.
5. **Ship the guest's dynamic loader in the CURRENT root, and make the exec name its own failure.** The guest's busybox is dynamically linked, and the interpreter recorded in it is an **absolute** path (`/lib/ld-musl-aarch64.so.1`). The kernel resolves that path against the root the process is in at that moment — the ramdisk, not `/owrt` — so the guest's loader (plus the libc it points at) must also exist under the ramdisk's own `/lib/`, added in the same archive. Without it the exec dies with `ENOENT` before the script ever runs, which is indistinguishable from "the tree was not unpacked".

   And have the failure say which error it was: the raw syscall returns `-errno`, so branch on it instead of journalling a generic message, since each value points at a different fix and a generic one costs a whole flash cycle to disambiguate.

   ```c
   {
       long er = sys3(NR_EXECVE, (long)EXE, (long)sav, (long)senv);
       if (er == -2)       (void)run_op(OP_DUMP, "execve FAILED errno=ENOENT path missing in this root");
       else if (er == -8)  (void)run_op(OP_DUMP, "execve FAILED errno=ENOEXEC loader/format");
       else if (er == -13) (void)run_op(OP_DUMP, "execve FAILED errno=EACCES not executable");
       else                (void)run_op(OP_DUMP, "execve FAILED errno=other");
   }
   ```

   `ENOENT` means the archive's contents are not in this root, `ENOEXEC` means the loader/format rule above, `EACCES` means permissions — three different repairs that one named line separates from a single boot.
The switch script (runs as a child of PID 1 in the default, safe configuration; it becomes PID 1 itself only in the later exec experiment):

   ```sh
   BB=/owrt/bin/busybox
   $BB mkdir -p /owrt/proc /owrt/sys /owrt/dev /owrt/dev/pts /owrt/tmp /owrt/run
   # Mounts for the new root MUST be made from inside it: a process rooted in the
   # ramdisk creating /owrt/proc leaves the guest init with no readable /proc.
   CI="$BB chroot /owrt /bin/busybox"
   $CI mount -t proc  proc  /proc
   $CI mount -t sysfs sysfs /sys
   $CI mount -t tmpfs tmpfs /dev || $CI mount -t ramfs ramfs /dev
   $CI mount -t tmpfs tmpfs /tmp;  $CI mount -t tmpfs tmpfs /run
   $CI mount -t devpts devpts /dev/pts
   # static safety nodes first, then mdev populating from /sys (no devtmpfs here)
   for d in "console c 5 1 600" "null c 1 3 666" "zero c 1 5 666" "tty c 5 0 666" \
            "random c 1 8 666" "urandom c 1 9 666" "ptmx c 5 2 666"; do set -- $d; $CI mknod -m $4 /dev/$1 $2 $3 $5; done
 J=/nx679j-journal
 say() { echo "switch.sh: $*" >> $J 2>/dev/null; }        # the live reader shows these
 for m in "proc proc /proc" "sysfs sysfs /sys" "tmpfs tmpfs /dev" \
          "tmpfs tmpfs /tmp" "tmpfs tmpfs /run" "devpts devpts /dev/pts"; do
     set -- $m
     if $CI mount -t $1 $2 $3 2>>$J; then say "mount $3 OK"; else say "mount $3 FAILED"; fi
 done
 # No mdev applet in OpenWrt's busybox (rc=127): the mknod calls above ARE /dev.
 say "/dev inside the guest: $($CI ls /dev 2>/dev/null | tr '\n' ' ')"
 say "init executable: $([ -x /owrt/sbin/init ] && echo YES || echo NO)"
 $BB mount --bind /owrt /owrt          # switch_root requires a mount point
 if [ -x /owrt/sbin/init ]; then
     say "handing over with exec (the guest init becomes PID 1)"
     exec $BB chroot /owrt /sbin/init
     say "exec chroot returned"
 fi
 say "handover NOT done; staying alive so the device does not panic"
 while :; do $BB sleep 3600; done
 ```

 Two rules make that script safe, and both were learned by watching the device reboot instead of reaching the guest (they apply to the exec path too, once that path is proven — until then the child is what keeps the device diagnosable):

 - **Every step journals its own line before it acts**, mounts included, recorded per target as `OK`/`FAILED`. The reader then names the step that failed instead of leaving a silent cycle to interpret.
 - **Nothing may end the script.** PID 1 exiting is a kernel panic, so the `exec` is taken only after the new root is verified complete, and every other path ends in an infinite keep-alive loop. Measured signature when this rule is missing: the boot repeats every ~(boot time + panic timeout) — ~40 s of bring-up plus `panic=10` is the "about a minute" a user reports.
 - **The guest's init running is not the guest's service layer running.** A distribution's `init` expects to be PID 1 *and* the reaper; as a chrooted child it can sit alive having started nothing — measured: the guest init in state `S` beside a **zombie** child of itself, no `ubusd`, no `netifd`, nothing listening on port 22, only your own diagnostics child on a port, and `ubus list` answering `Failed to connect to ubus`. Read the process table and the listeners from inside the guest (a populated list with an empty listener column is that fault, and a different repair from an init that died). Until that layer is understood, do not gate your control channel on it: mint a host key at run time and start the service **directly** from your script — `$CI /usr/bin/dropbearkey -t rsa -f /etc/dropbear/dropbear_rsa_host_key`, then `$CI /usr/sbin/dropbear -F -E -p 22`, both appending to the journal. A dropbear warning about a missing `_ed25519_host_key` is harmless: the RSA key just generated is used and the login succeeds.

 `switch_root` and `chroot` are busybox applets already present in the guest rootfs, so no new program has to be written or shipped: the only build change is the exec above. Check the exec'd binary's mode **inside the built archive**, not in the staging tree — a loader copied by a build step is 0644, and an interpreter without an execute bit makes the exec return `EACCES` even for root.
5. **Give the guest the minimum config it needs to be reachable.** A router userspace does not know your gadget interface exists: write the network config (`/etc/config/network` for OpenWrt) with `usb0` as a static interface (`10.0.0.1/24`) and `delegate 0`, or netifd will bring the link up without the address and the hand-over looks like a failure. Put an SSH key in place to have a real login: generate the pair on the host (`ssh-keygen -t ed25519 -f ~/.ssh/<name>`), ship only the **public** key as `/etc/dropbear/authorized_keys`, mode 0600, uid 0.
6. **Success criterion, stated before flashing:** `ssh -i ~/.ssh/<name> root@10.0.0.1 'cat /etc/openwrt_release'` returns the guest distribution's banner. That is a login into the *real* userspace over the transport the custom init built.

## What does NOT prove the hand-over happened

- A diagnostics child that survives the switch **keeps the root it started with**: a relay or journal reader launched by the previous PID 1 still serves the old tree, so its dump showing the old journal is evidence about the bring-up, never about the guest. If the guest must be observable, observe it from outside (SSH, or the guest's own service started by its init).
- The host's network interface is **stale** across an image change: the name is derived from the port path and the carrier disappears when the phone re-enumerates, so re-detect by driver (`/sys/class/net/*/device/driver` matching `cdc_ncm`/`cdc_ether`) instead of reusing the previous name, and re-add the host address every time.
- A frozen logo, a missing transport, or silence says nothing about the hand-over either way: the guest's init may be running with nothing to say on the console.
- Silence is only evidence *inside a channel's coverage*. Establish what a diagnostic view actually reads before reading the absence of a message as a message: a dump that reported kernel version, cmdline, memory and the boot journal but never read `/dev/kmsg` proved nothing about the kernel's own complaints, and two flash cycles went into probes that channel could not answer. When a failure is generic, make the failing call name itself — errno, rc, the step's own journal line — and re-test once, instead of spending flash cycles on indirect evidence.

## Size and module budget on a 96 MiB boot partition

- Kernel region (~49 MiB for a stock Qualcomm 5.10 kernel) plus the ramdisk must fit; a stock OpenWrt `aarch64_generic` rootfs is ~19 MiB as a tree and compresses to ~8 MiB with `lz4 -l -9`, so it fits with room to spare (measured total ramdisk ~8.3 MiB, image still exactly 100 663 296 bytes with the original trailing bytes preserved at the same absolute offsets).
- **Do not load the guest's own kernel modules.** A distribution's `lib/modules` tree is built for the kernel *it* ships; the device runs the vendor kernel, so the bring-up must load the vendor module set and the guest's `kmods` stay unused. Load nothing from the guest tree at hand-over time.
- The first attempt at a hand-over is a single-variable experiment: keep the proven ramdisk, the kernel, the cmdline and the container byte-identical, and change only the ramdisk's contents (added tree plus the one spawn).

## Triage when the candidate cycles

A cycle shorter than the bring-up plus the panic timeout is a *user-visible* symptom with two possible owners, and the journal names which one without any new image:

- Boot the known-good slot (`adb reboot bootloader`, `fastboot set_active a`, `fastboot reboot`), then read the breadcrumb partition from Android root: `su -c 'dd if=/dev/block/by-name/<breadcrumb> bs=4096 count=N'`. The partition survives the reboots; the custom userspace's own relay does not.
- Grep the dump for the marker your init writes on its deliberate-restart path (`heartbeat`) and for the hand-over marker. Marker present => the init decided the boot failed and restarted; expected, and the interesting content is what precedes it. Marker absent while the phone still cycles => PID 1 died and the kernel panicked: look for the last line before the silence.
- Grep for the hand-over script's *own* first line. If the script's lines are absent altogether, the program never started at all — a loader/shebang problem, not a failed step inside it — and a bare `execve` in that position is the classic cause.
- Read the script's **last** line as a liveness probe for the guest. The script journals each step *before* performing it, so the line that is missing is the call still in progress: `starting <guest init> as a child` present while the following `... returned rc=` never appears proves the guest's init is executing right now and has not returned. That single reading separates "the hand-over never happened" from "the hand-over happened and the guest is stuck or silent", and it costs one grep of the dump you already have.
- Do not read the guest's *silence* as the guest not running. A child's output redirected into your journal (`... >> $J 2>&1`) arrives only if that child writes something, and a distribution init normally writes nothing on stdout — so a blank stretch of journal after the hand-over is the expected shape of a healthy daemon, not evidence that it failed to start.
- When the guest runs but nothing is reachable on it, measure it from **inside the guest root**, because the journal only carries what *your* script wrote and cannot answer what the guest is doing: read `/proc/[0-9]*/comm` (or `stat`) for the process list and `/proc/net/tcp` plus `/proc/net/tcp6` for listeners, using the guest's own `/proc` mount. A populated process list with an empty listener column means the guest init is up and its services are not — a different fault, and a different repair, from an init that died. Do not depend on guest applets for this (`ps`/`netstat` presence varies by build, and a missing applet exits 127 exactly like a real failure): read the `/proc` files directly.
- An error line that is **absent** is not a measurement. "The exec now succeeds" inferred from a missing `execve FAILED` line is an inference, and it was wrong here — the later positive probe showed the exec failing. Write a confirmation line on the success path (`child exit 0 SCRIPT STARTED`) and branch on the exit status, so success is a line you read rather than a line you failed to find.
- Never check ramdisk contents by grepping the finished boot image: the ramdisk is compressed, so printable strings are not the archive's directory listing. That grep found one guest path by coincidence in unrelated data and missed the hand-over script, which read as "the file is not in the image" while the build's own verification of the cpio listed it with the right mode and size. Parse the **uncompressed cpio** — the artifact the build actually packs — and when two of your own measurements contradict each other, resolve the contradiction before acting on either, instead of believing the convenient one.
- Confirm what the guest actually did from **outside** (SSH, the guest's own service), never from a diagnostics child started before the switch: that child keeps the root it was started in and will happily serve you the old tree's journal.
- Distribution releases move: list the download index instead of assuming a target name or a release series, and re-verify the rootfs identity after every download.

## Making the guest's own service layer serve

The guest init executing is the *first* milestone; its service layer is the second, and the blocker is usually a missing directory rather than a broken manager. Measured on a chrooted OpenWrt whose init sat alive with a **zombie** child of itself and nothing listening on its own services:

1. **Create the directories the guest's tooling assumes.** `/var` in OpenWrt is a symlink into `/tmp`, which you mounted as a fresh tmpfs, so `/var/lock`, `/var/run` and `/var/log` do not exist and *every* rc script dies before doing anything:

   ```sh
   $CI mkdir -p /var/lock /var/run /var/log /var/state /var/tmp /tmp/lock /tmp/run /tmp/log
   ```

   The signature in the guest's own words is `can't create /var/lock/<service>.lock: nonexistent directory`, emitted by the distro's service helper at the line that opens its lock. Until that line stops appearing, no amount of waiting will start a service, and the init's healthy-looking `S` state proves nothing about the layer above it.
2. **Start the services you need directly, and prove each one positively.** Do not route the control channel through the guest's service manager while the manager is still the open question. For a router userland, the three that make the web UI exist are `ubusd` (the bus), `rpcd` (the privileged transport) and the web server, each launched as a chrooted child with its output appended to your journal:

   ```sh
   $CI /sbin/ubusd >> $J 2>&1 &     ; $BB sleep 2
   $CI /sbin/rpcd  >> $J 2>&1 &     ; $BB sleep 2
   $CI /usr/sbin/uhttpd -f -h /www -x /cgi-bin -t 60 -T 30 \
        -k 20 -A 1 -n 3 -N 100 -R -p 0.0.0.0:80 >> $J 2>&1 &
   ```

   The verdicts are positive ones, not absences: `ubus list | wc -l` going from an error to a list of objects (measured 20, including `container` and `hotplug.*`), a listener on the port in the guest's own `/proc/net/tcp`, and an `HTTP/1.1 200 OK` fetched from the host. "The init runs" and "SSH answers" do not imply any of this.
3. **Key before password.** A web UI typically refuses to log in until a root password exists, and setting one **removes empty-password SSH** — the very channel you are using. Install the public key first, then the password:

   ```sh
   $CI mkdir -p /root/.ssh && chmod 700 /root/.ssh
   echo "<your public key>" > /root/.ssh/authorized_keys && chmod 600 /root/.ssh/authorized_keys
   ```

   Verify the key with a *fresh, non-interactive* login (`ssh -o BatchMode=yes ...`), not by reusing the session that was already open. Then set the password and **verify it** by comparing the hash in `/etc/shadow` against a freshly generated one (`openssl passwd -1 -salt <salt> <pw>`) rather than assuming the login will be accepted — a login form plus a 200 is not a credential check.
4. **Hand the credential to the user in a file, never in the conversation.** The chat is a log; write the temporary password, the URL, the user name and the change-it-now instruction to a file on their machine and point at it. Say that changing it will not lock them out, and make that true by having the key auth in place first.

## The guest's network layer: proto handlers, DHCP/DNS, and vendor-kernel quirks

Measured on a chrooted OpenWrt 25.x over a vendor 5.10 kernel; each item cost a live-debugging cycle.

- **Script proto handlers are PATH-dependent and load only at netifd start.** netifd registers `/lib/netifd/proto/*.sh` by running each one as `<script> <name> dump` at startup, and the dump calls `jshn` by bare name — so with `PATH=/` **no** handler registers (not even the stock `dhcp`) and interfaces fall back to `none`. Prove the layer with `ubus call network get_proto_handlers`; restart netifd after adding a handler (the list is built once).
- **An adopt-style proto owns what it reports.** `proto_init_update` + `proto_add_ipv4_address` + `proto_add_ipv4_route` + `proto_send_update` hand values to netifd, which then MANAGES them: an interface down flushes the addresses/routes it believes it installed. For an interface driven by an outside script (a modem data session), make the proto re-create address and route from the session's own state file when it finds the device bare, then RE-READ them from the kernel before reporting — never re-echo the file's copy, and validate values (IPv4 shape) before applying. Use `no_device=1` + `available=1` for a proto that does not own its device.
- **Busybox `ip` has no `-o`.** `ip -4 -o addr show` fails silently inside `$()` and yields an empty string; parse `ip -4 addr show dev <if> | awk '/inet /{print $2; exit}'`.
- **`uci set` without commit = active-only config.** The tooling sees it, a reboot does not. Use it for a runtime fallback (reduced bind when the full one is not ready), then `uci revert <pkg>` so a later respawn or a LuCI Save&Apply cannot make the reduced config permanent.
- **dnsmasq on a vendor kernel: do not trust procd's view.** The instance can read `"running": false` with `exit_code 1` while a healthy daemon serves, and `pidof` counts zombies. Start it AFTER the interface it serves has its address (a `dhcp-range` for a down interface is not served), check liveness by scanning `/proc/<pid>/stat` for a state other than `Z`, and keep a direct-spawn fallback (`dnsmasq -C /var/etc/dnsmasq.conf.<cfg> -x /var/run/dnsmasq/<cfg>.pid &`). Create `/tmp/resolv.conf.d/` yourself or the init aborts before launching anything.
- **No nf_tables on the vendor kernel.** `fw4`/`nft` cannot initialise; iptables (xtables) is built in. Lift the userspace from Alpine (musl, runs on the guest libc): `xtables-legacy-multi` + `libxtables` + `libip4tc`/`libip6tc` + the extension pack (`/usr/lib/xtables/*`; `libxt_MASQUERADE.so`, `libxt_standard.so`, `libxt_conntrack.so` are separate files) + a `libc.musl-aarch64.so.1` symlink to the loader. NAT shape: `-t nat -A POSTROUTING -s <lan-cidr> -o <wan> -j MASQUERADE` plus explicit FORWARD rules; verify positively (`-t nat -S POSTROUTING | grep -q MASQUERADE`) because a `-C || -A` chain fails silently.
- **Jail helpers break on vendor kernels — and a deleted helper comes back.** procd's `ujail` fails with `clone: Invalid argument`, taking dnsmasq/hostapd down with it; rename the binary away in the guest's boot script EVERY boot (the layered ramdisk restores it — see the concatenation rule in SKILL.md) and verify by process liveness, not by the init's exit code.
- **A guest AP with no `htmode` runs 802.11g.** An OpenWrt `wifi-device` without `htmode` yields `hw_mode=g` and no `ieee80211n`: `iw dev <ap> info` prints `no HT`, the PHY tops at 54 Mbit/s and a client measured ~8 Mbit/s end to end. `option htmode 'HT20'` took the same path to ~199 Mbit/s down / ~26 Mbit/s up.

## Never hand your control interface to the guest's network manager

The interface that carries your control channel is the one thing the guest must not reconfigure. Measured: running the guest's own `/etc/init.d/network start` handed the gadget netdev to `netifd`, and the host immediately went to `No route to host` while the gadget was still enumerated on USB — the transport was physically present and logically gone. Two consequences:

- Decide ownership before you run anything: either leave the interface **undeclared** in the guest's network config (your bring-up keeps it), or declare it and accept that the guest owns it. Do not discover the choice by experiment while your only channel rides that netdev.
- That script can also block for ever: with the manager it asks for never starting, a 300 s timeout expired with no output at all. Treat it as a mutating operation, not a probe.
- Recovery needs a physical power cycle and a re-write of the known-good image, so keep the current partition backed up on the host before every write.

## Writing a partition from inside the running guest

Once the guest is reachable, its own running system is the shortest flash path: no fastboot, no slot switch, no finger. Validated sequence:

1. **Enumerate from the kernel's own view**, not by guessing majors:

   ```sh
   for d in /sys/class/block/sd*; do n=${d##*/}
     if [ -f $d/partition ]; then echo "PART $n dev=$(cat $d/dev) start=$(cat $d/start) size=$(cat $d/size)"
     else echo "DISK $n dev=$(cat $d/dev) size=$(cat $d/size)"; fi
   done
   ```

   `start`/`size` are in **512-byte** units here, while a GPT header read at a 4 KiB logical sector size counts in 4096-byte units — do not mix them.
2. **Identify the target by name *and* offset.** Size alone does not identify a boot partition: four entries of exactly 96 MiB coexisted (`boot_a`, `boot_b`, `vendor_boot_a`, `vendor_boot_b`). Read the disk's GPT, take the partition whose name is the one you want, and confirm its `start_512` equals the `start` of the candidate node. That cross-check is what turns "probably this one" into a fact; the disk nodes are per-LUN (`/dev/sda` = LUN 0, `/dev/sdaN` = its partitions), so a boot partition can be absent from LUN 0's table entirely.
3. **Back up, write, read back, then reboot:**

   ```sh
   ssh root@<ip> 'dd if=/dev/sdXN bs=1M count=<n> 2>/dev/null' > /tmp/<part>-backup.img
   cat <image> | ssh root@<ip> 'dd of=/dev/sdXN bs=1M'
   ssh root@<ip> 'dd if=/dev/sdXN bs=1M count=<n> 2>/dev/null | sha256sum'   # compare with the local hash
   ```

   Only a matching digest licenses the reboot. Writing the boot partition of the slot you are *running from* needs no slot change at all: the bootloader re-reads that same slot on the next boot, so the running image replaces itself.
4. **Reboot through the kernel, not the guest's `reboot`.** A busybox `reboot` inside a chroot did nothing whatever — it returned silently and the next login showed the same PIDs. Use:

   ```sh
   echo 1 > /proc/sys/kernel/sysrq ; echo b > /proc/sysrq-trigger
   ```

   and confirm the device actually cycled by watching for the new process IDs (or a changed uptime) after it comes back, rather than by trusting the command's exit status.

## Unpacking the vendor ramdisk blob on the host

The vendor ramdisk pulled off a Qualcomm device is a compressed cpio blob, and the compression is usually LZ4 **legacy**, which `file` names as `LZ4 compressed data (v0.1-v0.9)`. Unpack it with the CLI rather than a language binding:

```sh
lz4 -d -l -f vendor_ramdisk00 out.cpio      # or: unlz4 -f vendor_ramdisk00 out.cpio
mkdir out && cd out && cpio -idm --quiet < ../out.cpio
```

Measured: 35 205 632 bytes of cpio, 337 files, **327 `.ko`** plus the `modules.load*` metadata — which makes this blob the authoritative host-side copy of everything the device can load, with no device access needed. Two failed attempts are worth avoiding: the Python `lz4` module may be absent (the CLI is usually there), and an `except` that swallows the decompression error prints a byte count of zero with the reason discarded. Never wrap an extraction step in a handler that hides why it failed; if a step can fail, make it print its own reason.

## A subsystem the guest cannot see: read the kernel's inventory first

When the guest reports nothing about a piece of hardware, the fault is more often a userspace omission than an unsupported device. Check the kernel's own view before theorising — and before changing anything:

```sh
ls  /sys/class/remoteproc/                     # empty => no remote processor (modem, DSPs) is up
ls  /sys/class/net/  | grep -c rmnet           # rmnet_* appear only once the modem data path exists
ls  /sys/module/     | grep -iE 'q6v5|rmnet|ipa|ipc_router|mhi|cnss'
ls  /lib/firmware /vendor/firmware_mnt         # the firmware path the kernel expects may not exist at all
dmesg | grep -iE 'remoteproc|q6v5|rmnet|ipa|modem'
```

Measured on a device whose SIM was invisible to the guest: `remoteproc` empty, no `rmnet_*`, none of the modem/accelerator drivers in `/sys/module`, no firmware mount — while the *infrastructure* those drivers need (`qmi_helpers`, `qcom_glink*`, `qcom_smd`) was already loaded. The correct reading is "the userspace never loaded the stack", not "the distribution does not support this hardware".

**Do not answer this by changing kernel version.** The vendor driver set for such a device is out-of-tree and bound to the stock kernel, and that kernel is the only reason storage, display, USB and the modem work at all; a newer mainline kernel for a partially-supported SoC whose specific device is not upstream trades a working device for a long porting project and still does not by itself produce a modem. Say so with the evidence above instead of proposing the kernel swap. If a *freely-licensed* WLAN driver is the real goal, that is a separate second-kernel experiment alongside this one, never a replacement for it.

Status of the modem bring-up itself: **open, with no verified sequence yet.** The materials are in hand (the vendor blob's 327 modules include the accelerator/WLAN stack — `ipa_fmwk`, the `mhi_*` family, `glink_*`, `cnss2`, `icnss2` — while `q6v5`/`rmnet`/`ipc_router` are not in that set and must be located elsewhere before concluding they are missing). The unverified path is: load the modem/accelerator module closure with its dependencies, make the firmware partition the kernel expects available, then check `/sys/class/remoteproc/*/state` for `running` and `/sys/class/net` for `rmnet_*` before touching QMI at all. Treat each of those as a measurement to run, not as steps known to work, and journal every one of them.
