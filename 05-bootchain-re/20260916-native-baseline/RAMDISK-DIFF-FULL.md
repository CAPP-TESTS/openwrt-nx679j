# Confronto esaustivo delle ramdisk: Android funzionante vs nostra v5
Data: 2026-09-17. Metodo: parsing completo cpio newc di entrambe le ramdisk, voce per voce (nome, tipo, modo, uid/gid, dimensione, sha256 del contenuto), poi classificazione per impatto sul boot del kernel.
## 1. Input e contenitori
| | Android (funziona) | nostra v5 (non parte) |
|---|---|---|
| sorgente | `magisk_patched-30700_Zx2eF.img` (ramdisk del boot) | `boot_b-minimal-gadget-v5.img` |
| ramdisk compresso | 2.200.983 B | 1.147.147 B |
| magia | `02214c18` = LZ4 **legacy** | `1f8b0800` = **gzip** |
| non compresso | 2.306.300 B | 3.598.688 B |
| formato | cpio newc (`070701`) | cpio newc (`070701`) |
| voci | **30** | **86** |

> Nota: anche `vendor_ramdisk00` del device e' LZ4 legacy (`02214c18`). La nostra era l'unica gzip. Test `probe3` l'ha ricompressa in LZ4 legacy: **fallisce ugualmente**, quindi il formato non e' la causa.

## 2. Inventario delle differenze (tutte)
- voci comuni: **5**
- solo Android: **25**
- solo nostre: **81**
- stesso percorso, **contenuto diverso**: **1**
- stesso percorso, **metadati diversi**: **2**

### 2a. Contenuto diverso (1)
| percorso | Android | nostra |
|---|---|---|
| `init` | file 263928 B sha 8e26e33c3db28482 | file 10507 B sha 4ebac295a09d24ec |

L'unica voce con contenuto diverso e' **`/init`**: ELF AArch64 263.928 B contro script `#!/bin/sh` di 10.507 B.

### 2b. Metadati diversi (2)
- `TRAILER!!!`: Android 0o755 0:0 — nostra 0o0 0:0
- `init`: Android 0o100750 0:0 — nostra 0o100755 0:0

### 2c. Solo in Android (tutte le 25)
- `.backup` [dir, 0 B]
- `.backup/.magisk` [file, 143 B]
- `.backup/.rmlist` [file, 99 B]
- `.backup/init.xz` [file, 891140 B]
- `debug_ramdisk` [dir, 0 B]
- `first_stage_ramdisk` [dir, 0 B]
- `first_stage_ramdisk/debug_ramdisk` [dir, 0 B]
- `first_stage_ramdisk/dev` [dir, 0 B]
- `first_stage_ramdisk/metadata` [dir, 0 B]
- `first_stage_ramdisk/mnt` [dir, 0 B]
- `first_stage_ramdisk/proc` [dir, 0 B]
- `first_stage_ramdisk/second_stage_resources` [dir, 0 B]
- `first_stage_ramdisk/sys` [dir, 0 B]
- `metadata` [dir, 0 B]
- `mnt` [dir, 0 B]
- `overlay.d` [dir, 0 B]
- `overlay.d/sbin` [dir, 0 B]
- `overlay.d/sbin/init-ld.xz` [file, 1556 B]
- `overlay.d/sbin/magisk.xz` [file, 181004 B]
- `overlay.d/sbin/stub.xz` [file, 963636 B]
- `second_stage_resources` [dir, 0 B]
- `system` [dir, 0 B]
- `system/etc` [dir, 0 B]
- `system/etc/ramdisk` [dir, 0 B]
- `system/etc/ramdisk/build.prop` [file, 914 B]

### 2d. Solo nostre (tutte le 81)
- `bin` [dir, 0 B]
- `bin/basename` [link, 7 B]
- `bin/busybox` [file, 458773 B]
- `bin/cat` [link, 7 B]
- `bin/dd` [link, 7 B]
- `bin/head` [link, 7 B]
- `bin/ip` [link, 7 B]
- `bin/ln` [link, 7 B]
- `bin/ls` [link, 7 B]
- `bin/mkdir` [link, 7 B]
- `bin/mknod` [link, 7 B]
- `bin/mount` [link, 7 B]
- `bin/rm` [link, 7 B]
- `bin/sh` [link, 7 B]
- `bin/sleep` [link, 7 B]
- `bin/sync` [link, 7 B]
- `bin/tail` [link, 7 B]
- `bin/uname` [link, 7 B]
- `bin/wc` [link, 7 B]
- `bin/which` [link, 7 B]
- `dev/console` [chardev, 0 B]
- `dev/kmsg` [chardev, 0 B]
- `dev/null` [chardev, 0 B]
- `dev/pmsg0` [chardev, 0 B]
- `dev/tty` [chardev, 0 B]
- `dev/zero` [chardev, 0 B]
- `lib` [dir, 0 B]
- `lib/ld-musl-aarch64.so.1` [link, 7 B]
- `lib/libc.so` [file, 590852 B]
- `lib/libgcc_s.so.1` [file, 131088 B]
- `lib/libubox.so.20240329` [file, 65633 B]
- `lib/modules` [dir, 0 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604` [dir, 0 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/altmode-glink.ko` [file, 32800 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/clk-qcom.ko` [file, 347896 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/crypto-qti-common.ko` [file, 55472 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/crypto-qti-hwkm.ko` [file, 17320 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/debug-regulator.ko` [file, 53952 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/dwc3-msm.ko` [file, 237896 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/fsa4480-i2c.ko` [file, 22456 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/gdsc-regulator.ko` [file, 33088 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/minidump.ko` [file, 127424 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/nvmem_qcom-spmi-sdam.ko` [file, 14376 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/nvmem_qfprom.ko` [file, 25832 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/pdr_interface.ko` [file, 30760 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/phy-msm-snps-eusb2.ko` [file, 44520 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/phy-msm-snps-hs.ko` [file, 36208 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/phy-msm-ssusb-qmp.ko` [file, 47888 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/phy-qcom-emu.ko` [file, 16376 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/phy-qcom-ufs-qmp-14nm.ko` [file, 20088 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/phy-qcom-ufs-qmp-v3.ko` [file, 24536 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/phy-qcom-ufs-qmp-v4-cape.ko` [file, 27208 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/phy-qcom-ufs-qmp-v4-diwali.ko` [file, 27208 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/phy-qcom-ufs-qmp-v4-lahaina.ko` [file, 27208 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/phy-qcom-ufs-qmp-v4-waipio.ko` [file, 27208 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/phy-qcom-ufs.ko` [file, 40120 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/pmic_glink.ko` [file, 34320 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/proxy-consumer.ko` [file, 22472 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/qcom-scm.ko` [file, 202536 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/qcom_glink.ko` [file, 88136 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/qcom_glink_smem.ko` [file, 20112 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/qcom_glink_spss.ko` [file, 20400 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/qcom_ipc_logging.ko` [file, 55808 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/qcom_smd.ko` [file, 44760 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/qmi_helpers.ko` [file, 39480 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/qti-regmap-debugfs.ko` [file, 29832 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/repeater.ko` [file, 16504 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/rproc_qcom_common.ko` [file, 63848 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/smem.ko` [file, 25392 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/ssusb-redriver-nb7vpq904m.ko` [file, 49088 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/ucsi_glink.ko` [file, 37616 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/ufs_qcom.ko` [file, 156408 B]
- `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/ufshcd-crypto-qti.ko` [file, 18200 B]
- `sbin` [dir, 0 B]
- `sbin/insmod` [link, 10 B]
- `sbin/kmodloader` [file, 65601 B]
- `sys/fs` [dir, 0 B]
- `sys/fs/pstore` [dir, 0 B]
- `sys/kernel` [dir, 0 B]
- `sys/kernel/config` [dir, 0 B]
- `tmp` [dir, 0 B]

## 3. Classificazione per impatto (dalla piu' alta alla piu' bassa)

### Classe 1 — impatto critico sul percorso di boot
| Differenza | Perche' conta | Stato |
|---|---|---|
| `/init` e' uno **script** `#!/bin/sh` invece che un ELF statico | il kernel deve risolvere `#!` -> `/bin/sh` -> busybox -> loader musl. Se un anello non regge, il kernel non ottiene un PID 1 | **smentito come causa diretta**: la catena e' integra e verificata in QEMU con lo stesso kernel |
| l'init **dipende da /bin/sh, /bin/busybox, /lib/ld-musl-aarch64.so.1, /lib/libc.so** | Android non ha nulla di tutto cio' (il suo init e' statico): e' la differenza strutturale piu' grande | verificato presente e coerente nella nostra ramdisk |

### Classe 2 — impatto alto, dimostrato come causa del silenzio
| Differenza | Perche' conta |
|---|---|
| **i moduli caricati dall'init** | noi carichiamo 40 moduli in ordine arbitrario; il device dichiara le dipendenze in `modules.softdep`: `smem pre: qcom_hwspinlock`, `dwc3_msm pre: phy-generic phy-msm-snps-hs phy-msm-ssusb-qmp eud` |
| **i moduli mancanti**: `qcom_hwspinlock`, `phy-generic`, `eud` | senza di essi `smem.ko` non si carica (-> niente UFS -> niente journal) e `dwc3_msm.ko` non si carica (-> niente USB -> niente gadget) |
| il vendor ramdisk fornisce `modules.load` (98), `modules.load.recovery` (329), `modules.dep`, `modules.softdep`, `modules.alias`, `modules.blocklist` e 327 `.ko` piatti in `lib/modules/` | e' la sorgente autorevole: il nostro init non la usa affatto |
| **287 moduli del device che non carichiamo mai** | fra cui le dipendenze reali; la lista recovery ne carica 329 |

### Classe 3 — nessun impatto sul kernel, servono solo all'init di Android
`.backup/` (`.magisk`, `.rmlist`, `init.xz`), `debug_ramdisk/`, `first_stage_ramdisk/*` (incluso `build.prop`), `metadata/`, `mnt/`, `overlay.d/sbin/{init-ld,magisk,stub}.xz`, `second_stage_resources/`, `system/etc/ramdisk/build.prop`.
Sono l'infrastruttura Magisk + first-stage di Android: il kernel non li guarda. **Non possono essere la causa.**

### Classe 4 — nostre aggiunte, nessun impatto negativo
`dev/{console,kmsg,null,pmsg0,tty,zero}` (Android nel ramdisk **non ha nodi device**: il nostro e' piu' completo), `bin/busybox` + 18 applet, `lib/libubox.so`, `sbin/kmodloader`, `sys/{fs/pstore,kernel/config}`, `tmp/`.
Rilevante: con la nostra ramdisk il primo-stage **puo'** caricare moduli (Android in questa fase non carica `dwc3_msm`: non e' nella sua lista dei 98, lo carica piu' tardi da altre partizioni — che noi non montiamo).

## 4. Ipotesi falsificate una per una (con il test che le ha uccise)
| Ipotesi | Test | Esito |
|---|---|---|
| la firma AVB dell'ABL blocca | Magisk (firma non valida) su slot B | **avvia** -> falsificata |
| `signature_size=0` non piace all'ABL | probe2 con firma 4096 | fallisce -> falsificata |
| la cmdline e' fatale (`kvm-arm.mode=protected`, `qcom-dload-mode`) | probe1: Magisk + nostra cmdline | **avvia Android** -> falsificata |
| la ramdisk e' troppo grande | v5, 1,09 MiB (meta' di quella Android) | fallisce -> falsificata |
| il formato di compressione (gzip vs LZ4) | probe3: LZ4 legacy | fallisce -> falsificata |
| il kernel non supporta il gzip initramfs | QEMU con lo stesso kernel | decomprime e esegue -> falsificata |
| il kernel non esegue il nostro /init | QEMU: `Run /init as init process` + log dell'init | esegue -> falsificata |
| l'init di Android (vendor ramdisk) sovrascrive il nostro | inventario del vendor ramdisk: nessun `/init` | falsificata |

## 5. Conclusione
Il contenuto della ramdisk e' ok e l'init gira; la differenza che **spiega il silenzio** e' il **modo in cui carichiamo i moduli**: ordine arbitrario e softdependencies del device ignorate, con `qcom_hwspinlock`, `phy-generic` ed `eud` assenti dalla nostra lista. Risultato: UFS non sale (niente journal) e DWC3 non sale (niente gadget) -> nessun canale di evidenza, nessun panic, telefono fermo e invisibile.
Correzione (v6): usare i file del device (`modules.load.recovery` / `modules.dep` / `modules.softdep` / `modules.alias` / `modules.blocklist`) e caricare i moduli rispettando le dipendenze reali, includendo `qcom_hwspinlock`, `phy-generic`, `eud`.
