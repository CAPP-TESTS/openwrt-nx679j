# Boot chain, flash e accesso device — NX679J

## Il device
- Nubia RedMagic 7 **NX679J**, SM8450/Waipio, kernel vendor 5.10.66, AArch64.
- Slot A = Android+Magisk (baseline e ORACOLO; riavviabile per vedere come driver/HWC pilotano l'hw). Slot B = OpenWrt.
- Bootloader sbloccato (`unlocked`/`orange`). **EDL NON funziona** su questa unità → recovery = slot A.
- OpenWrt 25.12.5 r33051 armsr/armv8, chroot su rootfs Android.

## Boot chain (fatti)
- `abl_a` ELF32 → volume UEFI → sezione LZMA → PE32+ LinuxLoader.
- DTB `dtb_idx=5`, overlay `dtbo_idx=35`; board-id `0x10008` dall'overlay.
- Stato slot nei bit GPT (non in misc).
- `reboot2` (da OpenWrt): `LINUX_REBOOT_CMD_RESTART2` con stringa `bootloader` → ABL → fastboot. Permette: OpenWrt → bootloader → `fastboot set_active a/b` → Android/Linux, tutto senza azione fisica.
- Riavvio normale: `reboot -f` (il reboot busybox non attraversa il chroot; alternativa `echo b > /proc/sysrq-trigger`).

## Accesso
- **USB gadget**: `ssh -i ~/.ssh/nx679j_key -o StrictHostKeyChecking=no root@10.0.0.1` (rtt ~3ms, canale preferito). Interfaccia host: `enp103s0f3u1`. Gadget NCM `18d1:4ee7`.
- **WiFi**: AP `NX679J-TEST` 5GHz; device su `192.168.77.1`.
- `scp` SEMPRE con `-O` (no sftp-server).
- La host key dropbear cambia ad ogni boot → `UserKnownHostsFile=/dev/null`.
- Helper del progetto: `experiments/20260920-wifi-luci/nxssh.sh 'COMANDO'`.

## Flash (interno, da SSH — mai fastboot per il ramdisk!)

**Protocollo forte** (il sync da solo NON basta — il primo v82 "sembrava ok" ma era cache):
```sh
N=/dev/sde41                      # boot_b
[ -b $N ] || { rm -f $N; mknod $N b 259 25; }
dd if=<img> of=$N bs=4M oflag=direct conv=fsync
sync -f $N 2>/dev/null; sync
dd if=$N bs=1M count=96 2>/dev/null | md5sum    # LETTURA FREDDA 1 (96MB = dimensione img)
sleep 3
dd if=$N bs=1M count=96 2>/dev/null | md5sum    # LETTURA FREDDA 2
# entrambe DEVONO = md5 dell'immagine, SOLO POI: reboot -f
```
Nota: se `/dev/sde41` esiste come file regolare vuoto → `rm -f` prima di `mknod`.

## Partizioni note
- `sde41` = boot_b (nodo `259:25`); `sde45` = dtbo_b; `sde52` = VNDRBOOT; `sde11` = rawdump (64MB, `/dev/rd`, PULITO al boot); `sdf1-5` = EFS (modemst1/2, fsg, fsc).
- `/rfs` = firmware modem (`modem_pr`) — readonly/readwrite, volatile.

## Immagini
- Sequenza: v54→v64 (WiFi), v76 (catena), v80-84 (MM standard), v85-87 (persistenza).
- **v87 corrente** (`boot_b-v87-final.img`, md5 `0d23aa57b0a1365975c2d5ef57826c66`), v84 safe (`5dc9fc4d2c171e43`), v76 SAFE.
- Build: `build-vNN.py` (estratto di build-v87.py) — estrae `owrt-liveNN.tar.gz` in `build-v64/overlay/owrt/`, applica override, cpio + lz4 + header + sig. ramdisk ~44MB (boot image 96MB fissi).

## Il boot (v84+)
`/init` (PID1, Android) → ... → `switch.sh` (dal ramdisk) prepara il chroot: mount, copia binari, mount interni, **PERSIST-INSIDE** (v87: estrae i tar), ubusd, dropbear, wifi, rcS → `/etc/«redacted»-boot-services.sh` (tool symlink, S70-95) → mm-standard-boot (modem) → display-late (dopo uptime>900).

## Tracce utili
- `/«redacted»-journal` (root Android): log del boot `say` di switch.sh + SSH. Ruota (256KB) — per i fatti del boot, verificare il RISULTATO non il log.
- `/tmp/chain.log`, `/tmp/mm-standard-boot.log`: log del modem.
- `dmesg` per MSS/remoteproc.
