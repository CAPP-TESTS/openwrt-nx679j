# VERIFY-v5 — immagine minima "gadget probe" per Nubia NX679J (slot B)

Data: 17 settembre 2026 — profilo `kernel-re`
Base: `/home/user/nx679j-stock/experiments/20260916-122926-native-baseline/`

## 0. Perche' esiste la v5 (contesto misurato)

Sul telefono reale la v4 (stesso kernel stock 5.10.66, stesso `vendor_boot_b`, stesso
slot B, ma ramdisk da 12 MB con rootfs OpenWrt + 327 moduli + il nostro init)
**non ha prodotto nulla**: nessuna enumerazione USB in 140 s (l'immagine Magisk
nota-buona sullo stesso slot enumerava Android in 26 s) e, dopo il recovery, il
journal-breadcrumb sulla partizione rawdump era **ancora tutto a zero**, cioe' il
nostro init non e' mai arrivato nemmeno a caricare `ufs_qcom`.

L'unica variabile era il ramdisk. La v5 discrimina fra due ipotesi:

* **(a)** il problema e' la dimensione/contenuto del ramdisk (12 MB, rootfs OpenWrt, 327 moduli);
* **(b)** il kernel non arriva mai a `/init`, indipendentemente dal contenuto.

La v5 rimuove la massa e nient'altro: kernel stock invariato, cmdline invariata,
init quasi identico, ramdisk ridotto al minimo che puo' ancora dimostrare
"il kernel ha raggiunto lo userspace e puo' tirare su un gadget".

## 1. Immagine

| campo | valore |
|---|---|
| path | `/home/user/nx679j-stock/experiments/20260916-122926-native-baseline/candidate-minimal-gadget-v5/boot_b-minimal-gadget-v5.img` |
| dimensione | **50266112 byte** (47.94 MiB) |
| sha256 | `62c7fa4bb28cdd5ec96faa960b431ed8b5faf734a9e298405ea1400040b8b1f4` |
| header | Android boot header **v4**, os_version 12.0.0, `signature_size = 0` (non firmata) |
| capacita' partizione | 0x6000000 = 100.663.296 B (headroom 50397184 B) |
| cmdline | identica byte-per-byte alla v4 (include `panic=10`) |
| kernel (payload) | 49.108.324 B, sha256 `f0aa949c95d1421381850cb1982bd2a505e96fcd7565543ec28d5b1f7d81a0cc` |

Il kernel nel payload e' **byte-identico** al kernel stock letto da
`current-readback/boot_a.img` (sha256 atteso
`f0aa949c95d1421381850cb1982bd2a505e96fcd7565543ec28d5b1f7d81a0cc`) ed e'
byte-identico al kernel usato dalla v4.

## 2. Ramdisk

| campo | valore |
|---|---|
| dimensione compressa | **1147147 byte** (gzip -9, mtime 0) |
| dimensione cpio (newc) | 3598688 byte |
| sha256 | `33f0cb1b30d3ce61cfcfa2b87400d43f87219bd3925981864ae66c4a3d6ed915` |
| numero di entry | **85** (la v4 ne aveva 1541) |
| moduli `.ko` | 40 (uno per nome nella init; zero moduli extra) |
| nodi device | 6 statici |
| `/sbin/init` | **assente** (per progetto: la v5 non fa handover) |

### 2.1 Cosa c'e' dentro (inventario completo)

| gruppo | contenuto | byte |
|---|---|---|
| PID 1 | `/init` = `candidate-init-v5.sh` (sha256 `4ebac295a09d24ec28e359b253c55d79acda50d596ff2748fd61dc760b1e0f8c`) | 10.507 |
| shell | `/bin/busybox` (BusyBox v1.36.1, ELF AArch64 dinamico musl) + 18 symlink applet: `sh`, `uname`, `cat`, `ls`, `wc`, `basename`, `which`, `rm`, `dd`, `head`, `tail`, `ip`, `mkdir`, `mount`, `mknod`, `ln`, `sync`, `sleep` | 458.773 |
| caricatore moduli | `/sbin/kmodloader` (OpenWrt, invocato come `insmod` tramite symlink `/sbin/insmod -> kmodloader`) | 65.601 |
| librerie (chiusura completa) | `/lib/libc.so`, `/lib/ld-musl-aarch64.so.1 -> libc.so`, `/lib/libgcc_s.so.1`, `/lib/libubox.so.20240329` (richiesta da kmodloader) | 787.573 |
| nodi device statici | `/dev/console` 5:1, `/dev/kmsg` 1:11, `/dev/pmsg0` 252:0, `/dev/null` 1:3, `/dev/zero` 1:5, `/dev/tty` 5:0 | 0 |
| moduli | `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/` con **solo** i 40 `.ko` risolti | 2.262.752 |
| directory | `dev`, `proc`, `sys`, `sys/kernel`, `sys/kernel/config`, `sys/fs`, `sys/fs/pstore`, `tmp`, `bin`, `sbin`, `lib`, `lib/modules`, `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604` | 0 |
| **totale** | **85 entry**, cpio 3598688 B -> gzip 1147147 B | |

**Deliberatamente NON incluso**: albero OpenWrt (procd, netifd, logd, uhttpd,
dropbear, opkg, `/etc/config`, `/www`), `/sbin/init`, gli altri 287 moduli vendor,
telnetd (nessun `/dev/ptmx`, nessun devpts montato: non serve al segnale di
successo). La verifica controlla esplicitamente che 12 path OpenWrt vietati siano
assenti.

Nota utile per chi legge la init: nel ramdisk `/sbin/insmod` esiste sempre, quindi il
ramo `INSMOD="$BB insmod"` e' codice morto — e questa busybox OpenWrt **non ha
l'applet `insmod`** (usa kmodloader, che si comporta da `insmod` in base al nome
con cui viene invocato).

## 3. Tabella di risoluzione dei moduli

Lista, ordine, tabella mixed-spelling e logica di risoluzione sono quelli della v4
(nessuna modifica). La risoluzione e' verificata *contro il ramdisk costruito*,
non contro la staging directory.

| # | nome in candidate-init-v5.sh | file nel ramdisk | spelling | byte | vermagic |
|---|------------------------------|------------------|----------|------|----------|
| 1 | `phy_qcom_emu` | `phy-qcom-emu.ko` | all-hyphens | 16376 | ok |
| 2 | `phy_msm_snps_eusb2` | `phy-msm-snps-eusb2.ko` | all-hyphens | 44520 | ok |
| 3 | `repeater` | `repeater.ko` | literal | 16504 | ok |
| 4 | `phy_qcom_ufs_qmp_v4_lahaina` | `phy-qcom-ufs-qmp-v4-lahaina.ko` | all-hyphens | 27208 | ok |
| 5 | `qcom_glink_spss` | `qcom_glink_spss.ko` | literal | 20400 | ok |
| 6 | `phy_qcom_ufs_qmp_14nm` | `phy-qcom-ufs-qmp-14nm.ko` | all-hyphens | 20088 | ok |
| 7 | `phy_qcom_ufs_qmp_v3` | `phy-qcom-ufs-qmp-v3.ko` | all-hyphens | 24536 | ok |
| 8 | `nvmem_qfprom` | `nvmem_qfprom.ko` | literal | 25832 | ok |
| 9 | `fsa4480_i2c` | `fsa4480-i2c.ko` | all-hyphens | 22456 | ok |
| 10 | `altmode_glink` | `altmode-glink.ko` | all-hyphens | 32800 | ok |
| 11 | `dwc3_msm` | `dwc3-msm.ko` | all-hyphens | 237896 | ok |
| 12 | `phy_msm_ssusb_qmp` | `phy-msm-ssusb-qmp.ko` | all-hyphens | 47888 | ok |
| 13 | `phy_msm_snps_hs` | `phy-msm-snps-hs.ko` | all-hyphens | 36208 | ok |
| 14 | `ssusb_redriver_nb7vpq904m` | `ssusb-redriver-nb7vpq904m.ko` | all-hyphens | 49088 | ok |
| 15 | `ucsi_glink` | `ucsi_glink.ko` | literal | 37616 | ok |
| 16 | `pmic_glink` | `pmic_glink.ko` | literal | 34320 | ok |
| 17 | `pdr_interface` | `pdr_interface.ko` | literal | 30760 | ok |
| 18 | `qmi_helpers` | `qmi_helpers.ko` | literal | 39480 | ok |
| 19 | `rproc_qcom_common` | `rproc_qcom_common.ko` | literal | 63848 | ok |
| 20 | `qcom_smd` | `qcom_smd.ko` | literal | 44760 | ok |
| 21 | `qcom_glink_smem` | `qcom_glink_smem.ko` | literal | 20112 | ok |
| 22 | `qcom_glink` | `qcom_glink.ko` | literal | 88136 | ok |
| 23 | `ufs_qcom` | `ufs_qcom.ko` | literal | 156408 | ok |
| 24 | `ufshcd_crypto_qti` | `ufshcd-crypto-qti.ko` | all-hyphens | 18200 | ok |
| 25 | `qti_regmap_debugfs` | `qti-regmap-debugfs.ko` | all-hyphens | 29832 | ok |
| 26 | `phy_qcom_ufs_qmp_v4_cape` | `phy-qcom-ufs-qmp-v4-cape.ko` | all-hyphens | 27208 | ok |
| 27 | `phy_qcom_ufs_qmp_v4_diwali` | `phy-qcom-ufs-qmp-v4-diwali.ko` | all-hyphens | 27208 | ok |
| 28 | `phy_qcom_ufs_qmp_v4_waipio` | `phy-qcom-ufs-qmp-v4-waipio.ko` | all-hyphens | 27208 | ok |
| 29 | `phy_qcom_ufs` | `phy-qcom-ufs.ko` | all-hyphens | 40120 | ok |
| 30 | `nvmem_qcom_spmi_sdam` | `nvmem_qcom-spmi-sdam.ko` | mixed-spelling alias | 14376 | ok |
| 31 | `crypto_qti_common` | `crypto-qti-common.ko` | all-hyphens | 55472 | ok |
| 32 | `crypto_qti_hwkm` | `crypto-qti-hwkm.ko` | all-hyphens | 17320 | ok |
| 33 | `clk_qcom` | `clk-qcom.ko` | all-hyphens | 347896 | ok |
| 34 | `gdsc_regulator` | `gdsc-regulator.ko` | all-hyphens | 33088 | ok |
| 35 | `proxy_consumer` | `proxy-consumer.ko` | all-hyphens | 22472 | ok |
| 36 | `debug_regulator` | `debug-regulator.ko` | all-hyphens | 53952 | ok |
| 37 | `qcom_ipc_logging` | `qcom_ipc_logging.ko` | literal | 55808 | ok |
| 38 | `qcom_scm` | `qcom-scm.ko` | all-hyphens | 202536 | ok |
| 39 | `minidump` | `minidump.ko` | literal | 127424 | ok |
| 40 | `smem` | `smem.ko` | literal | 25392 | ok |

Riepilogo: **40/40 nomi risolti**, 0 righe `RESOLUTION-FAILED` raggiungibili,
istogramma dei meccanismi {"all-hyphens": 24, "literal": 15, "mixed-spelling alias": 1}. L'unico nome che richiede
l'alias mixed-spelling e' `nvmem_qcom_spmi_sdam -> nvmem_qcom-spmi-sdam.ko`
(presente: True). Il set impacchettato coincide esattamente con il set risolto
(40 packed, 40 resolved, stray=[]). Tutti e 40 i moduli hanno il vermagic atteso
`5.10.66-gki-g491fe99db339 SMP preempt mod_unload modversions aarch64`.

## 4. Verifica statica (strutturale)

`python3 verify-candidate-v5.py` -> **97/97 check passati, 0 falliti**
(log in `candidate-minimal-gadget-v5/verify-structural.log`, riepilogo in `verify-structural.json`).

Coperti, fra gli altri:

* header v4, magic `ANDROID!`, dimensioni dichiarate == manifest, sha256 immagine == manifest;
* payload kernel == kernel stock del telefono (e == kernel v4);
* immagine <= 0x6000000; cmdline con un solo `panic=10` e identica alla v4;
* ramdisk: `gzip -t` ok, newc parsa fino a `TRAILER!!!`, 85 entry == manifest;
* appartenenza: `/init` eseguibile, `/bin/busybox`, `/bin/sh -> busybox`, `/sbin/kmodloader`,
  `/sbin/insmod -> kmodloader`, `libc`/`ld-musl`/`libgcc`/`libubox`, 13 directory, 6 nodi device
  con i major/minor giusti (`dev/kmsg` 1:11, `dev/pmsg0` 252:0);
* minimalita': nessun path utenti OpenWrt, esattamente 40 `.ko` in un'unica directory
  versionata, 85 entry <= budget 100, ramdisk <= 2 MB compresso;
* **tutti e 40 i `.ko` byte-identici** alle copie nel ramdisk vendor (40/40);
* **chiusura delle librerie ricalcolata dai byte del ramdisk**: ogni `NEEDED` e ogni
  interprete di ogni ELF utente presente e' risolto dentro il ramdisk (25 ELF utente);
* ogni symlink del ramdisk punta a una entry presente;
* **esecuzione reale con l'emulatore utente**: il busybox *del ramdisk* serve tutte
  e 18 le applet che la init invoca (`qemu-aarch64 -L <ramdisk> bin/<applet> --help`
  -> banner BusyBox), il suo `sh` accetta la init spedita con `-n` (sintassi, rc=0), e
  `/sbin/insmod` si comporta come l'applet insmod di kmodloader
  (rc=255, `Failed to find v5-verify-nonexistent. Maybe it is a built in module ?`);
* confronto v4/v5 della init: il **codice** dei tre helper di journaling
  (`log`, `find_rawdump`, `flush`), il loop dei 40 moduli e il blocco UFS sono
  **identici alla v4** (solo i commenti sono aggiornati); nel codice della v5 non
  c'e' nessun `exec` e nessun `telnetd`.

`python3 verify-modules-v5.py` -> **exit 0**, 40/40 risolti, nessuno stray
(`module-resolution-static.log`).

## 5. Evidenza QEMU

`python3 qemu-candidate-v5.py` -> **RESULT PASS** (exit 0), 104.99 s di run,
log completo in `candidate-minimal-gadget-v5/qemu/boot.log`, marcatori in `qemu-smoke-v5.json`.

Comando (kernel e ramdisk **estratti dall'immagine costruita**, non dalla staging):

```
qemu-system-aarch64 -machine virt -cpu cortex-a57 -accel tcg -smp 2 -m 2048 \
  -nodefaults -nic none -display none -monitor none -serial stdio -no-reboot \
  -kernel .../qemu/kernel-from-image -initrd .../qemu/ramdisk-from-image.gz \
  -append 'console=ttyAMA0 earlycon loglevel=7 panic=10 rdinit=/init'
```

Righe di evidenza, verbatim dal log seriale (ogni riga compare due volte nel log:
canale kmsg con timestamp kernel e canale console; qui una sola copia):

```
[    0.305663][    T1] Trying to unpack rootfs image as initramfs...
[    0.564059][    T1] Run /init as init process
[    0.653042][    T1] nx679j: mount tmpfs /tmp rc=0
[    0.668642][    T1] nx679j: mount proc /proc rc=0
[    0.680340][    T1] nx679j: mount sysfs /sys rc=0
[    0.694568][    T1] nx679j: mount devtmpfs /dev rc=255
[    0.705196][    T1] nx679j: mount configfs /sys/kernel/config rc=0
[    0.715628][    T1] nx679j: mount pstore /sys/fs/pstore rc=0
[    0.657683][    T1] nx679j: init v5 entered
[    0.727352][    T1] nx679j: stage0 kernel=5.10.66-android12-9-00005-gf6e6376090be-ab8060604
[    0.750605][    T1] nx679j: stage0 modprobe=/sbin/insmod
[    0.790761][    T1] nx679j: modules dir=/lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604 insmod=/sbin/insmod count=40
[    0.820539][    T1] nx679j: insmod phy_qcom_emu rc=0 resolved=phy-qcom-emu.ko 
...  (40 righe `insmod <nome> rc=<n> resolved=<file>`, una per nome)
[    2.154394][    T1] nx679j: insmod nvmem_qcom_spmi_sdam rc=0 resolved=nvmem_qcom-spmi-sdam.ko (mixed spelling) 
[   17.723581][    T1] nx679j: ufs sda after 15s present=no
[   17.744445][    T1] nx679j: partitions=16
[   17.760060][    T1] nx679j: usb_role nodes found=0
[   17.762286][    T1] nx679j: gadget attempt 1 (wait up to 30s for a real UDC)
[   48.273127][    T1] nx679j: udc=NONE after 30s cycle=0
[   51.393930][    T1] nx679j: udc=NONE after 3s cycle=1
[   69.489039][    T1] nx679j: udc=NONE after 3s cycle=2
[   87.579445][    T1] nx679j: udc=NONE after 3s cycle=3
[   48.314878][    T1] nx679j: cycle 1 uptime=48.28 93.90 modules=12
[   66.435919][    T1] nx679j: cycle 2 uptime=66.41 129.98 modules=12
[   84.525500][    T1] nx679j: cycle 3 uptime=84.50 166.04 modules=12
[  102.614712][    T1] nx679j: cycle 4 uptime=102.59 202.11 modules=12
```

Lettura dell'evidenza:

* l'initramfs **si scompatta** (`Trying to unpack rootfs image as initramfs...`,
  nessun `Initramfs unpacking failed`) e `/init` viene eseguito come PID 1;
* il ramdisk minimo e' **autosufficiente**: busybox+musl girano, `mount` funziona
  (tmpfs/proc/sysfs/configfs/pstore rc=0), `mknod`/`ln`/`sync`/`sleep`/`dd`/`ip`/`basename`/`which`
  funzionano, kmodloader carica i moduli che il kernel QEMU puo' accettare;
* `mount devtmpfs /dev rc=255` e' **atteso e innocuo**: il kernel stock ha
  `CONFIG_DEVTMPFS` unset, per questo i 6 nodi device sono statici nel ramdisk
  (la v4 non registrava affatto gli rc dei mount: la v5 li logga);
* la catena dei moduli viene percorsa per intero: `count=40`, 40 righe `insmod`,
  **0 `RESOLUTION-FAILED`**, 12 moduli caricati con `rc=0` (debug_regulator, nvmem_qcom_spmi_sdam, nvmem_qfprom, phy_msm_ssusb_qmp, phy_qcom_emu, phy_qcom_ufs, proxy_consumer, qcom_scm, qmi_helpers, qti_regmap_debugfs, repeater, smem) e `/proc/modules`
  che ne mostra 12 in ogni ciclo; gli altri 28 danno rc!=0 perche' QEMU `virt` non
  ha il device tree Waipio (dwc3-msm, phy-msm-*, ufs_qcom e simili non possono fare
  probe). Comportamento identico a quello della v4 in QEMU;
* il ciclo di ritentativo funziona: cicli 1-4 rilevati dall'host a 1=48.6s, 2=66.7s, 3=84.8s, 4=102.9s
  (i timestamp kernel delle stesse righe sono ~0,2 s prima, es. `cycle 1` a 48,31 s),
  la scansione UDC viene rifatta ogni ciclo, PID 1 vivo alla fine, **nessun
  `Kernel panic`**, nessun `Attempted to kill init`, nessun OOM;
* in QEMU **non esiste un UDC reale**: `udc=NONE after 30s` al primo tentativo e
  `udc=NONE after 3s` nei cicli successivi, `usb_role nodes found=0`. QEMU non
  puo' quindi mostrare ne' il bind del gadget ne' l'enumerazione.

## 6. Segnale di successo lato host (test hardware)

Dopo aver scritto l'immagine sullo slot B e avviato il telefono collegato via USB:

1. **segnale primario** — l'host vede comparire un **nuovo dispositivo USB
   `18d1:4ee7`** (gadget NCM):
   `lsusb -d 18d1:4ee7` mostra
   `ID 18d1:4ee7 Google Inc. ...`, oppure `dmesg` mostra
   `usb 1-1: new high-speed USB device ... idVendor=18d1, idProduct=4ee7`
   seguito da `cdc_ncm` e da una nuova interfaccia di rete (`ip link` mostra
   `enx...`/`usb0`).
2. **segnale secondario** — la rete NCM si alza e l'host pinga `10.0.0.1`:
   `ping -c3 10.0.0.1` (la init configura `usb0` a `10.0.0.1/24`).
3. **controprova** — se l'host non vede nulla ma il journal su rawdump non e' piu'
   tutto a zero, allora il kernel e' arrivato a `/init` e il problema e' a valle
   (UDC/ruolo USB). Suggerimento di lettura dai primi 4096 byte del journal:
   `adb shell su -c "dd if=/dev/block/by-name/rawdump bs=4096 count=1 2>/dev/null | strings | head -40"`
   cercando le righe `nx679j: ...` (in particolare `cycle N` col timestamp).
4. se invece rawdump resta **tutto a zero** anche con la v5, allora l'ipotesi (a) e'
   **falsificata**: il kernel non arriva a `/init` neanche con un ramdisk da 1,1 MB,
   e la causa e' a monte (ABL/AVB/bootconfig/carico del ramdisk).

## 7. Confronto dimensioni con la v4 (l'ipotesi dimensione e' ancora testabile?)

| metrica | v4 | v5 | v5/v4 |
|---|---|---|---|
| ramdisk compresso | 12051145 B (11,49 MiB) | **1147147 B (1,09 MiB)** | **9.5%** |
| entry nel ramdisk | 1541 | 85 | 5.5% |
| moduli `.ko` | 327 | 40 | 12.2% |
| immagine totale | 61169664 B (58,34 MiB) | **50266112 B (47,94 MiB)** | 82.2% |
| kernel | 49.108.324 B, sha256 `f0aa949c...` | invariato (stesso sha256) | 100% |
| riferimento Android (ramdisk di `boot_a`) | 2.200.983 B | 1147147 B | **52.1%** |

La parte dell'immagine che la v5 **non** puo' ridurre e' il kernel: 49,1 MB su 50,3 MB
totali. E' pero' lo stesso kernel della v4 e della Magisk nota-buona, quindi non e'
una variabile del test. La variabile sotto test e' il ramdisk, che scende da 11,49 MiB
a **1,09 MiB**, cioe' circa **meta' del ramdisk Android (2,10 MiB)** che si sa
enumerare in 26 s sullo stesso slot.

Conseguenza: **l'ipotesi dimensione resta testabile ed e' ora ben discriminata.**
Se il telefono con la v5 enumera `18d1:4ee7`, la massa del ramdisk era la causa
(ipotesi a). Se non enumera **e** rawdump resta a zero, la causa e' a monte di
`/init` (ipotesi b), perche' con 1,09 MiB di ramdisk non c'e' piu' nessuna massa
plausibile da incolpare.

## 8. Cosa NON e' provato (elenco esplicito)

1. **Accettazione dell'immagine da parte di ABL/AVB** sul dispositivo: la v5 non e'
   firmata (`signature_size = 0`). Se il bootloader rifiuta immagini non firmate su
   quello slot, questa immagine non parte: solo l'hardware puo' dirlo.
2. **Caricamento dei 40 moduli sul device tree reale**: in QEMU 28/40 falliscono
   perche' il DT Waipio non esiste. Non sappiamo quanti ne caricheranno davvero su
   NX679J.
3. **Ruolo periferico del controller DWC3 / role-switch**: la init scrive `device`
   in ogni `/sys/class/usb_role/*/role` esistente, ma in QEMU non esiste nessun nodo
   `usb_role` (`nodes found=0`) e su hardware non e' mai stato provato. Questa poke
   e' un'aggiunta **oltre** la specifica del build, dichiarata non provata: se il
   controller resta in ruolo host/none il bind del gadget fallisce con `-ENODEV` e
   l'host non vede nulla.
4. **Bind del gadget ed enumerazione NCM `18d1:4ee7`**: mai osservati. In QEMU non
   esiste un UDC reale; sull'hardware nessuna nostra immagine ha ancora enumerato.
5. **Scrittura effettiva del journal su rawdump sul telefono**: la v5 usa lo stesso
   meccanismo della v4 (partizione trovata per dimensione 524288 settori, `dd` sui
   primi 4096 byte). In QEMU il ramo non e' mai stato esercitato: la log non contiene
   nessuna riga `rawdump device=` (la macchina `virt` non espone nessuna partizione
   da 524288 settori; `partitions=16` e' solo il conteggio delle righe di
   `/proc/partitions`), quindi `find_rawdump` e' tornato 1 e
   `flush()` ha restituito 0 senza scrivere — che e' anche la prova che la init non
   abortisce quando la partizione non c'e'.
6. **Canale pstore**: in QEMU `mount pstore rc=0`, ma non c'e' backend ramoops, quindi
   `/dev/pmsg0` non e' stato esercitato come canale reale.
7. **Comportamento dopo un reset**: il loop infinito non e' mai stato interrotto da un
   reset hardware, quindi la leggibilita' del journal *dopo* il reset non e'
   dimostrata (la v4 aveva la stessa proprieta' non provata).
8. **Nessun accesso interattivo al dispositivo**: la v5 non include telnetd ne'
   devpts, quindi anche se il gadget enumerasse non c'e' una shell pronta; l'unico modo
   di leggere lo stato interno resta il journal su rawdump dopo il recovery.
9. **Nessuna prova sul comportamento del LED/slot/rollback**: la v5 non tocca
   `misc`, `vbmeta` ne' i flag di slot; il comportamento di ABL dopo un fallimento di
   boot resta quello descritto nel resto del progetto, non verificato qui.

## 9. Artefatti e comandi

| file | cos'e' |
|---|---|
| `candidate-minimal-gadget-v5/boot_b-minimal-gadget-v5.img` | **immagine da scrivere sullo slot B** |
| `candidate-minimal-gadget-v5/manifest.json` | dimensioni, sha256, cmdline, tabella moduli, confronto v4 |
| `candidate-minimal-gadget-v5/inputs.json` | input usati (boot_a, rootfs OpenWrt, ramdisk vendor), tabella moduli, librerie, applet |
| `candidate-minimal-gadget-v5/verify-structural.log` / `.json` | 97 check statici |
| `candidate-minimal-gadget-v5/module-resolution-static.log` / `.json` | risoluzione 40/40 |
| `candidate-minimal-gadget-v5/qemu-smoke-v5.log` / `.json`, `qemu/boot.log` | evidenza QEMU (PASS) |
| `candidate-minimal-gadget-v5/verify-scratch/` | ramdisk estratto, usato per le prove con `qemu-aarch64` |
| `candidate-init-v5.sh` | script PID 1 spedito nel ramdisk |
| `build-candidate-v5.py` | build riproducibile |
| `verify-candidate-v5.py`, `verify-modules-v5.py`, `qemu-candidate-v5.py` | verifiche |

Riproduzione completa:

```
cd /home/user/nx679j-stock/experiments/20260916-122926-native-baseline
python3 build-candidate-v5.py            # ricostruisce l'immagine
python3 verify-candidate-v5.py           # 97/97 check statici
python3 verify-modules-v5.py             # 40/40 nomi risolti
python3 qemu-candidate-v5.py             # smoke test QEMU (~110 s)
```

## 10. Differenze della init v5 rispetto alla v4 (riassunto operativo)

* i tre helper di journaling, il loop dei 40 moduli (lista, ordine, alias
  mixed-spelling, formato del log `insmod <nome> rc=<n> resolved=<file>`) e il blocco
  UFS sono **riportati identici** dalla v4: verificato byte-per-byte dal
  verificatore strutturale (a meno dei commenti), quindi il comportamento di
  logging e' lo stesso che il progetto conosce;
* i mount ora registrano l'rc (`mount <fstype> <dir> rc=<n>`);
* la sezione gadget diventa `gadget_try(<secondi>)`: ricerca di un UDC reale
  (esclusi `dummy_udc*`), creazione del gadget configfs NCM
  (`idVendor 0x18d1`, `idProduct 0x4ee7`, stringhe `OpenWrt`/`NX679J`, `MaxPower 250`,
  symlink `functions/ncm.usb0` in `configs/c.1`, scrittura dell'UDC), `usb0` up e
  `10.0.0.1/24`;
* nessun `exec /sbin/init`: dopo il primo tentativo (attesa fino a 30 s per un UDC)
  la init entra in `while true` e **ogni 15 s** rifa' la scansione UDC, riasserisce
  gadget e indirizzo e appende un record al journal (`cycle N uptime=... modules=...`);
* `usb_role`/role-switch poked come descritto al punto 8.3 (aggiunta non provata);
* telnetd rimosso (nessun `/dev/ptmx`/devpts).
