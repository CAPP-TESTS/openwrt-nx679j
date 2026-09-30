# VERIFY-v4 — NX679J (SM8450) slot-B, immagine candidata v4

Data: 2026-09-17. Directory di lavoro:
`/home/user/nx679j-stock/experiments/20260916-122926-native-baseline/`

Scopo di v4: **correggere il difetto dei nomi dei moduli** documentato in
`candidate-stockkernel-v3/VERIFY.md` §6.1. In v3 il ciclo dei moduli cercava
solo `$M/$ko.ko` e `$M/${ko}.ko` — la stessa stringa due volte — mentre i file
nel ramdisk hanno i trattini: 25 nomi su 40 non venivano mai trovati
(`insmod … ABSENT`), quindi niente `dwc3-msm` (nessun gadget USB), niente
`ufs_qcom` (nessuna UFS) e di conseguenza nessuna journal breadcrumb su
rawdump. Sul telefono l'effetto era un device apparentemente morto.

In v4 **l'unica modifica è la risoluzione del nome-file nel ciclo dei moduli**.
Kernel, cmdline, moduli vendor, rootfs OpenWrt e assemblaggio del ramdisk sono
quelli di v3, byte per byte.

---

## 1. Artefatto consegnato

| Campo | Valore |
|---|---|
| Immagine | `candidate-stockkernel-v4/boot_b-stockkernel-openwrt-v4.img` |
| Dimensione | 61.169.664 byte (58,34 MiB) |
| sha256 immagine | `1a46f350376ba5fc609e2da52cce22e9086fc3b850d3ed22f425066e4e3f941b` |
| sha256 immagine v3 (per confronto) | `a35a28c14b7ca18217740f9727aba8b971efd443adff9092c982d879d963ceba` (61.165.568 byte) |
| Header Android boot | `ANDROID!`, **header_version = 4**, header_size 1584, page size 4096 |
| Firma | `boot.img signature size: 0` → **immagine non firmata** (AVB non verificato) |
| Capacità partizione | 100.663.296 byte (0x6000000 = 96 MiB) |
| Margine libero | 39.493.632 byte |
| Manifest | `candidate-stockkernel-v4/manifest.json` |
| Verifica strutturale | **51/51 controlli PASS** → `verify-structural.log`, `verify-structural.json` |
| Check statico dei nomi modulo | **40/40 risolti, 0 non risolti** → `module-resolution-static.log`, `module-resolution-static.json` |
| Riproducibilità | builder eseguito **due volte** da zero: sha256 immagine identico in entrambe (`1a46f350…`) |

Il file `inputs.json` registra gli hash degli input (kernel stock, rootfs
OpenWrt, ramdisk vendor); `not_yet_verified` nel manifest elenca i limiti.

## 2. Kernel (input non sostituibile, invariato rispetto a v3)

| Campo | Valore |
|---|---|
| Sorgente | `current-readback/boot_a.img` (boot header v4, pagina 4096) |
| Dimensione | 49.108.324 byte |
| sha256 | `f0aa949c95d1421381850cb1982bd2a505e96fcd7565543ec28d5b1f7d81a0cc` |

Controlli automatici PASS: sha256 identico a quello atteso; identico a quello
dichiarato dal **manifest v3**; ri-estratto **dall'immagine v4 costruita**
(`qemu/kernel-from-image`) con sha256 identico; confermato in modo indipendente
da `unpack_bootimg --boot_img …` → `kernel_size: 49108324`, `header version: 4`.

## 3. Ramdisk

| Campo | Valore |
|---|---|
| Dimensione compressa | 12.051.145 byte (offset 49.115.136, allineato a pagina) |
| sha256 | `e87ae90d25d659cfc811496ac4e92a83a2f6475d451e6610d36b075862489589` |
| Numero di entry newc | **1541** (+ record `TRAILER!!!`) |
| Verifica | `gzip -t` OK; parsing newc manuale fino a `TRAILER!!!` OK; `cpio` indipendente presente |
| Moduli vendor | **327** file `.ko` sotto `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/` |
| Device node statici | 6 |
| Membri richiesti | `init` (0o100755), `sbin/init`, `bin/busybox`, `proc`, `sys`, `sys/kernel/config` |

### 3.1 Cosa è cambiato rispetto al ramdisk v3 — e cosa no

Confronto **membro per membro** fra il ramdisk in v3 e quello in v4
(sha256 di ogni entry):

```
v3 members 1541 | v4 members 1541
only in v3: []      only in v4: []
content differs for: ['init']
```

Cioè: su 1541 membri **cambia un solo file, `init`**. Tutti i 327 moduli
vendor, la rootfs OpenWrt, i device node e la struttura newc sono byte-identici
a v3. La ramdisk compressa cresce di **886 byte** (12.050.259 → 12.051.145) e
l'immagine di 4096 byte (v3 61.165.568 → v4 61.169.664), interamente dovuti al
nuovo script.

Lo script è `candidate-init-v4.sh` (sha256
`79b6213a3a18e9a7a2c763a7789614bffa7742eb480acc312fb9093cc3c9a306`).

## 4. Riga di comando (725 byte, dal campo cmdline dell'header)

```
stack_depot_disable=on kasan.stacktrace=off kvm-arm.mode=protected cgroup_disable=pressure cgroup.memory=nokmem console=ttyMSM0,115200n8 loglevel=6 kpti=0 log_buf_len=256K kernel.panic_on_rcu_stall=1 swiotlb=noforce loop.max_part=7 cgroup.memory=nokmem,nosocket pcie_ports=compat service_locator.enable=1 msm_rtb.filter=0x237 allow_mismatched_32bit_el0 cpufreq.default_governor=performance pelt=8 kasan=off rcupdate.rcu_expedited=1 rcu_nocbs=0-7 irqaffinity=0-3 ftrace_dump_on_oops pstore.compress=none fsa4480_i2c.async_probe=1 can.stats_timer=0 video=vfb:640x400,bpp=32,memsize=3072000 qcom-dload-mode.download_mode=1 bootconfig msm_drm.dsi_display0=qcom,mdss_dsi_r6130_1080_2400_amoled_cmd: rootwait ro init=/init panic=10
```

Controlli automatici PASS: cmdline == v2 + ` panic=10`; prefisso v2
byte-identico; **cmdline byte-identica alla v3** (`cmdline == V3MAN['cmdline']`);
un solo token `panic=`; `unpack_bootimg` conferma gli stessi 725 byte.
`manifest.json` → `"cmdline_v3_identical": true`.

## 5. Cosa è cambiato esattamente in `candidate-init-v4.sh`

Diff completo: `candidate-stockkernel-v4/init-v3-vs-v4.diff`. Controlli
automatici PASS (non è un'affermazione a mano):

* il diff è confinato a **due regioni**: il blocco di commenti in testa e il
  ciclo dei moduli;
* **tutto ciò che sta fra l'intestazione e il ciclo è byte-identico** (70 righe)
  e **tutto ciò che segue il ciclo è byte-identico** (63 righe);
* nessuna riga di codice modificata fuori dal ciclo (4 righe vecchie → 19 righe
  nuove, tutte dentro il ciclo);
* **la lista dei 40 moduli è identica token per token** (stesso ordine, stesse
  stringhe);
* restano invariati: journal su rawdump identificato per dimensione 524288
  settori, `flush()` con `dd` + `sync`, gadget NCM configfs
  (`functions/ncm.usb0`), `exec /sbin/init`, nessun `set -e` (lo script non
  abortisce mai).

Modifica funzionale (solo la risoluzione del nome-file). Prima:

```sh
    for cand in "$M/$ko.ko" "$M/${ko}.ko"; do      # la stessa stringa due volte
        [ -f "$cand" ] && f="$cand" && break
    done
    if [ -z "$f" ]; then
        log "insmod $ko ABSENT"
```

Dopo:

```sh
    f=""; hit=""
    for cand in "$M/$ko.ko" "$M/${ko//_/-}.ko" "$M/${ko//-/_}.ko"; do
        if [ -f "$cand" ]; then f="$cand"; hit="$(basename "$cand")"; break; fi
    done
    if [ -z "$f" ]; then
        case "$ko" in
            nvmem_qcom_spmi_sdam) cand="$M/nvmem_qcom-spmi-sdam.ko" ;;
            *) cand="" ;;
        esac
        if [ -n "$cand" ] && [ -f "$cand" ]; then
            f="$cand"; hit="$(basename "$cand") (mixed spelling)"
        fi
    fi
    if [ -z "$f" ]; then
        log "insmod $ko RESOLUTION-FAILED tried $ko.ko ${ko//_/-}.ko ${ko//-/_}.ko"
        continue
    fi
    out=$($INSMOD "$f" 2>&1)
    log "insmod $ko rc=$? resolved=$hit ${out:+msg=$out}"
```

Ordine di tentativo, come richiesto: **(1)** nome letterale `<nome>.ko`,
**(2)** tutte le `_` convertite in `-`, **(3)** tutti i `-` convertiti in `_`,
**(4)** la tabella dei nomi misti (vedi §5.1). Ogni riga di log ora dice
**quale grafia ha risolto** (`resolved=<file>`), e se nessuna delle tre grafie
generiche esiste e il nome non è nella tabella, lo script scrive una riga
esplicita `insmod <nome> RESOLUTION-FAILED tried …` **nominando il modulo**.

### 5.1 Il caso `nvmem_qcom_spmi_sdam` (nome misto) — confermato

Il nome nella lista è `nvmem_qcom_spmi_sdam` (stile `/proc/modules`). Le tre
grafie generiche **non lo trovano**: sul device il file si chiama
`nvmem_qcom-spmi-sdam.ko`, cioè **underscore dopo `nvmem`, trattino dopo
`qcom`** — non è né la grafia tutta-trattini (`nvmem-qcom-spmi-sdam.ko`) né
quella tutta-underscore (`nvmem_qcom_spmi_sdam.ko`). Il file esiste ed è
presente nel ramdisk:

```
nvmem_qcom_spmi_sdam  ->  nvmem_qcom-spmi-sdam.ko   [mixed-spelling alias]  ok
```

Nella v4 c'è quindi una **tabella esplicita di un solo elemento** (nel `case`
del ciclo) che mappa `nvmem_qcom_spmi_sdam` → `nvmem_qcom-spmi-sdam.ko`,
etichettata nel log con `(mixed spelling)`. Prove:

* **statica**: il file è presente nel ramdisk (stesso `MOD_DIR` dei 327 moduli)
  e il check `verify-modules-v4.py` lo dichiara `present=True`;
* **dinamica (QEMU)**: `insmod nvmem_qcom_spmi_sdam rc=0
  resolved=nvmem_qcom-spmi-sdam.ko (mixed spelling)` — il kernel ha accettato il
  file e il suo `init` è andato a buon fine (`rc=0`, nessun simbolo mancante):
  non è solo un nome che combacia, è il modulo giusto di quel nome.

**Contabilità onesta**: con le sole tre grafie generiche si fermano 39/40; il
contatore passa a **40/40 solo grazie alla tabella esplicita**. Entrambe le
letture sono riportate qui sopra e nella tabella §6.

## 6. Tabella completa di risoluzione (40/40)

La lista dei nomi e la tabella dei nomi misti sono **lette dallo script**
(costanti del file), e i nomi-file sono **letti dal ramdisk dentro l'immagine
costruita**: il check non può divergere né dallo script né dall'artefatto.
Script: `verify-modules-v4.py` → `module-resolution-static.log` /
`module-resolution-static.json`.

Mappa `MOD_DIR` = `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/`

| # | nome nella lista v4 | file risolto nel ramdisk (`MOD_DIR` + …) | grafia | esito |
|---:|---|---|---|---|
| 1 | `phy_qcom_emu` | `phy-qcom-emu.ko` | `_` → `-` | ok |
| 2 | `phy_msm_snps_eusb2` | `phy-msm-snps-eusb2.ko` | `_` → `-` | ok |
| 3 | `repeater` | `repeater.ko` | letterale (`<nome>.ko`) | ok |
| 4 | `phy_qcom_ufs_qmp_v4_lahaina` | `phy-qcom-ufs-qmp-v4-lahaina.ko` | `_` → `-` | ok |
| 5 | `qcom_glink_spss` | `qcom_glink_spss.ko` | letterale (`<nome>.ko`) | ok |
| 6 | `phy_qcom_ufs_qmp_14nm` | `phy-qcom-ufs-qmp-14nm.ko` | `_` → `-` | ok |
| 7 | `phy_qcom_ufs_qmp_v3` | `phy-qcom-ufs-qmp-v3.ko` | `_` → `-` | ok |
| 8 | `nvmem_qfprom` | `nvmem_qfprom.ko` | letterale (`<nome>.ko`) | ok |
| 9 | `fsa4480_i2c` | `fsa4480-i2c.ko` | `_` → `-` | ok |
| 10 | `altmode_glink` | `altmode-glink.ko` | `_` → `-` | ok |
| 11 | `dwc3_msm` | `dwc3-msm.ko` | `_` → `-` | ok |
| 12 | `phy_msm_ssusb_qmp` | `phy-msm-ssusb-qmp.ko` | `_` → `-` | ok |
| 13 | `phy_msm_snps_hs` | `phy-msm-snps-hs.ko` | `_` → `-` | ok |
| 14 | `ssusb_redriver_nb7vpq904m` | `ssusb-redriver-nb7vpq904m.ko` | `_` → `-` | ok |
| 15 | `ucsi_glink` | `ucsi_glink.ko` | letterale (`<nome>.ko`) | ok |
| 16 | `pmic_glink` | `pmic_glink.ko` | letterale (`<nome>.ko`) | ok |
| 17 | `pdr_interface` | `pdr_interface.ko` | letterale (`<nome>.ko`) | ok |
| 18 | `qmi_helpers` | `qmi_helpers.ko` | letterale (`<nome>.ko`) | ok |
| 19 | `rproc_qcom_common` | `rproc_qcom_common.ko` | letterale (`<nome>.ko`) | ok |
| 20 | `qcom_smd` | `qcom_smd.ko` | letterale (`<nome>.ko`) | ok |
| 21 | `qcom_glink_smem` | `qcom_glink_smem.ko` | letterale (`<nome>.ko`) | ok |
| 22 | `qcom_glink` | `qcom_glink.ko` | letterale (`<nome>.ko`) | ok |
| 23 | `ufs_qcom` | `ufs_qcom.ko` | letterale (`<nome>.ko`) | ok |
| 24 | `ufshcd_crypto_qti` | `ufshcd-crypto-qti.ko` | `_` → `-` | ok |
| 25 | `qti_regmap_debugfs` | `qti-regmap-debugfs.ko` | `_` → `-` | ok |
| 26 | `phy_qcom_ufs_qmp_v4_cape` | `phy-qcom-ufs-qmp-v4-cape.ko` | `_` → `-` | ok |
| 27 | `phy_qcom_ufs_qmp_v4_diwali` | `phy-qcom-ufs-qmp-v4-diwali.ko` | `_` → `-` | ok |
| 28 | `phy_qcom_ufs_qmp_v4_waipio` | `phy-qcom-ufs-qmp-v4-waipio.ko` | `_` → `-` | ok |
| 29 | `phy_qcom_ufs` | `phy-qcom-ufs.ko` | `_` → `-` | ok |
| 30 | `nvmem_qcom_spmi_sdam` | `nvmem_qcom-spmi-sdam.ko` | **tabella nome misto** | ok |
| 31 | `crypto_qti_common` | `crypto-qti-common.ko` | `_` → `-` | ok |
| 32 | `crypto_qti_hwkm` | `crypto-qti-hwkm.ko` | `_` → `-` | ok |
| 33 | `clk_qcom` | `clk-qcom.ko` | `_` → `-` | ok |
| 34 | `gdsc_regulator` | `gdsc-regulator.ko` | `_` → `-` | ok |
| 35 | `proxy_consumer` | `proxy-consumer.ko` | `_` → `-` | ok |
| 36 | `debug_regulator` | `debug-regulator.ko` | `_` → `-` | ok |
| 37 | `qcom_ipc_logging` | `qcom_ipc_logging.ko` | letterale (`<nome>.ko`) | ok |
| 38 | `qcom_scm` | `qcom-scm.ko` | `_` → `-` | ok |
| 39 | `minidump` | `minidump.ko` | letterale (`<nome>.ko`) | ok |
| 40 | `smem` | `smem.ko` | letterale (`<nome>.ko`) | ok |

Riepilogo macchina: `{"all-hyphens": 24, "literal": 15, "mixed-spelling alias": 1}`
→ **risolti 40/40, non risolti: nessuno**. Coerente con v3, che contava 24 nomi
esistenti con i trattini e 1 nome inesistente in entrambe le grafie pure
(`nvmem_qcom_spmi_sdam`).

Controllo di regressione: le grafie di v3 avrebbero mancato **25 di questi 40
nomi** (cercavano la stessa stringa letterale due volte).

## 7. Test QEMU (smoke test)

Immagine e ramdisk sono **estratti dall'immagine costruita** (`qemu/kernel-from-image`,
`qemu/ramdisk-from-image.gz`), non dai file di staging: il test esercita
l'artefatto consegnato. Log completo: `candidate-stockkernel-v4/qemu/boot.log`
(42.395 byte, sha256
`269fd114044a4b0e4c5bbf8998d919cade769a9cb971c84bcbec8611648f43f1`).
QEMU terminato con SIGTERM dal test dopo 63,9 s (procedura prevista), nessun
panic. Marker: `unpacking_initramfs_seen: true`, `unpack_failed: false`,
`run_init_as_init_process_seen: true`, `init_script_ran: true`,
`stage0_cmdline_logged: true`, `procd_seen: true`, `kernel_panic: false`.
`qemu-candidate-v4.py` esce 0 solo se tutti questi marker sono veri.

### 7.1 Scompattamento del ramdisk e avvio di `/init`

```
131:[    0.287792][    T1] Trying to unpack rootfs image as initramfs...
264:[    0.859223][    T1] Run /init as init process
265:[    0.935339][    T1] nx679j: init v2 entered
274:[    1.131377][    T1] nx679j: modules dir=/lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604 insmod=/sbin/insmod count=327
```

La stringa `Initramfs unpacking failed` **non compare** nel log. (Nel kernel
5.10 del telefono il messaggio è `Trying to unpack rootfs image as initramfs...`;
`Unpacking initramfs...` appartiene ai kernel 6.x — cercare la stringa sbagliata
dà un falso negativo.)

### 7.2 La correzione dei nomi è visibile a runtime

**40 nomi risolti, 0 `RESOLUTION-FAILED`, 0 `ABSENT`** (80 righe `insmod` nel
log = 40 nomi × 2 copie, kmsg + console):

```
349:[    1.642583][    T1] nx679j: insmod dwc3_msm rc=255 resolved=dwc3-msm.ko
430:[    2.186460][    T1] nx679j: insmod ufs_qcom rc=255 resolved=ufs_qcom.ko
474:[    2.492444][    T1] nx679j: insmod nvmem_qcom_spmi_sdam rc=0 resolved=nvmem_qcom-spmi-sdam.ko (mixed spelling)
```

Confronto strumentale v3 → v4, dagli stessi log QEMU:

| | v3 (difettoso) | v4 (corretto) |
|---|---|---|
| nomi risolti a un file reale | 15/40 | **40/40** |
| `insmod … ABSENT` (nomi unici) | **25** | **0** |
| `insmod … RESOLUTION-FAILED` | n/a | **0** |
| moduli con `rc=0` | 4 | **12** |
| moduli con `rc=255` | 11 (sui 15 trovati) | 28 (sui 40 trovati) |

Gli 8 moduli che passano da `ABSENT` a `rc=0` — cioè che ora si caricano
davvero in QEMU — sono `nvmem_qcom_spmi_sdam`, `phy_msm_ssusb_qmp`,
`phy_qcom_emu`, `phy_qcom_ufs`, `proxy_consumer`, `qcom_scm`,
`qti_regmap_debugfs`, `debug_regulator` (più i 4 già `rc=0` in v3). Tutti e 25 i
nomi prima `ABSENT` ora risolvono (elenco in §6).

I `rc=255` restano attesi in QEMU: senza device tree Waipio i driver qcom non
trovano nodi/compatibili (`dwc3_msm`, `ufs_qcom` ecc. sono `rc=255` **in QEMU**).
`phy_msm_ssusb_qmp` e `phy_qcom_ufs` passano invece a `rc=0`: è la prova che il
caricamento non è più impedito dal nome-file. **Questi valori non dicono nulla
sul comportamento sul telefono.**

### 7.3 Attese e handover

```
515:[   18.035783][    T1] nx679j: ufs sda after 15s present=no
519:[   38.358144][    T1] nx679j: udc=NONE after 20s
521:[   38.369023][    T1] nx679j: handover init=present
537:[   44.029017][    T1] procd: - init -
538:Please press Enter to activate this console.
```

Il ramo `exec /sbin/init` viene eseguito e la userspace OpenWrt parte davvero
(procd `- early -` / `- ubus -` / `- init -`), sempre con il kernel stock 5.10.
`udc=NONE` ⇒ nessun `dwc3`, quindi il ramo gadget/NCM **non** viene eseguito e
`telnetd` non parte: atteso in QEMU.

## 8. Cosa NON è provato (da non confondere con verifica hardware)

1. **Nulla sull'hardware Qualcomm.** QEMU `virt` non ha il device tree Waipio:
   `dwc3-msm`, `phy-msm-*`, `fsa4480` non fanno probe. Non esiste alcun UDC,
   alcuno stato di link USB, alcuna enumerazione NCM. I 28 `rc=255` sono
   l'impronta di questa assenza, non un difetto.
2. **Enumerazione USB / ruolo peripheral / NCM**: non provata. Il gadget
   configfs è presente e corretto come codice, ma non è mai stato bindato su un
   UDC reale (in QEMU `udc=NONE`).
3. **UFS reale** (UFSHCD, PHY UFS, gear/lane, crypto inline): non esercitata —
   nessun `sda` (`present=no`), nessuna partizione.
4. **Journal su rawdump**: **non provata**. In QEMU non esiste alcun blocco con
   `size = 524288` settori, quindi `find_rawdump()` fallisce e `flush()`
   termina senza scrivere. La leggibilità del record su rawdump da Android resta
   non verificata.
5. **`panic=10` esiste solo come byte nella cmdline.** Nessun panic è occorso in
   QEMU e il test usa `-no-reboot`: il riavvio automatico dopo panic **non è
   stato osservato**. La modifica è verificata a livello di immagine, non di
   comportamento.
6. **Accettazione ABL/AVB**: non provata. L'immagine ha `signature size: 0` (non
   firmata); il verdetto di ABL su slot B, della selezione slot A/B e di
   `bootcontrol`/`misc` non è testato.
7. **Ordine di caricamento dei moduli sul DT reale** e le dipendenze
   (`dwc3-msm` ← `phy-msm-ssusb-qmp`, `ufs_qcom` ← `phy-qcom-ufs`…) restano
   verificati solo staticamente (ordine di v3 mantenuto identico) e non su
   hardware.
8. **Esito di un boot reale su slot B**: non provato da questa sessione.

## 9. File prodotti

```
build-candidate-v4.py                        builder v4 (helper v3/v2 riusati verbatim)
candidate-init-v4.sh                         init v4 (ciclo moduli corretto)
candidate-stockkernel-v4-build-stdout.log    stdout del builder
verify-candidate-v4.py                       verifica strutturale + risoluzione nomi (51 controlli)
verify-modules-v4.py                         STATIC CHECK dedicato nomi modulo -> file nel ramdisk
qemu-candidate-v4.py                         smoke test QEMU
candidate-stockkernel-v4/
  boot_b-stockkernel-openwrt-v4.img          artefatto
  manifest.json, inputs.json                 manifest e hash degli input
  verify-structural.log / verify-structural.json
  module-resolution-static.log / module-resolution-static.json
  qemu-smoke-v4.log / qemu-smoke-v4.json
  qemu/boot.log                              log di boot QEMU (prove)
  qemu/kernel-from-image                     kernel estratto dall'immagine
  qemu/ramdisk-from-image.gz                 ramdisk estratto dall'immagine
  init-v3-vs-v4.diff                         diff dei due init (commenti + ciclo moduli)
  work/kernel, work/ramdisk.cpio.gz          staging del builder
```

## 10. Conclusione

v4 è costruito, riproducibile (stesso sha256 su due esecuzioni da zero) e
strutturalmente corretto (**51/51**), con kernel, cmdline, rootfs e 327 moduli
vendor **identici a v3**. Il difetto dei nomi è corretto: **tutti e 40 i nomi
della lista risolvono a un file realmente presente nel ramdisk** (24 grafia
trattini, 15 letterale, 1 nome misto `nvmem_qcom_spmi_sdam` →
`nvmem_qcom-spmi-sdam.ko` via tabella esplicita), e lo smoke test QEMU lo
conferma a runtime (40 righe `resolved=`, 0 `RESOLUTION-FAILED`, 0 `ABSENT`,
contro 25 `ABSENT` in v3) oltre a provare che il ramdisk si scompatta, che
`/init` parte come PID 1 e che il passaggio a `/sbin/init` avviene con procd
attivo. Resta **non provato** tutto ciò che riguarda l'hardware Qualcomm:
probe dei driver sul DT reale, enumerazione USB/NCM, UFS reale, journal su
rawdump, accettazione ABL/AVB e l'effetto di `panic=10` sul telefono (presente
solo come byte nella cmdline).
