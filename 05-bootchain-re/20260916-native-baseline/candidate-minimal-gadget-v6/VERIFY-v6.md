# VERIFY-v6 — gadget probe con `/init` STATICO e catena moduli dal device (Nubia NX679J, slot B)

Data: 17 settembre 2026 — profilo `kernel-re`
Base: `/home/user/nx679j-stock/experiments/20260916-122926-native-baseline/`
Kernel: invariato, stock `5.10.66-android12-9-00005-gf6e6376090be-ab8060604`
(`f0aa949c95d1421381850cb1982bd2a505e96fcd7565543ec28d5b1f7d81a0cc`, 49.108.324 B)

## 0. Perche' esiste la v6 (contesto misurato, non ipotesi)

| run | kernel | ramdisk | esito misurato |
|---|---|---|---|
| controllo Magisk su B | stock | Magisk (100 MB img) | Android enumerato in **26 s** → ABL avvia immagini non firmate su B |
| v4 | stock | 12 MB OpenWrt + 327 moduli | **niente** USB in 140 s, journal rawdump tutto a zero |
| v5 | stock | 1,15 MB (busybox+musl+kmodloader+40 moduli) | **niente** USB in 150 s |
| v5/probe2 | stock | v5 kernel + ramdisk Magisk | **niente** USB in 150 s |

### 0.1 La causa trovata (nuova, verificata sui byte)

Il boot chain **non** consegna solo la nostra ramdisk: `vendor_boot` porta il
`vendor_ramdisk00` del device, che finisce nello stesso rootfs. Estratto dal
readback di questo telefono (`current-readback/vendor_boot_b.img`, lz4 legacy,
sha256 del cpio `35205632` B) e confrontato con l'estrazione del profilo
(`port-work/stock_vendor_ramdisk/`): **identico** (6/6 file di metadati e 47/47
moduli con lo stesso sha256). Contenuto reale:

```
lib/modules/*.ko                 327 moduli, PIATTI (nessuna sottodirectory di versione)
lib/modules/modules.load          98 voci  (lista first-stage di Android)
lib/modules/modules.load.recovery 329 voci (variante recovery)
lib/modules/modules.dep           327 righe (dipendenze HARD, percorsi assoluti)
lib/modules/modules.softdep        17 righe (dipendenze SOFT)
lib/modules/modules.alias         714, modules.blocklist 64
first_stage_ramdisk/fstab.qcom, avb/*.avbpubkey
```

Dentro `modules.softdep` ci sono esattamente le due righe che spiegano il
silenzio di v4/v5:

```
softdep smem     pre: qcom_hwspinlock
softdep dwc3_msm pre: phy-generic phy-msm-snps-hs phy-msm-ssusb-qmp eud
```

La lista hardcoded di 40 nomi della v5 **non conteneva** `qcom_hwspinlock`,
`phy-generic` ne' `eud` (verificato: 40 nomi risolti → i tre non ci sono).
Senza quei provider `smem.ko` e `dwc3_msm.ko` non risolvono i simboli e non si
caricano → niente UFS (niente journal) e niente USB (niente gadget). In QEMU
tutto questo non era visibile: li' non esistono UFS ne' UDC, quindi il test
passava comunque.

La v6 quindi non cambia il kernel e non aggiunge "piu' roba": **smette di
indovinare l'ordine dei moduli** e legge la metadata del device.

## 1. Immagine

| campo | valore |
|---|---|
| path | `.../candidate-minimal-gadget-v6/boot_b-staticinit-v6.img` |
| dimensione | **50.020.352 byte** (47,70 MiB) |
| sha256 | `273fd8be271b1389dd886c4b2d77a35e0f77ee8b810dca61c418a0f39aab029b` |
| header | Android boot header **v4**, `header_size=1584`, os 12.0.0 / 2022-02, `signature_size=0` (non firmata) |
| capacita' partizione | 0x6000000 = 100.663.296 B → margine 50.642.944 B |
| cmdline | **byte-identica alla v5** (e quindi alla v4), `panic=10` incluso |
| kernel (payload) | 49.108.324 B, sha256 `f0aa949c…a0cc`, **byte-identico** al readback `boot_a.img` |

## 2. Ramdisk

| campo | valore |
|---|---|
| dimensione compressa | **901.952 B** (gzip -9, mtime 0) |
| dimensione cpio (newc) | 3.237.608 B |
| sha256 | `8378e9a3398f55bfee72166b8bcd2df94b5cca098418d948cf3bf44019a9d035` |
| entry | **65** (v5: 85) |
| nodi device | 6 statici (`/dev/kmsg` 1:11, `/dev/console` 5:1, `/dev/pmsg0` placeholder 252:0, null, zero, tty) |
| file | `/init` (modo 0755, 663.456 B) + 47 `.ko` in `lib/modules/<krel>/` + `lib/modules/fallback.order` |
| directory | 10 (`dev`, `proc`, `sys`, `sys/kernel`, `sys/kernel/config`, `sys/fs`, `sys/fs/pstore`, `lib`, `lib/modules`, `lib/modules/<krel>`) |
| utenti non privilegiati | nessuno: niente busybox, niente musl, niente kmodloader, niente shell, niente `/sbin/init`, niente albero OpenWrt, niente tmpfs/tar/gzip |

`lib/modules/fallback.order` (47 righe, ordine topologico) ha due usi, entrambi
nell'init: (a) e' la **lista ordinata di fallback** quando la metadata del device
non c'e' (caso QEMU); (b) e' l'insieme **goal** aggiunto alla catena del device,
perche' `modules.load`/`modules.load.recovery` non contengono ne' `ufs_qcom`
(assente da entrambe le liste!) ne' altre parti della catena UFS.

## 3. Piano dei moduli (la parte che cambia rispetto a v5)

| campo | valore |
|---|---|
| sorgente metadata | `unpacked-current/vendor_boot_a/vendor_ramdisk00` (device, lz4) |
| seed | 40 nomi (la lista della v5, dal suo manifest) |
| **closure bundlata** | **47 moduli**, 2.561.880 B raw |
| aggiunti dalla closure | `qcom_hwspinlock`, `phy_generic`, `eud`, `qrtr`, `hwkm`, `ns`, `tmecom_intf` |
| moduli non risolvibili nel vendor set | nessuno |
| goal espliciti nell'init | 28 nomi (`GOALS[]`, tutti verificati presenti nella closure dal build) |
| sha256 metadata (device, verificati identici fra i due readback) | `modules.load d44ddf1f…`, `modules.load.recovery 6d5ff72c…`, `modules.dep 30bedfce…`, `modules.softdep 8f0d2002…`, `modules.blocklist 34c31aa3…`, `modules.alias 4650b3f3…` |

Ordine risultante (posizioni rilevanti):

```
order[  6] qcom_hwspinlock      order[ 36] dwc3_msm
order[  7] smem                 order[ 42] ufs_qcom
order[ 32] phy_generic          order[ 45] phy_qcom_ufs_qmp_v4_waipio
order[ 35] eud
```

### 3.1 Due cose che vanno dette sulla catena

* **vermagic**: i moduli del vendor set dichiarano
  `5.10.66-gki-g491fe99db339 SMP preempt mod_unload modversions aarch64`, il
  kernel in esecuzione `5.10.66-android12-9-00005-gf6e6376090be-ab8060604`.
  Non e' un problema: con `CONFIG_MODVERSIONS=y` (che questo kernel ha)
  `same_magic()` ignora la parte di release e confronta solo
  `SMP preempt mod_unload modversions aarch64`, che combacia. Conferma
  indipendente: il telefono vivo ha 376 moduli caricati (`runtime-v2/modules.txt`).
  Misura diretta nella v6: in QEMU i 47 moduli della closure caricano **47/47
  rc=0** e nella catena completa 294/304.
* **spelling misto**: nel vendor set esistono nomi come
  `nvmem_qcom-spmi-sdam.ko`. Risolvere il nome per tentativi (tutti trattini /
  tutti underscore) sbagliava: l'init ora indicizza la **lista reale** dei file
  (`/lib/modules` piatto del device e `/lib/modules/<krel>` nostro) e risolve
  per nome canonico. Il primo tentativo in QEMU perdeva proprio questo modulo;
  dopo la correzione `missing=0`.

## 4. `/init` (binario statico, PID 1, non esce mai)

Sorgente `candidate-init-v6.c` (sha256 `495886c2e0f8fefa…`), compilato con
`aarch64-linux-gnu-gcc 16.1.0 -static -Os -s` → **663.456 B**, ELF aarch64
`EXEC`, `statically linked`, **nessun `PT_INTERP`, nessuna `DT_NEEDED`**, non e'
uno script (niente `#!`). Non c'e' un solo `exit()/_exit()/abort()` nel
sorgente, `main()` termina in un ciclo infinito e la sua ultima istruzione e'
irraggiungibile: **PID 1 non puo' morire** e il kernel non puo' andare in panic
per "init exited".

Sequenza reale eseguita:

1. **canali**: apre `/dev/kmsg` (1:11) e `/dev/console` **O_NONBLOCK** (una
   seriale senza lettore non puo' bloccare PID 1); se nessuno dei due si apre
   usa fd 1. `/dev/pmsg0` **non** e' un major fisso: `fs/pstore/pmsg.c` registra
   il chrdev con `register_chrdev(0, "pmsg", …)` (major dinamico, 252 sul
   telefono vivo), quindi l'init legge il major reale da `/proc/devices`,
   ri-crea il nodo e solo allora lo apre; se `/proc/devices` non elenca `pmsg`
   lo dichiara **non disponibile** invece di scrivere su un major indovinato.
2. **mount**: proc, sysfs, configfs (`/sys/kernel/config`), pstore.
3. **catena moduli** (prima cosa utile che fa):
   * se esiste `/lib/modules/modules.dep` → percorso **primario**: indicizza i
     `.ko` reali, parsa `modules.dep` (dipendenze hard), `modules.softdep`
     (`pre:`/`post:`), `modules.blocklist`, costruisce la lista `modules.load`
     (98) **poi** `modules.load.recovery` (329) e vi aggiunge i 28 goal;
     carica con `finit_module(2)` (fallback `init_module(2)` se il kernel non
     ha finit_module) in **ordine topologico** DFS: dipendenze hard, poi
     `pre:`, poi il modulo, poi `post:`; se `modules.dep` non esiste → percorso
     di **fallback** su `/lib/modules/fallback.order`.
   * ogni modulo produce una riga di journal: `modload <nome> rc=<n> (<errno>)`
     con `ENOENT/ENOEXEC/EINVAL…` tradotti; un fallimento **non** ferma nulla.
   * appena `dwc3_msm` e' caricato (e quindi le sue softdep prima) l'init tenta
     il gadget *subito*, senza aspettare la fine della catena.
4. **gadget**: `poke_role()` sui nodi `/sys/class/usb_role/*/role`, poi configfs:
   `g1` con `idVendor 0x18d1`, `idProduct 0x4ee7`, funzione **NCM** (`ncm.usb0`,
   `u_ether`/`libcomposite`/`f_ncm` sono **built-in**, quindi nessun modulo serve
   per il gadget), bind sulla UDC reale (esclusa `dummy_udc`), poi `usb0` a
   `10.0.0.1/24` via `ioctl(SIOCSIFADDR/SIOCSIFNETMASK/SIOCSIFFLAGS)` — non
   esiste `ip` in questa ramdisk.
5. **journal**: buffer circolare in memoria di **64 KiB** (nessun filesystem),
   riscritto su rawdump (offset 0, 4 KiB alla volta, solo quando cresce) appena
   compare un block device da 524.288 settori = la partizione rawdump da 256 MiB
   (senza ueventd non esiste `/dev/block/by-name`).
6. **ciclo infinito**: ogni ~15 s ristampa ciclo/uptime, ri-poke del ruolo,
   nuovo tentativo di gadget e nuovo flush del journal. Mai `exit()`.

## 5. Verifica — cosa e' stato misurato, non dedotto

### 5.1 Struttura (`verify-candidate-v6.py` → `verify-structural.json`): **80/80 PASS**

Controlla sulle **byte dell'immagine spedita**, non sullo staging: header v4
(magic, `header_version=4`, `header_size=1584`, os 12.0.0/2022-02,
`signature_size=0`), cmdline byte-identica alla v5, kernel byte-identico al
readback `boot_a`, cpio/gzip coerenti, 10 directory + 6 nodi + 65 entry,
47/47 `.ko` con lo sha256 del manifest **e byte-identici alla copia del device**,
`qcom_hwspinlock.ko`/`phy-generic.ko`/`eud.ko` presenti, `fallback.order` con
l'intera closure una volta sola e nell'ordine giusto (`qcom_hwspinlock` prima di
`smem`; `phy-generic`/`eud` prima di `dwc3-msm`), `/init` statico senza
interprete, sorgente senza `exit()/abort()`, sorgente che contiene la logica
`modules.dep`/`modules.softdep`/`modules.load.recovery`/`finit_module`.
Liveness: il binario eseguito sotto `qemu-aarch64` **deve essere ucciso** dopo
12 s (rc=124 = era ancora vivo) e continua a ciclare.

### 5.2 Boot completo QEMU, percorso di fallback (`qemu-candidate-v6.py`): **21/21 PASS**

Ramdisk della v6 così com'e' (senza metadata del device, come in QEMU):
`modload source=fallback … ok=47 fail=0 skip=0 missing=0`, `dwc3_msm rc=0`,
`ufs_qcom rc=0`, ordine softdep rispettato sulla linea del log, gadget tentato
dopo la catena, 5 cicli di retry, PID 1 vivo alla fine, **nessun panic**.

### 5.3 Boot completo QEMU, percorso PRIMARIO con la ramdisk del device (`qemu-candidate-v6-vendor.py`): **13/13 PASS**

Il test costruisce la ramdisk composita **come la costruisce il boot chain**
(`cpio(ours) + cpio(vendor_ramdisk00)`, gzip unico) e avvia:

```
init v6 entered pid=1                                  (il NOSTRO /init vince)
modload indexed 327 .ko files on disk                  (47 nostri + 327 vendor, nomi duplicati)
modload source=device modules.load=98 recovery=329 parsed deps=389 softdep pre=4 post=0
modload summary loaded=294 skipped=0 failed=8 missing=2 dwc3_seen=1
modload dwc3_msm rc=0 …/dwc3-msm.ko                    (softdep pre caricate prima)
modload ufs_qcom rc=0 …/ufs_qcom.ko
314 righe di journal con rc, di cui 58+ per-modulo …   (gadget dopo dwc3_msm, 24 cicli, nessun panic)
```

Gli 8 fallimenti + 2 mancanti sono **attesi e non riguardano la catena UFS/USB**:
`qcom_arm_smmu_mod` e `qcom_tlmm_vm_irqchip` sono richiesti dalle softdep di
`msm_kgsl` e `pinctrl_waipio` ma **non esistono nel vendor set** (stanno in
`vendor_dlkm`), quindi `cnss2`/`icnss2` non risolvono i simboli (`rc=-2`), e i
moduli WLAN/QMI falliscono con `ENODEV` perche' in QEMU non c'e' hardware radio.
In QEMU non compare nessuna UDC (non esiste il device tree Waipio), quindi il
gadget non puo' legarsi: e' esattamente il limite dichiarato del test.

## 6. Cosa NON e' provato (dichiarato, non nascosto)

1. **Che il boot chain consegni davvero il `vendor_ramdisk00` accanto alla
   nostra ramdisk su questo telefono.** E' quello che implica la struttura A/B
   + `vendor_boot` (il kernel scompatta piu' archivi cpio nello stesso rootfs) e
   quello che rende conto delle softdep mancanti, ma nessuno l'ha ancora visto
   da userspace: la v6 lo *misura* (se `modules.load`/`modules.dep` non sono
   leggibili, il log dira` `modload: no /lib/modules/modules.dep`).
2. **Che l'immagine venga accettata da ABL/AVB su slot B**: la v5 non e' mai
   stata provata; il controllo Magisk su B *e'* partito (26 s), quindi
   l'ipotesi e' ragionevole ma non e' evidenza per questa immagine.
3. **Che i 294 moduli carichino sul telefono**: in QEMU caricano (stesso kernel,
   stessa ABI), ma i numeri veri sono solo quelli che scrivera' il journal della
   prossima run hardware.
4. **Che `dwc3_msm` porti su la UDC `a600000.dwc3`** e che il gadget NCM si
   leghi: in QEMU non esiste UDC. La UDC e' stata vista solo sul telefono con
   Android avviato (376 moduli caricati).
5. **Che il kernel raggiunga `/init` partendo da un'immagine su boot_b**: la v6
   e' costruita per rendere la risposta non ambigua (banner immediato su kmsg +
   pmsg + rawdump), ma la risposta arrivera' dal telefono.
6. **Che `rawdump` sia davvero identificabile per dimensione** (524.288 settori):
   senza ueventd non c'e' il symlink by-name, quindi la v6 usa la dimensione; se
   un altro device avesse la stessa dimensione il journal finirebbe li'.
7. **Che `ramoops`/`pmsg` esistano in questo boot**: il major e' letto a runtime
   da `/proc/devices`; se non c'e', il canale e' dichiarato assente (in QEMU
   succede esattamente questo).
8. **Il canale di rete**: nessuno ha ancora visto `18d1:4ee7` ne' pingato
   `10.0.0.1` su hardware.

## 7. Come si prova su hardware

```bash
cd /home/user/nx679j-stock/experiments/20260916-122926-native-baseline
./test-v6-slotb.sh 180      # telefono in Android su slot A, adb+root attivi
```

Lo script riusa la procedura di controllo gia' provata: hash pre-intervento dei
slot B, push + `dd` su `boot_b` + readback con confronto sha256 (se non combacia
si ferma **senza** riavviare), attivazione B con `fastboot set_active b`,
monitor USB 180 s via `monitor-boot.sh`, valutazione del segnale
(**18d1:4ee7**; se compare, prova anche il ping a 10.0.0.1), **ritorno a slot A**,
e infine lettura dell'evidenza: primi 64 KiB di
`/dev/block/by-name/rawdump` (journal `nx679j-v6: …`) e `/sys/fs/pstore`.

Lettura degli esiti:

* gadget visibile → il kernel arriva a `/init` **e** la catena moduli funziona:
  si puo' passare alla parte OpenWrt/netd.
* niente USB ma journal su rawdump → la catena gira e fallisce su un modulo
  preciso: il journal nomina il primo `rc!=0` (es. `dwc3_msm rc=-2`).
* niente USB e rawdump a zero → UFS non e' salita: guardare i primi `rc!=0` e
  `missing` nel journal; se il journal non c'e' affatto, `ufs_qcom` e' il primo
  sospetto.
* niente di niente (nemmeno su pstore/kmsg) → il kernel non arriva a `/init`:
  il problema e' a monte (immagine non accettata, header, cmdline), non
  nell'init, e la v6 lo dimostra perche' l'init non puo' morire.

## 8. Artefatti e provenienza

| file | ruolo | sha256 |
|---|---|---|
| `candidate-minimal-gadget-v6/boot_b-staticinit-v6.img` | **immagine da flashare su boot_b** | `273fd8be271b1389dd886c4b2d77a35e0f77ee8b810dca61c418a0f39aab029b` |
| `candidate-init-v6.c` | sorgente di `/init` | `495886c2e0f8fefa…` |
| `candidate-minimal-gadget-v6/work/init` | binario compilato (dentro la ramdisk) | `a2c901f0375e940521388587f713967f70d56ac629350613d40a0952a5b15e02` |
| `build-candidate-v6.py` | build: kernel stock + binario statico + 47 moduli + `fallback.order` + mkbootimg | — |
| `v6-module-plan.py` | piano moduli: seed v5 → closure su `modules.dep`/`modules.softdep` → ordine topologico | — |
| `verify-candidate-v6.py` | 80 controlli strutturali + liveness | — |
| `qemu-candidate-v6.py` | boot full-system, percorso di fallback | 21/21 |
| `qemu-candidate-v6-vendor.py` | boot full-system con `vendor_ramdisk00` appesa, percorso primario | 13/13 |
| `test-v6-slotb.sh` | test hardware su slot B + ritorno a A + lettura evidenza | — |
| `candidate-minimal-gadget-v6/manifest.json`, `inputs.json` | tutti gli hash, la tabella moduli, il piano, le softdep citate | — |

Log/JSON dei test: `candidate-minimal-gadget-v6/qemu-smoke-v6.json`,
`candidate-minimal-gadget-v6/qemu-vendor-ramdisk-v6.json`,
`candidate-minimal-gadget-v6/verify-structural.json`,
`candidate-minimal-gadget-v6/qemu/boot-v6.log`,
`candidate-minimal-gadget-v6/qemu/boot-v6-vendor.log`.

## 9. Differenze rispetto alla v5 (sintesi)

| | v5 | v6 |
|---|---|---|
| PID 1 | script `sh` + busybox + musl | **binario statico** (nessun interprete, nessuna lib) |
| ordine moduli | lista hardcoded di 40 nomi | `modules.load` (98) + `modules.load.recovery` (329) + goal, con **dipendenze hard e softdep** lette a runtime dal device |
| softdep `smem`/`dwc3_msm` | ignorate → `smem`/`dwc3_msm` non caricabili | rispettate (`qcom_hwspinlock`, `phy-generic`, `eud` bundlati e caricati prima) |
| gadget | ultimo tentativo, dopo 40 moduli | tentato **appena `dwc3_msm` e' in**, poi ogni ciclo |
| moduli nella ramdisk | 40 (2,26 MB) | 47 (2,56 MB) + `fallback.order` |
| journal | 4 KiB (coda), su rawdump se UFS sale | 64 KiB, riscritto quando cresce, con **rc per ogni modulo** |
| pmsg | `252:0` fisso | major risolto da `/proc/devices` |
| console | apertura bloccante | `O_NONBLOCK` (non puo' bloccare PID 1) |
| moduli per nome misto | risolti con tabella alias nel build | indicizzati dalla lista reale dei file a runtime |
| immagine | 50.266.112 B | 50.020.352 B (ramdisk 78,6% della v5) |

Nessuna verifica della v5 e' stata rimossa: le 80 verifiche strutturali della v6
includono anche assenza di busybox/musl/kmodloader/shell e l'identita' byte-per-byte
del kernel stock, e le due verifiche di boot in QEMU restano entrambe verdi.
