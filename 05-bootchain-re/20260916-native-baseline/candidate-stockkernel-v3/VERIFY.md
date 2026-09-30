# VERIFY — NX679J (SM8450) slot-B, immagine candidata v3

Data: 2026-09-17. Directory di lavoro:
`/home/user/nx679j-stock/experiments/20260916-122926-native-baseline/`

Scopo di v3: riprodurre il candidato v2 con **una sola modifica**, `panic=10`
sulla riga di comando del kernel, così che un panic riavvii il telefono da solo
invece di restare appeso fino a che qualcuno preme il tasto di accensione.
Nessun altro comportamento è cambiato.

---

## 1. Artefatto consegnato

| Campo | Valore |
|---|---|
| Immagine | `candidate-stockkernel-v3/boot_b-stockkernel-openwrt-v3.img` |
| Dimensione | 61.165.568 byte (58,33 MiB) |
| sha256 immagine | `a35a28c14b7ca18217740f9727aba8b971efd443adff9092c982d879d963ceba` |
| Header Android boot | `ANDROID!`, **header_version = 4**, header_size 1584, page size 4096 |
| Firma | `boot.img signature size: 0` → **immagine non firmata** (AVB non verificato) |
| Capacità partizione | 100.663.296 byte (0x6000000 = 96 MiB) |
| Margine libero | 39.497.728 byte — sotto il limite richiesto ✅ |
| Manifest | `candidate-stockkernel-v3/manifest.json` |

Verifica strutturale: **35/35 controlli PASS** (`verify-structural.log`,
`verify-structural.json`, script `verify-candidate-v3.py`).

## 2. Kernel (input non sostituibile)

| Campo | Valore |
|---|---|
| Sorgente | `current-readback/boot_a.img` (boot header v4, pagina 4096), kernel stock Android |
| Dimensione | 49.108.324 byte |
| sha256 | `f0aa949c95d1421381850cb1982bd2a505e96fcd7565543ec28d5b1f7d81a0cc` |

L'hash dell'immagine attesa coincide con quello del kernel estratto ed è stato
ri-verificato **dopo** l'estrazione dal file immagine costruito
(`qemu/kernel-from-image`, sha256 identico) e, in modo indipendente, con
`unpack_bootimg --boot_img ...` → `kernel_size: 49108324`, header version 4.

## 3. Ramdisk

| Campo | Valore |
|---|---|
| Dimensione compressa | 12.050.259 byte (offset 49.115.136, allineato a pagina) |
| sha256 | `d4406cb86bc6515615813fcc1bd55ef2d48def48bae6b23ab38b55535a177b85` |
| Numero di entry newc | **1541** (+ record `TRAILER!!!`) |
| Verifica | `gzip -t` OK; parsing newc manuale fino a `TRAILER!!!` OK; `cpio -itv` indipendente: 1541 entry |
| Moduli vendor | **327** file `.ko` sotto `lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/` |
| Device node statici | 6 (`dev/console`, `dev/null`, `dev/kmsg`, `dev/pmsg0`, …) |
| Membri richiesti | `init` (0o100755, 6759 B), `sbin/init`, `bin/busybox`, `proc`, `sys`, `sys/kernel/config` |

Differenza rispetto al ramdisk v2 (`3e9ac466…`, 12.050.089 B): **un solo
membro cambiato**, `init` — tutti gli altri 1540 membri sono byte-identici
(confronto sha256 membro per membro). La crescita di 170 byte del `.gz` deriva
solo da questo.

`candidate-init-v3.sh` (sha256 `1aefccf55646dc4221390e58cf296547ee97ffe7a02e4528e28c7cd5a9cc7b9f`)
è il file v2 (sha256 `e2a2185f…`, 6499 B) con **solo commenti in testa** che
registrano la provenance v3: il corpo dello script è byte-identico (`diff -u`
salvato in `candidate-stockkernel-v3/init-v2-vs-v3.diff`: nella testa del file 1
riga modificata e 5 aggiunte, nessuna riga di codice toccata). La journal su rawdump, la catena dei 40 moduli,
il gadget NCM e `exec /sbin/init` sono esattamente quelli di v2.

## 4. Riga di comando completa (725 byte, dal campo cmdline dell'header)

```
stack_depot_disable=on kasan.stacktrace=off kvm-arm.mode=protected cgroup_disable=pressure cgroup.memory=nokmem console=ttyMSM0,115200n8 loglevel=6 kpti=0 log_buf_len=256K kernel.panic_on_rcu_stall=1 swiotlb=noforce loop.max_part=7 cgroup.memory=nokmem,nosocket pcie_ports=compat service_locator.enable=1 msm_rtb.filter=0x237 allow_mismatched_32bit_el0 cpufreq.default_governor=performance pelt=8 kasan=off rcupdate.rcu_expedited=1 rcu_nocbs=0-7 irqaffinity=0-3 ftrace_dump_on_oops pstore.compress=none fsa4480_i2c.async_probe=1 can.stats_timer=0 video=vfb:640x400,bpp=32,memsize=3072000 qcom-dload-mode.download_mode=1 bootconfig msm_drm.dsi_display0=qcom,mdss_dsi_r6130_1080_2400_amoled_cmd: rootwait ro init=/init panic=10
```

Prove sulla singola modifica (controlli automatici, tutti PASS):

* `cmdline == cmdline_v2 + " panic=10"` → confronto stringa esatto con il
  letterale in `build-candidate-v2.py` (`cmdline.lstrip`/prefisso byte-identico).
* prefisso fino a `… rootwait ro init=/init` **byte-identico** a v2.
* un solo token `panic=` presente, valore `10`.
* `unpack_bootimg` sul v2 mostra la stessa riga **senza** ` panic=10` e tutto
  il resto identico → differenza strumentale confermata.
* `manifest.json` → `"cmdline_v2_prefix_identical": true`.

## 5. Test QEMU (smoke test: il ramdisk si scompatta e /init parte)

Immagine e ramdisk vengono **estratti dall'immagine costruita**, non dai file di
staging: il test esercita l'artefatto consegnato.

Comando esatto (dall'artefatto: `candidate-stockkernel-v3/qemu-smoke-v3.json`):

```
qemu-system-aarch64 -machine virt -cpu cortex-a57 -accel tcg -smp 2 -m 2048 \
  -nodefaults -nic none -display none -monitor none -serial stdio -no-reboot \
  -kernel candidate-stockkernel-v3/qemu/kernel-from-image \
  -initrd candidate-stockkernel-v3/qemu/ramdisk-from-image.gz \
  -append "console=ttyAMA0 earlycon loglevel=7 panic=10 rdinit=/init"
```

Log completo: `candidate-stockkernel-v3/qemu/boot.log` (31.235 byte, sha256
`d90bfc2f99075ee5d6cf72b51766978b736f23a8afa2de0e59f550d08be2f718`).
QEMU terminato con SIGTERM dal test dopo 62,7 s (procedura prevista); durata
della sessione 62,7 s, nessun panic.

### Righe di log che provano le tesi richieste

Scompattamento dell'initramfs (il kernel 5.10 usa questo testo, non
`Unpacking initramfs...` che compare nei kernel 6.x):

```
131:[    0.292752][    T1] Trying to unpack rootfs image as initramfs...
263:[    0.904574][    T1] Freeing unused kernel memory: 3072K
```

Nessun fallimento di scompattamento: la stringa `Initramfs unpacking failed`
**non compare** nel log (nel controllo precedente `qemu-current-b.log`, con il
ramdisk rotto, era presente: `Initramfs unpacking failed: broken padding`).

`/init` parte come PID 1 e gira il nostro script:

```
264:[    0.905287][    T1] Run /init as init process
265:[    0.975369][    T1] nx679j: init v2 entered
270:[    1.041619][    T1] nx679j: stage0 cmdline=stack_depot_disable=on … panic=10 rdinit=/init
274:[    1.161612][    T1] nx679j: modules dir=/lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604 insmod=/sbin/insmod count=327
```

Catena moduli, attesa UFS e UDC, handover:

```
420:[   16.900008][    T1] nx679j: ufs sda after 15s present=no
424:[   37.229622][    T1] nx679j: udc=NONE after 20s
426:[   37.240967][    T1] nx679j: handover init=present
```

Il ramo `exec /sbin/init` viene eseguito e la userspace OpenWrt parte davvero
(procd, sempre con il kernel stock 5.10):

```
437:[   42.234112][    T1] procd: - early -
438:[   42.912286][    T1] procd: - ubus -
442:[   42.947605][    T1] procd: - init -
443:Please press Enter to activate this console.
```

Marker JSON (`qemu-smoke-v3.json`): `unpacking_initramfs_seen: true`,
`unpack_failed: false`, `run_init_as_init_process_seen: true`,
`init_script_ran: true`, `stage0_cmdline_logged: true`, `procd_seen: true`,
`kernel_panic: false`. Lo script esce 0 solo se tutti i marker di scompattamento
e avvio init sono veri.

## 6. Cosa NON ha funzionato / limiti osservati

1. **25 moduli su 40 non vengono caricati: `insmod … ABSENT`.** La lista in
   `candidate-init-v2.sh` (mantenuta identica in v3, come richiesto) usa i nomi
   stile `/proc/modules` (underscore), ma i file nel ramdisk hanno i trattini.
   Il ciclo cerca solo `$M/$ko.ko` e `$M/${ko}.ko` — la stessa stringa due
   volte, quindi non c'è alcuna conversione `_` → `-`.
   * 24 esistono con nome a trattini (`dwc3_msm` → `dwc3-msm.ko`,
     `phy_msm_snps_hs` → `phy-msm-snps-hs.ko`, `fsa4480_i2c` → `fsa4480-i2c.ko`,
     `qcom_scm` → `qcom-scm.ko`, `clk_qcom` → `clk-qcom.ko`, …);
   * 1 non esiste con nessuna delle due grafie: `nvmem_qcom_spmi_sdam`
     (a bordo c'è `nvmem_qcom-spmi-sdam.ko`).
   Elenco completo degli ABSENT: `phy_qcom_emu`, `phy_msm_snps_eusb2`,
   `phy_qcom_ufs_qmp_v4_lahaina`, `phy_qcom_ufs_qmp_14nm`,
   `phy_qcom_ufs_qmp_v3`, `fsa4480_i2c`, `altmode_glink`, `dwc3_msm`,
   `phy_msm_ssusb_qmp`, `phy_msm_snps_hs`, `ssusb_redriver_nb7vpq904m`,
   `ufshcd_crypto_qti`, `qti_regmap_debugfs`, `phy_qcom_ufs_qmp_v4_cape`,
   `phy_qcom_ufs_qmp_v4_diwali`, `phy_qcom_ufs_qmp_v4_waipio`, `phy_qcom_ufs`,
   `nvmem_qcom_spmi_sdam`, `crypto_qti_common`, `crypto_qti_hwkm`, `clk_qcom`,
   `gdsc_regulator`, `proxy_consumer`, `debug_regulator`, `qcom_scm`.
   **È un difetto reale e non un artefatto di QEMU**: le stesse righe ABSENT
   comparirebbero sul telefono. È una regressione rispetto a v1, dove la lista
   usava i nomi-file a trattini e tutti e 25 i moduli risultavano `rc=0`
   (`candidate-stockkernel-v1/qemu/boot.log`). Non è stato corretto in v3 perché
   il requisito imponeva la catena v2 identica: la correzione è una riga
   (un terzo candidato `${ko//_/-}.ko` nel ciclo) e va messa in v4.
2. **11 dei 15 moduli effettivamente trovati escono `rc=255`** in QEMU
   (`minidump`, `pdr_interface`, `pmic_glink`, `qcom_glink`, `qcom_glink_smem`,
   `qcom_glink_spss`, `qcom_ipc_logging`, `qcom_smd`, `rproc_qcom_common`,
   `ucsi_glink`, `ufs_qcom`). Atteso: senza device tree Waipio i driver qcom non
   trovano nodi/compatibili. `rc=0`: `nvmem_qfprom`, `qmi_helpers`, `repeater`,
   `smem`. Questi valori **non dicono nulla** sul comportamento sul telefono.
3. **UFS assente** (`present=no`): QEMU `virt` non ha il controller UFS.
4. **UDC assente** (`udc=NONE`): nessun `dwc3`, quindi il ramo gadget/NCM non
   viene mai eseguito; `telnetd` (dentro quel ramo) non parte e la journal
   scritta su rawdump non è mai stata prodotta.
5. **La journal su rawdump non è verificata in QEMU.** Non esiste alcun blocco
   con `size = 524288` settori, quindi `find_rawdump()` fallisce e `flush()`
   termina senza scrivere. Il canale breadcrumb resta *non provato* fuori dal
   telefono.
6. **`panic=10` non è stato esercitato.** Nessun panic è occorso in QEMU,
   quindi il riavvio automatico non è stato osservato; inoltre il test usa
   `-no-reboot`, per cui un eventuale panic avrebbe chiuso QEMU invece di
   riavviare. La modifica è verificata solo a livello di immagine (byte della
   cmdline), non a livello di comportamento.
7. **Immagine non firmata** (`signature size: 0`): il comportamento di
   ABL/AVB su slot B non è testato.
8. Nota di metodo: nel kernel 5.10 del telefono il messaggio di scompattamento
   è `Trying to unpack rootfs image as initramfs...`; `Unpacking initramfs...`
   appartiene ai kernel 6.x (era il testo visto con il kernel OpenWrt armsr
   attualmente in slot B). Cercare la stringa sbagliata produce un falso
   negativo.

## 7. Cosa il test QEMU NON prova (da non confondere con verifica hardware)

* **Nulla sull'hardware Qualcomm.** QEMU `virt` non ha il device tree Waipio:
  `dwc3-msm`, `phy-msm-*`, `fsa4480` non fanno probe; non esiste alcun UDC,
  alcuno stato di link USB, alcuna enumerazione NCM. Il gadget configfs e il
  ruolo peripheral restano **non verificati**.
* UFS/UFSHCD reali, PHY UFS, gear/lane, crypto inline: non esercitati
  (nessun `sda`, nessuna partizione, nessun `rawdump` da leggere o scrivere).
* La journal su rawdump (524288 settori) e la sua leggibilità da Android:
  non provate.
* Accettazione del boot image da parte di ABL/AVB, verdetto di dm-verity,
  selezione slot A/B, `bootcontrol`/misc: non provati.
* L'efficacia di `panic=10` sul telefono (riavvio automatico dopo panic):
  non provata.
* L'ordine di caricamento dei moduli sul DT reale e la dipendenza
  `dwc3-msm`/PHY in condizioni reali: non provata.
* L'esito di un boot reale su slot B: non provato da questa sessione.

## 8. Riproducibilità

Il builder è stato eseguito **due volte** da zero (`build-candidate-v3.py`,
`shutil.rmtree(OUT)` all'avvio): immagine identica, sha256
`a35a28c1…` in entrambe le esecuzioni (`gzip.compress(…, 9, mtime=0)` e newc con
mtime 0 → pacchetto deterministico). Output completo del builder:
`candidate-stockkernel-v3-build-stdout.log`.

## 9. File prodotti

```
build-candidate-v3.py                        builder v3 (helper v2 riusati verbatim)
candidate-init-v3.sh                         init v3 (= v2 + commenti; corpo identico)
candidate-stockkernel-v3-build-stdout.log    stdout completo del builder
verify-candidate-v3.py                       verifica strutturale (35 controlli)
qemu-candidate-v3.py                         smoke test QEMU
candidate-stockkernel-v3/
  boot_b-stockkernel-openwrt-v3.img          artefatto
  manifest.json, inputs.json                 manifest e hash degli input
  verify-structural.log / .json              esito verifica strutturale
  qemu-smoke-v3.log / .json                  esito smoke test
  qemu/boot.log                              log di boot QEMU (prove)
  qemu/kernel-from-image                     kernel estratto dall'immagine
  qemu/ramdisk-from-image.gz                 ramdisk estratto dall'immagine
  init-v2-vs-v3.diff                         diff dei due init (solo commenti)
  work/kernel, work/ramdisk.cpio.gz          staging del builder
```

## 10. Conclusione

v3 è costruito, riproducibile e strutturalmente corretto (35/35): header v4,
dimensioni coerenti col manifest, 61.165.568 B < 0x6000000, ramdisk newc valido
con 1541 entry e 327 moduli vendor, kernel identico a quello stock estratto dal
telefono, cmdline = v2 + ` panic=10`. Lo smoke test QEMU prova che il ramdisk si
scompatta, che `/init` parte come PID 1, che lo script percorre la catena moduli,
attende UFS/UDC, registra `handover init=present` e passa a `/sbin/init` con
procd attivo. Resta aperto, e va corretto in una v4, il difetto dei nomi modulo
(25/40 ABSENT); tutto ciò che riguarda l'hardware Qualcomm, ABL/AVB, UFS reale e
il canale breadcrumb su rawdump **non è provato** da questo test.
