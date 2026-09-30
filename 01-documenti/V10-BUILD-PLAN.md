# v10 — OpenWrt nativo sul NX679J: piano di costruzione (verificato, pronto da eseguire)

Data: 2026-09-17. Metodo: costruzione diretta con script, nessun subagent.

## Fatti verificati (non ipotesi)

**Rootfs OpenWrt** (già scaricato e verificato):
- `experiments/openwrt-rootfs/owrt-aarch64-rootfs.tar.gz` — 5.853.270 B,
  sha256 `2aaddba935ea4e6ad300bfc9946c4d944e2693af6246251e528ed8e27133e962`, 1307 voci
- OpenWrt **25.12.5**, target `armsr/armv8`, `aarch64_generic` (musl); estratto in `root/` = **19 MB**
- `/sbin/init` = procd (16.649 B) · `/bin/busybox` (421.909 B) · `/lib/ld-musl-aarch64.so.1 -> libc.so`
- `/etc/init.d/`: boot, network, dropbear, firewall, dnsmasq, odhcpd, log, cron, done, led...
- **MANCA `/etc/config/network`** → va creato con `usb0` static 10.0.0.1/24
- 111 `.ko` OpenWrt: **incompatibili col kernel 5.10 → NON caricarli**; il bring-up usa i moduli vendor

**Base da modificare** (`experiments/20260917-init-v9/`):
- `nx679j-init-v9.c` (18.394 B) = il PID 1 attuale · `nx679j-relay.c` · `build-candidate-v9.py` (38.375 B)
- worker: `experiments/20260917-init-v8/nx679j-worker.c` (logica invariata tra v8 e v9)
- toolchain: `aarch64-linux-gnu-gcc 16.1.0`, `-static -Os`; container: patch **in-place** dell'immagine v9
  (kernel, cmdline, DTB, coda AVB intatti)
- già funzionanti su hardware: UDC `a600000.dwc3`, `usb0 10.0.0.1/24`, ping 3/3 dal PC,
  relè in sola lettura su TCP **:9999** (`nc -N 10.0.0.1 9999`)

## Design v10 — il minimo codice possibile

1. **ramdisk**: il rootfs sotto `/owrt` + i file v9 (`/init`, `/nx679j/worker`, `/nx679j/relay`)
   + `/nx679j/switch.sh` + `/owrt/etc/config/network` + `/owrt/etc/dropbear/authorized_keys`
2. **`/nx679j/switch.sh`** (busybox sh, eseguito DOPO journal+gadget+moduli):
   - `mount -t proc proc /owrt/proc`, `mount -t sysfs sys /owrt/sys`, `mount -t tmpfs tmpfs /owrt/dev`
   - popolare `/dev` (mdev) — il kernel stock **non ha devtmpfs**
   - `exec chroot /owrt /sbin/init`
3. **init**: **una riga** — dopo il bring-up esegue `execve("/owrt/bin/busybox", ["busybox","sh","/nx679j/switch.sh"])`;
   se `execve` fallisce, prosegue nel ciclo di mantenimento (fallback: il telefono resta raggiungibile)
4. **/dev**: mdev + nodi statici minimi (console, null, zero, tty) come rete di sicurezza
5. **SSH**: chiave generata sul PC (`ssh-keygen -t ed25519 -f ~/.ssh/nx679j_key`), pubblica in
   `authorized_keys`. Test: `ssh -i ~/.ssh/nx679j_key root@10.0.0.1`

## Passi meccanici (script, secondi)

a. ricompilare l'init: `aarch64-linux-gnu-gcc -static -Os -s -o init.new nx679j-init-v10.c`
b. cpio: staging → `cpio -o -H newc` → **riscrivere uid/gid=0** (passata python) → `lz4 -l -9`
c. patch del container v9 in-place (campo `ramdisk_size` + regione ramdisk, resto intatto)
d. verifica: sha256, `cmp` contro v9 (differenze **solo** in header+ramdisk), round-trip lz4→cpio,
   presenza di `/owrt/sbin/init`, `/nx679j/switch.sh`, `/owrt/etc/config/network`
e. flash: `fastboot flash boot_b` → `set_active b` → reboot → attesa gadget (~40 s) → SSH

## Criterio di successo (misurabile)

`ssh -i ~/.ssh/nx679j_key root@10.0.0.1 'cat /etc/openwrt_release'` risponde con OpenWrt 25.12.5,
e il canale resta stabile. Prove di supporto: il relè resta raggiungibile su :9999.

## Rischi noti, da verificare per primi

1. **`/dev` senza devtmpfs**: primo sospetto di blocco di procd → se procd non parte, guardare
   `/dev/console`, `/dev/null`, `pts` e la presenza di `mdev`.
2. **procd come PID 1 dopo `chroot`**: `/proc` e `/sys` devono essere montati *dentro* `/owrt`
   prima dell'exec (lo fa `switch.sh`).
3. **Il relè** è figlio del PID 1 precedente: sopravvive al `chroot` ma **conserva la radice vecchia**
   → non usarlo come prova che OpenWrt gira; la prova è l'SSH.
4. Se l'SSH non risponde: leggere il dump del relè (sezione kernel/journal) **prima** di riflashare.

## Nota di metodo (richiesta dall'utente)

Niente subagent per costruire le immagini: download, compilazione, impacchettamento e verifica
si fanno qui, con script; il test funzionale lo fa il telefono. Il download+verifica del rootfs
ha richiesto 4,3 secondi contro ~40 minuti passando da un subagent.
