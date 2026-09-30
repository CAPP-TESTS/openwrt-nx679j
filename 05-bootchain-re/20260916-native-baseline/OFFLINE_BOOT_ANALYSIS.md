# NX679J — analisi offline del readback attuale

## Risultato

La coppia kernel/ramdisk attualmente su `boot_b` fallisce in QEMU con `Initramfs unpacking failed: broken padding`. Lo stesso identico kernel con l'archivio OpenWrt integro raggiunge `/init`, poi `/sbin/procd` come PID 1, e una console interattiva OpenWrt 24.10.0. Il difetto di formato è provato offline; non è provato che il telefono arrivi al punto di estrazione della ramdisk.

Nessuna scrittura, flash, modifica BCB/GPT o riavvio del telefono è stato eseguito durante questa analisi. I test QEMU hanno usato TCG con macchina `virt`, senza block device del telefono, passthrough, reti o filesystem host condivisi.

## Kernel e ramdisk attuali

- Fonte: `current-readback/boot_b.img`, 100663296 byte, SHA-256 `b8ab71f95a40e2445c8f3413a2b22a3a6766a5e9651186fadaf50d21936004f4`.
- Kernel: 35312128 byte, SHA-256 `69d6f75c4b064c848ec65d402c3de5f0262420033110c2fae2d46d06b9f7d9eb`.
- Versione osservata nell'Image e in QEMU: `Linux version 6.6.60 ... #4 SMP PREEMPT Fri Aug 7 00:51:25 CEST 2026`.
- Il kernel coincide byte per byte con `/home/user/nx679j-stock/kernel-patch-test/linux-6.6.60/arch/arm64/boot/Image` (hash ricalcolati).
- Ramdisk: 57120448 byte, SHA-256 `0780f62babd8b550597dadeeb5c52206d95a03e9aee2a1414778bbb754b967f1`.
- Header Android v4, firma v4 assente, cmdline vuota, patch level 2024-01: `unpacked-current/boot_b-info.txt`.
- Ramdisk a offset 35319808 nell'immagine. I primi 512 byte contengono un archivio newc minimale (`dev`, `dev/console`, `root`, `TRAILER!!!`); primo byte non-zero successivo a offset 513 nella ramdisk. Non è una concatenazione valida di normali archivi initramfs a quel punto.

## Esperimento A: payload reale letto da B

Comando completo e tempi: `qemu-current-b.json`. Log: `qemu-current-b.log`.

- Riga 157: `Initramfs unpacking failed: broken padding`.
- Riga 260: `Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0)`.
- Il processo QEMU termina con rc=0 per `panic=1` e `-no-reboot`: il codice di uscita del processo non significa boot riuscito.

## Esperimento B: cambia solo la ramdisk

Procedura ripetibile: `python3 qemu-ramdisk-control.py`. Comando e risultati: `qemu-openwrt-control.json`. Log: `qemu-openwrt-control.log`.

Input sostitutivo: `/home/user/nx679j-stock/port-work/openwrt-phase1/openwrt-rootfs.cpio.gz`.

- SHA-256: `b7dfd262f7d408ba2931cb71bb0196c2e3b92c3c8534ad4e849cdbcbd5d0458d`.
- Dimensione compressa: 4242661 byte; espansa: 14455296 byte.
- Parser newc: 1254 record inclusa terminazione; solo zero dopo il trailer.
- Config QEMU, kernel e cmdline equivalenti al test A, ramdisk sostituita.
- Osservato: `Run /init as init process`, `procd: - init -`, console BusyBox interattiva.
- Comandi eseguiti nella VM: `uname -r`, `cat /etc/openwrt_release`, `readlink /proc/1/exe`, `/proc/1/cmdline`, uptime, `poweroff -f`.
- Risultati: `6.6.60`, OpenWrt `24.10.0 r28427-6df0e3d02a`, `PID1=/sbin/procd`.
- Nessun errore unpack initramfs e nessun kernel panic. Spegnimento richiesto nella VM.

## Perché questo non è ancora un candidato fisico

Il file `ramdisk-analysis/current-b.config` proviene da IKCONFIG dentro l'Image letto dal telefono, non da una configurazione ipotizzata.

| Opzione | Stato osservato |
|---|---|
| `CONFIG_USB_F_NCM` | `m` |
| `CONFIG_USB_F_ACM` | `m` |
| `CONFIG_USB_CONFIGFS` | `m` |
| `CONFIG_PHY_QCOM_USB_SNPS_FEMTO_V2` | `m` |
| `CONFIG_PHY_QCOM_QMP_UFS` | `m` |
| `CONFIG_SCSI_UFS_QCOM` | `m` |
| `CONFIG_USB_G_NCM` | non impostata |
| `CONFIG_PSTORE_RAM` | non impostata |
| `CONFIG_PSTORE_CONSOLE` | non impostata |
| `CONFIG_PSTORE_PMSG` | non impostata |

L'archivio OpenWrt integro contiene `/lib/modules/6.6.73`, non moduli per `6.6.60`. QEMU segnala `kmodloader: no module folders for kernel version 6.6.60 found`. Non si può dedurre da questo test alcun supporto operativo di USB/UFS/PHY su NX679J.

Il DT running Android riserva ramoops dinamicamente: `size=0x200000`, `pmsg-size=0x200000`, `mem-type=2`, senza `reg` fisso, `record-size` o `console-size` espliciti (`current-device/running_fdt_20260916.dts:377`). Questa descrizione non stabilisce un log console persistente in una variante upstream.

## Vendor boot attuale

`unpack_bootimg` dei readback A/B mostra stessi header, dimensioni, bootconfig e vendor ramdisk:

- `vendor_ramdisk00` SHA-256 identico: `8e9280413248b94e7e03685965087ca691777e85f51e3a494875582d2c62fb51`.
- `bootconfig` SHA-256 identico: `fbab70e49b81a2c679a7802d8e27712323ed79a2a7116bab037cf29b5198cba6`.
- DTB container A SHA-256: `b67334af3954fc47bc07cefb8d7fc1c7a59ce8258c2b4c8a55bed92fee5c88ad`.
- DTB container B SHA-256: `d6891d5ec81be11319f072ddaa6ccd002aec2d0d01a6297af2b2bbf171ca3441`.
- La differenza è quindi nel DTB (oltre all'eventuale metadata/padding dell'immagine); il primo blob mostra `kvm-arm.mode=nvhe` anteposto ai bootargs. L'analisi completa di tutti i blob è separata.

## Decisione

Non ritentare il boot dell'attuale B come se fosse l'immagine 6.12 descritta dall'handoff. La nuova baseline identifica una variante successiva con ramdisk difettosa e dipendenze USB modulari non risolte. Prima del test fisico occorre un'immagine documentata che conservi il percorso di recovery, dia un segnale da `/init`, includa le dipendenze esatte dei driver e gestisca consapevolmente il comando BCB `bootonce-bootloader` già osservato.
