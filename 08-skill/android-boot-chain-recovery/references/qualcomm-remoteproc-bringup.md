# Qualcomm remoteproc bring-up on a stock kernel (modem / adsp)

Use when the deliverable is a remote processor — the modem above all — running under a custom userspace on the stock Qualcomm kernel, the same kernel that boots Android fine. The working system is the specification: read it first, then reproduce its mechanics in the init.

## 1. Read the working system (the reference)

Boot the known-good Android slot and take four readings, each naming a mechanism the custom side must reproduce:

- `adb shell su -c dmesg | grep -iE 'remoteproc|q6v5|mss|firmware'` — how the driver really loads firmware. Measured pair:
  `qcom_q6v5_pas 4080000.remoteproc-mss: Direct firmware load for modem.b11 failed with error -2`
  `qcom_q6v5_pas 4080000.remoteproc-mss: Falling back to sysfs fallback for: modem.b11`
  `ueventd: firmware: loading 'modem.b11' for '/devices/.../remoteproc-mss/firmware/modem.b11'`
  => the kernel's own path is `/lib/firmware`; the fallback is served by userspace (`ueventd`).
- `/sys/class/remoteproc/` — which rprocs exist (`spss adsp cdsp slpi mss`), each with `name` (the DT node) and `state`. The **device existing** means the probe succeeded; empty/`offline` is the idle state and the firmware is not loaded yet — do not wait for `running` as the criterion of success at this stage.
- `/proc/modules` — the live module set with refcounts (`qcom_q6v5_pas` live, `mdt_loader` / `rproc_qcom_common` at high refcounts, `rmnet_*` / `ipam` for the data path).
- `mount | grep -i firmware` — which partition carries the firmware and how it is mounted (measured: `/dev/block/sde6 on /vendor/firmware_mnt type vfat ro`; that block device is the `modem_a` partition).

## 2. Serve the firmware where the kernel asks for it

- The direct path is `/lib/firmware`. A custom initramfs has no `ueventd`, so the sysfs fallback is never answered and the files must exist at `/lib/firmware`. Bind the partition's `image/` directory onto it: `mount("/vendor/firmware_mnt/image", "/lib/firmware", NULL, MS_BIND, NULL)` — mount 40, fstype NULL, `MS_BIND` = 4096.
- The mount point must exist or `mount()` returns `ENOENT`: create it with `mkdirat(34, AT_FDCWD, path, 0755)`. Busybox `mount --bind` creates nothing either.
- `vfat` is a **module**: mounting the firmware partition before the module chain fails, and the failure looks like a missing source rather than a missing module. Do the firmware mounts after the chain (or load `vfat` / `nls_cp437` first). Measured: the same bind returned `ENOENT` from the init pre-chain and `OK` from the busybox script post-chain on the same boot.
- The firmware is requested at **start**, not at probe: a registered rproc shows empty `state`/`firmware` until started, then the boot logs `powering up` / `Booting fw image <name>.mdt` / `remote processor ... is now up`.

## 3. Why a remote processor never probes: the supplier with no driver

The classic deferred list can be empty while the device is still blocked.

- For each `supplier:platform:<name>` symlink under `/sys/bus/platform/devices/<dev>/`, read the supplier's own `driver` symlink; an empty one is the blocker, and the consumer's `waiting_for_supplier` attribute names it.
- Measured root cause of a modem that never probed while every module dependency was loaded: all nine `soc:qcom,smp2p-*` devices had **no driver**, because `smp2p.ko` was never in any load list. The consumer's probe calls `qcom_smem_state_get()` and defers for ever.
- Check two layers, not one: the driver module's `modules.dep` closure, and the **providers of the symbols it resolves**. A symbol provider that is itself a module can be absent while the dependency closure looks complete. `/proc/modules` and `/sys/module` are the ground truth, not the list you wrote.

## 4. The deferred-probe retry and how to trigger it

- A deferred device is retried when a **new driver registers**, while the retry window is open (measured on this kernel: 1424 s). Loading a module that is not yet loaded (`finit_module(273)` on a never-loaded `.ko`) makes the kernel walk its deferred list again.
- A module whose symbol dependencies are missing fails with `Unknown symbol ... (err -2)` and triggers nothing — supply the dependency first (measured: `qcom_glink_spss.ko` before `qcom_spss.ko`).
- `delete_module(106)` + `finit_module` of an already-loaded module also produces a registration, but loading a fresh module is one step instead of two.
- This is how a fix made *after* the chain (firmware now visible) reaches devices that failed *during* the chain: bind/mount, then register, then re-read `/sys/class/remoteproc/`.
- Confirm by name, not by count: `remoteprocN: name=4080000.remoteproc-mss state=offline` appearing means the probe ran and succeeded.

## 5. Starting it

- `echo start > /sys/class/remoteproc/<N>/state` is **synchronous**: it runs the remote processor's boot in the caller's context.
- Never write it from PID 1 or the foreground of the process that owns your control channel. Measured: a foreground `start` stalled the init before the hand-over — the gadget enumerated, but SSH and the diagnostics relay never came up, and recovery needed a physical action. Use a background child/subshell (`( … ) &`), or do it after the hand-over.
- Background is necessary but not sufficient: a start that lands **while the probe is still in flight** makes the probe fail and the device get released. Measured: a background start fired the instant the module chain ended and `remoteproc ...: releasing <name>` was logged for every processor at exactly that second, while the same image with a 25 s delay registered all five and brought four up `running`. Wait for the device, read `state` as `offline`, then start.
- Verify the start landed (`state` changed from `offline`): a scheduled start from a background child can silently not fire (measured: no journal line and states still `offline`, while the identical writes from a live shell worked). Trust the state file, not the script that was supposed to write it.
- Success shape: `state=running` and `dmesg` showing `Booting fw image adsp.mdt` — firmware coming from your own `/lib/firmware`.

## 5b. Bring up the whole stack, or it takes the system down

- A remote processor running **without its data path** destabilises the SoC. Measured: with the modem `running` but no `rmnet_*`/`ipam`/`ipa_*` loaded, the system entered a reboot loop (~55 s up, ~40 s down, repeating); the same image with the processors merely registered (`offline`) ran stable for minutes. Treat "processor up" as a milestone that is only safe once the stack it serves is loaded.
- The vendor ramdisk is **not** the complete vendor module set: measured, the data-path modules (`rmnet_core`, `ipam`, `ipa_clientsm`, the `rmnet_*` family) were absent from its 327 `.ko` (the copied count stayed at 15 as 30 names were submitted), while Android runs those names from its **vendor partition** (`/vendor/lib/modules`). Source them from the partition, not from the ramdisk, and assert the copy count equals the list — the build skips missing names silently.
- The loader that decides is the kernel-side `finit_module` from your own code, not the distro's: measured, `kmodloader /lib/modules/rmnet_core.ko` exited 255 with **no output and no dmesg line**, while the init's `finit_module` on the same file loaded it — `rmnet_core`, `ipam`, `rmnet_shs` all `OK`, `rmnet_ctl` `EEXIST` — after both the chain and `kmodloader` had refused them. Retry a batch's failures once at the end: dependency-order refusals surface as the names that needed a later provider (`ipa_clientsm` returned `ENOENT` while its provider loaded later in the same batch).
- Pull vendor-partition copies through `su`: `adb pull /vendor/lib/modules/<name>.ko` is `Permission denied` for the shell user; `su -c 'cp /vendor/lib/modules/<names> /data/local/tmp/ && chmod 644 /data/local/tmp/*.ko'` followed by `adb pull /data/local/tmp/` works. A `vermagic` string that differs from `uname -r` is not why a module is refused — measured: all 341 modules in the image (327 from the ramdisk, 14 from the vendor partition) declared `5.10.66-gki-g491fe99db339-dirty` against a kernel reporting `5.10.66-android12-9-00005-...`, and 131 of them loaded.

## 6. Raw syscalls for a freestanding init (aarch64)

| call | nr | shape to get right |
|---|---|---|
| openat | 56 | `(dirfd, path, flags, mode)`; dirfd `AT_FDCWD` = -100 — passing the path first yields an errno that matches none of the usual ones |
| faccessat | 48 | `(dirfd, path, mode, flags)` — four arguments |
| mkdirat | 34 | `(dirfd, path, mode)` — create mount points before mounting |
| mount | 40 | `(dev, dir, fstype, flags, data)`; bind = fstype NULL, flags `MS_BIND` 4096 |
| finit_module | 273 | `(fd, "", 0)` — the probe runs in the caller |
| delete_module | 106 | `(name, 0)` |

Log each call's raw return value, or map it exhaustively: a collapsed `KO` / `altro` destroys the one datum that identifies the cause, and each destroyed errno costs a flash cycle.

## 7. Il canale di servizio (GLINK/QMI): dove sta e come si scopre

- I nodi dei canali non esistono da soli (nessun `devtmpfs`): ogni canale dichiara il suo `major:minor` in `/sys/class/glinkpkt/<nome>/dev` e il nodo si crea con `mknod` (misurati: `smdcntl8`=504:2, `smd11`=504:5, `smd7`=504:3, `smd8`=504:4, `at_mdm0`=504:0).
- Per sapere **quale** canale usa davvero lo stack che funziona, non indovinare fra i nomi: enumera i fd dei processi sul sistema funzionante e risolvi i device aperti (`ls -l /proc/*/fd 2>/dev/null | grep -oE '/dev/(smd|qmi|rmnet|mhi)[a-zA-Z0-9_.-]*' | sort | uniq -c`), poi identifica il processo proprietario con `comm`/`cmdline` (misurato su Android: il processo `radio` tiene aperto `/dev/rmnet_ctrl`, `qmipriod` non tiene aperto nessun device). Quello che *tu* crei non è la prova: conta cosa apre chi parla davvero col modem.
- Eliminazioni verificate, per non ripercorrerle: il device `mhi_1103_00.01.00` (pipe `488:0` / `488:1`) è il **WiFi** `cnss` su PCIe RC0 — lo dice il suo stesso dmesg (`cnss: Setting MHI state`, `msm_pcie_...`, `cnss_pci 0000:01:00.0`, canale `IPCR`), non il modem. Il modem **non** passa da QRTR su questa piattaforma (esistono solo `mhi_qrtr_cnss` e `qrtr-gunyah`). `qmi_encap` non esiste nel set vendor: il framing QMI lo fa il RIL in userspace.
- Un modulo che fallisce può dirsi da solo cosa *non* è: `ipa_clientsm` manca dei simboli `ipa3_*_ipc_logbuf` ⇒ è il driver client del tethering USB, non il data path del modem — non inseguirlo come blocco.
- Con il modem `running` compaiono nuovi char device dell'IPA: leggi `/proc/devices` a modem avviato (misurati: 501 `ipa_odl_ctl`, 502 `ipa_adpl`, 503 `ipa`).
- **Aperto**: nessuno dei canali legacy ha risposto alla sonda QMUX (`smd11`, `smd7`, `smd8`, `smdcntl8`). La prossima misura identificata è catturare il dialogo reale del processo `radio` sul sistema che funziona (fd/fdinfo, `strace`) e replicarne canale e framing — non provare un altro canale a caso: ogni tentativo con il modem acceso e senza interlocutore costa un riavvio del sistema.
