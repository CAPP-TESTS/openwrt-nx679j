# NX679J - OpenWrt nativo sul RedMagic 7: la storia del port

Questo documento racconta, dall'inizio alla fine, un port completo: **OpenWrt 25.12.5 in chroot sullo slot B** di un Nubia RedMagic 7 (`NX679J`, SM8450/Waipio), con il **kernel vendor 5.10.66 lasciato intatto** e display, touch, modem X65 e interfaccia utente funzionanti sul telefono vero.
Il principio che regge ogni pagina e' uno solo: **ogni affermazione porta con se' la sua evidenza** — comandi, file, righe di log, md5, offset, nomi di modulo — e cio' che non e' stato verificato non viene raccontato come se lo fosse.
Per la stessa ragione ogni contributo esterno (distribuzioni, tool, sorgenti vendor, thread, articoli) e' accreditato in coda: il lavoro poggia su molta conoscenza di altri, e quei crediti appartengono ai loro autori.

## Indice

- [Fase 0d: l'inizio - inventario e primi esperimenti](#fase-0d-linizio---inventario-e-primi-esperimenti)
- [Fase 0a: la catena di boot e il secure boot](#fase-0a-la-catena-di-boot-e-il-secure-boot)
- [Fase 0b: EDL/QDL, la modalita' buggata](#fase-0b-edlqdl-la-modalita-buggata)
- [Fase 0c: dentro la catena - XBL, ABL, UEFI](#fase-0c-dentro-la-catena---xbl-abl-uefi)
- [Fase 1: la catena di boot](#fase-1-la-catena-di-boot)
- [Fase 2: l'immagine di boot e il sistema](#fase-2-limmagine-di-boot-e-il-sistema)
- [Fase 3: il modem X65](#fase-3-il-modem-x65)
- [Fase 4: l'interfaccia utente](#fase-4-linterfaccia-utente)
- [Fase 5: kernel, display e scoperte](#fase-5-kernel-display-e-scoperte)
- [Crediti e riferimenti: strumenti e codice di terzi](#crediti-e-riferimenti-strumenti-e-codice-di-terzi)
- [Crediti e riferimenti: fonti di conoscenza](#crediti-e-riferimenti-fonti-di-conoscenza)
- [Nota finale](#nota-finale)
- [Complementi](#complementi) — cronologia, scheda hardware, manuale operativo, archivio

## Come leggere questo documento

Le cinque sezioni di fase sono **autonome**: si puo' leggere solo quella che interessa, senza aver letto le precedenti.
Le **fonti locali** (file di stato, esiti, log, script del progetto) sono citate dentro il testo, con nomi e valori esatti.
L'etichetta **Da completare** segnala cio' che non e' presente nei documenti locali e che quindi **non e' stato inventato** per far quadrare il racconto.

## Fase 0d: l'inizio - inventario e primi esperimenti

**Il dump di sessione piu' vecchio non coincide con l'inizio del progetto.** Il primo artefatto Hermes del profilo `kernel-re` e' `sessions/request_dump_20260904_221805_4f6f50_20260904_221811_908760.json` (04/09 22:18:11): contiene un unico messaggio utente, "Rispondi solo: quale modello e provider stai usando adesso?", ed e' un errore di configurazione (`invalid_request_error`, 404 `model_not_found`, modello `claude-haiku-4-5`). Le sessioni di quel giorno registrate in `state/state.db` sono 7, tutte tra le 22:13 e le 22:29, tutte test di modello/provider; la prima (`20260904_221355_4267e7`, gpt-5.6-luna, 8 messaggi) parla genericamente di "un modulo kernel vendor closed-source per una scheda PCIe". **Nessuna di quelle sessioni contiene lavoro sul dispositivo.** Il progetto inizia il 16/09 alle 12:26:13 (sessione `20260916_122610_968914`, titolo "Installare OpenWrt nativamente su NX679J"): "abbiamo un nuovo progetto, dobbiamo installare nativamente openwrt sullo smartphone collegato USB a questo PC", con allegato `NX679J_OPENWRT_KERNEL_RE_HANDOFF.md` (7.157 token).

**Cio' che era gia' stato fatto prima (2 lug - 7 ago 2026).** Questa storia non sta nei dump ma nel file system, e precede di due mesi l'avvio del 16/09:

- 01/07 23:56 -> 02/07 00:11 download del firmware stock (`V311-download.log`: "2026-07-02 00:11:32 ... 'NX679J-V311-update.zip' saved [3916284140/3916284140]"); 02/07 00:12 prima estrazione in `extracted/` (boot, vendor_boot, dtbo, recovery, vbmeta, vbmeta_system) con `payload-dumper-go`.
- 02/07 11:44 boot patchato con Magisk (`magisk_patched-30700_Zx2eF.img`); 02/07 16:47-16:48 primo backup della slot B, con hash: `phase0-backups/2026-07-02-slotb-preproto/` (`SHA256SUMS`, 6 immagini, `restore_slot_b.sh`).
- 02-04/07 primi prototipi OpenWrt: `port-work/openwrt-phase1` ... `openwrt-phase4-proto1-rootfsfirst` (quest'ultima con `boot_b_phase4_proto1_rootfsfirst.img.sha256`).
- 05/07 23:07 estrazione dei moduli stock (`stock-modules/`, 283 `.ko`); 08/07 asset UEFI mu_aloha (`port-work/*-boot-assets/uefi`); 07/07 `vboot-dtb-swap`.
- 10/07 il primo inventario scritto: `port-work/device-facts-*.md` ("Phase 3 Device Facts ... facts only - no re-flash"), con identita' del dispositivo, layout delle partizioni e mappa di memoria UEFI; accanto `ROOT-CAUSE-900e.md` e `phase4-minimal-payload.md`, piu' `verified-v311/` e `verified-port/` (incl. `mu_aloha_platforms-main`).
- 11/07 13:28-13:49: `recovery-v411-bootchain-20260711/`, `recovery-v311-magisk-20260711/boot_a-magisk-30700.img`, `runtime-evidence-20260711/` (dmesg, properties, gpio, pinmux, regulator_summary).
- 17/07: primo readback reale dal dispositivo via EDL: `edl-recovery/recon-20260717-183330/` (printgpt.txt 159 righe, 32 partizioni `Active True` e 72 `False`; sha256.txt 17 righe; boot/xbl/abl di entrambe le slot).
- 06-07/08: RE del bootloader (`bootloader-re/`: abl_a, xbl_a, hyp_a, uefi_fv.bin, FINDINGS.md, FINAL_REPORT.md) e `hypervisor-bypass/` (hyp_attack.c, kexec_injector.c; `kexec_injector.ko` compilato il 14/09 16:27).

**Stato del dispositivo prima di qualunque modifica.** Da `runtime-evidence-20260711/properties.txt`: `[ro.product.model]=NX679J`, `[ro.board.platform]=taro` (SM8450), `[ro.boot.serialno]=3dbd****`, `[ro.build.display.id]=SKQ1.211113.001 test-keys`, `[ro.boot.flash.locked]=0`, `[ro.boot.verifiedbootstate]=orange`; A/B con slot `_a` attiva (device-facts). Al 16/09 il telefono non era collegato: l'handoff riporta "adb devices: no device; fastboot devices: no device; lsusb: no Qualcomm 9008, Android fastboot, or Android ADB device".

**Le sessioni del 19/09 non appartengono a questa fase**: sono successive all'avvio del 16/09. Quella delle 11:56 (`20260919_115631_581888`, "Portare internet SIM su OpenWrt") parte da `experiments/MODEM-HANDOFF.md`; quella delle 14:41 (`20260919_144105_3d0bbe`) e' una consultazione a domanda singola sul trasporto QMI (`/dev/rmnet_ctrl` vs `smdcntl8`) e non ha prodotto risposta nel dump.

### Riferimenti

- `sessions/request_dump_20260904_221805_4f6f50_20260904_221811_908760.json`; `state/state.db` (tabelle sessions/messages).
- `NX679J_OPENWRT_KERNEL_RE_HANDOFF.md` (mtime 16/09 10:30); `experiments/NX679J-OPENWRT-REPORT.md` (sez. 0 "Origine e metodo").
- `V311-download.log`; `extracted/`; `phase0-backups/2026-07-02-slotb-preproto/{SHA256SUMS,restore_slot_b.sh}`; `magisk_patched-30700_Zx2eF.img`.
- `port-work/{device-facts-*.md,ROOT-CAUSE-900e.md,phase4-minimal-payload.md,openwrt-phase1,openwrt-phase4-proto1-rootfsfirst}`; `stock-modules/`; `verified-v311/`; `verified-port/`.
- `runtime-evidence-20260711/{properties.txt,dmesg.txt,gpio.txt,regulator_summary.txt}`; `recovery-v311-magisk-20260711/`; `edl-recovery/recon-20260717-183330/{printgpt.txt,sha256.txt,dump.log}`; `bootloader-re/`; `hypervisor-bypass/`; `full_extracted_v311/` (38 voci al livello superiore); `re-nubia-disp/` (l'unica dir "re-nubia*" esistente).

### Da chiarire

- Nessun dump di sessione e nessuna riga in `state.db` prima del 04/09 22:13: il lavoro del 2 luglio-7 agosto non e' attribuibile a questo profilo Hermes ne' a uno strumento registrato; l'autore/strumento di quella fase non e' documentato nel tree.
- Contraddizione di date: `experiments/NX679J-OPENWRT-REPORT.md` afferma che "il progetto e' iniziato il 16 settembre 2026", ma gli artefatti nel tree partono dal 02/07/2026: le due affermazioni descrivono probabilmente fasi diverse (prima fase senza Hermes vs fase con Hermes) - non risolvibile con le fonti attuali.
- `hypervisor-bypass/.hermes/` (14/09 16:26) suggerisce un'esecuzione Hermes il 14/09, ma `state.db` non registra sessioni quel giorno: provenienza non determinata.
- Lo stato "prima di qualunque modifica" non ha uno snapshot raw antecedente al 02/07: le immagini in `extracted/` provengono dal pacchetto OTA scaricato, non da un readback del dispositivo; il primo readback presente nel tree e' l'EDL recon del 17/07.

---

## Fase 0a: la catena di boot e il secure boot

`NX679J` (SM8450/Waipio) ha una catena di boot Qualcomm standard con componente UEFI: `abl_a`/`abl_b` è l'applicazione UEFI **`LinuxLoader`** (il fastboot dichiara `kernel:uefi`), `xbl_a`/`xbl_b` sta su `sdb1`/`sdc1`, l'ABL su `sde10`/`sde38`. I due slot sono **simmetrici per firmware** (ABL, UEFI, XBL, vbmeta, dtbo identici A/B). Il secure boot è **fuso**: la catena XBL→ABL è firmata dall'OEM e la chiave privata non esiste sul device; la verifica AVB delle boot image, invece, viene **saltata** perché il device è `unlocked`/`orange`. Lo stato misurato dal telefono il 16/09 è il punto di partenza di tutto il resto.

### (a) Stato misurato: `orange` + `unlocked`, ma `secure:yes`

16/09 12:33, da Android con root Magisk, un solo comando (`request_dump_20260916_122610_968914_20260916_131315_306080.json`):

```sh
adb -s 0123456789ABCDEF shell 'id; getprop ro.product.model; getprop ro.boot.slot_suffix; getprop ro.boot.verifiedbootstate; getprop ro.boot.vbmeta.device_state; getprop sys.boot_completed; uname -a; su -c id'
```
```text
uid=2000(shell) ... context=u:r:shell:s0
NX679J
_a
orange
unlocked
1
Linux localhost 5.10.66-android12-9-00005-gf6e6376090be-ab8060604 #1 SMP PREEMPT Fri Jan 7 14:51:36 UTC 2022 aarch64
uid=0(root) gid=0(root) groups=0(root) context=u:r:magisk:s0
```

Fastboot, `fastboot getvar all` (23:40, `current-fastboot-getvar.txt`, 240 righe) — le righe che contano:

```text
(bootloader) unlocked:yes
(bootloader) secure:yes
(bootloader) slot-count:2
(bootloader) current-slot:b
(bootloader) slot-successful:a:yes          slot-successful:b:no
(bootloader) slot-unbootable:a:no           slot-unbootable:b:no
(bootloader) slot-retry-count:a:7           slot-retry-count:b:7
(bootloader) kernel:uefi
(bootloader) product:taro                  serialno:3dbd****
(bootloader) max-download-size:805306368
(bootloader) partition-size:vbmeta_a: 0x10000   partition-size:vbmeta_b: 0x10000
(bootloader) partition-size:vbmeta_system_a/b: 0x10000
(bootloader) erase-block-size: 0x1000      logical-block-size: 0x1000
```

La `grep` di lavoro in sessione — `fastboot getvar all 2>&1 | grep -iE '(avb|verity|lock|secure|vbmeta)'` — restituisce `unlocked:yes`, `secure:yes` e le sole partizioni `vbmeta*`: **non esistono getvar `avb*` né `verity*`**. `secure:yes` e `unlocked:yes` convivono perché descrivono cose diverse (catena firmware vs stato unlock): è la coppia che in sessione ha prodotto l'errore di lettura raccontato in (d).

### (b) Firmware: catena firmata, chiave privata assente

- ABL A/B **byte-identici**, `443d1956e836aea3fe30ac13113dae787d88223c1e24f875c6831f69d86acea3` (1.048.576 B); XBL A/B `685f0a75…`; UEFI A/B `d32139de…`; `vbmeta_a` = `vbmeta_b` = `cc0de687c935714a9b6de4bf3492391474e640d2e23f752f6c626f6413b07e22` (65.536 B); `dtbo_a` = `dtbo_b` = `9ee95463…`.
- Firma ABL: **ECDSA P-384/SHA-384**, DER 104 B a `0x291B0`, verificata sui byte `[0x29000,0x291B0)`; l'hash table corrisponde al codice reale ⇒ nessuna alterazione dopo la firma. Catena: leaf `O=SecTools` ← `CN=Generated Ztemt Attestation CA` ← root `CN=Generated Ztemt Root CA` (auto-firmata, verificata). Root hash `5ad5c780…`.
- Nel readback **non esiste materiale di chiave privata** (nessun marker PEM/PRIVATE KEY): solo firme, certificati e chiavi pubbliche. Le chiavi di test del `qtestsign` locale hanno root `qtestsign Root CA - NOT SECURE`, quindi non riproducono la catena Ztemt.
- I byte non-zero sono **byte-identici** all'estrazione dell'OTA ufficiale Nubia V311 (`payload.bin` con `FILE_HASH` coerente e firma RSA-2048 verificata contro `META-INF/com/android/otacert`, subject `O=nubia`) ⇒ **bootloader stock V311**, non una build modificata.

### (c) AVB: cosa c'è scritto nelle partizioni, e cosa non lo è

```sh
avbtool info_image --image .../current-readback/vbmeta_b.img
```
```text
Minimum libavb version:   1.0
Header Block:             256 bytes
Authentication Block:     576 bytes
Auxiliary Block:          5824 bytes
Public key (sha1):        f43904b57d85ac17dfbfa33e632c3cdf2875fa96
Algorithm:                SHA256_RSA4096
Rollback Index:           0
Flags:                    0
Release String:           'avbtool 1.2.0'
Descriptors:
    Chain Partition descriptor:  recovery       (Rollback Index Location 1, key 2597c218…)
    Chain Partition descriptor:  vbmeta_system  (Rollback Index Location 2, key cdbb7717…)
    Prop: com.android.build.boot.fingerprint -> 'nubia/NX679J-UN/NX679J-UN:12/SKQ1.211113.001/eng.nubia.20220308.204459:user/release-keys'
    Hash descriptor:
      Image Size:            50499584 bytes
      Hash Algorithm:        sha256
      Partition Name:        boot
      Salt:                  aecce9546a2bf506396deb64f4f81ffaec499ab1bd9a8fddb00c6594b1c55e85
      Digest:                f0abae29b198534160dc5c0123a28bf6ae392bb50ada970b4a5d041003407ece
      Flags:                 0
    Hashtree descriptor:  odm / vendor / vendor_dlkm (dm-verity 1, blocchi 4096, salt d790240e…)
```

```sh
avbtool info_image --image .../current-readback/boot_a.img
```
```text
===boot_a
Footer version:           1.0
Image size:               100663296 bytes
Original image size:      51322880 bytes
VBMeta offset:            51322880
VBMeta size:              896 bytes
Minimum libavb version:   1.0
Authentication Block:     0 bytes
Algorithm:                NONE
Hash descriptor: Image Size 50499584, Partition Name boot, Digest f0abae29b1985341…407ece
===boot_b
/usr/bin/avbtool: Given image does not look like a vbmeta image.
```

Tre fatti che insieme chiudono la questione:

1. Il digest dichiarato in `vbmeta` per `boot` (`f0abae29…`, `Image Size 50499584`) **non corrisponde** all'immagine che gira: `sha256(boot_a.img[0:50499584])` = `ec823514e7345cc9ab454a2bea21a836f26886889a34ce79a4a00cdc0a2fb8ea` (ricalcolato sul readback `current-readback/boot_a.img`, 100663296 B, hash pieno `0cd94d56…` = `magisk_patched-30700_Zx2eF.img`). La stessa digest `f0abae29…` è quella scritta nel footer AVB **dentro** `boot_a`, che descrive ancora l'immagine stock da 51322880 B: **i metadati AVB sul device si riferiscono al boot stock, non a Magisk né a OpenWrt**, e `boot_b` non ha alcun footer AVB valido.
2. La verifica si può permettere di fallire perché l'ABL ha un ramo esplicito per lo stato unlocked. Dalle stringhe dell'ABL estratte in sessione (`edl-recovery/abl-unpack/linuxloader_strings.txt`, 201 righe): righe 94-97 `Device critical unlocked: %a`, **`Device is unlocked, Skipping boot verification`**, `Device unlocked: %a`; righe 133-134 `ERROR: Device State %a, AvbSlotVerify returned %a`; riga 112 `ERROR: AvbSlotVerify slot data error: …`. libavb è quindi **presente, linkato ed eseguito** (per estrarre `vbmeta.device_state`, `vbmeta.size`, digest e popolare `androidboot.*`), ma in stato unlocked **non blocca il boot**.
3. Sul device questo si legge come `ro.boot.verifiedbootstate=orange` + `ro.boot.vbmeta.device_state=unlocked` (punto (a)), coerente con `androidboot.slot_suffix=_a` e `ro.boot.flash.locked=0`.

**Conseguenza operativa** (quella che ha reso possibile tutto il resto): nessuna scrittura su `vbmeta`/`vbmeta_system`, nessun `--disable-verity`, nessun `avbtool add_hash_footer` sono mai stati necessari; `secure:yes` protegge la catena **XBL→ABL**, non le boot image che scriviamo noi.

### (d) Il ramo sbagliato del 16/09, e la correzione

Vale la pena scriverlo perché è la trappola che `secure:yes` tese:

1. Dopo il primo fallimento di boot su slot B (~14,8 s e ritorno in fastboot) la sessione formula: «`secure:yes` → **AVB è ABILITATO** e richiede una signature chain valida; se `signature_size = 0` AVB rigetta l'immagine anche se unlocked». Su questa ipotesi costruisce `add-dummy-signature.sh` (4096 B di zeri in coda, «This is NOT a cryptographic signature») e porta `signature_size` da 0 a 4096 nel header v4.
2. Il test smentisce l'ipotesi: **stesso identico esito** (~14 s, ritorno in fastboot) con e senza la firma fittizia. Il timing non discrimina nulla.
3. La sessione corregge in due passi: prima osserva che `SECBOOT_FUSE=0` nel codice LinuxLoader e il ramo `case NO_AVB: return LoadImageNoAuthWrapper(Info);` sono coerenti con «AVB live risulta disabilitato»; poi ritratta il valore probatorio di `SECBOOT_FUSE` (è una **costante di compilazione**, non una lettura del fuse: lo stato runtime si ottiene via SCM `TZ_INFO_GET_SECURE_STATE`). Resta la spiegazione **verificata** del ramo unlocked nell'ABL (punto c.2).
4. Il handoff di fine sessione mette la riga corretta: **[H] «Accettazione di un'immagine non firmata su slot B da parte di ABL/AVB: mai testata»**. Il test di controllo preparato per separarlo — `control-test-slotb.sh`, flash di `magisk_patched-30700_Zx2eF.img` su `boot_b`, poi monitor — è rimasto **DISARMATO, mai eseguito**.
5. La causa reale dei fallimenti di boot trovata in sessione non è AVB: il ramdisk cerca i moduli con i nomi a underscore (`dwc3_msm.ko`) mentre i file si chiamano con i trattini (`dwc3-msm.ko`) ⇒ **25 `insmod` su 40 falliscono `ABSENT`**, fra cui `dwc3-msm` (nessun UDC, nessun gadget) e `ufs_qcom`.

### (e) Lo stato A/B sta nella GPT, non in `misc`

Letto da Android con root; attributi delle entry GPT (`sde`), raw:

```text
abl_a      0x107f000000000000     boot_a         0x007f000000000000     vendor_boot_a  0x007f000000000000
abl_b      0x107b000000000000     boot_b         0x003a000000000000     vendor_boot_b  0x007b000000000000
```

Decodifica con il layout che l'ABL usa davvero (bit 48-49 Priority, 50 Active, 51-53 Retry, 54 Success, 55 Unbootable): `_a` = Priority 3 / **Active 1** / Retry 7 / Success 1 / Unbootable 0; `boot_b` al 16/09 = Priority **2** / **Active 0** / Retry 7 / **Success 0** / Unbootable 0 (le altre `_b` = Priority 3 / Active 0 / Success 1). Coerente con `slot-successful:a: yes` / `:b: no` dei getvar. Dopo il boot Android riuscito da slot B del 17/09 `boot_b` risulta `0x007b…`, cioè **Success 1 e Priority 3**: la GPT è il registro dello stato di boot, ed è l'unico posto dove vive.

`misc` (1 MiB, `sda3`) contiene invece un BCB AOSP: i primi 32 byte sono `bootonce-bootloader\0…` (hash `6c93debe281b2edb75e7733391b65d8a0dd64e349eb9a229a58867a3956f341d`). Va considerato un confondente del prossimo riavvio; in sessione è stato azzerato con:

```sh
adb shell su -c 'dd if=/dev/zero of=/dev/block/by-name/misc bs=32 count=1 conv=notrunc'
```

### (f) EDL/Sahara: il guasto è a monte del firmware

```sh
adb reboot edl      # la risposta adb non torna (atteso): timeout 30 s
```
```text
Bus 006 Device 004: ID 05c6:9008 Qualcomm, Inc. Gobi Wireless Modem (QDL mode)
```

Il loader usato è `edl-recovery/prog_firehose_ddr.melf` (1.580.132 B, `396d20e7b883b8c2a677a519727eb3a48ff315e3ccb7c6c30878bf43dcabd1a8`) con `--memory=ufs`. L'esito, ripetuto con due loader diversi, con `qdl` e via libusb:

```text
Qualcomm Sahara / Firehose Client V3.62 (c) B.Kerler 2018-2025.
main - Mode detected: sahara
sahara - Protocol version: 3, Version supported: 1
main - Skipping Sahara, connecting to firehose directly...
firehose - TargetName=
firehose - MemoryName=ufs
firehose - [LIB]: bytearray(b'\x04\x00\x00\x00\x10\x00\x00\x00\r\x00\x00\x00\x01\x00\x00\x00')
firehose_client - [LIB]: Error: Couldn't detect partition: boot_b
Available partitions:
```

- All'enumerazione il device emette **4 copie** del pacchetto `04 00 00 00 | 10 00 00 00 | 0d 00 00 00 | 01 00 00 00` = `SAHARA_END_OF_IMAGE(len=16) image_id=13 status=1 (INVALID_CMD)`; contatore `HELLO packets 0` su 64 byte. **Non emette mai `HELLO_REQ`**, e risponde allo stesso pacchetto a *ogni* comando host (HELLO_RSP v2/v3, EXECUTE_REQ, SWITCH_MODE, CMD_READY, DONE_REQ, MEMORY_READ, `<nop/>` Firehose, stub DLOAD raw e HDLC-framed) ⇒ **0 byte serviti**: nessuna immagine è mai stata trasferita al device.
- Due trappole degli strumenti, entrambe documentate in sessione: `edlclient` 3.62 stampa `firehose - Nop succeeded.` mentre l'unica risposta reale è il pacchetto `END_OF_IMAGE` (`getstatus()` ritorna True per XML senza attributo `value`), e **nessun client stock invia `DONE` (0x05) dopo `END_OF_IMAGE` (0x04)**, il passo che farebbe saltare il device al programmer.
- Il guasto è nello stadio **PBL/boot-ROM** (si presenta anche con ingresso EDL hardware a telefono spento, quindi prima di XBL/ABL: masterizzare `xbl`/`abl` non può ripararlo). La via di rientro del progetto resta **slot A**.

### (g) Nessuna telemetria del bootloader: pstore, rawdump, logdump

`/sys/fs/pstore` è **vuoto** (`runtime-v2/pstore-list.txt` contiene solo `.` e `..`), e `rawdump` (256 MiB, `sda11`) e `logdump` (512 MiB) sono **interamente a zero** (`tr -d '\0' | wc -c` → 0). Il kernel stock ha però `CONFIG_PSTORE=y`, `PSTORE_RAM/CONSOLE/PMSG=y`: il canale esiste, semplicemente non c'era nulla da raccogliere. Da qui la regola di metodo ribadita nel handoff: **il ritorno in fastboot, da solo, non diagnostica né watchdog né AVB né panic**.

### Riferimenti

- Dump di sessione: `sessions/request_dump_20260916_122610_968914_*.json` (14 file dal 16/09 13:13 al 17/09 01:59) e `request_dump_20260904_221805_4f6f50_20260904_221811_908760.json`.
- `nx679j-stock/current-fastboot-getvar.txt` (240 righe, `getvar all` `current-slot:b`); `experiments/20260916-122926-native-baseline/{BASELINE.md,OFFLINE_BOOT_ANALYSIS.md,HANDOFF-20260917.md}`; `current-readback/{manifest.tsv,boot_a.img,boot_b.img,vbmeta_a.img,vbmeta_b.img,misc.img}`; `runtime-v2/{gpt.json,pstore-list.txt}`.
- `nx679j-stock/authoritative-bootchain-verification.md`; `nx679j-stock/nx679j-openwrt-clean/{KNOWN_FACTS.md,KEY_AND_BYPASS_ANALYSIS.md}`; `nx679j-stock/edl-recovery/abl-unpack/linuxloader_strings.txt`; `nx679j-stock/experiments/20260917-boot-chain/XBL-ABL-DECISIONS.md`.
- Strumenti: `avbtool` 1.2.0 / libavb (`avb_slot_verify.c`, `avb_vbmeta_image.c`); `fastboot` 37.0.0; `edl` (bkerler) 3.62 in `/home/user/venvs/edk2/bin/edl`; `qdl` 7c5a28d; `prog_firehose_ddr.melf`; AOSP `bootimg.h` (header v4); OTA ufficiale Nubia V311 (`payload.bin`, `payload_properties.txt`, `META-INF/com/android/otacert`); `qtestsign`.

---

## Fase 0b: EDL/QDL, la modalita' buggata

L'EDL (Emergency Download, USB `05c6:9008`, protocollo Sahara → Firehose) e' la via di servizio Qualcomm: non dipende da fastboot, non dipende da Android, ed e' il solo percorso che nella prima fase del progetto ha scritto partizioni con verifica. Sulla stessa unità (seriale `4A09****`, NX679J/SM8450) **il 17/07/2026 l'EDL funzionava**; nella notte fra il 16 e il 17/09/2026 la stessa modalità entra in `9008` ma la fase boot-ROM risponde in modo degenere: nessun `HELLO_REQ`, **zero byte di loader trasferiti**, e a ogni comando di ogni client (`edl` di bkerler, `qdl`, `qdl-rs`, un client Sahara minimale scritto a mano) il device restituisce sempre lo stesso pacchetto da 16 byte. La conclusione operativa della sessione è che **questa modalità, così come si presenta sull'unità, è inutilizzabile come rete di sicurezza**.

### 0b.1 Baseline: il 17/07/2026 l'EDL/ Firehose funzionava

- Client e loader: `edl` di B.Kerler **V3.62** (`venvs/edk2/bin/edl`, copia storica in `edl-recovery/edl/`) + `edl-recovery/prog_firehose_ddr.melf` (1.580.132 B, sha256 `396d20e7b883b8c2a677a519727eb3a48ff315e3ccb7c6c30878bf43dcabd1a8`), invocato con `--loader=… --memory=ufs`.
- Readback completo in `edl-recovery/recon-20260717-183330/` (mtime 17/07 18.33-18.34): `boot_a/b`, `vendor_boot_a/b`, `dtbo_a/b`, `vbmeta_a/b`, `misc`, `xbl_a/b`, `xbl_config_a/b`, `abl_a/b`, `logdump.bin` (512 MiB), `logfs.bin`, più `printgpt.txt` (159 righe, 32 partizioni `Active True` e 72 `False`) e `sha256.txt` (17 righe). Nel log: `main - Mode detected: firehose`, poi `Dumped sector 374511 with sector count 24576 as …/boot_b.bin` con progresso a ~250-290 MB/s.
- Scritture verificate: `edl-recovery/edl_restore_*.sh` usano la forma `edl w <partizione> <file> --loader=… --memory=ufs` (GPT per LUN 1/2/4, catena XBL/ABL, `uefi`, `hyp`, `tz`, `vbmeta*`, `misc`, `setactiveslot a`); gli esiti sono nei log `restore-stock-a/edl-*-2026071*.log` (98 KB, 70 KB, 128 KB, 181 KB) con `RC=0`, e `recon-20260717-183330/analysis/REPORT.md` chiude con *"Magisk restored to boot_a — Readback SHA256 match: OK"*.
- **Conseguenza di metodo:** «EDL non funziona su questa unità» non è una proprietà dell'hardware, è **uno stato**, datato 16-17/09. Ogni affermazione sulla modalità va datata.

### 0b.2 Il segnale della modalità buggata: 4 copie di `END_OF_IMAGE`, zero `HELLO`

- All'enumerazione `9008` il device emette **4 copie** del pacchetto da 16 byte `04 00 00 00 10 00 00 00 0d 00 00 00 01 00 00 00`, cioè `SAHARA_END_OF_IMAGE(len=16) image_id=13 status=1`. Prova: `recon-20260917-001752/catch-hello.log` → `RX 64 04000000…0400` e riga finale `total bytes 64, HELLO packets 0`.
- **Il device non emette mai `HELLO_REQ`** (contatore `HELLO packets 0` su 64 byte ricevuti in 78 s). Non è quindi un problema di risposta host: la sequenza Sahara non parte proprio.
- La cattura è su **porta seriale** (`/dev/ttyUSB0`, creata da `qcserial`) e su **USB raw** (`sahara-libusb.log`, `EP_IN=0x81 EP_OUT=0x01` + `CLEAR_FEATURE(HALT)` su entrambi): il risultato non cambia.
- Lo stato non dipende da input: `RESET_REQ` (0x07 → `RESET_RSP` + ri-enumerazione, `sahara-recover.log`), cold boot, ingresso EDL da tasti fisici (quindi *prima* di XBL/ABL) e pulizia del BCB (`misc`, `sahara-after-bcb-clear.log`) danno lo stesso pacchetto.

### 0b.3 Cosa è stato tentato, comando per comando

| comando host inviato | risposta del device | log |
|---|---|---|
| `HELLO_RSP` v2 e v3, mode 0 (`IMAGE_TX_PENDING`), 2 (`MEMORY_DEBUG`), 3 (`COMMAND`) | stessa `END_OF_IMAGE` image=13 status=1 | `sahara-poke.log`, `memory-debug-probe.log`, `sahara-blindhello-mode0.log` |
| `EXECUTE_REQ` cmd 0x01 (`serial_num`), 0x02 (`msm_hwid`), 0x03 (`oem_pkhash`), 0x0C (`reset-state-machine`) | idem | `sahara-poke.log`, `memory-debug-probe.log` |
| `SWITCH_MODE` 0x0C (→ `IMAGE_TX_PENDING` e → `MEMORY_DEBUG`), `CMD_READY` 0x0B | idem | `sahara-poke.log` |
| `MEMORY_DEBUG` 0x09, `MEMORY_READ` 0x0A `addr=0x100000 len=0x40` | idem | `memory-debug-probe.log` |
| `DONE_REQ` 0x05 (ripetuto 13 volte) | idem, in ciclo | `sahara-after-bcb-clear.log`, `sahara-pblhack2.log` |
| Firehose XML `<data><nop /></data>` (42 B) | idem | `sahara-after-bcb-clear.log` riga 19 |
| stub DLOAD `11001200a0e30000c1e50140a0e31eff2fe1`, **raw** e **HDLC-framed** (escape + CRC16 + `0x7E`) | idem | `streaming-frame-probe.log` righe 3-9 |
| terze parti: `qdl` (`chipinfo`, `read`, `nop`), `qdl-rs` `nop` | `device rejected command mode (end-of-image status 1)`, `received non-successful end-of-image result`, `timeout waiting for read` | `qdl-chipinfo.log`, `qdl-read-gpt-lun0.log`, `qdl-rs-nop.log` |
| `edl printgpt` / `w boot_b` / `r boot_a` / `rl` / `rf` / `rs` | `Error: Couldn't detect partition …`, `Available partitions:` **vuoto**, timeout | sessione 16/09 20:33-22:13 |

- In **tutte** le acquisizioni tranne una il device non emette mai `READ_DATA`/`READ_DATA64`: **non ha mai chiesto un byte di loader**, quindi nessuna immagine è stata trasferita.
- **Unica eccezione, da tenere a mente:** `sahara-blindhello-mode0.log` riga 4, t=3,005 s, prima esecuzione con `HELLO_RSP` cieco → `RX 32 B cmd=0x12 READ_DATA64 len=32 payload=0d00********************************************`; il tool lo interpreta come `image_id=13, offset=0, length=0` e risponde scrivendo 0 byte (`TX DATA image=13 off=0x0 len=0x0 (total served 0)`), poi il device torna muto. Non riprodotto mai più.

### 0b.4 Gli errori, citati letteralmente

```text
main - Mode detected: sahara            # edl reset --resetmode=poweroff, 16/09 21:xx
[Command timed out after 15s]           # il device resta in 9008
```
```text
Error: Couldn't detect partition: misc
Available partitions:                   # lista vuota, con GPT valido in backup
```
```text
Reading from physical partition 0, sector 0, sectors 6
Done |----------|   0.0% Read (Sector 0x0 of 0x6) 0.00 MB/s     # edl rs 0 6 --lun=0, timeout 45 s
```
```text
Talking to device (PID 0x9008, serial: 4A09****)
device rejected command mode (end-of-image status 1)             # qdl chipinfo
received non-successful end-of-image result                      # qdl read 0/0+6
qdl-rs 0.1.0
Error: timeout waiting for read                                  # qdl-rs nop
```
```text
[ 120.120] served=0 bytes handshake=False done_sent=False        # sahara-done-test.log
[   0.74] reopened /dev/ttyUSB0
[listen] RX 64 04***************d00000001000000…                 # sahara-recover.log
```
```text
[    0.018] CLEAR_FEATURE(HALT) ep=0x01 ok                       # sahara-libusb.log
[    3.018] waiting for device …                                 # poi silenzio per 57 s
```
```text
write error: write failed: [Errno 5] Input/output error          # singolo 0x7E (EOP)
serial.serialutil.SerialException: Could not configure port: (5, 'Input/output error')
ValueError: The device has no langid (permission issue, no string descriptors supported
            or device error)                                     # edl peek, 10/07, modo 900e
```

### 0b.5 Le trappole dei client: i falsi successi (la parte più costosa)

1. **`edlclient getstatus()` ritorna `True` per risposte XML senza attributo `value`.** Per questo `edl-nop-corrected.log` stampa `firehose - Nop succeeded.` mentre l'unica risposta reale era il pacchetto `END_OF_IMAGE`; e `gpt-emergency-20260916-193410/edl-read-gpt-raw.log` stampa `Done |----| 0.0% Read (Sector 0x0 of 0x6)` mentre `gpt-lun0-current.bin` è **0 byte**. `FIREHOSE_TRANSPORT_CORRECTION.md` fissa la conseguenza: `nop`, `configure`, `getstorageinfo` **non sono prove** che Firehose sia in esecuzione.
2. **`edl` salta la fase Sahara**: nel log compare `Skipping Sahara` e il tool dichiara comunque `Mode detected: firehose`. Il loader non è mai stato caricato via `sahara.upload_loader()`.
3. **Nessun client stock invia `DONE` (0x05) dopo `END_OF_IMAGE` (0x04)** — è il passo documentato che fa saltare il device al programmer già caricato, e non è mai stato eseguito da un client standard in questa sessione.
4. **`qcserial` rivendica l'interfaccia `9008` e blocca libusb**; `modprobe -r qcserial usb_wwan` viene annullato dall'autoload udev alla successiva enumerazione (serve `install <mod> /bin/true` in `/etc/modprobe.d`). E l'endpoint bulk stallato fa fallire ogni write con `EIO`, uccidendo l'handle mentre il device resta enumerato: `CLEAR_FEATURE(HALT)` è obbligatorio.
5. **Falso positivo opposto, da non citare mai come prova:** `sahara-recover.log` riga `[12.75] HELLO seen: True` mentre `catch-hello.log` conta `HELLO packets 0` — il tool contava `END_OF_IMAGE` come `HELLO`.
6. **Le cartelle di backup della notte sono vuote**: `edl-backup-20260916-162212/` e `edl-backup-20260916-162306/` hanno 0 voci. Non esiste, per quella fase, nessun backup completo via EDL.
7. **`edl reset` non sblocca il device**: sia `reset --resetmode=poweroff` sia `reset` senza argomenti vanno in timeout (15 s e 10 s) e l'unità resta in `9008`; l'unica uscita è il **power-off fisico**.

### 0b.6 Conclusione: dove vive il guasto

- **Ipotesi consolidata (confidenza alta nella sessione):** il guasto vive nello **stadio PBL / boot-ROM**. Se ne deduce che **masterizzare `xbl`/`abl` non può ripararlo**: quelle immagini non vengono mai raggiunte, perché il boot-ROM non arriva a chiedere dati.
- **Conseguenza operativa immediata:** EDL è **fuori dal progetto** come piano di recupero. Non è stata trovata alcuna combinazione di client, loader, trasporto o framing che produca un trasferimento.
- **I loader alternativi non cambiano nulla, ed è dimostrato staticamente:** il loader Oppo `prog_firehose_sm8450_v15_oppo.melf` (sha256 `74bf8c08…`) e il loader Nubia `prog_firehose_ddr.melf` (`396d20e7…`) hanno **gli stessi due segmenti LOAD** (`0x2211c000`/`0x16000` → sha256 `2b7358ef…`; `0x22143000`/`0x280` → `6ef798c2…`), stesso entrypoint `0x2211c000`, stessa struttura `SvcImageAuthentication` (ECDSA P-521/SHA-512, stesso hash array e `QTI_ENTITLEMENT_ROOT_KID`); divergono solo nel materiale di configurazione/certificati successivo. Sul device si stava quindi già eseguendo lo stesso core Firehose (`loader-comparison/static-ufs/REPORT.md`).
- **Limite dichiarato:** la conclusione è un'inferenza, non un'osservazione diretta — e convive con il fatto che lo **stesso PBL porta a termine il boot normale** (Android slot A parte). Il guasto sembra quindi specifico del **percorso di ingresso in dload/EDL**, non del boot-ROM in sé (vedi *Da chiarire*).

### 0b.7 Alternative effettivamente usate al posto di EDL

1. **Fastboot** per flash, `set_active`, `getvar`: unico percorso di scrittura affidabile in quella fase (`fastboot flash boot_b`, `fastboot set_active b`, poi ritorno con `fastboot set_active a`).
2. **ADB root + `dd` diretto sui block device** come alternativa al flash: `adb shell su -c 'dd if=/data/local/tmp/… of=/dev/block/by-name/boot_b bs=1M && sync'` (in `experiments/20260916-122926-native-baseline/control-test-slotb.sh`, `test-v6-slotb.sh`); readback con `exec-out su -c 'dd if=/dev/block/by-name/<part> bs=4194304'`.
3. **Power-off fisico come reset**: l'unico modo di uscire dal loop `9008` quando `edl reset` non risponde.
4. **Client Sahara/QDL alternativi, per capire e non per flashare**: clone upstream di `edl`, `qdl` (`/usr/local/bin/qdl`, `--storage=ufs`, `--skip-reset`), `qdl-rs`, più quattro sonde scritte a mano in `tools/probe/` (`catch_hello.py`, `sahara_client.py`, `sahara_poke.py`, `sahara_recover.py`, `memory_debug_probe.py`, `streaming_frame_probe.py`, `usb_tap.py`, `ser_tap.py`). Tutti concordano sullo stesso pacchetto.
5. **Analisi statica dei loader** (segmenti, certificati, stringhe UFS/TME) al posto dei test sul campo, per non ripetere la "flash roulette".
6. **Loader di terzi solo offline**: `external-loaders/xbl_s_devprg_ns-odin2.melf` (SM8550, guida Renegade/AYN, sha256 `c522842d…`) e `external-loaders/prog_sdm845_firehose_ddr.elf` (`6a04d4c3…`, OneLabsTools) — scaricati per confronto, **mai inviati al device**.

### 0b.8 Conseguenze per il recupero

- **La recovery è lo slot A (Android + Magisk 30.7), non l'EDL.** Verificato il 16/09: dopo il power-off il device torna in Android su `_a`, con ADB e root (`recon-20260917-001752/android-before-edl.txt`: `NX679J`, `SM8450`, `_a`, `orange`, `unlocked`, `uid=0(root)`).
- **Ogni scrittura successiva va verificata con un readback + sha256**, perché non esiste più la rete di sicurezza del readback via EDL. Le immagini note buone restano `magisk_patched-30700_Zx2eF.img` (sha256 `0cd94d56…`) e `recovery-v311-magisk-20260711/boot_a-magisk-30700.img`; il riferimento di geometria delle partizioni resta `recon-20260717-183330/printgpt.txt`, l'ultimo GPT letto **dal device**.
- **Non usare la scrittura di `gpt`/`xbl`/`abl` come tentativo di riparare l'EDL**: il guasto è a monte di quelle immagini, e un GPT ricostruito senza readback fresco (es. `patch-gpt-active-bit.py`) è un modo per perdere l'unico riferimento di geometria.
- **Il trigger dell'ingresso in EDL è il BCB `bootonce-bootloader` in `misc`**, e in quella fase ha prodotto un **loop EDL persistente** che nessun comando EDL interrompe: da rimuovere da Android appena ADB è disponibile, mai "alla cieca" da fastboot.
- **Il ciclo di prova va quindi progettato attorno a fastboot + slot A**: un test di boot è accettabile solo se esiste un ritorno verificato (`fastboot set_active a`) e se la partizione di partenza è stata prima salvata con hash (come fa `control-test-slotb.sh`).
- **Limite di tracciabilità:** non esiste, nel tree, un backup EDL completo della notte 16-17/09 (cartelle vuote) e non è databile con certezza il passaggio `current-slot:b` → `current-slot:a`; le verifiche pre-scrittura vanno quindi rifatte dal vivo, non ricavate dai log.

### Da chiarire

- La lettura di `status=1` è duplice e mai risolta: la sessione usa sia `INVALID_CMD` (dai probe scritti a mano) sia `SAHARA_END_IMAGE_TX status 34/48` (dal report CVE): il campo non è stato confrontato con l'enumerazione upstream e la semantica esatta di `image_id=13` non è stabilita.
- Non è dimostrabile, per il 16-17/09, che **nessuna** scrittura EDL sia atterrata: le scritture `w boot_b` / `ws 374511` di quella notte (`100%` poi errore finale) non hanno mai avuto un readback, e il codice del client non è affidabile sulle conferme. Le scritture verificate di quella fase passano da ADB (`dd … of=/dev/block/by-name/…`).
- Il guasto è attribuito al PBL/boot-ROM, ma **lo stesso boot-ROM porta a termine il boot Android**: non è chiaro se si tratti dello stato-macchina Sahara, del percorso di ingresso in dload (BCB/`qcom-dload-mode`), o di un artefatto del trasporto (`qcserial` su `ttyUSB0` invece di USB raw: le catture "pulite" sono quasi tutte su `ttyUSB0`).
- La riga `READ_DATA64 … payload=0d000000…40**************` in `sahara-blindhello-mode0.log` resta non spiegata: richiesta degenere (`length=0`) o pacchetto concatenato? Entrambe le letture sono compatibili con le prove.
- La provenienza del materiale non è uniforme: il repo `nx679j-openwrt-clean/research/cve-2026-25262-sm8450/` è il clone di una ricerca condotta su **POCO F4 GT** (SM8450); i log in `recon-20260917-001752/` sono dell'NX679J (`PID 0x9008, serial: 4A09****`), mentre `evidence/pbl_status_*.md`, `pbl_status_en.md` e i loader di terzi sono materiale di quella ricerca, non misure su questo telefono.
- Non è stabilito se la modalità `05c6:900e` (luglio/agosto: crash del payload UEFI, `port-work/ROOT-CAUSE-900e.md`) e la modalità `9008` degenere di settembre siano **lo stesso stato** o due condizioni diverse con lo stesso esito pratico.

### Riferimenti

- `nx679j-stock/nx679j-openwrt-clean/research/cve-2026-25262-sm8450/recon-20260917-001752/`: `catch-hello.log`, `catch-hello-coldboot.log`, `sahara-recover.log`, `sahara-poke.log`, `sahara-pblhack.log`, `sahara-pblhack2.log`, `sahara-done-test.log`, `sahara-blindhello-mode0.log`, `sahara-fresh-pbl.log`, `sahara-after-bcb-clear.log`, `sahara-libusb.log`, `memory-debug-probe.log`, `streaming-frame-probe.log`, `edl-nop-corrected.log`, `edl-nop.log`, `edl-nop-oppo-loader.log`, `edl-getstorageinfo-debug.log`, `edl-getstorageinfo-oppo-loader.log`, `edl-getstorageinfo-skipstorageinit.log`, `edl-rawxml-{nop,configure-safe,configure-normal-readonly,getstorageinfo}.log`, `edl-rs-gpt-lun0.log`, `edl-info.log`, `qdl-chipinfo.log`, `qdl-read-gpt-lun0.log`, `qdl-rs-nop.log`, `android-before-edl.txt`, `our-loader.json`, `local-programmer-inventory.txt`, `programmer-elf-inventory.json`.
- `nx679j-stock/nx679j-openwrt-clean/research/cve-2026-25262-sm8450/`: `README.md`, `FIREHOSE_TRANSPORT_CORRECTION.md`, `firehose_artifacts.txt`, `firehose-analysis/{edl.py,firehose_client.py}.context.txt`, `tools/{README_en.md,probe/*,edl-upstream/,qdlrs/}`, `external-loaders/`, `loader-comparison/{diff-nubia-oppo.txt,nubia.json,oppo.json,odin2.json,static-ufs/REPORT.md}`, `evidence/pbl_status_en.md`.
- `nx679j-stock/edl-recovery/`: `recon-20260717-183330/{dump.log,printgpt.txt,sha256.txt,boot_a.bin,boot_b.bin,analysis/REPORT.md,analysis/misc-clear-bcb-2k.bin}`, `restore-stock-a/{edl-baseline-restore-20260717-203130.log,edl-gpt-restore.log,edl-restore.log,edl-fullchain-slotmatch-20260718-122011.log,edl-v311-package-20260718-123811.log}`, `edl_restore_{baseline_a,gpt_and_firmware,fullchain_slotmatch,v311_package,xbl_abl_a}.sh`, `prog_firehose_ddr.melf`, `prog_firehose_sm8450_v15_oppo.melf`, `sahara_handshake.py`.
- `nx679j-stock/gpt-emergency-20260916-193410/{edl-read-gpt-raw.log,gpt-lun0-current.bin}` (0 byte); `nx679j-stock/edl-backup-20260916-162212/` e `-162306/` (vuote).
- Sessione 16-17/09/2026 (42 dump in `profiles/kernel-re/sessions/`), in particolare `request_dump_20260916_122610_968914_20260916_203343_295548.json`, `…_213120_229859.json`, `…_220554_188902.json`, `…_20260917_012703_505522.json`, `…_20260917_015951_507084.json`; handoff ricostruito in `request_dump_20260919_115631_581888_20260920_141814_623113.json` (sez. 2 «EDL è rotto nello stadio boot-ROM», sez. 3 «Trappole degli strumenti»).
- `nx679j-stock/NX679J_OPENWRT_KERNEL_RE_HANDOFF.md` (sez. «EDL and Firehose status»); `nx679j-stock/port-work/ROOT-CAUSE-900e.md`; `nx679j-stock/port-work/ramdump-900e-minimal/{run.log,run2.log,peek-pstore.log,peek-pstore-sudo.log,ocimem-try.log}`; `nx679j-stock/experiments/20260916-122926-native-baseline/{control-test-slotb.sh,test-v6-slotb.sh,readback-current-bootchain.sh}`; `nx679j-stock/edl/qdl` (client di confronto).

---

## Fase 0c: dentro la catena - XBL, ABL, UEFI

**Cosa stabilisce questa fase.** Chi contiene davvero il codice che legge le immagini di boot, in che
forma è quel codice, e quali decisioni prende. Il risultato ribalta la lettura della prima passata
(agosto): l'`abl_a` non è "un ELF senza stringhe", è un **PE32+ AArch64 (applicazione UEFI `LinuxLoader`)
compresso LZMA dentro una firmware volume dentro un contenitore ELF32**; `xbl_*` e `uefi_*` non leggono
le immagini di boot; e il parametro `kvm-arm.mode=protected` che era stato attribuito all'ABL viene in
realtà dalla `CONFIG_CMDLINE` del kernel.

**Metodo.** Solo letture: `dd` in sola lettura dal device, `sha256sum` (host e device), `strings`,
`readelf`, `aarch64-linux-gnu-objdump`, `dtc`, Python (parser FV/FFS/sezioni, decompressione LZMA e
gzip, xref ADRP+ADD), `qc_inspect.py` (ispettore MBN/ELF di Littlenine, in
`port-work/qc-signature-inspector/`). Nessuna scrittura su partizioni, nessun flash, nessun `set_active`,
nessun reboot deliberato.

**Legenda.** **[VERIFICATO]** = misurato/disassemblato o riprodotto in questa ricostruzione;
**[IPOTESI]** = inferenza coerente con le evidenze ma non dimostrata riga per riga;
**[NON DETERMINATO]** = non risolto. Le offset sono sempre **offset nel payload decompresso**, non
indirizzi di RAM (il PE ha `ImageBase=0` e viene rilocato dal caricatore di FV).

---

### 0c.1 Chi vive dove: la mappa reale delle partizioni

| partizione | device node | byte | sha256 (readback diretto) |
|---|---|---|---|
| `xbl_a` | `/dev/block/sdb1` | 3.670.016 | `685f0a75fbff851bb6ef55ba158b7f86ea14a44c527c5388605ece94f47d637a` |
| `xbl_b` | `/dev/block/sdc1` | 3.670.016 | **identico a `xbl_a`** |
| `abl_a` | `/dev/block/sde10` | 1.048.576 | `443d1956e836aea3fe30ac13113dae787d88223c1e24f875c6831f69d86acea3` |
| `abl_b` | `/dev/block/sde38` | 1.048.576 | **identico a `abl_a`** |
| `uefi_a` | `/dev/block/sde1` | 5.242.880 | `d32139de305aeb2aba1dc8c12b4996c49b09a09a6032307bf100ecb1ecb2d312` |
| `uefi_b` | `/dev/block/sde29` | 5.242.880 | **identico a `uefi_a`** |
| `uefisecapp_a` / `_b` | `sde18` / `sde46` | — | partizioni dedicate esistenti ([VERIFICATO] nella by-name, non analizzate) |
| `imagefv_a` | `sde19` | 2.097.152 | (nessuna stringa di boot) |
| `dtbo_a` / `dtbo_b` | `sde17` / `sde45` | 25.165.824 | `9ee95463abce517d140eae3c5c892c2b38fd1344d6fb0a6e2da2af76a86de6a1` |
| `vbmeta_a` / `vbmeta_b` | `sde16` / `sde44` | 65.536 | `cc0de687c935714a9b6de4bf3492391474e640d2e23f752f6c626f6413b07e22` |

Comandi (riproducibili, sola lettura):

```
adb -s 0123456789ABCDEF exec-out "su -c 'dd if=/dev/block/by-name/abl_a bs=1M 2>/dev/null'" > abl_a.img
adb -s 0123456789ABCDEF shell "su -c 'sha256sum /dev/block/by-name/xbl_a /dev/block/by-name/xbl_b'"
ls -l /dev/block/by-name | grep -E 'xbl|abl|uefi|dtbo|vbmeta'
```

Nota di provenienza **[VERIFICATO]**: `bootloader-re/abl_a.img` (sha256 `443d1956…`) è la stessa
immagine letta ora dal device; il file storico `edl-recovery/recon-20260717-183330/abl_a.bin`
(sha256 `913e7242…`) **non** lo è. I nomi dei file vecchi non sono etichette di slot affidabili:
valgono gli hash e il readback diretto.

`xbl_a`/`xbl_b` **sono la stessa immagine**: tre misure indipendenti (due letture locali con `bs`
diverso e ordine invertito, più `sha256sum` eseguito *sul telefono*). L'osservazione precedente
("`xbl_a != xbl_b`") **non è riproducibile**. Vale lo stesso per `abl_a==abl_b`, `uefi_a==uefi_b`,
`dtbo_a==dtbo_b`, `vbmeta_a==vbmeta_b`.

---

### 0c.2 XBL (`sdb1`/`sdc1`): un contenitore di verifica, non di boot

Struttura misurata: contenitore **ELF32** con **tre ELF annidati**, a `0x0`, `0x162f4`, `0x439f4`
([VERIFICATO] scansione della magia `7f 45 4c 46` su tutta la partizione).

| immagine | offset | classe / machine | entry | contenuto |
|---|---|---|---|---|
| ELF#1 | `0x0` | ELF32, `e_machine=0x1` | `0x2211c000` | struttura di hash/certificati + stringhe di provisioning: `CHIP_PROD_PROV_K_LBL` @`0x14008`, `ENTITY_DEV_FACT_PROV_K_LBL` @`0x14094`, `SEQ_FW_BUILD_TYPE_STRING` @`0x1469c` |
| ELF#2 | `0x162f4` | ELF32, `e_machine=0xf3` | `0x20412800` | 186.112 B (span `0x439f4-0x162f4`); nessuna stringa di codice, non decode come AArch64; **contiene i subject della catena OEM**: `Generated Ztemt Attestation CA` @`0x423bf`, `Generated Ztemt Root CA` @`0x425e9`, `General Use Ztemt Key` @`0x42627` |
| ELF#3 | `0x439f4` | ELF64, AArch64 | `0x14819fa8` | codice **SBL1**: `SBL1, End` @`0xcc9f4`, `sbl1_mc.c` @`0xcca8e`, `CDT not found in any storage media` @`0xcca0a`, `xbl_sc` @`0xcd02d` |

**Cosa non c'è** **[VERIFICATO]**: in *tutta* la partizione, 0 occorrenze di `ANDROID!`, `VNDRBOOT`,
`LinuxLoader`, `UEFI`; `strings -n 6` sulla partizione dà **1.786** stringhe, fra cui
`SEQ_FW_BUILD_TYPE_STRING=RELEASE`, `SEQ_FW_RELEASE_BUILD_VERSION_STRING=r79` e i subject Ztemt con
validità `211009012008Z` / `411004012008Z`. Nessuna magia di compressione reale: l'unico match LZMA
(`5d 00 00 94` a `0x60748`) è un falso positivo — sono i byte dell'istruzione `bl`. XBL **non è il
componente che legge le immagini di boot**.

**Controprova sul device** **[VERIFICATO]** (`re/sigscan.sh`, `grep -abo` su ogni partizione by-name
non-LUN ≤ 300 MB): le uniche occorrenze non compresse delle due magie sono `boot_a`/`boot_b`
(`ANDROID!` a offset 0) e `vendor_boot_a`/`vendor_boot_b` (`VNDRBOOT` a offset 0). Le copie "di codice"
stanno **solo** nell'ABL, e lì sono compresse: questo è il motivo per cui la prima sessione aveva
giudicato l'ABL "senza stringhe di boot image".

Una nota di disaccordo fra fonti **[IPOTESI/NON DETERMINATO]**: l'analisi del 17/09 attribuisce i
subject `Generated Ztemt Attestation CA` e `General Use Ztemt Key` all'ELF#1; la misura di oggi li
colloca a `0x423bf`/`0x42627`, cioè nel **campo dell'ELF#2**. Non cambia la conclusione (XBL porta una
catena OEM), cambia a quale immagine annidata appartenga il blob.

---

### 0c.3 UEFI (`sde1`/`sde29`): la firmware volume EDK2 e la sua configurazione

**Contenitore** **[VERIFICATO]**, primi 24 byte di `uefi_a`:

```
7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00 02 00 28 00 01 00 00 00
        ^^ class = ELF64                       ^^^^^ e_machine = 0x0028
```

ELF64 con `e_machine=0x0028` (marcatore Qualcomm, non `0xB7`): una stranezza del contenitore, non un
errore di lettura. Dentro, **una sola firmware volume**:

| campo | valore |
|---|---|
| inizio FV | `0x1000` |
| firma `_FVH` | `0x1028` (= inizio FV + 0x28, layout PI) |
| `FvLength` | `0x2A0000` = **2.752.512 B** |

**Prova di identità**: la FV dentro `uefi_a` live (byte `0x1000…0x1000+0x2A0000`) ha sha256
`b94fe5a210e54724f971246ff49f92259714a75c56437f6a0ccac425666be37d` ed è **byte-identica** alla FV
estratta dall'artefatto OTA di luglio in `analysis/uefiextract/uefi_fv_offset0x1000.bin`. Questo rende
applicabile al firmware **live** l'inventario `uefiextract` di quella FV **[VERIFICATO]**:

- 1 **SEC core** (con TE image) + **DxeCore** (`d6a2cb7f-6a18-4e2f-b43b-9920a733700a`) → stack EDK2 completo;
- **88 DXE driver** e **90 sezioni PE32**, fra cui `QcomBds`, `PartitionDxe`, `FvSimpleFileSystem`,
  `DiskIoDxe`, `DisplayDxe`, `FontDxe`, `GraphicsConsoleDxe`, `QcomChargerDxeLA`, `ScmDxeLA`,
  `PILDxe`/`PILProxyDxe`, `ASN1X509Dxe`, `EnglishDxe`, `HALIOMMU`, `RngDxe`;
- `uefiplat.cfg` (`DDE58710-41CD-4306-DBFB-3FA90BB1D2DD`) come file Freeform a `0x47000`.

**La configurazione di piattaforma** (testo in chiaro dentro la FV, a `0x49226` = 299.558)
**[VERIFICATO]**, estratti:

```
[ConfigParameters]
ConfigParameterCount = 64
PlatConfigFileName = "uefiplatLA.cfg"
OsTypeString = "LA"
EnableShell = 0x1
SecPagePoolCount = 0x800
SharedIMEMBaseAddr = 0x146AA000
DloadCookieAddr = 0x01FD3000
PilSubsysDbgCookieAddr = 0x146AA6DC
UefiMemUseThreshold = 0x1900
UfsSmmuConfigForOtherBootDev = 1
## Security flag ##
SecurityFlag = 0xC4
DefaultChargerApp = "QcomChargerApp"
## Default app to boot in platform BDS init
DefaultBDSBootApp = "LinuxLoader"
EnableDisplayThread = 0x1
EnableDisplayImageFv = 0x1
AllowNonPersistentVarsInRetail = 0x1
NonPersistentVarsInRetail      = 0x1
NonPersistentVarsInRetail      = 0x1
EnableUefiSecAppDebugLogDump = 0x0
DetectRetailUserAttentionHotkeyCode = 0x17    # SCAN_ESC
MaxCoreCnt = 8
EarlyInitCoreCnt = 2
```

**Mappa di memoria nel config** (righe letterali, `sec_add_mem`/`AddMem`) **[VERIFICATO]**:

```
0xA7000000, 0x00400000, "UEFI FD",           AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK
0xA7400000, 0x00200000, "UEFI FD Reserved",  AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK
0xA7600000, 0x00001000, "CPU Vectors",       ...
0xA7602000, 0x00003000, "MMU PageTables",    ...
0xA7605000, 0x00008000, "Log Buffer",        ...
0xA760D000, 0x00040000, "UEFI Stack",        ...
0xA764D000, 0x0008C000, "SEC Heap",          ...
0xA76D9000, 0x00400000, "Sched Heap",        ...
0xA7ED9000, 0x00127000, "UEFI RESV",         ...
0xA8000000, 0x10000000, "Kernel",            AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN
0xB8000000, 0x02B00000, "Display Reserved",  ...
```

**Due firmware volume compresse gzip** dentro `uefi_a` **[VERIFICATO]** (magia `1f 8b`):

| offset | decompressa in | contenuto |
|---|---|---|
| `0x49F28` | 3.764.232 B (sha256 `259e2d44…`) | FV (`_FVH` a +0x30) con `QcomCharger` |
| `0x180FE0` | 2.773.000 B (sha256 `06bc2147…`) | FV (`_FVH` a +0x30) con `QcomCharger` |

Grep sui dati **decompressi**: `ANDROID!` 0, `VNDRBOOT` 0, `libavb` 0, `fastboot` 0, `UEFI Shell` 0,
`LinuxLoader` 0 → **neanche l'UEFI è il componente che carica `boot`/`vendor_boot`**.

**Le uniche due stringhe di boot presenti in `uefi_a`** sono il testo del config, non codice
**[VERIFICATO]**: `uefiplatLA.cfg` @299.558, `DefaultBDSBootApp` @301.280, `LinuxLoader` @301.301
(occorrenza **unica** in tutta la partizione). `uefisecapp` non compare come stringa letterale; il
riferimento più vicino è `EnableUefiSecAppDebugLogDump = 0x0` nel config (la sottostringa `UefiSecApp` è a
offset 301.891) — mentre le **partizioni**
`uefisecapp_a/_b` esistono nella GPT.

**`uefivarstore` è praticamente vuoto** **[VERIFICATO]**: 524.288 B, sha256
`7b4e3b1baa21f74a134dfa13820926770b214a337a5cb87ba5c0eda54bee82d7` (misurato su due copie
indipendenti: `edl-recovery/recon-20260717-200334/uefivarstore.bin` e
`nx679j-openwrt-clean/bootchain/uefivarstore.img`), **896 byte non-zero**, e **nessuna** occorrenza di
`BootOrder`, `BootNext`, `Boot0000`, `ConOut`, `LinuxLoader` (né ASCII né UTF-16) → il percorso di boot
non passa da variabili EFI standard, ma dal BDS/`DefaultBDSBootApp` di piattaforma **[IPOTESI]**.

---

### 0c.4 ABL (`sde10`/`sde38`): la catena di contenitori, e il PE32+ `LinuxLoader`

Qui sta il codice che prende le decisioni. Catena misurata per intero (i valori sono riprodotti
localmente su `bootloader-re/abl_a.img`, sha256 `443d1956…`, contro ogni singolo campo):

```
abl_a (1.048.576 B)
 └─ ELF32 @0x0        e_machine=ARM(0x28), e_entry=0x9fa00000, 3 phdr:
     │                 #0 NULL  off 0x0     len 0x94    (ELF hdr + phdr)
     │                 #1 LOAD  off 0x1000  len 0x28000 vaddr 0x9fa00000
     │                 #2 NULL  off 0x29000 len 0xd58   (segmento HASH MBN)
     │                 dal byte 0x29d58 alla fine della partizione: zeri
     └─ UEFI Firmware Volume @0x1000     _FVH @0x1028     FvLength=0x28000 (163.840 B)
         └─ FFS file @0x1048      Name GUID 9E21FD93-9C72-4C15-8C4B-E77F1DB2D792
         │                        Type 0x0B (FIRMWARE_VOLUME_IMAGE), Size 150.099, State 0xF8
             └─ Sezione GUID_DEFINED @0x1060   Guid = EE4E5898-3914-4259-9D6E-DC7BD79403CF (LZMA EDK2)
         │                       Size 150.075, DataOffset = 24
                 └─ flusso .lzma @0x1078, 150.051 B
                    header LZMA: props=0x5D, dict=0x01000000 (16 MiB), size=0x000930C8
                    └─ DECOMPRESSO = 602.312 B  = [RAW 4 B][FV_IMAGE 602.308 B]
                        └─ "payload" = 602.304 B   sha256 0d7786715ccacc2f396e21dbf75f02bc89f95e3d5fcaec8d320c76967f9870e5
                           └─ FV interna @+0x0 (FvLength=0x930C0, _FVH @+0x28)
                              └─ FFS file @+0x78  type 0x09 FIRMWARE_VOLUME_IMAGE
                                 ├─ sezione USER_INTERFACE @+0x90 → nome modulo "LinuxLoader" (UTF-16) @+0x94
                                 └─ sezione PE32 @+0xAC, 602.116 B
                                    └─ MZ @+0xB0 ; PE header: machine=0xAA64 (AArch64), PE32+,
                                       Subsystem 10 (EFI application), AddressOfEntryPoint RVA 0x1000,
                                       ImageBase = 0
```

**Comando che ha prodotto i campi** (riproducibile offline, una sola espressione):

```python
d = open('abl_a.img','rb').read()
out = lzma.decompress(d[0x1078:], format=lzma.FORMAT_ALONE)     # 602.312 B
pay = out[8:]                                                   # 602.304 B (payload)
hashlib.sha256(pay).hexdigest()   # 0d7786715ccacc2f396e21dbf75f02bc89f95e3d5fcaec8d320c76967f9870e5
pay.find(b'MZ')                   # 0xb0  -> PE32+ AArch64, subsystem 10, entry RVA 0x1000
```

**Le due stringhe che identificano il ruolo dell'ABL** (offset nel payload da 602.304 B)
**[VERIFICATO]** — sono adiacenti, cioè una *tabella delle magie di immagine*:

| stringa | offset (payload) | VA | riferimenti nel `.text` (ADRP+ADD) |
|---|---|---|---|
| `ANDROID!` | `0x632C4` | `0x63214` | `0xDAB4`, `0x17B88`, `0x313EC` |
| `VNDRBOOT` | `0x632CD` | `0x6321D` | `0xDACC`, `0x17CF4`, `0x180E8` |

Nel payload compaiono **tutto**: `libavb` (11 stringhe), `ufdt*` (18), `fastboot` (23),
`slot_suffix`, `unbootable`, `qcom,msm-id`/`qcom,board-id`, e il **percorso di build Nubia**:

```
/home/nubia/SCMWork/nrom/NX679J_Z69_UN_ZML1S_V311/20220308142115/app/out/target/product/taro/obj/ABL_OBJ/Build/DEBUG_CLANG35/AARCH64/QcomModulePkg/Application/LinuxLoader/LinuxLoader/DEBUG/LinuxLoader.dll
.../app/bootable/bootloader/edk2/QcomModulePkg/Library/avb/libavb/avb_slot_verify.c
```

**Perché la prima analisi l'aveva dichiarato "privo di stringhe di boot"** **[VERIFICATO]**: le stringhe
non esistono sulla partizione in chiaro, sono **LZMA**. Una scansione di magie di compressione su
`abl_a` senza decompressione le manca, ed è esattamente quello che era successo. Il sorgente locale
dello *stesso* modulo esiste in parallelo e serve da confronto:
`port-work/mu-aloha-clean-20260916/Platforms/QcomModulePkg/Application/LinuxLoader/LinuxLoader.c`.

---

### 0c.5 Le decisioni dell'ABL, provate a valle

Il codice non è ispezionabile riga per riga (le offset sono nel payload, non in RAM), ma **ogni
decisione lascia un'orma leggibile**: le sue stesse stringhe di log, le xref, e soprattutto gli
effetti osservabili sul dispositivo avviato.

**a) Quale dei nove DTB** — `vendor_boot` (header v4: `header_size=2128`, `dtb_size=3.879.360`,
`dtb_addr=0x1f00000`, area DTB a `0x995000`) contiene **nove** alberi:

| idx | offset | size | model | `qcom,msm-id` | `qcom,board-id` |
|---|---|---|---|---|---|
| 0 | 0 | 431.264 | Cape LTE Only SoC | `<0x212 0x10000>` | `<0 0>` |
| 1 | 431.264 | 431.252 | Cape SoC | `<0x212 0x10000>` | `<0 0>` |
| 2 | 862.516 | 347.467 | CapeP SoC | `<0x213 0x10000>` | `<0 0>` |
| 3 | 1.209.983 | 298.995 | Diwali HSP SoC | `<0x1fa 0x10000>` | `<0 2>` |
| 4 | 1.508.978 | 387.498 | Diwali SoC | `<0x1fa 0x10000>` | `<0 0>` |
| **5** | **1.896.476** | **496.030** | **Waipio v2 SoC** | **`<0x1c9 0x20000>`** | `<0 0>` |
| 6 | 2.392.506 | 495.598 | Waipio SoC | `<0x1c9 0x10000>` | `<0 0>` |
| 7 | 2.888.104 | 495.630 | WaipioP v2 SoC | `<0x1e2 0x20000>` | `<0 0>` |
| 8 | 3.383.734 | 495.626 | WaipioP SoC | `<0x1e2 0x10000>` | `<0 0>` |

Il criterio è il confronto (memcmp) di `qcom,msm-id` e `qcom,board-id` del candidato con i valori
locali della piattaforma (stringhe `qcom,msm-id` @`0x66eb2` con xref `0x1b220`, `0x1c728`;
`qcom,board-id` @`0x66e13` con xref `0x1b044`, `0x1c944`; log `Best match DTB tags %u/%08x/…` @`0x66934`,
`Exact DTB match found. DTBO search is not required` @`0x66a84`). Scelto **l'indice 5**: è l'**unico**
dei nove con `msm-id=<0x1c9 0x20000>`; tutti e nove hanno `board-id=<0 0>` (jolly), quindi il board-id
non discrimina in questa fase. Prova a runtime:

```
androidboot.dtb_idx  = "5"        # /proc/bootconfig, letto dal device
androidboot.dtbo_idx = "35"
model      = "Qualcomm Technologies, Inc. Waipio MTP with PM8010"
qcom,board-id = <0x00010008 0x00000000>
qcom,msm-id   = <0x1c9 0x20000>, <0x1e2 0x20000>
```

**b) Quale overlay dtbo** — `dtbo_a` header `dt_table` (big-endian): magic `0xd7b7ab1e`,
`total_size=9.447.224`, `dt_entry_size=32`, **`dt_entry_count=44`**. L'ABL usa **libufdt statico**
(`ufdt_install_blob`, `ufdt_apply_overlay`, `ufdt_apply_multi_overlay`, `ufdt_overlay_do_fixups`,
`ApplyOverlay: ufdt apply overlay failed` @`0x19d78`, `Dtbo count = %u LocalBoardDtMatch = %x`
@`0x1dae8`) e applica la **voce 35** (`size=334.200`, `offset=0x65eac4`); in quella voce, a `0x120`, il
pattern `00 01 00 08 00 00 00 00` = `qcom,board-id=<0x10008 0>`: il board-id reale del telefono arriva
**dall'overlay**, non dal DTB base. `model` e `compatible` della voce 35 coincidono con quelli del DT
vivo → prova che il DT finale = DTB#5 + overlay#35.

**c) La `cmdline`: quattro segmenti e chi li mette** — `/proc/cmdline` (717 B) = segmento 1 (111 B) +
`/chosen/bootargs` (605 B), verificato per concatenazione:

| # | lunghezza | contenuto (inizio) | fonte provata |
|---|---|---|---|
| 1 | 111 B | `stack_depot_disable=on kasan.stacktrace=off kvm-arm.mode=protected cgroup_disable=pressure cgroup.memory=nokmem` | **`CONFIG_CMDLINE` del kernel** (`/proc/config.gz`), con `CONFIG_CMDLINE_EXTEND=y` |
| 2 | 434 B | `console=ttyMSM0,115200n8 loglevel=6 kpti=0 … can.stats_timer=0` | **`/chosen/bootargs` del DTB base #5** |
| 3 | 83 B | `video=vfb:640x400,bpp=32,memsize=3072000 qcom-dload-mode.download_mode=1 bootconfig` | **cmdline di `vendor_boot`** (offset 28) |
| 4 | 85 B di contenuto (87 con i due separatori) | `msm_drm.dsi_display0=qcom,mdss_dsi_r6130_1080_2400_amoled_cmd: rootwait ro init=/init` | **[IPOTESI] generato dal bootloader**: 0 occorrenze nelle 44 voci dtbo e nella cmdline di `vendor_boot`, ma il valore coincide col `compatible` del pannello nell'albero finale. Ricostruzione verificata: `111+1+434+1+83+2+85 = 717` = lunghezza esatta di `/proc/cmdline` |

Orma digitale del meccanismo **[VERIFICATO]**: fra il segmento 3 e il 4 ci sono **due spazi
consecutivi** (`…bootconfig  msm_drm.dsi_display0=…`), cioè la concatenazione aggiunge il separatore
anche quando il campo aggiunto è vuoto. Il `/chosen/bootargs` dell'albero in esecuzione misura **605 B**
= `434+1+83+2+85` (il dump base64 di `live/chosen_bootargs.txt` ne contiene 641 perché include anche la
proprietà successiva nel nodo `chosen`, `/soc/qcom,qup_uart@99c000:115200n8`).

**d) `bootconfig`: 85 B nel `vendor_boot`, 22 chiavi a runtime** — il blob in `vendor_boot` (85 B a
offset 13.934.592, sha256 `fbab70e49b81a2c679a7802d8e27712323ed79a2a7116bab037cf29b5198cba6`, identico
nei due slot) contiene 3 chiavi (`androidboot.hardware=qcom`, `androidboot.memcg=1`,
`androidboot.usbcontroller=a600000.dwc3`); `/proc/bootconfig` è **887 B, sha256 `1fe3616812e8fd36…`,
22 chiavi**, e le **19 aggiunte non esistono in nessuna partizione di boot**: sono le informazioni di
stato che solo il bootloader possiede — `androidboot.bootdevice=1d84000.ufshc`,
`androidboot.boot_devices=soc/1d84000.ufshc`, `androidboot.serialno=3dbd****`,
`androidboot.baseband=msm`, `androidboot.dtb_idx=5`, `androidboot.dtbo_idx=35`,
`androidboot.force_normal_boot=1`, `androidboot.fstab_suffix=default`,
`androidboot.verifiedbootstate=orange`, `androidboot.keymaster=1`,
`androidboot.vbmeta.device=PARTUUID=ef598a96-a359-bf7a-9361-16bf46ef6b3c`, `…avb_version=1.0`,
`…device_state=unlocked`, `…hash_alg=sha256`, `…size=11904`,
`…digest=030f49bd051dbd2c4b6f33560db51a1e4e13f7817115a4a484a780c0a1bf33c2`,
`…invalidate_on_error=yes`, `androidboot.veritymode=enforcing`, `androidboot.slot_suffix=_a`.
Nota di formato **[VERIFICATO]**: `/proc/bootconfig` non è una copia del blob ma la resa del kernel
(`androidboot.memcg = "1"`, virgolette per le stringhe), quindi il confronto si fa sulle chiavi, non sui
byte. Trasporto: il bootloader appende in coda all'initrd `[bootconfig][csum u32][size u32]["#BOOTCONFIG\n"]`
e il token `bootconfig` nella cmdline fa il resto (`init/main.c`: `bootconfig_params()` riga 392,
`setup_boot_config()` riga 401, `get_boot_config_from_initrd()` riga 313, che cerca la magia negli ultimi
12 byte dell'initrd e poi **stacca** il trailer riscrivendo `initrd_end`).

**e) Lo slot A/B: attributi GPT, non `misc`** — decodifica fatta ora su
`experiments/20260917-boot-chain/re/work/sde-gpt-entries.bin` (16.384 B = 128 entry):

| campo | bit (u64) | istruzione che lo prova |
|---|---|---|
| Priority | 48-49 | `0x36fd8: ldrh w8,[x8,#54]` + `and x8,x8,#0x3` |
| Active | 50 | `0x3734c: ubfx x4,x9,#18,#1`; marcatura a `0x1f9d0: orr x8,x8,#0x4000000000000` |
| Retry/tries | 51-53 | `0x36fec: ubfx x8,x8,#19,#3` |
| Success | 54 | `0x37350: and x5,x8,#0x40000000000000` |
| Unbootable | 55 | `0x37354: and x6,x8,#0x80000000000000` |

Valori misurati (estratto): `abl_a = 0x107f000000000000`, `uefi_a = 0x107f…`, `abl_b = 0x107b…`,
`uefi_b = 0x107b…` → slot A **Active=1** (Prio 3, Retry 7, Success 1), slot B Active=0;
`boot_a = 0x007f000000000000`, `boot_b = 0x003a000000000000` → `boot_b` con **Success=0** e Priority 2
(è la slot B rimasta marcata non riuscita dagli esperimenti OpenWrt). Provenienza della misura: entry
GPT lette dal device (`dd if=/dev/block/sde bs=4096 skip=2 count=4` → `re/work/sde-gpt-entries.bin`,
sha256 `0d2fe55da5afbdec6682db5c38860e06a76ed6e3cc519419e5f491008c42310f`), attributi a offset 48 di
ogni entry da 128 B, nome a offset 56. Il documento del 17/09 elenca genericamente *tutte* le `_b` a
`0x007b…`: su `boot_b` la misura dice altro (vedi §0c.9). Il `misc` contiene la BCB AOSP
(`command = bootonce-bootloader`), ma **nell'ABL non esiste la stringa `bootonce-bootloader`**: la
scelta dello slot **non passa dalla BCB**. Questo spiega anche perché `slot-retry-count` resta a 7 su
entrambe le slot: con `Success=1` sui due slot il decremento non scatta **[IPOTESI]** (i due `tst` sui
bit 54/55 stanno nello stesso blocco: `0x51c1c`, `0x51ccc`). Coerente con il layout: **è** `boot_b` — e
solo `boot_b` — ad avere `Success=0`, cioè la traccia della slot B data per non riuscita.

**f) AVB: eseguito, non bloccante** — libavb è linkato staticamente (path sorgente Nubia nel §0c.4) e
i suoi rami decisionali sono nel testo: `Device is unlocked, Skipping boot verification` (@`0x5bec`),
`State: Unlocked, AvbSlotVerify returned %a, continue boot` (@`0x68f8`),
`Slot: %a, allow verification error: %a`. Sul device `unlocked:yes` e `verifiedbootstate=orange` →
la verifica può **fallire senza bloccare il boot**, che è il comportamento osservato.

---

### 0c.6 Firma: cosa c'è dentro l'ABL e cosa non c'è

Segmento HASH MBN dell'ABL (`p_type=0` a `0x29000`, `len 0xd58`) decodificato con
`qc_inspect.py` **sull'immagine live** (`443d1956…`) **[VERIFICATO]**:

| campo | valore |
|---|---|
| Versione hash segment | **7** |
| Hash Table Algorithm | SHA-384 (tabella 144 B) |
| Software ID | `0x1c` (Secondary `0x0`) |
| OEM Signature | 104 B, **ECDSA / SHA-384 / `secp384r1`** |
| OEM Certificate Chain | 2.880 B (**1 sola Root CA**) |
| Root Certificate Index | 0 |
| SoC Hardware Versions | `0xa001`, `0xa004`, `0xa008` — **Bound = True** |
| OEM ID / OEM Product ID | `0x4` / `0x0` (Bound to OEM ID = True) |
| Anti-Rollback Version | `0x0` |
| `Hash of OEM Data` | SHA-256 `7dc4dcaf581e4d865a5a0384861de8a381b221e42d58286a08c1eb7d541918e5` |

Catena X.509 (Root CA a `0x4e8`):

```
[Root CA]        C=US, CN=Generated Ztemt Root CA, OU=CDMA Technologies,
                 OU=General Use Ztemt Key, L=San Diego, O=SecTools, ST=California
                 Cert-DER SHA256 (OEM_PK_HASH) = 5ad5c780085682ddc9cbe4281ffe476781f32ea2da881dba7a2944fa55e50ea5
                 Cert-DER SHA384               = fbc0a90afe54b2e38d7961e51aab31e5dbcef21f53b5d67c5d98561d74bdd4c0e9371fdb6519e7ecb032f691674fb644
[Attestation CA] C=US, ST=California, CN=Generated Ztemt Attestation CA, O=SecTools, L=SanDiego
[Leaf]           C=US, ST=California, O=SecTools, L=San Diego
```

**Cosa non c'è**: nessuna stringa di chiave privata (`PRIVATE KEY`, `RSA PRIVATE`, `BEGIN PRIVATE
KEY`, `EC PRIVATE`). È il comportamento corretto di ECDSA — la firma contiene `r, s` e materiale
pubblico; ricavare `d` richiederebbe di risolvere il logaritmo discreto su `secp384r1`. **La chiave
privata OEM non è nel firmware** e quindi non è un dato disponibile.

Contesto comparativo utile **[VERIFICATO]**: l'ABL di ROCKNIX/Armada per SM8550 è firmato con
`qtestsign Root CA - NOT SECURE` (RSA-2048/SHA-256): funziona sui dispositivi con secure boot
disabilitato/non fuseato, cioè un'anchor diversa dalla nostra. Sul NX679J `fastboot getvar` riporta
`secure:yes` e `unlocked:yes`: `unlocked` riguarda AVB/bootloader Android e **non** dimostra che XBL
accetti una nuova catena ABL con root diversa. Un ABL test-signed è costruibile e confrontabile
offline (formato MBN v7, `SW_ID`, hash segment, entry point, certificati), ma **non va scritto** su
`abl_a`/`abl_b` senza sapere se XBL applica la root fuse ZTE: il rischio è un rifiuto dell'immagine e
un brick che richiede EDL. Dettaglio **[IPOTESI]**: `SecurityFlag = 0xC4` nel config UEFI = `0x80 |
0x40 | 0x04` = `LoadKeymasterFlag | LoadSecAppFlag | CommonMbnLoadFlag`, cioè **`SecBootEnableFlag`
(0x1) non è impostato nel config** mentre il device dichiara `secure:yes` (probabilmente stato dei fuse
QFPROM, non del file di config).

---

### 0c.7 Il fastboot dell'ABL: 23 comandi, e nessun `boot`

Tabella statica decodificata dal payload (ciclo di registrazione a `0x5038c`, 23 voci × 16 B = `0x170`,
tabella a VA `0x6f4b8`; decodifica in `re/logs/fb-table-decoded.log`):

`flash:` · `erase:` · `set_active` · `flashing get_unlock_ability` · `flashing unlock` ·
`flashing lock` · `oem nubia_unlock` · `oem nubia_lock` · `oem nubia_device-info` ·
`oem nubia_unlock_critical` · `oem nubia_lock_critical` · `oem enable-charger-screen` ·
`oem disable-charger-screen` · `oem off-mode-charge` · `oem select-display-panel` · `oem device-info` ·
`continue` · `reboot` · `reboot-bootloader` · `reboot-recovery` · `reboot-fastboot` · `getvar:` ·
`download:`

**Assenti**: `boot` (avvio da RAM), `oem edl`, `reboot edl`, `flashall`, `powerdown`, `upload`. Il
dispatcher stampa `unknown command` (VA `0x6dd2c`, usata a `0x4f32c`) — è letteralmente la risposta
ottenuta sul campo per `fastboot boot`, `oem edl`, `reboot edl`.

Conseguenze operative **[VERIFICATO]**:
- `fastboot boot <img>` **non è implementabile** su questa unità (non esiste il percorso "avvia da RAM"):
  il solo canale è `flash:` su `boot`/`vendor_boot` (o lo slot B);
- EDL esiste come voce del **menu di boot** (`Boot to edload`, accanto a `Reboot system now`,
  `Reboot to recovery mode`, `Power off`, in `.data` @`0x75090`), non come comando fastboot;
- i `getvar` utili per l'analisi (`secure`, `variant`, `version-bootloader`, `current-slot`,
  `slot-retry-count`, `slot-unbootable`, `slot-successful`, `slot-suffixes`, `has-slot`, …) esistono,
  ed è da lì che viene la lettura `secure:yes` / `unlocked:yes`.

---

### 0c.8 Correzioni rispetto alle ipotesi della prima passata (agosto 2026)

| ipotesi precedente | stato ora | prova |
|---|---|---|
| "l'ABL inietta `kvm-arm.mode=protected` a runtime, dopo il DT" | **smentita**: quel parametro è il **primo segmento** della cmdline e viene dalla **`CONFIG_CMDLINE` del kernel** (`CONFIG_CMDLINE_EXTEND=y`), non dal bootloader | `/proc/config.gz` + concatenazione byte esatta `seg1 + " " + chosen/bootargs == /proc/cmdline` |
| "UEFI FV at offset `0x1028`" (FINAL_REPORT) | **impreciso ma coerente**: `0x1028` è la **firma `_FVH`**, la FV inizia a `0x1000`; `FvLength=0x28000` (163.840 B) coincide con la "size 163.840" riportata allora | misura dei campi FV |
| "l'ABL non contiene stringhe di boot image" | **smentita nel metodo**: le stringhe ci sono, **LZMA-compresse**; `ANDROID!` @`0x632C4`, `VNDRBOOT` @`0x632CD` nel payload decompresso | decompressione + hash del payload |
| "`xbl_a != xbl_b`" | **smentita**: byte-identici | 3 misure indipendenti (locale ×2, device) |
| "crash a 15 s: watchdog/ipervisore uccide il kernel" | **osservazione corretta, causa non dimostrata**: l'esito del boot fallito è il **ritorno in fastboot** (`18d1:d00d`) a **14,80 / 14,83 / 14,84 s** in tre tentativi indipendenti (`diagnostic-v2`, `framebuffer-v3`, `lz4-v4`). Il log dice solo "USB sparita e fastboot è tornato": **non** prova che Linux sia stato raggiunto, né dice *chi* ha riportato il device in fastboot | `native-openwrt-usb-build/headless-upstream/*/boot-attempt-1-monitor.log` |
| "`SECBOOT_FUSE 0` = secure boot disabilitato" | **smentita**: `#define SECBOOT_FUSE 0` è l'**indice del bit** nel valore di stato letto via SCM/TrustZone, non un valore di configurazione | `QcomModulePkg/Library/avb/libavb/avb_util.c` |

---

### 0c.9 Cosa resta non determinato

- **Indirizzi di runtime dell'ABL**: il PE32+ ha `ImageBase=0` e viene decompresso dalla FV; tutte le
  offset citate sono nel payload, non indirizzi assoluti di RAM.
- **`xbl_a` ELF#2** (186.112 B): nessuna stringa di codice e non decodificabile come AArch64 →
  compresso/cifrato con schema proprietario. Nulla prova il contenuto (l'unico fatto è che l'intera
  partizione non contiene `ANDROID!`/`VNDRBOOT`/`fastboot`/`avb`).
- **Algoritmo di scoring della scelta DTB** (i campi stampati da `Best match DTB tags …`, la regola di
  tie-break) e **provenienza esatta dei "tag" locali** (SMEM vs FDT passato da XBL).
- **Funzione di match delle voci dtbo** (da 44 voci alla #35): il *risultato* è verificato, l'algoritmo no.
- **Regola esatta di decremento dei `tries`** e di marcatura `Unbootable` (bit 54/55 testati, regola inferita).
- **Implementazione di `read_is_device_unlocked`** (fuse vs partizione `devinfo`/`frp`) e rapporto con le
  stringhe Nubia (`nubia_unlock`, `unlock password check fail`).
- **Matcher completo del fastboot** (prefissi `flash:`/`getvar:`/`download:`, argomenti dei `reboot-*`) e
  comportamento del *fastbootd* userspace: fuori dal bootloader.
- **Bit 60 dell'attributo GPT** (presente su tutte le entry `_a` e su `uefi_b`/`devinfo`/`uefivars`/
  `secdata`/`limits`/`toolsfv`): non usato dal codice slot disassemblato.
- **Identità byte-per-byte della FV di `uefi_a` con la FV dell'OTA** è verificata (sha256 uguale); **non**
  è verificato che *tutti* i moduli elencati da `uefiextract` siano caricati a runtime né in quale ordine
  il BDS risolva `DefaultBDSBootApp="LinuxLoader"`.
- **`uefivarstore`**: praticamente vuoto (896 B non-zero) ma non decodificato variabile per variabile.
- **Discrepanza di fonte**: l'analisi del 17/09 cita per il file FFS dell'ABL il GUID
  `f536d559-459f-48fa-8bbc-43b554ecae8d`; la misura di oggi legge a `0x1048` il GUID
  `9E21FD93-9C72-4C15-8C4B-E77F1DB2D792` (type `0x0B`, size 150.099). La sezione LZMA (`EE4E5898-…`,
  size 150.075, DataOffset 24) e il payload decompresso (sha256 `0d778671…`) coincidono: la differenza
  riguarda solo l'etichetta del file FFS esterno.
- **`boot_b` GPT**: il documento del 17/09 elenca `boot_b`/`vendor_boot_b` fra le entry `_b` con
  `attr = 0x007b…`; la decodifica di `re/work/sde-gpt-entries.bin` dà per `boot_b`
  `0x003a000000000000` (Priority 2, Active 0, Retry 7, **Success 0**) e per `vendor_boot_b`
  `0x007b…`. La conseguenza interpretativa (la slot B risulta marcata *non riuscita*) è plausibile ma
  non spiegata dal codice disassemblato.
- **Etichettatura della hash del payload ABL**: la fonte del 17/09 riporta `sha256 0d778671…` accanto a
  "602.312 B"; la misura mostra che `0d778671…` è la hash del *payload* da **602.304 B** (i 602.312 B
  sono il flusso LZMA decompresso, `[RAW 4 B][FV_IMAGE 602.308 B]`), il cui sha256 è
  `594606800bc4db04604234829169a0b65e260e6cdc196153bd8d48cf674523e9`. Nessuna delle due letture cambia
  le offset: le stringhe sono a `0x632c4`/`0x632cd` in entrambe.
- **Aritmetica del segmento 4 della cmdline**: la fonte scrive "85 B (2 spazi + 83)"; la misura dà 85 B
  di contenuto e 87 con i due separatori (il totale 717 B di `/proc/cmdline` è coerente solo con la
  seconda lettura).

### Riferimenti

- `experiments/20260917-boot-chain/XBL-ABL-DECISIONS.md` (analisi 17/09, 12:40-13:05 CEST; la ricostruzione principale);
- `experiments/20260917-boot-chain/BOOT-CHAIN-NX679J.md` (§3 PBL, §4 XBL, §5 UEFI/ABL, §6.1-§6.5 DTB/DTBO/cmdline/bootconfig/AVB);
- `experiments/20260917-boot-chain/` — script e log: `re/fv-unpack.py`, `re/fv-struct.py`, `re/xref2.py`, `re/fb-table3.py`, `re/dtbo-parse.py`, `re/fdt-enum.py`, `re/asmdump.py`, `re/gpt-slots.py`, `re/sigscan.sh`, `re/logs/abl_text.asm`, `re/logs/abl-strings.txt`, `re/logs/fv-*.log`, `re/logs/fb-table-decoded.log`, `re/logs/gpt-slots.log`, `re/out/abl_a.img@1000_f1048_gd1060_fvi_4.bin`, `re/out2/uefi_gz_49f28.bin`, `re/out2/uefi_gz_180fe0.bin`, `re/work/sde-gpt-entries.bin`, `re/work/abl_text.bin`, `re/work/dtbo_entry35.dtb`;
- `experiments/20260917-boot-chain/re/bl/` — readback diretti con hash (`xbl_a/b`, `abl_a/b`, `uefi_a/b`, `vbmeta_a/b`, `dtbo_a`, `misc`);
- `experiments/20260917-boot-chain/live/` — `byname.txt`, `bootconfig.txt`, `cmdline.txt`, `hashes_bl.txt`, `elf-images.log`, `string-map.txt`, `gpt_sdb_entries.txt`, `gpt_sde_entries.txt`, `cmdline-attribution.log`, `vendor-boot-layout.log`, `avb-crosscheck.log`;
- `/home/user/nx679j-stock/authoritative-bootchain-verification.md` e `authoritative-bootchain-20260916-235306/`, `authoritative-android-verification-20260916-234837/`;
- `analysis/uefiextract/uefi_fv_offset0x1000.bin{,.report.txt,.guids.csv}` (inventario FV/moduli EDK2 — nella copia pubblica restano i report testuali; la cartella `.dump/` con l'albero GUID completo **non è inclusa**);
- `analysis/abl-stock-signature-inspection.txt`, `analysis/abl-rocknix-signature-inspection.txt` (rimosso in questa revisione: riguarda SM8550, non il dispositivo — vedi `THIRD_PARTY_LICENSES.md` §3), `analysis/diag-live/`;
- `port-work/qc-signature-inspector/qc_inspect.py` (ispettore MBN/ELF, autore Littlenine) — eseguito su `bootloader-re/abl_a.img`;
- `bootloader-re/` — `FINAL_REPORT.md`, `ACTUAL_RESULTS.md`, `FINDINGS.md`, `REVERSE_ENGINEERING.md`, `IMMEDIATE_ACTION_PLAN.md`, `REALITY_CHECK.md`, `abl_a.img`, `xbl_a.img`, `abl_code.bin`, `uefi_fv.bin`, `dtb-injection-test/`, `bootconfig-test/bootconfig.txt`, `kernel-cmdline-patch/cmdline_rewrite_patch.S`;
- `port-work/mu-aloha-clean-20260916/Platforms/QcomModulePkg/` (sorgenti `Application/LinuxLoader/LinuxLoader.c`, `Library/avb/libavb/*.c`);
- `native-openwrt-usb-build/headless-upstream/diagnostic-v2/boot-attempt-1-monitor.log` (14,83 s);
- `full_extracted_v311/` (`uefi.img`, `boot.img`, `vendor_boot.img`), `nx679j-openwrt-clean/bootchain/manifest.json`.

---

## Fase 1: la catena di boot

### Stato di partenza

`NX679J` (SM8450/Waipio), kernel `5.10.66-android12-9-00005-gf6e6376090be`. Slot A = Android+Magisk (oracolo), slot B = OpenWrt; `boot state: orange`, `device state: unlocked`, `fastboot getvar`: `unlocked: yes`, `secure: yes`, `slot-count: 2`. EDL inutilizzabile (Sahara `END_OF_IMAGE`, `status=1 INVALID_CMD`) ⇒ via di rientro: slot A.

### (a) Perché la catena accetta la nostra immagine

Il `boot_b` di partenza era un rifiuto pulito: header v4, **cmdline vuota**, `signature_size: 0` (`b8ab71f9…`). Il 16/09 tre cicli tornano in fastboot invece che in Linux: `build-boot-with-cmdline.sh` (mkbootimg v4, cmdline da `/proc/cmdline`, kernel 6.6.60) → `boot-test-monitor-20260916-214603.log`: «⚠ FASTBOOT DEVICE DETECTED — boot failed, returned to fastboot»; poi `add-dummy-signature.sh` (+4096 B: «This is NOT a cryptographic signature») → idem; e infine la build **stockkernel** (`build-stockkernel-openwrt.sh`, `boot-test-stockkernel-20260916-221207.log`) → stesso ritorno. La differenza che ha funzionato è l'**imballaggio**: patchare in loco il contenitore Magisk — nuova ramdisk a offset **49115136**, `ramdisk_size` a offset **12**, ogni altro byte identico allo stesso offset assoluto, `signature_size` lasciato a 4096 — con ramdisk cpio `newc` in **lz4 legacy** (`lz4 -l -9`), non gz come mkbootimg. Immagini `boot_b-probe-v7.img` (`77d7675e…`) e `boot_b-probe-v7b.img` (`7424a599…`), 100663296 B.

Cosa non abbiamo toccato:

- **vbmeta**: `vbmeta_a` = `vbmeta_b` (`cc0de687…`), `Flags = 0`. Il digest AVB dichiarato per `boot` (`f0abae29…`, `image_size 50499584`) non corrisponde a `sha256(boot_a[0:50499584])` = `ec823514…`, e Android parte lo stesso da `boot_a`: l'ABL contiene `Device is unlocked, Skipping boot verification` (`androidboot.vbmeta.device_state="unlocked"`, `verifiedbootstate="orange"`). ⇒ **nessuna scrittura su vbmeta, nessun `--disable-verity`**: libavb **e' presente ed eseguito** (estrae i metadati), ma su device unlocked la verifica **non e' bloccante** — non e' 'AVB assente', e' 'AVB che non ferma il boot'.
- **GPT/slot**: lo stato A/B sta negli attributi GPT, non in `misc` (`boot_a = 0x007f000000000000`, `boot_b = 0x003a000000000000`; bit 50 Active, 54 Success, 55 Unbootable).
- **Prova diretta** (17/09 10:17): `magisk_patched-30700_Zx2eF.img` (`0cd94d56…`) su `boot_b` con readback uguale, `fastboot set_active b` → USB `05c6:908c` a **+26 s**, «ANDROID E' PARTITO DA SLOT B».
- BCB: `misc` conteneva `bootonce-bootloader`, azzerato con `adb shell su -c 'dd if=/dev/zero of=/dev/block/by-name/misc bs=32 count=1 conv=notrunc'`.

### (b) Passare tra stock Android (A) e OpenWrt (B), e tornare indietro

```sh
# A→B, da Android (torna-b.sh)
adb reboot bootloader
fastboot getvar current-slot slot-successful:b slot-unbootable:b slot-retry-count:b unlocked
fastboot --set-active=b && fastboot continue        # oppure: set_active b + fastboot reboot
```
Da OpenWrt, senza azione fisica: `reboot2 bootloader` (`experiments/reboot2.c`: `syscall(SYS_reboot, 0xfee1dead, 672274793, 0xa1b2c3d4, "bootloader")`, stessa `LINUX_REBOOT_CMD_RESTART2` di `adb reboot bootloader`); riavvio normale `reboot -f` (o `echo b > /proc/sysrq-trigger`, perché il reboot busybox non attraversa il chroot). Ciclo chiuso: `OpenWrt → reboot2 bootloader → fastboot set_active a/b → Android o Linux`.

Flash di B: all'inizio `fastboot flash boot_b <img>`; poi dalla shell del device, **mai fastboot per il ramdisk**: `rm -f /dev/sde41; mknod /dev/sde41 b 259 25; dd if=/tmp/b.img of=/dev/sde41 bs=1M count=96 oflag=direct; sync -f /dev/sde41`, poi **due letture fredde** `md5sum /dev/sde41` che devono coincidere con l'host: solo allora `reboot -f`. Recovery: `boot_b` salvato prima (dd+hash) e ritorno `fastboot set_active a`; `ripristino-v175.sh` = device in fastboot (Volume Giù + Power), controllo md5 `98ac83f587bd6bb29ae263b48ebd35c7`, `fastboot --set-active=b`, `fastboot flash boot_b`, `fastboot reboot`. Dopo un hang: POWER ~15 s → POWER+VOL− → `fastboot set_active a`.

### (c) Primi boot riusciti: cosa bisognava cambiare

1. **PID 1 non deve bloccarsi.** v5/v6 restavano sul logo, senza USB e con `rawdump` tutto zero, perché PID 1 chiamava `finit_module` sui 327 `.ko`. `/init` v8 ha **8 sole syscall** (`clock_gettime, clone, execve, exit_group, kill, nanosleep, reboot, wait4`) e ogni operazione rischiosa è un figlio con deadline; solo `modules.load` (98 nomi), mai `modules.load.recovery` (329); la prima riga di journal in `rawdump` è scritta prima di ogni modulo.
2. **Prova locale**: in QEMU col kernel del telefono v7/v7b passano 12/12 — `init reached userspace as pid 1 attempt=1 uptime_ms=605` poi `reboot: Restarting system`.
3. **Userspace**: PID 1 tenuto vivo e lavoro spostato in `switch.sh`; un `libgcc_s.so.1` mancante causava `execve` «misteriosi». Primo risultato verificato: **SSH su `10.0.0.1:22` via gadget NCM `18d1:4ee7`**, LuCI HTTP 200.

**Assente nei documenti locali**: la data/log del primo boot OpenWrt *fisico* (il `test.log` v7b del 17/09 12:26 si ferma all'avvio del monitor da 240 s) e la prova che la firma fittizia da 4096 B sia mai stata *necessaria*.

### Riferimenti

`fastboot`/`adb`; `mkbootimg`/`unpack_bootimg`; `lz4` v1.10.0; `aarch64-linux-gnu-gcc` 16.1.0; QEMU; `avbtool`/libavb (`avb_slot_verify.c`, AOSP); aosp-mirror `platform_system_core` android-12.0.0_r1; CAF Qualcomm `gpt-utils`; OTA ufficiale Nubia V311 (`payload.bin`, `META-INF/com/android/otacert`, `O=nubia`); Magisk 30.7; regole dell'utente (skill `regole-utente`: «per andare su android devi andare su fastboot, settare il boot a e farlo partire... sulla parte android hai magisk con il root»).

## Fase 2: l'immagine di boot e il sistema

**Contenitore.** `boot_b` è un boot image Android **header v4** da 100663296 B (96 MB fissi). Dal file: magic `ANDROID!`, `kernel_size=0x2ed5564` (49108324 B), `ramdisk_size=0x2b901d8` (45679064 B nel v90), `os_version=0x18000162`, `header_size=1584`, `header_version=4` (offset 40), cmdline vuota. Il builder non ricostruisce l'header: `ks, rs = struct.unpack_from('<II', img, 8)`, poi riscrive solo il ramdisk (`struct.pack_into('<I', hdr, 12, len(newram))`) e ricompone `body = bytes(hdr) + img[P:P + ru(ks)] + newram + b'\0' * (ru(len(newram)) - len(newram)) + sig`, con `assert len(outimg) == len(img)` e la guardia `assert 10_000_000 < len(newram) < 60_000_000`.

**Ramdisk.** Tre cpio newc concatenati: `merged = v9 + new + bytes(data)` — `v9`/`new` da `20260917-init-v9/v10-work/{v9.cpio,new-uid0.cpio}` (uid/gid azzerati a `00000000`), `data` = cpio dell'overlay `OVL/owrt` (`find . | LC_ALL=C sort | cpio -o -H newc`) — poi `lz4 -l -9 -c`. Verificato sul ramdisk v90: 133720168 B decompressi, 3721 record cpio (3718 voci + **3 trailer** `TRAILER!!!`) (offset 3903468 / 30254904 / 133719576). A parità di path vince l'archivio successivo: `init` esiste in due copie (67384 B e 66240 B) e nel ramdisk finale vale la seconda.

**Chroot.** `/init` (ELF statico, stringhe `v19: exec OK (script started)` e `/nx679j/switch.sh`) lancia il ramdisk `switch.sh`, che prepara `/owrt` e ci entra: `CI="$BB chroot /owrt /bin/busybox"`, … `$BB chroot /owrt /sbin/init >> $J 2>&1`, restando vivo per sempre (`while :; do $BB sleep 3600; done`) perché «PID 1 CANNOT die: a dead PID 1 is a kernel panic».

**Persist-tars e iniezione tardiva.** `persist-tars/etc.tar` (342 entry), `luci.tar` (181), `tools.tar` (46, 18.7 MB) — conteggi ricalcolati sui tar sanificati vengono estratti in `OVL/owrt` dallo step **PERSIST-INSIDE**, prima del cpio. Regola del builder: si inietta **dopo** il persist. L'incidente che l'ha provata è il firmware del touch — i tar «contengono il rootfs intero e cancellano tutto», e la copia anticipata «li faceva sparire» (build-v90.py, commenti ll. 212-219 e 249-263). Ora `goodix-fw/*.bin` finiscono **dopo** in `lib/firmware` (root vero: `request_firmware()` gira in un kthread del kernel) e in `owrt/lib/firmware`: `goodix_cfg_group.bin` 4614 B md5 `ac7895cba4123d88b4e4f4183927b5d2`, `goodix_firmware.bin` 182528 B md5 `da71e43aa632cc9d660d511b2581fa0d` — md5 verificati dal ramdisk v90, non dal file su disco.

**Moduli vendor.** Nella chroot nessuno fa autoload (manca l'helper uevent), quindi `nx679j-modules.sh`, chiamato dal launcher, esegue `insmod "$KM/$1.ko"` su `qcom-pon` → `pm8941-pwrkey` → `pmic-pon-log` → `qti_battery_charger` → `charger-ulog-glink` → `ucsi_glink` (6 dei 7 `.ko`; `pmic_glink.ko` non si ricarica). I nodi `/dev/input/event*` si creano leggendo da `/sys/class/input/event*/dev`, mai stimando i numeri.

**Timing del launcher display.** `X=${DISPLAY_LATE_X:-70}` e `GAP=${DISPLAY_LATE_GAP:-5}`, polling ogni 2 s. Avviato da boot-services in parallelo a rcS (~36 s), ma la gate interna resta la protezione: con polling 15 s partiva a uptime 94. Misure: kiosk ~109-150 s (era ≥900).

**Flash.** `rm -f /dev/sde41; mknod /dev/sde41 b 259 25`, `dd if=<img> of=$N bs=4M oflag=direct conv=fsync`, `sync -f $N; sync`, due letture fredde con md5 (`dd if=$N bs=1M count=96 | md5sum`, `sleep 3`, ripetuta) e `reboot -f` solo se coincidono. Il doppio controllo nasce da un incidente reale: il primo flash del v82 «sembrava ok» (readback `c51278ea`) ma «era cache del block device» e al reboot c'era ancora il v81. Gate aggiuntivi: niente scrittura se la build non è pulita, se l'immagine è identica alla precedente o se il backup non combacia col valore atteso — «il gate del flash che confronta il backup ha fermato un flash su premesse sbagliate: tenerlo» (STATO-ATTUALE.md, ll. 1084 e 1212). `ripristino-v175.sh` è la forma eseguibile: `[ "$H" = "98ac83f587bd6bb29ae263b48ebd35c7" ] || { echo "MD5 DIVERSO: mi fermo"; exit 1; }`.

**Nota.** Il codice del `WRITE_GATE` non e' nel progetto ma **esiste ed e' stato usato**: `~/.hermes/profiles/kernel-re/cache/scratch/nx679j-flash-v12{7,8}.sh` (stampa `WRITE_GATE_PASS`/`FAIL`, doppia lettura `iflag=direct`). Va spostato nel progetto. `boot_b-v90-mmwd.img` su disco (md5 `f8d1324f…`) non è il v90 documentato (`37d450c8…`), perché **tutti** i `build-vNN.py` scrivono su quel nome: contiene l'ultimo payload (v180). Gli md5 attesi di `ripristino-v175.sh` (`98ac83f5…`) e del file `boot_b-current-v170.img` (`cc592be0…`) non combaciano più: oggi quel gate rifiuterebbe. Nessun log/ESITO locale dell'incidente firmware: solo i commenti del builder e la skill. `goodix-cmdline-path.py` (`firmware_class.path=/owrt/lib/firmware`) non risulta applicato: la cmdline delle immagini v87/v90/v170 è tutta NUL.

**Riferimenti.** Artefatti locali: `build-v90.py`, `persist-tars/{etc,luci,tools}.tar`, `switch-v58-live.sh`, `…-display-late.sh`, `…-modules.sh`, `…-boot-services.sh`, `goodix-cmdline-path.py`, `ripristino-v175.sh`, `RIPRESA.md`, `STATO-ATTUALE.md`, `DIAGNOSI-v121-non-flashata.md`. Skill: `kernel-re/nx679j-openwrt` (`references/boot-flash-device.md`, `persistenza-v87.md`, `vendor-modules.md`) e `android-boot-chain-recovery/references/…-display-touch.md`. Strumenti esterni di questa fase: formato AOSP boot image header v3/v4, `lz4 -l -9`, `cpio -H newc`, busybox (`chroot`/`insmod`/`mknod`), `dd oflag=direct` + `sync -f`, `fastboot`, debugfs.

## Fase 3: il modem X65

**1. Trasporto QRTR, qmi-proxy, versione di qmicli.** Il modem è **node 0** e ogni servizio QMI è una porta su quel node (48 servizi): **non esiste `/dev/cdc-wdm`**, tutto parla `-d qrtr://0`. Su QRTR il servizio CTL **non esiste** — libqmi lo emula in locale (`qmi-endpoint-qrtr.c`, «We implement the CTL service here») e alloca i CID in locale, quindi `ctl_port=-1` è normale e `--client-cid`/`--client-no-release-cid` **non attraversano il confine di processo** (MR !382, chiusa). Il launcher fa bind+start **in un solo processo** (owner del CID). Lo strato di stato usa `qmicli -p -d qrtr://0` e `qmi-proxy` accetta la URI, ma `-p` **non serializza**: race di apertura concorrente aperta (#113; `ClientIdsExhausted` a 15 client, #62) — il polling resta **sequenziale** (20/20 query, <1 s). Il `qmicli` stock di OpenWrt usa la **collection basic**: 11 soli `--nas-get-*`, e `--nas-get-lte-cphy-ca-info` = `Unknown option` (verifica: `strings /usr/bin/qmicli | grep -c nas-get`). Il default libqmi, senza `CONFIG_LIBQMI_COLLECTION_MINIMAL/BASIC`, è **full**: si ricostruisce senza toccare `.config` (`./scripts/feeds update -a && ./scripts/feeds install libqmi && make package/libs/libqmi/compile`; richiede glib2 con `-Dsysprof=disabled`, altrimenti non compila). Trappole: niente `timeout` sul device, dati su **stderr**, una azione per invocazione.

**2. Sessioni DPM/WDS e il fix `hold`.** `prepare` apre l'endpoint (`rmnet-query rmnet_ipa0` → `endpoint=1 (0x1)`, `pipes consumer=2 producer=23`); la catena crea il holder con `/tmp/qmi-qrtr-observed dpm-session 4 1 2 23 86400`, attendendo `[dpm] OPENED endpoint=4:1 rx=2 tx=23`, poi la WDS con `wds-session internet.it 4 1 1 86400` (all'epoca 3600, poi corretto dal fix v127) e `[session] HOLDING `. **Difetto misurato** (uptime ≈ 37300 s, hold 3600): helper in `Z`, ping 100% loss, ma `ip -4 addr show` dava ancora `10.97.82.164/29` e default route `via 10.97.82.165`; log: `[request] 00 06 00 21 ...` (WDS Stop 0x0021) → `[session] stopping handle=635623792`. LuCI mostrava «Connected: yes, Uptime: 10h 20m»: falso verde. **Fix v127**: hold `3600 → 86400`. Rinnovo: `10.99.143.159/26` via `10.99.143.160`, ma **L3 non riapplicato** → ping KO; con `ip addr add 10.99.143.159/26; route del default; route add default via 10.99.143.160; ip addr del 10.97.82.164/29` → ping 4/4.

**3. Proprietà dell'L3 e `wan_early`.** L'L3 è della catena. `proto static` è **falsificato** (netifd diventa proprietario e rimuove address/route); `proto none` è **falsificato** (netifd rivendica il device e fa flush dell'L3: address e default route spariti ~73 s dopo, netdev ancora `UP`). Forma corretta: proto **`nx679j`** (read-only), `proto_init_update "$ifname" 1 1` (terzo argomento = *address-external*), nessun `ip` scritto e **nessuna route riportata** — `address-external` copre solo gli indirizzi, perciò v129 ha rimosso `proto_add_ipv4_route`. Accettazione: `ubus call network get_proto_handlers | grep -i nx679j` e l'asset `.../protocol/nx679j.js`. `chain.sh` la crea con `uci set network.wan_early.proto=nx679j`, `device=$DE`, `auto=1`, `defaultroute=0` (log `v113: interfaccia UCI 'wan_early' ... su $DE`).

**4. Ordine della catena.** `S95nx679j-modem` → `chain.sh`: bootstrap MSS → READY → dms (mode 5→0) → prepare → dpm → ingress → **data `for s in egress wda mux wds`** → cell → `DONE` (`criteria=1`). **Lezione d'ordine**: il formato dati va negoziato sull'endpoint **prima** della call WDS — con WDS attiva `wda-qmap` è rifiutato (`[wda] returned format is not verified plain QMAP; stop`) e l'endpoint resta raw-IP ad aggregazione spenta (`wda-get`: `0x11=2, 0x12=0, 0x13=0`): indirizzo e route corretti danno comunque 100% loss, e uccidere un holder DPM in più **non** sblocca. Firma di accettazione: `SET_VERIFIED raw-IP=2 UL-QMAP=5 DL-QMAP=5 QoS=0` (TLV `0x12=5/0x13=5`). In `prepare` l'ingress `agg8192` (0x2e) dev'essere la **prima**, altrimenti EINVAL (`EP 23 already allocated`).

**5. Cella, CA, salute.** `nx679j-cell.sh` emette **28 chiavi** `cell.*`/`ca.*` con `qmicli -d qrtr://0 --nas-get-rf-band-info`, `--nas-get-cell-location-info`, `--nas-get-signal-info`, `--nas-get-lte-cphy-ca-info`. La CA si vede solo con traffico attivo; QMI 74 `InformationUnavailable` = nessuna CA allocata, 94 = non supportato; l'RSRP per portante si ricava **incrociando il PCI**. Il fetcher lo chiama ogni 10 cicli (~30 s), cache `/tmp/ui-cell.txt`; in `/tmp/ui-data.txt` la salute è `modem.health=$(cat /tmp/link-health)` = `up|down <uptime> fails=N`.

**6. Recovery.** **(a) Reboot pulito**: con MSS non registrato (`dmesg`: `releasing 4080000.remoteproc-mss` a ~19 s) o endpoint incastrato è la via misurata — catena end-to-end senza passi manuali (`data wds rc=0`, `bearer_ready=1`, `cell ping rc=0`; seconda passata: `healthy: rmnet_data0 con IP e ping ok, catena saltata`). **Mai** rebind sysfs del MSS: ha crashato il device. **(b) Comando di servizio**: il servizio è `/etc/init.d/nx679j-modem` (`START=95`, procd → `chain.sh`); il rinnovo si innesca con `ubus call luci.nx679j-modem doReconnect` (scrive `/tmp/nx679j-link-renew.request`), e `nx679j-link-watch.sh` esegue `renew()`: kill sessioni, nuove `dpm-session`+`wds-session` con `hold 86400`, L3 dalla sessione nuova (`ip addr add` + `route replace` + `del` vecchia), `ifup wan_early`, scrittura di `/tmp/link-health`. Relaunch manuale: symlink del toolkit in `/tmp`, poi `setsid sh "$D/chain.sh" > /tmp/chain.out 2>&1 &`. I gate `/tmp/*.once` rendono il secondo giro **non** equivalente a un boot pulito.

### Riferimenti
- libqmi / libqrtr-glib (GitLab freedesktop): issue #113, #62, #133, #7, #122, #51; MR !382 (chiusa); kernel `net/qrtr/*`: `QRTR_NS_MAX_LOOKUPS=128`, `qrtr_port_remove()`.
- ModemManager issue #361 (endpoint embedded, SDM845) e NEWS MM 1.18.x/1.20.0.
- openwrt/packages (mwan3, uqmi), openwrt/openwrt #8368; forum.openwrt.org 188925, 185556, 188956; ROOter (whirlpool.net.au).
- paldan.altervista.org (QMAP/qmi_wwan multi-PDN); postmarketOS/pmaports: rmtfs, pd-mapper, qrtr-ns.

## Fase 4: l'interfaccia utente

### Perche' un client DRM grezzo scritto a mano

Prima di scrivere codice, il controllo diretto dei feed 25.12.5 per armsr/armv8 + aarch64_generic (`ESITO-v135-v137-ui-display.md`) ha mostrato che `cog`/`wpewebkit` sono **assenti** (niente browser su DRM/KMS), `lvgl` **assente**, e `libEGL`/`libGLESv2`/`libgbm` **assenti** -> SDL2/KMSDRM inutilizzabile. Sul device: nessuna libreria grafica, nessun compositor, nessun font. La decisione e' in `DECISIONE-cog-vs-native-ui.md`: (A) LuCI vera via WebKit richiederebbe GL (nessun driver Mesa per Adreno nel feed, solo `llvmpipe`/`softpipe` software) e il path atomico di Cog non regge su questo driver vendor (l'insieme di proprieta' del plane non e' ricavabile con l'enumerazione standard, `ENOTTY` sul pageflip legacy). Scelta **(B)**: C statico aarch64 a **zero dipendenze** che riusa l'ossatura provata di `nx679j-kiosk3.c` (348 righe, pitch 4352 XR24) — ioctl DRM grezzi (`GETCRTC 0xc06864a1`, `CREATE_DUMB 0xc02064b2`, `ATOMIC 0xc03864bc`), **nessun libdrm/EGL**, dumb buffer, commit atomici `NONBLOCK` con `OUT_FENCE_PTR`, un commit in volo, fence con `poll()`. Font bitmap compilato `ui-font8x16.h` (GPL-2.0, 95 glifi).

Il vincolo che ha dettato l'architettura intera, scritto nell'intestazione di `ui-1-base.c`: *"Il pannello di questo telefono collassa se resta senza commit per piu' di ~58 ms: il loop non si ferma MAI per fare I/O"*. Quindi il **disegno e' CPU a 1080x2400** e single-threaded, a ~45 fps (`fps 45.4`, `gap_max_ms 24-25`, `fence_timeout=0`).

### Struttura: 4 categorie, 23 pagine, campi modificabili

`ui-7-menu.c`: struttura a due livelli — "4 categorie in basso (bersagli grandi per il dito), una riga di pagine sotto la barra del titolo, il contenuto sotto". `enum { CAT_STATO, CAT_RETE, CAT_MODEM, CAT_SISTEMA, CAT_N }` con `cat_npages[CAT_N] = { 7, 6, 3, 7 }` = **23 pagine**: STATO (Panor, Log sist, Log kern, Processi, Rotte, Lease, Mount), RETE (Iface, Wi-Fi, Firewall, Diag, Lease, Hostname), MODEM (Stato, SIM, Sessioni), SISTEMA (Servizi, Setup, Backup, Riavvio, Modifica, Crontab, Fuso).

`ui-8-kbd.c` aggiunge la **tastiera a schermo** ("4 righe da 10 tasti + barra comandi", `KB_Y 1420`) per modificare i campi di testo: hostname, SSID, chiave Wi-Fi, password root, lanip/netmask/dhcp_start/dhcp_limit/dns e le voci nuove (`dnnew`, `shnew`); le password sono mostrate con asterischi (`kbd_mask`).

### Pipeline dati: un file, non chiamate dirette

`nx679j-ui-fetch.sh` spiega il motivo: *"la UI deve mantenere i commit DRM continui... Ogni chiamata ubus/qmicli dura centinaia di ms e bloccherebbe il loop"*. Il daemon scrive `/tmp/ui-data.txt` (key=value) ogni 3 s, **in modo atomico** (file temporaneo + `mv`); lo stato modem costa ~1 s (5 chiamate qmicli) ed e' rinfrescato un giro su tre, tenuto in una **cache separata** `/tmp/ui-modem-cache.txt` (altrimenti il giro successivo lo cancellerebbe); cella/carrier aggregation ogni 10 cicli (~30 s) via `nx679j-cell.sh` verso `/tmp/ui-cell.txt`. Le fonti sono ubus (`luci.nx679j-modem getStatus`, `network.interface.*`, `nx679j-ui-gather.sh`), uci e qmicli. Il file ha **217 chiavi** osservate (buffer `MAXKV 512` in `ui-4-data.c`, log `"dati aggiornati: %d chiavi"`).

Il **toggle SIM** e' `A_SENS_UI 7` in `ui-5-pages.c`: un tasto "NASCONDI SIM"/"MOSTRA SIM" che copre ICCID/IMSI lasciando le ultime 3 cifre (`snprintf(sbuf, ..., "***%s", v + l - 3)`) su uno schermo sempre visibile.

### Tasti, il bug dell'orologio, la guardia di 600 ms

Il tasto laterale e' `pmic_pwrkey` su `/dev/input/eventN` (**KEY_POWER**, bit 116): log della UI `"tasto: /dev/input/event0 aperto (pwrkey/POWER, fd=5)"`. La UI cerca i **nodi per NOME** (`strstr(nm,"pmic_pwrkey")` / `pmic_resin` sui nomi in `/sys/class/input/eventN/device/name`): i nodi cambiano ordine a ogni avvio, quindi il numero non e' mai un'identita'. Semantica: volume giu' = scorre la ghiera arancione, **click singolo = standby**, **doppio click = attiva**, lungo `>= 1.5 s` = indietro (`POWER_LONG_MS 1500`, `POWER_DBL_MS 350`). VOLUME SU **non e' esposto al kernel** (`pmic_resin` dichiara solo il bit 114).

Il bug che spiego' mezza giornata: `ui-1-base.c` calcolava `now_ms()` come `(int)(tv_sec*1000+tv_usec/1000)`; nel 2026 sono ~1.79e12 ms -> overflow int -> valore **negativo** (-654699432). Due gate smettevano di funzionare: i tasti **non venivano mai aperti** (nessuna riga `tasto:` nel log) e l'anti-tocco-fantasma scartava **ogni** tocco prima di `do_action()`. Fix: `clock_gettime(CLOCK_MONOTONIC)`; l'harness host non lo vedeva perche' non chiamava `key_scan()` (ora 9 FAIL sul codice vecchio). Poiche' il nodo del tasto consegna **due esemplari per click**, una guardia di **600 ms** (`POWER_WAKE_GUARD_MS`) consume il click di risveglio: `"standby: risveglio, click consumato (nessun riarmo per %d ms)"`.

### Standby v180: schermo nero, link vivo

E' **vietato spegnere il link**: su questo tree il DPMS-off uccide il DSI command mode (`failed wait_for_idle: -110`, `wr_ptr_irq wait failed`) e il pannello non risorge senza riavvio; lo stock Android non lo fa mai (`SetDisplayState: state=0, teardown=0`). Il design v180 (`ui-6-main.c`, binario `5a8ca8b8`): standby = **un frame nero committato + backlight 0** (`/sys/class/backlight/panel0-backlight/brightness`), niente DPMS/unprepare, e il loop **continua a committare nero** tenendo il link esercitato; heartbeat ogni 10 s `"standby: loop vivo (nero) (frame %d, tasti %d/%d aperti)"`. Log chiave: `standby: frame nero committato (link vivo)`, `standby: schermo nero (link vivo)`, `standby: schermo riacceso`, `standby: luminosita' a 0`. Comando di servizio: **`/tmp/ui-standby`** (si crea il file per commutare senza il tasto).

Verifica **end-to-end con la webcam** (obbligatoria per lo stato dello schermo): off -> webcam NERA, `bl=0`, `dpms=On` (mai toccato), frame che avanzano; on -> la webcam **legge l'interfaccia** con dati live, `bl=2608`, `fence_timeout=0` (misure di appoggio: `ui-data` che cresce 11990->12019 e telemetria con `fence_timeout=0` dopo il risveglio). Lo stato remoto si legge dalla telemetria in `/tmp/display-late.log`, formato `TELEMETRIA frame=%d fps=%.1f gap_max_ms=%d fence_timeout=%d chiavi=%d pagina=%s/%s` (esempio `TELEMETRIA frame=12300 fps=45.4 gap_max_ms=24 fence_timeout=0 chiavi=217 pagina=MODEM/Stato`).

## Riferimenti

- **Impalcatura DRM**: `nx679j-kiosk3.c` (kiosk3) riusata riga per riga come base trasporto (dumb FB ruotanti, commit plane-only, pitch 4352/X24).
- **Font**: `ui-font8x16.h`, estratto dal kernel, GPL-2.0, 95 glifi.
- **Documentazione DRM UAPI/KMS**: `https://docs.kernel.org/gpu/drm-uapi.html` e `.../gpu/drm-kms.html`, citati in `RICERCA-UI-LAYER-kiosk3.md` (che confronta il tree kernel v6.12 locale `/home/user/linux-6.12`) e in `STATO-ATTUALE.md` (pitch di `drm_mode_create_dumb`).
- **Sorgenti vendor / SDE-DSI techpack** per il comportamento dello screen-off; la ricerca standby della skill `nx679j-openwrt/references/display-standby.md` cita anche la **wiki postmarketOS**.
- **Alternativa scartata** (documentata): requisiti Cog/WPE (`docs/platform-drm.md` di Cog, dipendenze del .deb Debian) in `DECISIONE-cog-vs-native-ui.md`.
- **NON trovato localmente**: nessun "kiosk sample" esterno; nessun riferimento, nei documenti della Fase 4, alle note `/home/user/re-dsi-research/` e `/home/user/idlepc_research/` (che esistono su disco con `modeset-vs-idlepc-dsi-byteclk-reparent.md`, `techpack/`, `te-tearcheck-frame-done-5.10.md`) — appartengono al filone display/touch precedente e non sono citate dai file della UI.

## Fase 5: kernel, display e scoperte

### Kernel vendor intatto + moduli vendor
Il port non ricompila **nessun kernel**: gira sul **kernel vendor intatto 5.10.66** — `Linux (none) 5.10.66-android12-9-00005-gf6e6376090be-ab8060604 #1 SMP PREEMPT Fri Jan 7 14:51:36 UTC 2022 aarch64` (`uname -a`, `STATO-ATTUALE.md:91`) — **lo stesso kernel usato da Android** (cambiano solo `kernel_size` in header e ramdisk). Le funzionalità si prendono caricando i **`.ko` dello stock** (`stock-modules/`; il vendor ramdisk e' `vendor_boot_unpacked/vendor_ramdisk/`). La scoperta del 25/09 (`vendor-modules.md`): nella chroot **nessun autoload esiste** (manca l'helper uevent), quindi ciò che il boot vendor non carica *sembra* non supportato. Il tasto laterale esige la catena `qcom-pon → pm8941-pwrkey → pmic-pon-log`; il touch `gpi.ko → i2c-msm-geni.ko → goodix_core.ko` (senza `gpi` il bus resta in deferred probe). Trap di nome: il modulo PON è `qcom-pon.ko`, **non** `qpnp-power-on.ko`.

### Il boot non ha un device tree proprio
`boot_b` e' una customizzazione di **ramdisk** dentro la partizione da 96 MiB (`boot_a`/`boot_b`) sopra il kernel stock: **non contiene un DTB**. Il tree arriva dal percorso ABL: contenitore vendor_boot (`vendor_boot_unpacked/dtb`, 3.879.360 B, **9 FDT concatenati**) con `ro.boot.dtb_idx=5`, più l'overlay **dtbo** (`dtbo_idx=35`, board-id `0x10008`; `experiments/20260917-boot-chain/XBL-ABL-DECISIONS.md`). Il running FDT resta quello vendor (`port-work/vboot-dtb-swap/stock_dump/running_fdt.dts`). Il fallback `qcom,dsi-default-panel` — che nel **fragment 35 del dtbo puntava a R66451** (pannello sbagliato!) — e' la ragione documentata del fix; **[IPOTESI]**: la catena causale esatta del fallback non e' tracciata nelle fonti locali. Fix reversibile: `qcom,dsi-default-panel = <&dsi_r66451_amoled_cmd>` → `<&dsi_nubia_r6130_amoled_cmd_dphy>`, ricostruzione DTBO (44 entry, round-trip byte-perfect), `fastboot flash dtbo_b`. Prova: la dir debugfs diventa `qcom,mdss_dsi_r6130_1080_2400_amoled_cmd`.

### R6130 in command mode: idle_pc_state e fence
Pannello Raydium **R6130** (`vtdr6130`), 1080x2400 AMOLED, **command mode**, 4 lane + DSC. Il kernel live è **pre-fix "PLL prima del parent RCG"**: `grep -c dsi_display_phy_pll_enable /proc/kallsyms` = **0**. Catena: `WARN_CLK "rcg didn't update its configuration."` + `-EBUSY` (`clk-rcg2.c:111-134`) → `sde_encoder_prepare_for_kickoff: resource kickoff failed`; l'init al boot avviene con lo splash attivo (reparent saltato) e funziona, poi **58 ms** di idle (`IDLE_POWERCOLLAPSE_DURATION`) portano al collasso del link (**causalita' dichiarata 'da verificare' nelle fonti**). Rete userspace: `idle_pc_state` sul CRTC (id 166, `2=idle_pc_disable`). Con 2 i fence completano (59.9–61.2 fps, 0 in ritardo); col default 0 → **200 fence consecutivi in ritardo** e pannello morto fino al reboot. Va scritta **prima del primo commit** (il launcher fa `touch /tmp/ui-idle-pc-disable` prima della UI). Un commit reale **bloccante** ha prodotto `[drm:_sde_encoder_phys_cmd_handle_wr_ptr_timeout:1550] [sde error]enc55 intf1 wr_ptr_irq wait failed, switch_te:0` e `...ppdone_timeout:500 ... kickoff timed out ctl 0 koff_cnt 2`, poi `panic(__func__)` (DEFAULT_PANIC 1) → bite watchdog. Da lì il design atomico **a ioctl grezzi**: solo `NONBLOCK (0x200)` + `OUT_FENCE_PTR`, un commit in volo, `TEST_ONLY (0x500)` prima, `SET_CLIENT_CAP(ATOMIC)` obbligatoria (`drm_atomic_uapi.c:1306`), e **fence timeout 100 → 40 ms** (100 ms superava da sola la soglia di collasso). Verifica: `fps 45.4`, `gap_max 25 ms`, `fence_timeout=0`.

### Touch Goodix GT9897
L'init del driver è **identica riga per riga** al log stock: `[GTP-INF][goodix_ts_core_init:2578] Core layer init:v1.2.2`, `[GTP-INF][brl_read_version:725] rom_pid: BERLIN`, `pid: 9897`, `[GTP-INF][print_ic_info:916] FW-State: 0x102DC, 36`; `[GTP-ERR][goodix_parse_dt:1322] can't find valid panel` è **benigno** (lo stampa anche lo stock: `active_panel=NULL` su entrambi). Lo stato vero: l'hardware risponde sul bus ma **non consegna eventi** (IRQ ~4 statico, 0 byte su eventN mentre si tocca); il fingerprint condivide il controller e `fp_switch` non si azzera. **Mai** toccare i nodi `fwupdate/result`, `get_rawdata`, `esd_info`: fanno crashare il kernel.

### Standby: mai unprepare
Il DPMS-off su questo tree **uccide il DSI command link** — `failed wait_for_idle: -110` / `wr_ptr_irq wait failed` — e non risorge senza riavvio. Lo stock Android non lo fa mai: `SetDisplayState: state=0, teardown=0`, il display resta preparato. Fix finale (v180, verificato con webcam): **non spegnere mai il link** — un frame nero + `bl_set(0)` su `/sys/class/backlight/panel0-backlight/brightness`, niente DPMS né unprepare, il loop continua a committare; su AMOLED i pixel neri sono spenti fisicamente.

### Riferimenti
Fonti esterne citate nei documenti locali per questa fase:
- `github.com/ztemt/NX679S` (e NX709S) — sorgenti **ufficiali ZTE/Nubia** (5.10.101): `display-drivers.zip`, `msm/dsi/dsi_display.c`, `dsi_panel.c`; pinned `0280bdce…`.
- `git.codelinaro.org/clo/la/platform/vendor/opensource/display-drivers` (peer CLO 5.10) e il commit CLO **`13d0d423af78`** *"turn on the PLL before switching RCG parent during clk on"*.
- `docs.kernel.org/gpu/drm-kms.html`, `docs.kernel.org/gpu/drm-uapi.html`, `kernel.org/doc/html/v5.10/gpu/drm-uapi.html`; `github.com/gregkh/linux/blob/v5.10.66/…` (`clk-rcg2.c`, `drm_file.c`).
- `github.com/TheGammaSqueeze/GammaOSNextDistribution` — `hw_device_drm.cpp` (SDM SM8450).
- Techpack **SDE/DSI** vendor, **siêu DRM v5.10** e **wiki postmarketOS** (fonti dichiarate in `display-standby.md`).
- Driver mainline: `drivers/gpu/drm/panel/panel-visionox-vtdr6130.c`; **Goodix** `goodix_berlin_core.c` + `goodix_berlin_{i2c,spi}.c` (merged 2024; SPI già gt9897, I2C no → ~3 righe).
- XML pannello dall'ABL: `full_extracted_v311/uefi.img.dump/…/Panel_r66451_60hz_fhd_plus_dsc_cmd.xml/body.bin`.

**Non trovato localmente:** nessun thread LKML/`lore.kernel.org`/`freedreno@lists.freedesktop.org` per display o touch (compare solo `freedreno/msm` come driver Mesa in `DECISIONE-cog-vs-native-ui.md`); la wiki postmarketOS è citata **senza URL**; nessun link a un repo "Goodix driver" esterno (il driver è citato come file mainline o come `goodix_core.ko` vendor); il sorgente **esatto NX679J 5.10.66 non esiste localmente** (tree pubblica 5.10.101; `kernel-patch-test/stock-kernel-source/Makefile` dichiara 5.4.242).

## Crediti e riferimenti: strumenti e codice di terzi

> **Convenzione.** Le licenze indicate sono quelle **note a monte** (non dedotte dai documenti del progetto); dove non sono note si scrive `(non specificato localmente)`. Gli artefatti vendor Qualcomm/Nubia (kernel, moduli `.ko`, firmware, DTBO, immagini di partizione) sono **copie locali estratte dall'unità in nostro possesso**: non vengono redistribuiti e restano soggetti alle licenze del produttore. Sono elencate anche le alternative **valutate e scartate**, con la ragione, perché la decisione fa parte del lavoro.
>
> **Nota di revisione (29/09/2026).** In questa copia pubblica quegli artefatti **non sono presenti**: sono stati rimossi, insieme ai file che portavano un'intestazione di riservatezza del produttore (`Confidential and Proprietary`), alle copie del rootfs OpenWrt e a una chiave privata di test. L'elenco completo di ciò che è stato rimosso è nella sezione «Revisione per la pubblicazione» di questo documento.

---

### 1. Distribuzione di base, rootfs e servizi

- **OpenWrt 25.12.5 (`r33051-f5dae5ece4`), target `armsr/armv8`, `aarch64_generic` (musl)**
  - Uso: distribuzione di base del port sullo slot B; rootfs consegnata dal ramdisk, init, servizi, LuCI, gestione dei pacchetti via `apk`.
  - Origine: <https://openwrt.org> · <https://github.com/openwrt/openwrt> · <https://downloads.openwrt.org/releases/>
  - Licenza: GPL-2.0 (ogni pacchetto mantiene la propria licenza).

- **OpenWrt SDK 25.12.5 armsr/armv8 (`gcc-14.3.0_musl`, `aarch64-openwrt-linux-gcc`)**
  - Uso: cross-compilazione di ModemManager e libqmi patchati, della UI C, dei tool DRM/touch e dei moduli del progetto.
  - Origine: <https://downloads.openwrt.org/releases/25.12.5/targets/armsr/armv8/>
  - Licenza: GPL-2.0.

- **Toolchain di sistema `aarch64-linux-gnu-gcc` (glibc, `-static`)**
  - Uso: fallback usato dai documenti quando l'SDK OpenWrt non era presente; i binari glibc statici girano sul rootfs musl del device (verificato). Tool host per le sonde DRM (`drm-probe`, `drmprops`, `plane78-scanout-once`).
  - Origine: GNU/Linux — cross-toolchain della distribuzione host.
  - Licenza: GPL-3.0 (con runtime exception).

- **Componenti OpenWrt di sistema: `procd`, `ubus`/`ubusd`, `uci`, `netifd`, `rpcd`, `uhttpd`, `libubox`, `libubus`, `libuci`, `libblobmsg-json`, `libjson-c`, `jshn`, `kmodloader`, `ujail`**
  - Uso: init e supervisione dei servizi, bus di stato (`ubus call network.interface.modem status`), configurazione persistente (`uci`), pagine LuCI, hotplug.
  - Nota: la UI sul display legge lo stato via **ubus** (`network.interface.*`, `system.*`, `file`/`rpcd-mod-file`, `luci.setPassword`); `ujail` è stato **disattivato** perché i jail procd falliscono su questo kernel vendor.
  - Origine: <https://github.com/openwrt/openwrt> (repo satellite `openwrt/rpcd`, `openwrt/netifd`, `openwrt/ubox`)
  - Licenza: GPL-2.0 / LGPL-2.1 secondo il componente.

- **`apk` / apk-tools 3 (formato feed OpenWrt ≥ 25.12)**
  - Uso: installazione e allineamento dei pacchetti `luci-*` sul device; il device parte dal 1970, quindi `apk` richiede prima la sincronizzazione NTP e i feed su `http://` (il `wget` busybox non completa il TLS).
  - Origine: <https://gitlab.alpinelinux.org/alpine/apk-tools>
  - Licenza: GPL-2.0.

- **`apk-tools-static` 3.0.8 (Alpine `edge`, x86_64)**
  - Uso: decodifica dei `packages.adb`/`.apk` OpenWrt per ottenere dimensioni e dipendenze reali (usato per la stima di ingestibilità di Cog/WPE).
  - Origine: <https://dl-cdn.alpinelinux.org/alpine/edge/main/x86_64/apk-tools-static-3.0.8-r0.apk>
  - Licenza: GPL-2.0.

- **`busybox` (OpenWrt) e busybox del vendor**
  - Uso: shell del rootfs; applet limitate (niente `stat`, `od`, `timeout`, `pkill`, `top`, `modetest`, `mdev`) — vincolo che ha determinato molti script del progetto.
  - Origine: OpenWrt / <https://busybox.net>
  - Licenza: GPL-2.0.

- **`dropbear` (client/server SSH)** — Uso: accesso al device via USB gadget (10.0.0.1, rtt 3 ms) e Wi-Fi (192.168.77.1); host key rigenerata a ogni boot. Origine: <https://matt.ucc.asn.au/dropbear/dropbear.html> · Licenza: (non specificato localmente).

- **`dnsmasq`, `hostapd`/`wpad-basic-mbedtls`, `iw`, `iwinfo`, `wireless-regdb`, `wifi-scripts`, `ucode-mod-*`**
  - Uso: AP Wi-Fi 5 GHz `NX679J-TEST` sul QCA6490, DHCP LAN, watchdog e riavvio di dnsmasq dopo l'AP.
  - Origine: feed OpenWrt `base`/`packages` · Licenza: GPL-2.0 / BSD-3-Clause (hostapd) secondo il componente.

- **`firewall4` / `nftables`** — valutati e **non usabili**: il kernel vendor non ha `nf_tables` (`nft: cache initialization failed`), quindi `fw4` non parte e il launcher non lo tocca. Origine: <https://github.com/openwrt/firewall4> · Licenza: (non specificato localmente).

- **`iptables-legacy` 1.8.11 da Alpine aarch64/musl (`xtables-legacy-multi`, `/usr/lib/xtables/*.so`, `libxtables`/`libip4tc`/`libip6tc`)**
  - Uso: NAT del WAN condiviso SIM → LAN Wi-Fi (il kernel ha `xtables` built-in ma non il userspace iptables).
  - Origine: pacchetto Alpine Linux · Licenza: GPL-2.0.

- **LuCI + `luci-proto-modemmanager` (versione allineata `26.263.44884~0834d09`)**
  - Uso: interfaccia web (uhttpd bindato **solo** su 10.0.0.1 e 192.168.77.1), pagina `Status → Cellular Network`, descrittore del proto; i file JS sul device sono minificati, quindi le citazioni di riga si fanno sul sorgente upstream.
  - Origine: <https://github.com/openwrt/luci> · Licenza: Apache-2.0.

---

### 2. Strumenti host di build e impacchettamento

- **Python 3** — Uso: tutti i builder (`build-vNN.py`, `build-v90.py`), gli strumenti (`goodix-cmdline-path.py`, `dts-on-command-to-txcmd.py`, `luci-mm-tokcmp.py`) e le verifiche. Origine: <https://www.python.org> · Licenza: PSF-2.0.

- **GNU `cpio`** — Uso: creazione/estrazione degli archivi `newc` del ramdisk e del rootfs (`find . | LC_ALL=C sort | cpio -o -H newc`, `cpio -idmu`); i layer sono **concatenati** (l'ultima voce vince) e le voci `uid/gid` riscritte a 0. Origine: GNU project · Licenza: GPL-3.0.

- **`lz4` (CLI, `lz4 -l -9` = frame *legacy*)**
  - Uso: compressione del ramdisk dell'immagine `boot_b`; distinzione critica fra LZ4 **legacy** (`02 21 4c 18`) e **frame** (`04 22 4d 18`).
  - Origine: <https://github.com/lz4/lz4> · Licenza: BSD-2-Clause.

- **Magisk / `magiskboot` 30.7**
  - Uso: unpack/repack delle immagini boot (`magiskboot unpack -h`, `magiskboot repack`) e compressione `compress=lz4_legacy` nella prima fase del port (`port-work/openwrt-phase4-*`); l'immagine **Magisk-patchata su slot A** è la baseline/oracolo che avvia Android e serve da controllo a una variabile.
  - Origine: <https://github.com/topjohnwu/Magisk> · Licenza: GPL-3.0.

- **Header AOSP di boot image (`system/tools/mkbootimg/include/bootimg/bootimg.h`, `boot_img_hdr_v3/v4`, `vendor_boot_img_hdr_v4`)**
  - Uso: interpretazione/riscrittura dell'header `ANDROID!` (44 byte fissi, cmdline 1536 B su v3 e 4096 B su v4 — sfruttato da `goodix-cmdline-path.py` per `firmware_class.path=`), tabella ramdisk del `vendor_boot`.
  - Origine: AOSP — <https://android.googlesource.com/platform/system/tools/mkbootimg> · Licenza: Apache-2.0.

- **`avbtool`** — Uso: ispezione delle immagini `vbmeta` nella fase di ricostruzione della catena di boot. Origine: AOSP (external/avb) · Licenza: Apache-2.0.

- **`dtc` (device tree compiler)** — Uso: decompilazione del FDT vivo (`dtc -I dtb -O dts -f`) e ricostruzione dell'immagine **DTBO** per il fix `qcom,dsi-default-panel` del fragment 35 (round-trip md5 verificato prima del flash). Origine: <https://git.kernel.org/pub/scm/utils/dtc/dtc.git> · Licenza: GPL-2.0 / BSD-2-Clause (doppia).

- **`qemu-aarch64` / qemu-user** — Uso: prova locale della catena di boot e dei moduli **prima** del flash (`qemu-aarch64 -L <root> busybox echo OK`); i suoi verdetti su shebang non valgono come verdetti del kernel. Origine: <https://www.qemu.org> · Licenza: GPL-2.0.

- **Strumenti host di servizio**: `dd` + `sync -f`/`drop_caches` + `md5sum`/`sha256sum` (protocollo di flash con due letture fredde), `nc` (lettura dello snapshot dal relè v9), `ssh`/`scp -O`, `curl`, `jq`. Licenze: GPL-2.0/GPL-3.0 (coreutils, netcat, openssh) salvo dove diversamente indicato — `(non specificato localmente)` per i singoli pacchetti.

- **`opencli` — Browser Bridge (`/usr/bin/opencli`)**
  - Uso: verifica delle pagine LuCI con un browser reale (`opencli --profile pdc8925e browser <sess> open|state|screenshot`), senza chiedere screenshot all'utente.
  - Origine: `/usr/bin/opencli` (strumento di ambiente) · Licenza: (non specificato localmente).

---

### 3. Modem, QMI e datapath

- **`libqmi` / `qmicli` — versione sul device 1.36.0** (master OpenWrt nel 2026: 1.38.0)
  - Uso: lettura stato modem su QRTR (`qmicli -p -d qrtr://0`, NAS/DMS/UIM), origine della pagina Cellular scritta da noi; `--wds-*` come riferimento per il bearer.
  - **Questioni di versione/collezione** (verificate): i fix di memoria `93e65e4`+`fd79fd3` (OOB read nel path QRTR, issue #133) **non sono in nessuna release**; `--client-cid`/`--client-no-release-cid` **non attraversano i processi** su QRTR (MR !382 chiusa); `qmi-network` è **pericoloso** qui (riscrive il data-format WDA senza verificare il lato kernel) e quindi **non va usato**; `-p`/`qmi-proxy` non è la risposta alla serializzazione (issue #113 aperta, `ClientIdsExhausted` con #62).
  - Origine: <https://gitlab.freedesktop.org/mobile-broadband/libqmi> · <https://github.com/linux-mobile-broadband/libqmi> · Licenza: LGPL-2.1+.

- **`libqrtr-glib` — 1.2.2-3** — Uso: trasporto QRTR dei QMI, timeout della lookup di bus **1 s per processo**, lettura del name service in-kernel. Bug noti non corretti (`f4fd658` UAF in `qrtr_node_remove_service_info`, issue #7) — da trattare come crash possibile. Origine: <https://gitlab.freedesktop.org/mobile-broadband/libqrtr-glib> · Licenza: LGPL-2.1+.

- **`qmi-proxy`** — Uso: valutato per condividere un device QMI fra più client; presente sul device (visti anche processi `qmi-proxy` residui) e usato come tramite dalle invocazioni con `-p`, ma **non serializza**: le invocazioni `qmicli` separate non condividono il CID (race #113 aperta; `Unknown client 1`). Origine: parte di libqmi · Licenza: LGPL-2.1+.

- **`ModemManager` 1.24.0 (+ `libmm-glib`, plugin `qcom-soc`) — adottato e poi RIMOSSO**
  - Uso: fase standard (v63→v90) di controllo del modem e pagina LuCI; poi **decisione del 24/09 di eliminarlo** (−179 s sull'oggetto modem, −324 s di cleanup, via il rischio `Carrier: Absent`), con la sola funzione da conservare (riconnessione su deattivazione dell'operatore) riassegnata al nostro supervisor.
  - Patch nostre ispirate a sorgenti terzi: `0100` (net virtuali), `0101` (skip data-format su QRTR), `0102` (rmnet come data port), `0200/0201` (`net_driver`→`ipa`, multiplex REQUIRED), `0202` (link port timeout 10 s), `0203` (`iflink`), `0204` (nome `qmapmux` senza punto); regola udev `80-mm-<device>.rules`.
  - Origine: <https://gitlab.freedesktop.org/mobile-broadband/ModemManager> · <https://github.com/linux-mobile-broadband/ModemManager> · Licenza: GPL-2.0+ / LGPL-2.1+.

- **`qualcomm-linux/meta-qcom` — 6 patch QTI del datapath (`0001`…`0005`)**
  - Uso: **modello** per le nostre patch (fallback EMBEDDED per driver non riconosciuto su QRTR = MR !1452, QRTR+MHI = MR !1443, BindMuxDataPort, DPM open port, `sio_port_per_port_number`); file conservati in `qti-patches/`.
  - Origine: `qualcomm-linux/meta-qcom` (`recipes-connectivity/modemmanager/files/`, branch master) · Licenza: (non specificato localmente).

- **`meizu-m2172-mainline/ModemManager` (fork athbe)** — Uso: fonte dei commit `95882e2` (fallback su generic QMI data port), tag `ID_MM_QMI_FIXED_MUX_ID` (`9e407303`), `ID_MM_QMI_DEFAULT_MULTIPLEX` (`18105f69`), branch `sdx55m-mhi-wds-mux-1.24`; le regole udev meizu confermano il nostro pattern `ID_MM_PHYSDEV_UID=qcom-soc`. Origine: <https://github.com/meizu-m2172-mainline/ModemManager> · Licenza: (non specificato localmente, derivata da ModemManager).

- **`libudev-zero` (OpenWrt, 1.0.5, Daniel Golle)** — valutato e **scartato**: solo `libudev.pc`, nessun `gudev-1.0` (MM richiede ≥ 232), nessun demone; OpenWrt pinna MM con `-Dudev=false` e MM fa il parsing interno delle regole udev (parser dichiaratamente "non completo"). Origine: <https://github.com/openwrt/packages> (`libs/libudev-zero`) · Licenza: (non specificato localmente).

- **`uqmi` (OpenWrt) e il demone `uqmid` (in-tree)** — Uso: **valutati e scartati**: `uqmi` parla QMI solo su `/dev/cdc-wdm*`, **zero hit su QRTR**; `uqmid` idem e non pacchettizzato in 25.12. Origine: <https://github.com/openwrt/uqmi> · <https://lxr.openwrt.org/source/uqmi/uqmid/uqmid.c> · Licenza: GPL-2.0.

- **`wwand` (ddimension, ucode)** — valutato e scartato: solo USB `qmi_wwan*`/MHI, rifiuta i driver non USB/MHI (`discovery.uc`), zero QRTR nel codice. Origine: <https://github.com/ddimension/wwand> · Licenza: (non specificato localmente).

- **Altri candidati OpenWrt valutati e scartati** (letti nel sorgente, non nelle descrizioni): `QModem` (FUjr — manager AT, QRTR compilato fuori), `qminfo` (modemfeed — `g_file_new_for_path()`, nessun URI), `modemdata`/`md_uqmi` (usa `uqmi`), `luci-proto-qmi` (match `/^cdc-wdm/`), `luci-app-5gmodem` (gate su character device). Licenze: (non specificato localmente).

- **`rmtfs`, `tqftpserv`, `pd-mapper` (linux-msm) — USATI**
  - Uso: servitori del modem sul device: `rmtfs` (EFS da `modemst1/2`, `fsg`, `fsc` — nel log `[RMTFS] open /boot/modem_fs1`), `tqftpserv` (root `/rfs`, serve i file che il modem chiede durante il bootup), `pd-mapper` (servizio 64 `tms/servreg` → `msm/modem/root_pd`). Ordine corretto: **tqftpserv prima del `start` del remoteproc**.
  - Origine: <https://github.com/linux-msm/tqftpserv> (e `rmtfs`, `pd-mapper` dello stesso namespace) · Licenza: **BSD-3-Clause** (i file `LICENSE` sono inclusi in questo archivio: `09-albero-originale/experiments-extra/refs/{tqftpserv-master,rmtfs-master,pd-mapper}/LICENSE`).

- **postmarketOS / pmaports — MR 3269 e port `postmarketos-blackshark-klein` (CaullenOmdahl)**
  - Uso: fonte dell'ordine dei servitori ("Ideally tqftpserv should be started before rmtfs…") e della tecnica di *forced EE transition* su MHI (`docs/modem/troubleshooting.md`).
  - Origine: <https://git.askiiart.net/askiiart/pmaports/src/commit/e60195f8ae51abe422b3ccda685c1e3612209130/modem/tqftpserv> · <https://github.com/CaullenOmdahl/postmarketos-blackshark-klein> · Licenza: (non specificato localmente).

- **`qrtr-ns` userspace, `diag-router`** — valutati e **non necessari**: su kernel 5.10 il name service QRTR è **in-kernel**; `diag-router` serve al routing DIAG, non a QMI+dati (in pmaports disabilitato di default). Origine: namespace `linux-msm` / pmaports · Licenza: (non specificato localmente).

- **`mwan3` e `watchcat`** — valutati come meccanismo di riconnessione e **scartati**: `mwan3` reagisce solo a eventi netifd `ifup/ifdown/connected/disconnected` e fa failover, non conserva il bearer. Origine: <https://github.com/openwrt/packages> (`net/mwan3`, `network/services/watchcat`) · Licenza: (non specificato localmente).

- **ROOter Connection Monitor (`ofmodemsandmen`) e script `10-report-down`**
  - Uso: **prior art** per il supervisor di riconnessione (isteresi ping "Interface Down/Up", azioni log/reboot/reconnect/toggle; monitor basato sullo stato MBIM nella versione successiva). Il dispatcher `10-report-down` di ModemManager+OpenWrt è il riferimento della notifica di disconnessione a netifd.
  - Origine: <https://ofmodemsandmen.com/monitor.html> · <https://github.com/ROOterDairyman/ROOter> · <https://raw.githubusercontent.com/openwrt/packages/master/net/modemmanager/files/usr/lib/ModemManager/connection.d/10-report-down> · Licenza: (non specificato localmente).

- **`libmbim`** — Uso: consultato per la semantica del multiplexing bearer (`MaxActiveMultiplexedBearers`) e per il confronto ECM/MBIM vs ping. Origine: <https://github.com/linux-mobile-broadband/libmbim> · <https://modemmanager.org/docs/libmbim/mbim-protocol> · Licenza: LGPL-2.1+.

---

### 4. Componenti vendor Qualcomm/Nubia riusati (artefatti locali del dispositivo)

> **Nota di revisione (29/09/2026).** In questa copia pubblica questi artefatti **non ci sono**. Le voci seguenti descrivono il tree di lavoro locale da cui il materiale è stato rimosso: restano come traccia di cosa il port usa e di come si ri-estrae (`ESTRAZIONE-BLOB.md`).

- **Kernel vendor Qualcomm/Nubia `5.10.66-android12-9-…` (immagine `boot` stock)** — Uso: **kernel della porta** (nessuna patch al kernel per display e modem); ricostruito solo nell'header/ramdisk. Origine: partizione stock dell'unità (`boot_a`/`boot_b`, vermagic `5.10.66-gki-g491fe99db339`). Licenza: GPL-2.0 (kernel) — artefatto locale non ridistribuito.
- **Moduli vendor estratti dal device** — `panel_event_notifier.ko`, `msm_ext_display.ko`, `gpi.ko`, `i2c-msm-geni.ko`, `goodix_core.ko`, `qcom-pon.ko`, `pm8941-pwrkey.ko`, `pmic-pon-log.ko`, `pmic_glink.ko`, `qti_battery_charger.ko`, `charger-ulog-glink.ko`, `ucsi_glink.ko`, lo stack `cnss`/`qca_cld3_qca6490.ko`, `rmnet_*`/`ipam`/`ipa_*`, `smp2p.ko`, `qcom_q6v5_pas.ko`, `mdt_loader`… — Uso: display, touch, tasto laterale, alimentazione/Type-C, Wi-Fi, modem e datapath. Origine: `vendor_ramdisk00` / `/vendor/lib/modules` dell'unità. Licenze: GPL-2.0 e licenze Qualcomm (artefatti locali, non ridistribuiti).
- **Firmware vendor** — `modem.mdt`/`modem.b*` e firmware X65 (`/rfs` con `modem_pr`, 165 file), `qca6490/{amss20,amss,bdwlan.elf,bdwlang.elf,regdb,m3}.bin`, firmware touch `goodix_firmware.bin` e `goodix_cfg_group.bin` — Uso: avvio di MSS/adsp/cdsp/slpi e del Wi-Fi; il firmware touch è iniettato in `/lib/firmware` del **root vero** (`/proc/1/root/lib/firmware`) con `firmware_class.path=` aggiunto alla cmdline. Origine: partizioni `modem_a` (`/dev/block/sde6`, `/vendor/firmware_mnt`), `/rfs` e rootfs Android stock. Licenza: proprietaria Qualcomm/Nubia — artefatti locali, non ridistribuiti.
- **DTB/DTBO vendor** — `dtb_idx=5`, `dtbo_idx=35`; il DTBO dello slot B è stato modificato in **una sola entry** (`qcom,dsi-default-panel`: `dsi_r66451_amoled_cmd` → `dsi_nubia_r6130_amoled_cmd_dphy`), ricostruito con `dtc` e flashato con round-trip byte-perfect. Origine: partizione `dtbo`/`vendor_boot` dell'unità. Licenza: (artefatto vendor locale).
- **`xbl_a`/`abl_a`/`uefi_a` stock Nubia `NX679J_Z69_UN_ZML1S_V311`** — Uso: studio della catena di boot (LLVM da `android.googlesource.com/toolchain/llvm-project` per l'identificazione del `LinuxLoader`); conferma che **la firma AVB non è applicata** con bootloader sbloccato. Licenza: proprietaria.
- **`crashlog-dump.sh` (vendor)** — Uso: unico canale che sopravvive a un reset silenzioso (scrive il tail di `dmesg` ogni 3 s negli slot del `rawdump`); il progetto ci ha aggiunto blackbox/watcher su slot 409/410.
- **RAWDUMP / misc / GPT** — Uso: canale di diagnosi e di persistenza; nota: il `rawdump` viene **azzerato al boot** e non è affidabile per la persistenza (approccio v60 falsificato).

---

### 5. Display, DRM/KMS e sorgenti Qualcomm consultate

- **Documentazione DRM/KMS e DRM UAPI del kernel**
  - Uso: riferimento normativo per `CREATE_DUMB`/`pitches[]` (il pitch reale è **4352** contro `1080*4=4320`: causa del glitch del vecchio kiosk), flag atomici (`TEST_ONLY`, `NONBLOCK`, `ALLOW_MODESET`, `PAGE_FLIP_EVENT`), fencing esplicito (`OUT_FENCE_PTR`), `lastclose` vs rilascio del master.
  - Origine: <https://docs.kernel.org/gpu/drm-kms.html> · <https://docs.kernel.org/gpu/drm-uapi.html> · <https://www.kernel.org/doc/html/v5.10/gpu/drm-uapi.html> · <https://github.com/gregkh/linux/blob/v5.10.66/drivers/gpu/drm/drm_file.c#L450-L453> · <https://github.com/gregkh/linux/blob/v5.10.66/drivers/clk/qcom/clk-rcg2.c#L100-L147> (handshake RCG / `CMD_UPDATE`, poll 500×1 µs)
  - Licenza: documentazione kernel (GPL-2.0 / CC-BY-SA per la doc).

- **Albero stable Linux `6.12` locale e alberi di riferimento peer (`re-cmdmode/*_510.c`, `lop_sm8450`, `linux-6.8`, `linux-6.11`)**
  - Uso: studio del loop atomico, del kickoff SDE, del fence e del power-collapse **per analogia**, senza attribuire al kernel live ciò che non è verificato byte-per-byte.
  - Origine: <https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git> (checkout locale `/home/user/linux-6.12`) · Licenza: GPL-2.0.

- **Sorgenti display vendor Qualcomm (`techpack` SDE) e comparabili pubblici**
  - Uso: ricostruzione della ricetta di commit dell'HAL (`SetupAtomic`: `PLANE_SET_FB_ID+SET_CRTC` ogni frame, `CRTC_SET_MODE` solo al first-cycle) e delle proprietà CRTC (`idle_pc_state`, `autorefresh`, `frame_trigger_mode`); la sorgente locale dichiara **5.4.242**, la tree pubblica ZTE/NX679S **5.10.101** → usate come ipotesi di flusso, mai come prova del kernel live 5.10.66.
  - Origine: `re-nubia-disp/src` (locale) · <https://github.com/ztemt/NX679S/tree/0280bdce975602ff17161f5de1b8fc63dc96bd47> (`display-drivers.zip`, `msm/dsi/dsi_panel.c`) · <https://git.codelinaro.org/clo/la/platform/vendor/opensource/display-drivers/-/blob/ac17a22157f56a438e24009e4fa91a69100612ef/msm/dsi/dsi_display.c#L1706-1907> · <https://github.com/TheGammaSqueeze/GammaOSNextDistribution/blob/0de890dede9a550da87920d61b4b08da7c80fb02/hardware/qcom-caf/sm8450/display/sdm/libs/core/drm/hw_device_drm.cpp>
  - Licenza: GPL-2.0 / licenze Qualcomm (sorgenti vendor).

- **`kmscube`, `weston`, `cog`/WPE WebKit, `wpebackend-fdo`, `libwpe`, `cage`, `wlroots`, `libsdl2`, Mesa (`llvmpipe`/`softpipe`), `libdrm`, `libgbm`, `libinput`, `libegl`/`libglesv2`, LVGL** — **alternative valutate e scartate** per la UI sul pannello: motore non pacchettizzato per `aarch64_generic` (configure di WebKit fallito nel buildbot), ~50–70 MB compressi contro 6,7 MiB liberi in `boot_b`, GL solo software, e il legacy page-flip di Cog è `ENOTTY` su questo driver; `kmscube` lanciato **senza master attivo** ha causato 154 fault SMMU e il blocco del device (incidente documentato, da non ripetere). Origine: <https://gitlab.freedesktop.org/mesa/kmscube>, <https://gitlab.freedesktop.org/wayland/weston>, <https://wpewebkit.org>, <https://github.com/Igalia/cog>, <https://lvgl.io> · Licenze: MIT/custom secondo il progetto `(non specificato localmente)`.

- **Project Mu (`mu_aloha_platforms-main`, `sm8450-mainline/Mu-Qcom`)** — Uso: riferimento per il bootloader/firmware UEFI nella fase di studio della catena; l'unità ha anche una copia locale in `verified-port/`. Origine: <https://github.com/microsoft/mu_aloha_platforms> · Licenza: (non specificato localmente).

- **`linux-msm.github.io/mainline-status` (SM8450)** — Uso: stato del supporto mainline per display DSI/DP/GPU, come orizzonte del lavoro futuro. Origine: <https://linux-msm.github.io/mainline-status/soc/sm8450> · Licenza: (non specificato localmente).

- **`panel-visionox-vtdr6130.c` (mainline `drivers/gpu/drm/panel/`)** — Uso: esistenza e riferimenti del driver **mainline** del pannello (compatible `visionox,vtdr6130`, DSC aggiunto nel 2026, default su SM8550/8650-QRD) come alternativa futura al driver vendor. Origine: <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git> · Licenza: GPL-2.0.

---

### 6. Touch Goodix — sorgenti consultate

- **Mainline `goodix_berlin_core.c` + `goodix_berlin_{i2c,spi}.c`** (merge 2024, in `kernel-patch-test/linux-6.11/drivers/input/touchscreen/`) — Uso: riferimento per la patch ~3 righe che aggiungerebbe il GT9897 su I2C (`gt9897_data` con `fw_version_info_addr=0x1000C`, `ic_info_addr=0x10068` + compatible in `goodix_berlin_i2c.c`); il bind Berlino-A è già documentato, l'I2C no → sarebbe il primo device pubblico GT9897-su-I2C. Licenza: GPL-2.0.
- **Sorgente driver vendor Nubia `goodix_berlin_driver_v1.0.1` (`goodix_brl_i2c.c`, `goodix_brl_spi.c`)** in `stock-kernel-source/drivers/nubia/touch/` — Uso: confronto **riga per riga** dell'init con lo stock (identico, incluse le due `[GTP-ERR]` benigne `can't find valid panel` e `failed get panel-max-p, use default`) → il guasto è nel **runtime**, non nell'init. Licenza: GPL-2.0 (sorgente vendor pubblicato) — artefatto locale.
- **Driver vendor `goodix_core.ko` e alias `i2c:gtx8_i2c` / `platform:nubia_goodix_ts`** — Uso: il driver **effettivamente** in uso sul device; caricato in catena `gpi.ko → i2c-msm-geni.ko → goodix_core.ko` (senza `gpi.ko` il probe I2C resta in deferred probe per sempre). Stato misurato: l'hardware risponde sul bus ma **non consegna eventi**. Licenza: (driver vendor, artefatto locale).
- **Nodi debugfs vendor del driver touch** (`fwupdate/result`, `get_rawdata`, `esd_info`, `nbsp_mode`, `bsp_mode`) — Uso: sondati e **dichiarati da non leggere** (un read ha coinciso con un crash del kernel). `(non specificato localmente)`.

---

### 7. Input, UI e font

- **Interfaccia kernel `uinput` (`/dev/uinput`, major 10 minor 223)** — Uso: iniezione di un tocco **vero** per verificare il percorso tocco→azione senza dita; `/dev` è una directory del ramdisk (non `devtmpfs`), quindi il nodo si crea con `mknod`. Licenza: GPL-2.0 (kernel).
- **`uinput-touch.c`, `touch-selftest.c`, `ui-preview.c`/`ui-preview2.c`** — **codice del progetto**, scritto su questa ABI: iniettore di tocchi, self-test di `touch_scan`+`poll_touch`+`hit_test` senza aprire il DRM, e rendering delle pagine in PPM per guardarle prima di toccare il pannello. (Non è codice di terzi.)
- **`nx679j-kiosk3.c` (e prima `nx679j-kiosk2.c`, `nx679j-atom10..17.c`, `nx679j-drmtest.c`) — codice del progetto**
  - Uso: client DRM di boot, poi **base della UI** (`nx679j-ui.c` ne riusa l'ossatura: 2 dumb FB ruotanti, commit atomico plane-only `NONBLOCK` + `OUT_FENCE_PTR`, un commit in volo, poll del fence, touch da `/dev/input/eventN`).
  - **Chiarimento**: `kiosk3` non è codice di terzi e non ha licenza esterna; deriva dalle sonde `atom*` del progetto. La UI finale (`ui-1-base.c` … `ui-7-*.c` → `nx679j-ui.c`) e i suoi strumenti (`nx679j-ui-fetch.sh`, `nx679j-ui-gather.sh`, `nx679j-ui-sample.sh`, `drm-probe.c`) sono **opera del progetto**.
- **Font bitmap del kernel — `ui-font8x16.h` (95 glifi)**
  - Uso: disegno testo della UI senza dipendenze grafiche, in dumb buffer DRM.
  - Origine/derivazione: **estratto dal font bitmap del kernel** (`lib/fonts/font_8x16.c`).
  - Licenza: **GPL-2.0** (dichiarata nei documenti del progetto).
- **`libfreetype` + `harfbuzz`** — valutati come alternativa per il testo e **non usati** (scelto il font bitmap). Origine: feed OpenWrt · Licenza: FTL / MIT `(non specificato localmente)`.

---

### 8. Verifica visiva e strumenti di misura

- **OBS Studio + `obs-websocket` v5** — Uso: cattura del video source della webcam puntata sul telefono per giudicare cosa mostra **davvero** il pannello (verifica primaria, pretesa dall'utente). Origine: <https://obsproject.com> · <https://github.com/obsproject/obs-websocket> · Licenza: GPL-2.0.
- **`websockets` (Python) + `obs_shot.py`** — Uso: script del progetto che si autentica a `ws://127.0.0.1:4455` e salva lo screenshot del source (`GetInputList` + `SaveSourceScreenshot`). Licenza della libreria WebSocket client: `(non specificato localmente)`.
- **`ffmpeg` (cattura V4L2 diretta)** — Uso: alternativa a OBS per acquisire frame senza dipendere da OBS (`ffmpeg -f v4l2 -input_format mjpeg -video_size 1920x1080 -i /dev/video0 -frames:v 8 …`), conservando il frame **più grande** (il primo esce verde/corrotto e il 4K MJPG è corrotto su questa camera). Origine: <https://ffmpeg.org> · Licenza: LGPL-2.1+ / GPL-2.0+ secondo la build.
- **`vision_analyze` (modello di visione dell'agente)** — Uso: lettura del PNG della webcam (o del dump PPM del frame) per dire "bande", "rosso pieno", "logo", "interfaccia viva" senza disturbare l'utente. Strumento di ambiente Hermes Agent (<https://hermes-agent.nousresearch.com/docs>) · Licenza: (non specificato localmente).
- **Strumenti di misura sul device**: `dmesg`/`/dev/kmsg`, `debugfs` (`dri/0/*`, `clk_summary`, `csd`), `/proc/interrupts`, `crashlog-dump.sh`, blackbox/watcher sul `rawdump` (slot 409/410), `nx679j-ui-watch.sh`/`nx679j-ui-sample.sh` (fence release **e** retire separate). Licenze: kernel/GPL-2.0 e codice di progetto.
- **Manuali e riferimenti consultati per gli strumenti**: `qmicli(1)` (<https://manpages.debian.org/bullseye/libqmi-utils/qmicli.1>, <https://www.freedesktop.org/software/libqmi/man/latest/qmicli.1.html>), documentazione device driver rmnet del kernel (<https://www.kernel.org/doc/html/latest/networking/device_drivers/cellular/qualcomm/rmnet.html>).

---

### 9. Ricerca, forum e thread consultati

- **postmarketOS wiki** e **pmaports** — Uso: stato del supporto mainline SM8450, ordine dei servitori del modem, comportamento `rmtfs`. Origine: <https://wiki.postmarketos.org> · `pmaports` (`modem/tqftpserv`, MR 3269) · Licenza: (non specificato localmente; la wiki è CC-BY-SA a monte).
- **Forum e comunità**: `forum.openwrt.org` (thread 165025, 185556, 188925, 188956 — deattivazione regolare dell'operatore, auto-reconnect LTE/5G, lease DHCP), `ofmodemsandmen.com` (ROOter), `forums.whirlpool.net.au` + `whirlpool.net.au/wiki/router_openwrt`, `community.particle.io` (Tachyon/QCM6490, "the modem takes around 55 seconds"), `paldan.altervista.org` (QMAP/multiple PDN). URL: <https://forum.openwrt.org/t/support-4g-5g-automatic-reconnection-using-modemmanager/165025> · <https://forum.openwrt.org/t/managing-4g-lte-isp-initiated-regular-deactivation/188925> · <https://forum.openwrt.org/t/how-to-auto-reconnect-lte-5g-module/185556> · <https://forum.openwrt.org/t/lte-dhcp-doesnt-renew-ip-after-lease-time/188956> · <https://ofmodemsandmen.com/monitor.html> · <https://forums.whirlpool.net.au/archive/9xwq1573-3> · <https://whirlpool.net.au/wiki/router_openwrt> · <https://community.particle.io/t/tachyon-1-1-43-the-modem-awakens/71005> · <https://paldan.altervista.org/linux-qmap-qmi_wwan-multiple-pdn-setup/> · Licenza: (non specificato localmente).
- **Issue/PR upstream OpenWrt**: `openwrt/openwrt` #8368, #5066; `openwrt/packages` #19794, #23551 (commit `c51a804a63`, hotplug modemmanager e virtuali), commit `bc754f31` (dispatcher `10-report-down`); `dev.openwrt.org` ticket 4108. URL: <https://github.com/openwrt/openwrt/issues/8368> · <https://github.com/openwrt/packages/issues/19794> · <https://github.com/openwrt/packages/commit/bc754f31cfdb004eefa43038f8f0827922107fc6> · Licenza: (non specificato localmente).
- **Issue/MR GitLab freedesktop**: libqmi #62, #113, #133, #122 e MR !382; libqrtr-glib #7; ModemManager MR !1443, !1452. Origine: <https://gitlab.freedesktop.org/mobile-broadband/ModemManager/-/merge_requests/1452> · API usata per ricontrollare gli issue: <https://gitlab.freedesktop.org/api/v4/projects/> · Licenza: (non specificato localmente).
- **Sorgenti upstream consultati per lo scaffolding della pagina Cellular**: `openwrt/luci` (commit `0834d099439e4ba788b4c33c54616d8ee9df5c6e`), `openwrt/rpcd` @ `4062f666ecd17a03a153a40e76af6904d3eb6a78`, `openwrt/packages` (`net/modemmanager`), `openwrt/ubox` (`log/logread.c`, `log/syslog.c` @ `6f78fa49`), pacchetto modello `luci-app-squid`; scheletro copiabile prodotto in `/home/user/rpcd-exec-minimal/`. Licenze: Apache-2.0 (LuCI) / GPL-2.0 (rpcd, ubox) `(non specificato localmente per i singoli file)`.
- **Thread kernel / patch status**: `patchew.org`, `patchwork.kernel.org` (API `/api/1.2/patches/`), `lore.kernel.org`, `lists.freedesktop.org/archives/libqmi-devel/`, `lists.infradead.org/pipermail/lede-commits/`, `elixir.bootlin.com` — Uso: verificare se un fix è **mergiato** o solo proposto (una serie su LWN non significa merged). Origine: <https://patchew.org/search?q=> · <https://patchwork.kernel.org/api/1.2/patches/?q=> · <https://lists.freedesktop.org/archives/libqmi-devel/2017-April/002285.html> · <https://elixir.bootlin.com> · Licenza: (non specificato localmente).
- **Forum XDA e blog/guide di settore** — Uso: categoria di ricerca "forum modder/hacker" nella metodologia di fan-out (XDA, whirlpool, ROOter, Reddit, blog) e riferimenti generici di boot-time su hardware embedded (Gateworks `trac.gateworks.com/wiki/boot_speed`, Toradex, `openwrt.org/docs/techref/preinit_mount`, `openwrt.org/docs/guide-user/network/wan/wwan/ltedongle`, QTI `docs.qualcomm.com/bundle/publicresource/topics/80-70022-10/46-performance-dashboard.html`). **Nessun thread XDA specifico è citato dai documenti locali.** Licenza: (non specificato localmente).
- **Riferimenti normativi e brevetti**: `patents.google.com/patent/US09544758B2` (Apple, "10-15 s time to cellular connectivity" — usato come *snippet*, non riletto); `linux-msm.github.io/mainline-status`; `android.googlesource.com/device/google/redbull` (analisi dell'init Android come oracolo del bring-up del modem). Licenza: (non specificato localmente).
- **Fonti Android/AOSP usate come oracolo**: `device/google/redbull/init.hardware.rc` (boot del modem come azione userspace, `start insmod_sh` in background, `write /dev/ipa 1` per il firmware IPA) — <https://android.googlesource.com/device/google/redbull/+/6d265bdf7f38a1383169013313...> · Licenza: Apache-2.0.
- **Ricerche in altre lingue** (cinese/russo) sul guasto clock DSI — metodologia delle skill `chinese-kernel-issue-research` / `russian-kernel-issue-research`; nessun risultato attribuito. Licenza: (non specificato localmente).

---

### 10. Voci del capitolato **non** riscontrate nei documenti locali

Queste voci erano attese dall'elenco ma **non compaiono** in nessun documento, script o skill del progetto: vanno quindi o dichiarate non usate, o aggiunte a mano da chi conosce il contesto esterno.

- **`drmdb.emersion.fr`** — nessuna occorrenza di `drmdb` né di `emersion` in tutta la documentazione del progetto: **non citato localmente** (la mappa delle proprietà DRM è stata ricostruita con `nx679j-drmprops` e con il **brute-force degli ID** `1..400` + `GETPROPERTY`, perché il driver vendor non elenca le proprietà dei plane in `OBJ_GETPROPS`).
- **`msm-fb-refresher`** — nessuna occorrenza in tutta la documentazione del progetto: **non citato localmente**; il "keeper di commit" del pannello è stato scritto da noi nel client DRM (la proprietà CRTC `idle_pc_state=2` è la rete di sicurezza, il commit continuo < 58 ms è il meccanismo).
- **`goodix_ts_berlin`** (nome esatto) — **non citato localmente**; i sorgenti Goodix effettivamente consultati sono `goodix_berlin_{core,i2c,spi}.c` (mainline), `goodix_berlin_driver_v1.0.1` (vendor Nubia) e l'alias `gtx8_i2c` (vedi §6).
- **`xda`** — citato solo come *categoria* di ricerca nella metodologia di fan-out: **nessun thread XDA specifico** con URL.
- **`mkbootimg`** — **non usato come binario**: l'header `ANDROID!` è scritto/letto dal codice Python del progetto secondo l'header AOSP (`system/tools/mkbootimg/include/bootimg/bootimg.h`); `magiskboot` è invece usato per unpack/repack (§2).

## Crediti e riferimenti: fonti di conoscenza

**Metodo.** Ogni URL elencato è *letteralmente presente* nei file locali del progetto ed è stato verificato con una ricerca testuale sull'albero. Fonti minerarie: `nx679j-stock/experiments/20260920-wifi-luci/` (STATO-ATTUALE.md, ESITO-*.md, RICERCA-*.md, DIAGNOSI-*.md, SPEC-STATO-MODEM.md), `re-dsi-research/`, `idlepc_research/`, le skill di `~/.hermes/profiles/kernel-re/skills/kernel-re/` e `skills/research/`, e i log dei fan-out di ricerca in `cache/delegation/`. Le voci citate nel lavoro ma prive di URL locale sono in "Da completare": non sono state inventate.

### 1. Display / DRM Qualcomm (SDE, DSI, idle_pc_state)

**drmdb (Simon Ser / emersion) — database delle proprietà DRM**
- `https://drmdb.emersion.fr/` — indice del database delle proprietà DRM dei driver.
- `https://drmdb.emersion.fr/properties?driver=msm_drm` — elenco delle proprietà esposte dal driver `msm_drm`; usato per sapere quali property il vendor kernel pubblica davvero.
- `https://drmdb.emersion.fr/properties/connector/DPMS` — semantica di `DPMS` lato connector; usato per lo spegnimento pannello.
- `https://drmdb.emersion.fr/properties/connector/frame_trigger_mode` — significato di `frame_trigger_mode`; usato per il trigger frame in cmd-mode.
- `https://drmdb.emersion.fr/properties/connector/autorefresh` — semantica di `autorefresh`; comportamento di self-refresh del pannello.
- `https://drmdb.emersion.fr/properties/connector/max%20bpc` — significato di `max bpc`; profondità colore sul link DSI.
- `https://r.jina.ai/https://drmdb.emersion.fr/devices` — variante via proxy di lettura usata per raggiungere la pagina `/devices`.

**postmarketOS (wiki + progetti)**
- `https://wiki.postmarketos.org/wiki/Display` — panoramica display su msm; riferimento per struttura DRM/KMS su ARM.
- `https://wiki.postmarketos.org/wiki/Display/Troubleshooting` — playbook schermo nero / nessun frame; base del confronto con il nostro flip-loop plane-only.
- `https://wiki.postmarketos.org/wiki/Troubleshooting:display` — pagina omologa sul wiki principale.
- `https://wiki.postmarketos.org/wiki/MSM` — pagina della famiglia MSM; contesto su driver e stack.
- `https://wiki.postmarketos.org/wiki/Direct_Rendering_Management` — spiegazione DRM (master, CRTC, plane); inquadramento del kiosk.
- `https://wiki.postmarketos.org/wiki/Msm-fb-refresher` — pagina che descrive il vincolo di framebuffer congelato senza refresh.
- `https://github.com/AsteroidOS/msm-fb-refresher` — progetto sorgente del refresher; prova che il problema è noto a monte.
- `https://github.com/AsteroidOS/msm-fb-refresher/blob/master/refresher.c` — implementazione (ioctl di refresh); letta per capire la tecnica.
- `https://raw.githubusercontent.com/AsteroidOS/msm-fb-refresher/master/README.md` — README; motivazione e limiti.
- `https://gitlab.com/postmarketOS/pmaports/-/raw/master/main/msm-fb-refresher/APKBUILD` — packaging Alpine/pmOS; integrazione in init.
- `https://gitlab.com/postmarketOS/pmaports/-/issues/2813` — issue sul refresher; confini noti della soluzione.
- `https://gitlab.postmarketos.org/postmarketOS/pmaports/-/issues/3348` — issue pmaports su display; riscontro su casi analoghi.
- `https://wiki.postmarketos.org/wiki/Power_Management` — gestione power; usata per idle/suspend.
- `https://wiki.postmarketos.org/wiki/Lomiri` — UI mobile su pmOS; confronto con l'approccio kiosk.
- `https://wiki.postmarketos.org/wiki/Qualcomm_Snapdragon_845/850_(SDM845/SDM850` — pagina SoC generazione precedente; analogia sulla pipeline DSI.

**Freedreno / mailing list / patchwork**
- `https://github.com/freedreno/freedreno/wiki` — wiki freedreno; base di conoscenza su MSM/Adreno.
- `https://github.com/freedreno/freedreno/wiki/DSI-Panel-Driver-Porting` — guida di porting pannello DSI; la fonte pubblica più vicina al nostro caso.
- `https://lists.freedesktop.org/archives/freedreno/2026-January/042861.html` — thread freedreno; discussione su DSI/DRM msm.
- `https://lore.kernel.org/r/8e1d33ff-d902-4ae9-9162-e00d17a5e6d1@postmarketos.org` — messaggio su lore (pmOS); riferimento su refresh/idle pannello.
- `https://patchwork.kernel.org/api/1.2/patches/?q=<query` e `https://patchew.org/search?q=<series` — endpoint di ricerca patch; usati per verificare se una modifica DSI/SDE esisteva upstream (procedura della skill openwrt-feature-state-verification).
- `https://lists.infradead.org/pipermail/lede-commits/<YYYY` — archivio commit OpenWrt; ricostruzione della storia di driver/pacchetti.

**Codice sorgente vendor/upstream (display, DSI, idle_pc_state)**
- `https://git.codelinaro.org/clo/la/platform/vendor/opensource/display-drivers/-/blob/ac17a22157f56a438e24009e4fa91a69100612ef/msm/dsi/dsi_display.c#L1706-1907` — sorgente QTI SDE/DSI; prova del percorso di set clock sorgente (`dsi_display_set_clk_src`).
- `https://github.com/SOHRA8/android_vendor_qcom_opensource_display-drivers/blob/7d283391248fc613f9413cc2758bf7f9f0fe91f6/msm/` — mirror navigabile dei vendor display-drivers (branch DISPLAY.LA.2.0.r1-11100-WAIPIO.0); usato da `re-dsi-research/`.
- `https://github.com/LineageOS/android_kernel_xiaomi_sm8450/blob/ef362912d37b761041709638c1e571d6394e9558/` — kernel SM8450 LineageOS; usato per `clk-rcg2.c` nel caso byteclk/reparent.
- `https://github.com/gregkh/linux/blob/v5.10.66/drivers/clk/qcom/clk-rcg2.c#L100-L147` — RCG2 su 5.10.66; il punto esatto del CMD_UPDATE bloccato verso `-EINVAL` (skill qualcomm-dsi-clock-failure-triage).
- `https://github.com/gregkh/linux/blob/v5.10.66/drivers/gpu/drm/drm_file.c#L450-L453` — lifecycle file DRM (last-close); distinzione tra chiusura userspace e shutdown driver.
- `https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder.c#L881` — DPU upstream; confronto con il comportamento vendor.
- `https://github.com/TheGammaSqueeze/GammaOSNextDistribution/blob/0de890dede9a550da87920d61b4b08da7c80fb02/hardware/qcom-caf/sm8450/display/sdm/libs/core/drm/hw_device_drm.cpp` — HWC SDM SM8450; chi setta le proprietà atomiche (idle_pc_state).
- `https://github.com/ztemt/NX679S/tree/0280bdce975602ff17161f5de1b8fc63dc96bd47` e `https://github.com/ztemt/NX679S/blob/0280bdce975602ff17161f5de1b8fc63dc96bd47/display-drivers.zip` — display-drivers vendor del fratello NX679S; base per il porting pannello.
- `https://docs.kernel.org/gpu/drm-kms.html` — documentazione KMS (proprietà atomiche, damage tracking, explicit fencing); design del kiosk (`RICERCA-UI-LAYER-kiosk3.md`).
- `https://docs.kernel.org/gpu/drm-uapi.html` e `https://www.kernel.org/doc/html/v5.10/gpu/drm-uapi.html` — UAPI DRM; riferimento per le chiamate del client.
- `https://linux-msm.github.io/mainline-status/soc/sm8450` — stato mainline SM8450 (linux-msm); cosa è già upstream su questa SoC.
- `https://cs.android.com/search?q=idle_pc_state` — code search AOSP per `idle_pc_state`; ricerca dei consumer della property.
- `https://raw.githubusercontent.com/torvalds/linux/v5.10/drivers/gpu/drm/drm_atomic.c` — atomico DRM 5.10; semantica commit/nonblock.

Nota: `idlepc_research/CRTC_PROP_IDLE_PC_STATE_findings.md` documenta la property citando repo/branch/SHA codelinaro (`display-kernel.lnx.5.10.c2`, `DISPLAY.LA.2.0.c25/c27`) **senza URL completo** — vedi Da completare.

### 2. Touch Goodix (GT9897 / Berlin-A / gtx8)

- `https://github.com/goodix/goodix_ts_berlin` — repo ufficiale Goodix del driver Berlin; riferimento primario per la variante del nostro GT9897.
- `https://github.com/goodix/goodix_ts_berlin/blob/master/docs/Porting_Guide.md` — guida di porting ufficiale; init/firmware/config del Berlin.
- `https://github.com/goodix/gtx8_driver_linux` — repo Goodix del ramo gtx8; il vendor NX679J usa l'alias `i2c:gtx8_i2c`, quindi è la famiglia corretta.
- `https://github.com/goodix/gtx8_driver_linux/blob/master/goodix_ts_core.h` — header del core gtx8; confrontato con `goodix_core.ko` del device.
- `https://raw.githubusercontent.com/torvalds/linux/master/drivers/input/touchscreen/goodix_berlin_core.c` — driver mainline (Linaro / Charles Hyde); base del supporto upstream Berlin.
- `https://raw.githubusercontent.com/torvalds/linux/master/drivers/input/touchscreen/goodix_berlin_i2c.c` e `https://github.com/torvalds/linux/blob/master/drivers/input/touchscreen/goodix_berlin_i2c.c` — layer I2C mainline; modello per il bus `i2c@990000/goodix-berlin@5d` (0x5D, 1 MHz).
- `https://codebrowser.dev/linux/linux/drivers/input/touchscreen/goodix_berlin_core.c.html` — versione navigabile del mainline core; API attese da firmware/config.
- `https://patchew.org/linux/20231021-topic-goodix-berlin-upstream-initial-v9-0-13fb4e887156@linaro.org/` — series iniziale goodix-berlin upstream (v9); storia della mainlinizzazione.
- `https://lore.kernel.org/all/20240916-goodix-berlin-a-v1-0-3a2e6d1c1d3f@linaro.org/` — revisione 2024 goodix-berlin; differenze rispetto al vendor.
- `https://github.com/goodix/fwupdate_for_berlin_linux` — tool di aggiornamento firmware Berlin; parte firmware/config.
- `https://github.com/ztemt/NX679S/tree/main/kernel_platform/msm-kernel/drivers/input/touchscreen/goodix_berlin_driver` — driver vendor ZTE/Nubia del Berlin; confronto diretto col nostro modulo.
- `https://github.com/tangalbert919/android_kernel_nubia_sm8350/blob/lineage-20/drivers/nubia/touch/goodix_berlin_driver_v1.0.1/goodix_ts_core.h` — driver Berlin di un kernel Nubia (SM8350); analogia di integrazione.
- `https://github.com/fwupd/fwupd/blob/main/plugins/goodix-tp/README.md` — plugin fwupd per touch Goodix; note su firmware e identificazione.
- `https://developers.goodix.com/en/bbs/detail/19f255b9cee04631a2b8c480f918b9f7` — forum/BB ufficiale Goodix; Q&A di configurazione.
- `https://docs.goodix.com/en/online/detail/datasheet_bl_b/Rev.1.0/d18ee3e7ced399d7290259ff5ebeb33f` — datasheet Berlin BL_B Rev.1.0; fonte hardware per init/I2C.
- `https://mjmwired.net/kernel/Documentation/devicetree/bindings/input/touchscreen/goodix` — binding devicetree goodix (mirror); nodo DT.
- `https://raw.githubusercontent.com/nguyenxuansu/kernel_xiaomi_sm8250_mod/main/drivers/input/touchscreen/goodix_9916/goodix_ts_tools.c` — implementazione community dei Goodix tools; comandi debug.
- `https://xdaforums.com/t/research-goodix-touch-sensitivity-smoothness-explained.4801689` — thread XDA di ricerca sul comportamento touch Goodix; contesto empirico.
- `https://www.xda-developers.com/nubia-red-magic-7-global-launch` — scheda dispositiva RedMagic 7; contesto di prodotto (touch/I2C).

### 3. Modem / cellulare (QMI, QRTR, WDS, bearer, ModemManager)

**libqmi (upstream, freedesktop)**
- `https://www.freedesktop.org/software/libqmi/man/latest/qmicli.1.html` — manpage ufficiale di `qmicli`; fonte dei flag usati/evitati (WDS/DMS/NAS/UIM, `-p`, `--client-cid`).
- `https://manpages.debian.org/bullseye/libqmi-utils/qmicli.1` — variante manpage su mirror Debian; cross-check dei flag.
- `https://github.com/linux-mobile-broadband/libqmi/blob/main/utils/qmi-network.in` — script `qmi-network`; semantica connect/disconnect e autoconnect.
- `https://lists.freedesktop.org/archives/libqmi-devel/2017-April/002285.html` — thread libqmi-devel su bearer/keep-alive; usato in `bearer-liveness-probes.md`.

**ModemManager**
- `https://gitlab.freedesktop.org/mobile-broadband/ModemManager/-/merge_requests/1452` — MR !1452 (Harsh Sharma/QTI) sul datapath qcom-soc; la deroga pubblica più vicina al nostro caso (`mm-modemmanager-openwrt-recipe.md`).
- `https://github.com/linux-mobile-broadband/ModemManager.git` — repo MM; base del flusso di ricerca upstream (skill modemmanager-upstream-research).
- `https://github.com/linux-mobile-broadband/ModemManager/blob/main/NEWS` — NEWS di MM (multiplexing >=1.18, note QRTR); citato in `ricerca-qmi-bearer-prior-art.md`.
- `https://gitlab.freedesktop.org/api/v4/projects/$P/issues?search=qrtr&state=all&per_page=100` e `https://gitlab.freedesktop.org/api/v4/projects/$P/merge_requests?search=qrtr&state=all` — endpoint API GitLab per issues/MR QRTR; procedura ripetibile della skill.
- `https://gitlab.freedesktop.org/api/v4/projects/$P/merge_requests/1216/changes` — diff di MR via API; lettura puntuale delle patch QRTR.

**OpenWrt / netifd / proto**
- `https://github.com/openwrt/openwrt/blob/master/package/network/utils/uqmi/files/lib/netifd/proto/qmi.sh` — proto `qmi` (uqmi); confronto con `modemmanager.sh` (chi implementa `renew`).
- `https://github.com/openwrt/packages/blob/master/net/modemmanager/files/lib/netifd/proto/modemmanager.sh` — proto `modemmanager`; teardown/simple-disconnect e assenza di `renew`.
- `https://forum.openwrt.org/t/support-4g-5g-automatic-reconnection-using-modemmanager/165025` — richiesta feature sulla riconnessione automatica; conferma il buco del reconnect.
- `https://forum.openwrt.org/t/managing-4g-lte-isp-initiated-regular-deactivation/188925` — thread sul regular-deactivation ISP (il caso WINDTRE ~4h).
- `https://openwrt.org/docs/guide-user/network/wan/wwan/ltedongle` — guida WWAN/LTE dongle; riferimento di configurazione.
- `https://openwrt.org/docs/techref/preinit_mount` — ordine di mount nel preinit; collocazione della catena modem (START=00/01).
- `https://github.com/qualcomm-linux/meta-qcom/blob/master/dynamic-layers/openembedded-layer/recipes-connectivity/modemmanager/files/0003-bearer-qmi-use-BindMuxDataPort-for-BAM-DMUX-WDS-client.patch` — patch QTI in meta-qcom (bind WDS/BAM-DMUX); base tecnica delle nostre patch bearer.
- `https://android.googlesource.com/device/google/redbull/+/6d265bdf7f38a1383169013313...` — init/remoteproc di riferimento (Pixel redbull); ordine di avvio servizi modem prima del remoteproc (VERIFICATA in RICERCA-anticipo-modem).

**Kernel rmnet / datapath**
- `https://www.kernel.org/doc/html/latest/networking/device_drivers/cellular/qualcomm/rmnet.html` — documentazione rmnet mainline; semantica link/mux (`rmnet_ipa0`/`rmnet_data0`, rmnet-link).

**postmarketOS modem (mainline)**
- `https://wiki.postmarketos.org/wiki/Modem` — pagina centrale modem di pmOS; mappa di rmtfs/tqftpserv/pd-mapper e ordine di avvio.
- `https://wiki.postmarketos.org/wiki/Talk:QMI` — talk QMI; note pratiche su qmi/proxy.
- `https://gitlab.postmarketos.org/postmarketOS/msm-modem.git` — pacchetto/init msm-modem; modello di orchestrazione modem.
- `https://github.com/sm8450-mainline/pmaports/tree/master/modem` — pmaports modem del ramo SM8450 mainline; riferimento diretto per la SoC.
- `https://raw.githubusercontent.com/CaullenOmdahl/postmarketos-blackshark-klein/main/docs/modem/troubleshooting.md` — troubleshooting modem (BlackShark Klein); checklist di diagnosi.
- `https://raw.githubusercontent.com/CaullenOmdahl/postmarketos-blackshark-klein/main/docs/modem/driver-internals.md` — internals del driver modem; mappatura dei servizi QMI.
- `https://github.com/miraro/postmarketos-x00td-cellular` — port cellulare x00td; analogia di integrazione.
- `https://github.com/linux-msm/tqftpserv` — tqftpserv; servizio necessario al boot del modem su mainline.
- `https://git.askiiart.net/askiiart/pmaports/src/commit/e60195f8ae51abe422b3ccda685c1e3612209130/modem/tqftpserv` — commit di riferimento tqftpserv (pmaports); usato in RICERCA-anticipo-modem.
- `https://gitlab.com/postmarketOS/pmaports/-/issues/2076` — issue pmaports su modem; riscontro su servizi e dipendenze.
- `https://github.com/ddimension/wwand` — alternativa a MM per il datapath (ucode, multi-PDP/QMAP); citata in `mm-modemmanager-openwrt-recipe.md` (nell'originale compare senza schema `https://`).

**Router/community modem**
- `https://ofmodemsandmen.com/monitor.html` — monitor di connettività di riferimento (IP tracking, ping count/interval/timeout); ispirazione del supervisor di riconnessione.
- `https://github.com/ROOterDairyman/ROOter` — progetto ROOter; script connect/reconnect per modem.

**Altro modem / boot correlato**
- `https://community.particle.io/t/tachyon-1-1-43-the-modem-awakens/71005` — caso pratico di bring-up modem (VERIFICATA).
- `https://forge.caseytunturi.com/Fimeg/SouveraineOS/src/branch/public/saf/device/modem.md` — note device modem (VERIFICATA).
- `https://patents.google.com/patent/US09544758B2` — brevetto Apple su sequencing modem/power (SNIPPET).
- `https://docs.qualcomm.com/bundle/publicresource/topics/80-70022-10/46-performance-dashboard.html` — doc Qualcomm su boot/performance dashboard (SNIPPET).

### 4. Catena di boot Android (AVB, ABL, fastboot, EDL)

- `https://source.android.com/docs/core/architecture/bootloader/boot-image-header` — specifica dell'header boot image (v4 vendor boot); lettura/parsing di `boot_b` e patch a campo singolo.
- `https://source.android.com/docs/core/architecture/bootloader` — indice della sezione bootloader AOSP; contesto su A/B e boot flow.
- `https://source.android.com/docs/core/architecture/bootloader/boot-reason` — boot reason / reboot cause; interpretazione del ritorno in fastboot dopo ~15 s (`re_sde/reboot-cause-research-20260923.md`).
- `https://github.com/linux-msm/qdl` — `qdl` (EDL/Sahara-Firehose userspace); alternativa open all'`edl` Python per il readback.
- `https://github.com/bkerler/edl` — `edl` (bkerler); tool EDL effettivamente usato per readback e restore.
- `https://github.com/bkerler/Loaders` — collezione di loader Firehose; fonte dei `prog_firehose_*.melf`.
- `https://github.com/quic/qdlrs.git` (e `https://github.com/qualcomm/qdlrs`) — `qdlrs` Qualcomm; confronto del percorso EDL ufficiale.
- `https://xdaforums.com/t/redmagic-nubia-nx679j-kernel-source.4612103/` — thread XDA sui kernel source RedMagic/Nubia; prova delle fonti GPL disponibili.
- `https://xdaforums.com/t/nubia-red-magic-7-nethunter-kernel.4561707` — kernel NetHunter per RedMagic 7; precedente di custom kernel/boot chain su questo device.
- `https://xdaforums.com/t/discussion-gplv2-violation-redmagic-11-pro-nx809j-incomplete-unbuildable-kernel-sources.4790008/` — discussione su kernel source Nubia incompleti; contesto di qualità delle fonti vendor.
- `https://android.googlesource.com/kernel/common/+/refs/heads/android12-5.10/...` — alberi `kernel/common` Android 12/5.10 (`gki_defconfig`, `unistd.h`, `pid.c`, `security/selinux/hooks.c`); usati dal fan-out su GKI/SELinux.
- `https://android.googlesource.com/kernel/msm/+/refs/heads/android-msm-waipio-5.10/techpack/display/msm/` — albero kernel msm waipio 5.10; base vendor display.
- `https://android.googlesource.com/kernel/msm-extra/display-drivers/+/ea5b69a0327130b9553a3ad6ede3efb2734be756/msm/dsi/dsi_display.c` — display-drivers msm-extra; confronto dsi_display/RCG.

Nota: `avbtool` è usato come strumento locale (analisi `vbmeta_a/b`), ma nessun URL alla documentazione AVB ufficiale è presente localmente — vedi Da completare.

### 5. Comunità cinesi e russe

**Russo (4PDA, linux.org.ru, OpenNet, Telegram, DuckDuckGo)**
- `https://4pda.to/forum/index.php?showtopic=1039956` — topic 4PDA (ZTE/RedMagic); corpus russo su boot/display di questi device.
- `https://4pda.to/forum/index.php?showtopic=1086317&st=220` — topic 4PDA (pagina specifica); riscontri su sintomi display.
- `https://www.linux.org.ru/search.jsp?q=` — ricerca linux.org.ru ("Всего найдено N результатов"); superficie russa verificata.
- `https://www.opennet.ru/kernel/` — changelog kernel in russo che cita i titoli `drm/msm`/`drm/dpu`; risalire alle patch upstream.
- `https://t.me/s/<channel>` — anteprime pubbliche Telegram (con `?q=`); canali pubblici senza login.
- `https://html.duckduckgo.com/html/?q=` — endpoint DDG HTML (solo via backend di extract, con `kl=ru-ru`).
- Habr: la skill `russian-kernel-issue-research` documenta l'endpoint `fetch('/kek/v2/articles/?query=...&order=relevance&perPage=20')` dalla pagina habr.com — percorso relativo, non URL completo.
- Avvertenza documentata: 4PDA è dietro Cloudflare ("Just a moment") su ogni URL di topic e anche via `r.jina.ai`; l'assenza di riscontri lì è una coverage gap, non un'evidenza.

**Cinese**
- `https://openwrt.org/zh/docs/techref/requirements.boot.process` — pagina OpenWrt in cinese sull'ordine del boot; sequenza preinit/rcS.
- `https://www.toradex.com/zh-cn/blog/embedded-linux-boot-time-optimization` — blog Toradex in cinese sull'ottimizzazione del boot time (SNIPPET).
- La skill `chinese-kernel-issue-research` fissa lo scope (Gitee, CSDN, Coolapk, GitHub, mailing list) ma non contiene URL: le query/risultati cinesi non sono stati salvati come link — vedi Da completare.

### 6. Altre fonti e documentazione di supporto

- `https://trac.gateworks.com/wiki/boot_speed` — note pratiche di boot speed (console seriale, init "quiet"); piano di ottimizzazione avvio.
- `https://openwrt.org/docs/guide-user/additional-software/opkg-to-apk-cheatsheet` — passaggio opkg->apk; comandi di installazione sul device.
- `https://openwrt.org/docs/guide-user/installation/sysupgrade.owut#pre-install_script` e `https://github.com/efahl/owut/blob/main/files/pre-install.sh` — hook pre-install di owut; riferimento per la persistenza cross-flash.
- `https://openwrt.org/wifi.device.json` (e `wifi.iface/station/vlan.json`) — schemi UCI wireless pubblicati da OpenWrt; usati dalla UI LuCI.
- `https://kernel.org/doc/html/v5.10/admin-guide/ramoops.html` — pstore/ramoops 5.10; diagnosi post-mortem senza console.
- `https://docs.kernel.org/filesystems/debugfs.html` — debugfs; fondamento della skill debugfs-callback-triage.
- `https://man7.org/linux/man-pages/man1/dmesg.1.html`, `https://man7.org/linux/man-pages/man8/ping.8.html`, `https://manpages.debian.org/buster/manpages/icmp.7.en.html` — manpage per i tool di diagnostica (log, liveness).
- `https://raw.githubusercontent.com/torvalds/linux/v5.10/Documentation/ABI/testing/dev-kmsg` e `https://raw.githubusercontent.com/torvalds/linux/v5.10/drivers/gpu/drm/drm_framebuffer.c` — vincoli ABI di `/dev/kmsg` e lifecycle framebuffer; relay di log e refcount del FB.
- `https://raw.githubusercontent.com/openwrt/ubox/6f78fa496bf36c55864a41e353df7d13f04b1077/log/syslog.c` e `https://raw.githubusercontent.com/openwrt/ubox/6f78fa496bf36c55864a41e353df7d13f04b1077/log/logread.c`, `https://raw.githubusercontent.com/openwrt/openwrt/v25.12.5/package/utils/busybox/Makefile` — comportamento di syslog/logread e busybox nella build; base della skill nx679j-runtime-diagnostics.
- `https://busybox.net/downloads/busybox-1.37.0.tar.bz2` — sorgente busybox allineata al device; verifica dei comandi disponibili (es. assenza di `type rmnet`).
- `https://github.com/marsounjan/icmp4a` — utilità ICMP per test di liveness; usata in `bearer-liveness-probes.md`.
- `https://developer.android.com/reference/android/net/ConnectivityDiagnosticsManager.DataStallReport` e `https://developer.android.com/training/monitoring-device-state/doze-standby` — semantica Android di data stall e doze; modello per la rilevazione di bearer morto.
- `https://people.cs.umass.edu/~arun/papers/TailEnder.pdf` e `https://ranger.uta.edu/~jmeng/pubs/sigmetrics18.pdf` — letteratura su tail latency/stall detection; base teorica del supervisor.
- `https://unix.stackexchange.com/questions/635577/` — Q&A su ping/liveness; riscontro pratico.

**Fonti interne al progetto** (provenienza delle citazioni, non URL esterne):
`experiments/20260920-wifi-luci/STATO-ATTUALE.md`, `ESITO-v129.md`, `ESITO-v130-modem-x65.md`, `ESITO-v131-v132.md`, `ESITO-v135-v137-ui-display.md`, `RICERCA-anticipo-modem-20260923.md`, `RICERCA-UI-LAYER-kiosk3.md`, `ricerca-qmi-bearer-prior-art.md`, `DIAGNOSI-bearer-1h.md`, `SPEC-STATO-MODEM.md`, `PROVA-MM-non-passivo.md`; `re-dsi-research/` (`modeset-vs-idlepc-dsi-byteclk-reparent.md`, `te-tearcheck-frame-done-5.10.md`, `atomic-commit-nonblock-probe-5.10.md`); `idlepc_research/CRTC_PROP_IDLE_PC_STATE_findings.md`; `NX679J_OPENWRT_KERNEL_RE_HANDOFF.md`.

### Da completare (citati nel lavoro, nessun URL locale)

Gli elementi seguenti sono citati nei documenti/skill ma **non** hanno un URL reperibile in locale: non sono stati inventati.

- Documentazione AVB/vbmeta ufficiale (`source.android.com/docs/security/...`, avbtool): solo lo strumento è citato, nessun link.
- Documentazione Magisk ufficiale (topjohnwu/Magisk docs) — Magisk 30.7, `magiskinit`, `magiskboot` citati in analisi e script senza URL.
- Documentazione Qualcomm ABL/XBL/AVB (policy di firma, boot attempt timeout, catena ECDSA P-384 "Generated Ztemt Root CA"): discussa in `NX679J_OPENWRT_KERNEL_RE_HANDOFF.md` senza link.
- Permalink codelinaro per `idle_pc_state` (repo/branch/SHA in `CRTC_PROP_IDLE_PC_STATE_findings.md`: `display-kernel.lnx.5.10.c2`, `DISPLAY.LA.2.0.c25`, `DISPLAY.LA.2.0.c27`; file `msm/sde/sde_crtc.c`, `sde_crtc.h`, `msm_prop.c`) — URL completo assente.
- Fonti cinesi concrete (Gitee, CSDN, Coolapk): la skill ne fissa lo scope, nessun URL salvato.
- Thread XDA/forum specifici per fastboot/AVB/unlock del RedMagic 7 (vbmeta, slot, getvar): non presenti (solo kernel source, NetHunter, GPLv2).
- Issue libqmi #113 / #62 / #133 e libqrtr-glib #7 (referenziate per numero in `SPEC-STATO-MODEM.md` e skill) — URL assente.
- MR !382 libqmi (chiusa, non mergiata) e MR pmaports 3269 (git.askiiart/pmaports) — citate senza URL.
- `openwrt/packages` commit c51a804a63 / PR#23551 e `wwand` PR#30185 — commit/PR citati senza URL (il repo `wwand` compare senza schema).
- Documentazione Gunyah / watchdog Qualcomm (proprietà del watchdog tra Gunyah, firmware, ABL, Linux): discussa senza link.
- Qualcomm SDM/HWC `sde-drm/drm_crtc.cpp` / `libdrmutils/drm_interface.h` su codelinaro per `CRTC_SET_IDLE_PC_STATE` — riferimenti a file/riga senza URL (stesso problema dei permalink codelinaro).

## Nota finale

Il lavoro e' stato condotto tenendo **l'oracolo Android** (slot A, con Magisk) come riferimento di verita': quando un comportamento del device non era documentato, la risposta si e' cercata in cio' che fa il sistema stock funzionante.
La **webcam** e' stata lo strumento di verifica visiva obbligatoria: un boot, una schermata, un pannello acceso non sono mai stati considerati "riusciti" senza un riscontro visivo registrato.
Da qui la regola che attraversa tutte le fasi: **nessun risultato senza verifica** — e, dove la verifica non e' stata possibile, la lacuna e' dichiarata nel testo invece di essere colmata a parole.
I crediti in coda appartengono ai loro autori: distribuzioni, toolchain, tool, sorgenti vendor e fonti di conoscenza elencate sono il terreno su cui questo port e' cresciuto.


---

# Complementi

*(sezioni prodotte dai ricercatori dai documenti locali; le fonti sono citate all'interno di ciascuna)*

## Cronologia del progetto

Tabella dei traguardi con date e versioni, ricavata **solo dai documenti locali** (`STATO-ATTUALE.md`, `ESITO-*.md`, `DIAGNOSI-*.md`, `DIFETTO-*.md`, `RICERCA-*.md`, `DECISIONE-*.md`, `RIPRESA.md`, `PIANO-UI-parita-luci.md`, `docs-snapshots/*`, `build-*.py`) e dai riferimenti della skill `kernel-re/nx679j-openwrt`. Nessuna data è inventata: dove il testo non la dichiara, la data viene dagli mtime dei file immagine (`boot_b-*.img`) o è marcata **data incerta**.

| Data | Versione/i | Cosa è stato fatto | Esito |
|---|---|---|---|
| 16/09/2026 | — (baseline Android+Magisk, slot A) | Analisi del boot_b e verifica preliminare della strada prima di investire tempo; EDL provato | Strada confermata dai log Android; EDL non funzionante su questa unità → recovery = slot A + verifica hash di ogni scrittura |
| 17/09/2026 | v10–v16 (init v8/v9) | Boot chain (ABL/XBL, DTB/overlay), ramdisk custom, PID 1, gadget USB NCM, chroot OpenWrt | OpenWrt avviato, SSH su 10.0.0.1:22, LuCI HTTP 200; ciclo di riavvii da PID 1 poi chiuso (v17: il lavoro passa in un figlio) |
| 17–18/09/2026 | v17–v53 | Modem: firmware nel path giusto, driver `smp2p`, probe differiti, `finit_module` al posto di `kmodloader`, stack dati caricato in un figlio, start dei remoteproc ritardato | Remoteproc registrati e modem avviato col suo firmware; documentato che il modem acceso senza interlocutore QMI fa riavviare la piattaforma |
| 19/09/2026 | v35–v53 | QMI: ipotesi QMUX falsificata, trasporto corretto QRTR (`AF_QIPCRTR`), frame DMS ONLINE, IPA / DPM / WDS | Modem raggiunto e dialogo QMI avviato; il primo traffico WAN resta aperto |
| 20/09/2026 | v54–v57 | Wi-Fi QCA6490 + AP `NX679J-TEST`, LuCI persistente, catena modem riprodotta sul boot di riferimento | Wi-Fi, LuCI (login + status 200) e regressione modem verificate; pannello non esposto sulla WAN cellulare |
| 20/09/2026 (sera) | v58–v60 | Fix `PATH` in switch.sh, attach `lan_wifi` su `phy0-ap0`, dnsmasq con spawn diretto, NAT/iptables-legacy (WAN share), AP spostato su 5 GHz VHT80 | v60 = immagine finale della sessione; 2 client DHCP reali, LAN→Internet via SIM verificato; throughput 8/8 → 30/12 Mbps |
| 20/09/2026 (notte) – 21/09/2026 (mattina) | v61–v80 | Iterazioni Wi-Fi/LuCI e preparazione dell'adozione di ModemManager/qmicli/ip-full (**data incerta per il contenuto**: i doc non descrivono le singole immagini) | Immagini costruite (mtime 20/09 20:02 → 21/09 09:43); esito per-versione non documentato |
| 22/09/2026 (giorno) | v81–v84 | Connect dati col proto **standard** `modemmanager`: patch libqmi 106 (probe mux), kill della WDS della catena, netlink-watch v3 + S71, protocollo di flash forte | v84: boot 100% automatico, iface modem UP in ~3 s, ping 1.1.1.1 0% loss — obiettivo centrato |
| 22/09/2026 (sera) | sola configurazione, nessuna immagine nuova | Riconnessione automatica dopo la deattivazione WINDTRE (~4 h): `device='qcom-soc'` + `force_connection='1'` | Risolto col meccanismo standard: disconnect → Reconnecting → nuovo IP → ping OK |
| 22/09/2026 (sera/notte) | v85–v87 | Display: `SET_CLIENT_CAP(ATOMIC)` mancante (EINVAL), commit atomico c-n-p, flip loop risolto col gap < 58 ms; persistenza PERSIST-INSIDE (etc/luci/tools dentro il ramdisk) | Flip infinito (60/60 commit OK) e pannello aggiornato; v87 verificata al boot: LUCI=1 TOOL=2 UCI=2 MODEM=1 |
| 23/09/2026 (pomeriggio) | v88–v90 | kiosk3 come client di boot, hardening, watchdog del modem; ottimizzazione dei tempi di boot | Boot da ~315 s a ~135 s (catena DONE 65 s, dati ~135 s, kiosk ~150 s); varianza residua ~90 s nel probe di MM |
| 23/09/2026 (sera/notte) | v91–v112 (v95 = backup GOOD 155 s) | Caccia ai boot che falliscono e all'interfaccia UCI: v104 rcS anticipato, v105 no, v106 misurata (nessun guadagno), v107 gate 90/gap 5, v108 device risolto a runtime, v109b dannoso, v111 ripristino, v112 | v104 riuscito (interfaccia up); connettività già a 61-63 s senza MM; l'interfaccia UCI resta la variabile impazzita (135-336 s) |
| 24/09/2026 (notte, fino alle 07:15) | v113–v122 | Interfaccia UCI dal bearer della catena, hook hotplug net, tentativi di proto; **v121 e v122 costruite e NON flashate** | v121 mai testata (job fermato: l'evidenza precedente è stata invalidata); v122 non flashata |
| 24/09/2026 (07:15–08:25) | v123–v124 | Dedup dell'owner di mm-standard-boot; regola udev qcom-soc/rmnet_ipa0 iniettata dopo il PERSIST-INSIDE | Immagini costruite e payload verificato; esito runtime non documentato |
| 24/09/2026 | v125–v128 | v125 proto custom read-only (address-external) al posto di `proto none`; v126 frontend LuCI del proto; v127 hold 3600→86400; v128 ripristino del restart di MM | v127: dati 64 s, UCI 74 s, kiosk 83 s, ping 25/25; v128: oggetto modem a 179 s, cleanup MM 324→14, pagina Cellular popolata |
| 24/09/2026 (19:21–20:33) | v128 | Verifica passiva lunga, 72 minuti oltre il vecchio cliff di un'ora (36 campioni) | 0 campioni con problemi → il cliff orario del bearer è eliminato |
| 24/09/2026 (19:00–20:36) | ricerca e decisione (nessuna immagine) | Prova che ModemManager **non** è passivo (tenta il reset di rmnet_ipa0) e che nessun componente upstream parla QRTR; causa del bearer di 1 h = `hold=3600` del nostro client | Decisione documentata: MM rimosso, pagina Cellular riscritta con rpcd+ACL+menu+vista, reconnect spostato nel supervisor |
| 24/09/2026 (sera) | v129–v133 | v129 proto senza route riportata; v130 Modem X65 senza MM (pagina LuCI + supervisor link-watch); v131 controllo PIN SIM; v132 fix del campo PIN; v133 fix del rinnovo (attesa doppia + troncamento dei log) | v129: dati 66 s; v130: dati 63 s, 0 processi MM, 4 boot verificati; v133: difetto del rinnovo corretto (attesa doppia + troncamento) |
| 24/09/2026 (22:54–23:27) | v134–v140 | UI nativa in C sul display (pagine MODEM/RETE/SISTEMA + fetcher dati + tool di tocco iniettato); fence timeout 100→40 ms; `idle_pc_state=idle_pc_disable` | v137 (45c8e86b): 45.4 fps, tap→cambio pagina, RICONNETTI→IP nuovo con ping OK; v140 (033e2cd0): 61.2 fps, 0 fence in ritardo, pannello che sopravvive alle pause |
| 25/09/2026 (notte, 00:16–01:23) | v141–v149 | v141 cadenza dei probe del supervisor; v142 menù a categorie (4 categorie: 18 pagine all'epoca, 23 nella versione finale); v145 tastiera a schermo (9 campi); v148 lease statici | Recupero su drop da ~300 s a ~42 s; pagine, tastiera e cancellazione lease verificate |
| 25/09/2026 (mattina, 08:28–12:23) | v150–v163 | Completamento delle pagine di parità LuCI; v156 anti-residuo sul vetro; v158 chiusura delle sovrapposizioni (7 titoli dentro la barra dei tab); moduli vendor PMIC/PON/glink | Verifiche sul display; scoperta che nella chroot nessuno fa autoload dei moduli → tasto laterale funzionante; regressione del rimedio automatico corretta lo stesso giorno |
| 25/09/2026 (12:39–13:10) | v164–v167 | v165 qmicli ricostruito dall'SDK (collection basic → full, per la carrier aggregation); v166 cella + CA con cache nel fetcher | Dati cella/CA verificati end-to-end; contributo per portante dichiarato come **stima** (non misurabile dal sistema operativo) |
| 25/09/2026 (13:27–17:49) | v168–v171 | Standby display: commit di spegnimento e risveglio con `idle_pc_state`, poi tentativo via DPMS (`OBJ_SETPROPERTY`); dalla v168 (13:49) ogni build salva uno snapshot datato dei documenti (`docs-snapshots/`) | **Bloccato**: EINVAL (-22) su entrambe le vie → è il driver SDE vendor a rifiutare la disattivazione del CRTC; v171 (58135186) standby non pulito |
| 25/09/2026 (sera) | v172 | Incidente: lo standby lasciava il CRTC a metà e bloccava la UI (touch e tasto morti, `kill -9` inefficace) → innesco dello standby DISATTIVATO | v172 (c503bbef) in flash, device sano (45.4 fps, 217 chiavi); regola: non armare una funzione che può bloccarsi senza percorso di errore provato |
| 25/09/2026 (sera/notte) | prove sul touch (nessuna immagine dedicata) | Riavvii, reset da tasto, ricarica del driver (unbind → crash del kernel), Android stock, spegnimento vero | Il chip risponde sul bus (rom_pid BERLIN, pid 9897) e l'init e' identica allo stock, ma il controller non consegna eventi (0 byte su eventN, IRQ fermo): **verdetto: guasto hardware** (identico su Android stock) |
| 25/09/2026 (sera/notte) | v176–v178 | Bug `now_ms()` in overflow int32 (i gate dei tasti non scattavano e ogni tocco veniva scartato) → `CLOCK_MONOTONIC`; standby con `dpms_set(3)` e risveglio `commit_cnp` | Tasti laterali verificati dall'utente (volume giù = scorri/attiva, lungo = indietro); il DPMS-off si rivela letale per il link DSI |
| 25/09/2026 (fine giornata) | v180 | Standby definitivo: frame NERO + backlight 0 con link VIVO (niente DPMS), loop che continua a committare, risveglio = `bl_restore` + ridisegno completo | **VERIFICATO end-to-end con webcam**: off → schermo nero, on → interfaccia viva con dati live; immagine f8d1324f…, UI 5a8ca8b8… |

> Nota sulle date: `STATO-ATTUALE.md` data solo alcune fasi; le date delle righe v91–v170 derivano dagli mtime dei file `boot_b-*.img` (data certa di **build**), non da affermazioni nel testo.

## Lezioni per data

- **17/09 (v17–v19)** — PID 1 non deve eseguire: il lavoro sporco va in un processo figlio che riporta l'exit status. Un `exec` che riesce ma il cui programma muore subito è, per il kernel, identico a un successo.
- **17–18/09 (v26–v34)** — «assente» non è un risultato finché non si sono verificati percorso, nome, strumento e prerequisiti; il verdetto di un tool che tace va misurato (`kmodloader` rc=255 senza messaggi → si usa `finit_module`).
- **20/09 (v55/v59)** — `dd` su un nodo /dev inesistente **crea un file regolare** con readback identico: prima `mknod` dal major:minor reale, poi pre-lettura d'identità, `sync`×3, `drop_caches` e due letture a cache fredda.
- **22/09 (v82)** — Anche con quel protocollo il device può mentire per cache del block device: `oflag=direct`, `sync -f` e due letture fredde **prima** del reboot.
- **20/09 (v57→v58/v60)** — `PATH` è un requisito, non un workaround: con `PATH=/` netifd non registrava nessun proto handler e il successo di v57 era in parte accidentale.
- **22/09 (v84)** — Un evento netdev non consegnato a ModemManager entro 2.5 s non esiste (`WAIT_LINK_PORT_TIMEOUT_MS`): da qui netlink-watch + S71. E `Carrier: Absent` sui netdev rmnet non è un indicatore di connettività.
- **22/09 notte (v86–v87)** — La persistenza vera è il ramdisk: /etc, /www e i tool devono stare nell'immagine, mentre /rfs, rawdump e la cartella del ramdisk sono volatili.
- **23/09 (v101–v106, v109b)** — Mai giudicare una modifica da un boot solo (la fase MM varia di ~90 s) e non ritentare esperimenti già misurati come dannosi.
- **24/09 (v133, v129)** — Un'attesa su un file di log condiviso è una race: tronca prima e attendi il dato che serve davvero. E `address-external` protegge **solo** gli indirizzi: una route riportata diventa proprietà di netifd e viene cancellata all'ifdown.
- **24–25/09 (v140, v172, v176, v180)** — Commit/ioctl DRM solo NONBLOCK con timeout, e nessuna funzione che può bloccarsi va armata senza percorso di errore provato (v172); i gate temporali si verificano con un harness che chiama la funzione VERA (`now_ms()`, v176); su questo tree il DPMS-off uccide il link DSI command → lo standby giusto è frame nero + backlight 0 con link vivo (v180).

## Da completare

- **Contenuto delle v61–v80**: i doc locali non lo descrivono (esistono solo le mtime).
- **Esito delle v113–v120**: solo menzioni sparse; manca cosa ha fatto ciascuna versione.
- **Mappatura versione→contenuto incompleta** per v91–v103, v143–v147, v149–v157, v159–v164, v167.
- **Versioni v173–v175 e v179**: non documentate in alcun file locale (il testo passa da v172 a v176 e da v178 a v180).
- **Immagini v171–v180 non archiviate come `boot_b-current-vNNN.img`**: il file `boot_b-v90-mmwd.img` è l'output riusato a ogni build; da v171 la numerazione esiste solo nel log.
- **Touch**: confine nel runtime (0 byte su eventN con IRQ fermo), non risolto; la UI resta non navigabile a dito.
- **Alimentazione**: la carica funziona ma non è né leggibile né controllabile (nessun `/sys/class/power_supply`) — aperto.
- **Parità UI/LuCI**: restano in sola lettura IP statici, DNS, hostname statici, regole firewall, fuso orario a lista, restore backup / sysupgrade.
- **Affidabilità**: campagne di boot ripetuti ancora dichiarate incomplete (alcune versioni con un solo boot misurato).

---

## Scheda hardware e partizioni

Fonti locali (solo fatti trovati sul disco, con la stringa esatta; percorsi relativi al tree di progetto `~/nx679j-stock/`): `runtime-evidence-20260711/{interrupts.txt,dmesg.txt}` (nota: **le righe del touch** — `IRQ:404`, `rom_pid`, `FW-State` — vengono da `phase2-failure-analysis/artifacts/crash-capture-20260702-185014/`, non da qui), `experiments/20260917-boot-chain/live/byname.txt`, `port-work/device-facts-*.md`, `current-fastboot-getvar.txt`, `vendor_rd_build/first_stage_ramdisk/fstab.qcom`, `experiments/20260916-122926-native-baseline/{adb-mounts.txt,adb-props.txt,current-device/running_fdt_20260916.dts}`, `experiments/20260920-wifi-luci/STATO-ATTUALE.md`, skill `nx679j-openwrt`.

### 1. SoC, identità e periferiche principali

| Voce | Valore (stringa trovata) |
|---|---|
| Modello | `NX679J` / device `NX679J-UN` (REDMAGIC 7), produttore `nubia` |
| SoC | `ro.soc.model` = `SM8450`, `ro.soc.manufacturer` = `QTI`, `ro.board.platform` = `taro` (Waipio) |
| Modello FDT runtime | `Qualcomm Technologies, Inc. Waipio MTP with PM8010`; compatible `qcom,waipio-mtp`, `qcom,waipio`, `qcom,mtp`; FDT 748187 byte |
| Indici boot | `ro.boot.dtb_idx=5`, `ro.boot.dtbo_idx=35`; board-id `0x10008` (nel FDT base; l'overlay 35 porta lo stesso valore) |
| Firmware stock | `SKQ1.211113.001` / `NX679J_UNCommon_V3.11` / `NX679J_Z69_UN_ZML0S_V311` |
| Kernel | `5.10.66-android12-9-00005-gf6e6376090be-ab8060604` (vermagic `5.10.66-gki-g491fe99db339`); `Linux (none) 5.10.66-… #1 SMP PREEMPT Fri Jan 7 14:51:36 UTC 2022 aarch64` |
| Storage | UFS `soc/1d84000.ufshc` (LUN `/dev/sda` … `/dev/sdf`) |
| USB | controller `a600000.dwc3`; gadget NCM `18d1:4ee7` |
| AVB / A-B | `ro.boot.flash.locked=0`, `verifiedbootstate=orange`, `ro.build.ab_update=true`, dynamic partitions; slot A = Android+Magisk (oracolo), slot B = OpenWrt |
| Seriale | `3dbd****` (anche USB iSerial sul path 900e) |
| Console kernel | `console=ttyMSM0,115200n8`, `loglevel=6` |

### 2. Display

| Voce | Valore (stringa trovata) |
|---|---|
| Compatible pannello (cmdline) | `msm_drm.dsi_display0=qcom,mdss_dsi_r6130_1080_2400_amoled_cmd:` |
| Nodo DT | `qcom,mdss_dsi_r6130_1080_2400_amoled_cmd` (running_fdt 20260916, riga 27088) |
| `qcom,mdss-dsi-panel-name` | `"dsi vdtr6130 1080 2400 amoled command dphy panel"` |
| Tipo | `qcom,mdss-dsi-panel-type = "dsi_cmd_mode"` (command mode), `qcom,mdss-dsi-panel-physical-type = "oled"` |
| Risoluzione | `qcom,mdss-dsi-panel-width = <0x438>` = **1080**, `qcom,mdss-dsi-panel-height = <0x960>` = **2400** |
| Framerates nel DT (5 timing) | `0x3c` (60), `0x5a` (90), `0x78` (120), `0x90` (144), `0xa5` (165) — tutti 1080x2400 |
| Porch (tutti i timing r6130) | `v-front-porch <0x28>` (40), `v-back-porch <0x12>` (18), `v-pulse-width <0x02>` (2), `h-front-porch <0x14>` (20), `h-back-porch <0x14>` (20), `h-pulse-width <0x02>` (2) |
| Identità pannello (da skill) | **R6130** (driver `vtdr6130`), 1080x2400 AMOLED, command mode, 4 lane DPHY + DSC, **TE su tlmm 82**, **reset su tlmm 24**; DT `dsi-panel-r6130-nubia-dsc-fhd-cmd-dphy.dtsi`; **nessun backlight hardware** (luminosità via DCS `0x51`, `bl_ctrl_dcs`) |
| Controparte mainline (crediti) | `panel-visionox-vtdr6130.c`, compatible `visionox,vtdr6130` |
| DRM runtime | `/dev/dri/card0`, connettore `card0-DSI-1`, driver `msm_drm 1.4.0`; **nessun `/dev/fb0` legacy** |
| Modo attivo (GETCRTC/GETCONNECTOR) | `1080x2400x90cmd`, clock `248410`, `ht=1122 vt=2460`; 5 modi: `90/165/144/120/60cmd`; CRTC **152**, FB **57**, `pitch[0]=4352` |
| Revisione HW SDE (dmesg) | `sde hardware revision:0x80010000` |
| Framebuffer UEFI (PCD) | 1080x2400, 32 bpp; Display Reserved `0xB8000000` size `0x02B00000` |
| Collasso idle | `IDLE_POWERCOLLAPSE_DURATION` = **58 ms** (vincolo che ha dettato l'architettura della UI) |

### 3. Touch (Goodix, IC Berlin)

| Voce | Valore (stringa trovata) |
|---|---|
| Nome device | `nubia_goodix_ts` (`nubia_goodix_ts.0`), alias `gtx8_i2c`; driver vendor `goodix_berlin_driver_v1.0.1` (`goodix_brl_i2c.c`), modulo `goodix_core.ko` da `/vendor/lib/modules/` |
| Init (log) | `[GTP-INF][goodix_ts_core_init:2578] Core layer init:v1.2.2`; `[GTP-INF][goodix_i2c_bus_init:256] Goodix i2c driver init`; `[GTP-INF][goodix_get_ic_type:165] ic type is BerlinA`; `goodix_ts_core probe success` |
| Errori benigni noti (identici allo stock) | `[GTP-ERR][goodix_parse_dt:1322] can't find valid panel`; `[GTP-ERR][goodix_parse_dt_resolution:1173] failed get panel-max-p, use default` |
| Firmware | default `goodix_firmware.bin` + `goodix_cfg_group.bin`; iniettati in `/lib/firmware` del root vero (`/proc/1/root/lib/firmware`); `firmware_class.path=` in cmdline (applicazione da verificare nelle immagini correnti) |
| GPIO (dmesg) | `get iovdd-gpio[390] from dt`, `get reset-gpio[321] from dt`, `get irq-gpio[322] from dt` — **390/321/322 sono handle del pinctrl, non numeri GPIO**: i GPIO reali sono **iovdd 89, reset 20, irq 21** (`runtime-evidence-20260711/pinctrl.txt:267,198,199`) |
| **IRQ del controller** | `[GTP-INF][goodix_ts_irq_setup:1516] IRQ:404,flags:2` → `success register irq` (capture stock 2026-07-11: `404: 3 … msmgpio 21 Edge nubia_goodix_ts`); in un boot OpenWrt osservato **395** fermo a `3` — **valore definitivo da rimisurare** |
| Stato | Il chip **risponde** sul bus (`rom_pid: BERLIN`, `pid: 9897`, `FW-State: 0x102DC`) ma **non consegna eventi** (0 byte su eventN col dito); confine attribuito al runtime, non all'init |

### 4. Tasti laterali (PMIC PON)

| Voce | Valore (stringa trovata) |
|---|---|
| Tasto power | `pmic_pwrkey` con **`KEY_POWER` (bit 116)** |
| Tasto volume | `pmic_resin` = **bit 114 (VOLUMEDOWN)**: e' la ghiera che funziona. **Nota per chi riprende**: `/proc/interrupts` mostra anche `332: spmi-gpio 5 Edge volume_up` (`runtime-evidence-20260711/interrupts.txt:176`) — un ingresso volume-up **esiste a livello di linea GPIO** e non e' mai stato esplorato dal progetto |
| IRQ in `/proc/interrupts` | `342: … pmic_arb 20382103 Edge pmic_pwrkey`; `344: … pmic_arb 20316567 Edge pmic_resin` |
| Moduli necessari nella chroot | catena PON (`qcom-pon`; NON si chiama "qpnp-power-on") + `pm8941-pwrkey` + `pmic-pon-log` |
| Node path stock | `/devices/platform/soc/c42d000.qcom.spmi/…/pon_hlos@1300:resin/input/input3` (o `input4`) |

### 5. Modem

| Voce | Valore (stringa trovata) |
|---|---|
| Identità usata nel progetto | `Modem X65` (pagina LuCI: `Protocol: Modem X65 (SIM — adottato da script)`) |
| Catena | MSS su `remoteproc`, `S95*-modem` / `chain.sh` (log `/tmp/chain.log`), servitori `rmtfs` / `tqftpserv` / `pd-mapper` |
| Firmware | `modem.mdt` / `modem.b*`; `/rfs` con `modem_pr` (165 file); partizione `modem_a` = `/dev/block/sde6` montata `/vendor/firmware_mnt` |
| Porte | physdev `rmnet_ipa0` (annunciato), `qmapmux*` (log: `Using dynamic mux ID 2`), `rmnet_data0` |
| Indirizzi osservati | bearer `10.96.49.67/29` su `qmapmux2_1`; `rmnet_data0 10.181.52.50/30` |

### 6. Partizioni (GPT / by-name, come letto sul device)

LUN principale della **catena di avvio** = **`/dev/sde`** (74 partizioni); `super` / `userdata` / `rawdump` stanno su **`/dev/sda`**; le EFS su `/dev/sdf`.

| by-name | device | Cos'è |
|---|---|---|
| `boot_a` | `/dev/block/sde13` | boot slot A (Android+Magisk, baseline/oracolo); size `0x6000000` (96 MiB) |
| **`boot_b`** | **`/dev/block/sde41`** | **immagine di boot della porta** (header Android + kernel stock + ramdisk lz4 con `/init` che prepara il chroot OpenWrt); nodo `259:25`, size `0x6000000`; flash via `dd` + 2 letture fredde |
| `vendor_boot_a` / `_b` | `sde24` / `sde52` | header v4, 96 MiB; quella usata dal port è `sde52` |
| `dtbo_a` / `dtbo_b` | `sde17` / `sde45` | overlay DTB, 24 MiB; boot con `dtbo_idx=35` |
| `abl_a` / `abl_b` | `sde10` / `sde38` | ABL (ELF32 → volume UEFI → PE32+ LinuxLoader); **stock, mai flashato** |
| `uefi_a` / `uefi_b` | `sde1` / `sde29` | UEFI (`DefaultBDSBootApp = "LinuxLoader"`) |
| `vbmeta_a` / `_b` | `sde16` / `sde44` | `size 0x10000` (mai toccate) |
| `super` | `/dev/block/sda7` | dynamic partitions (system/ext/product/vendor/…); **9 GiB** |
| `userdata` | `/dev/block/sda12` | `/data` f2fs, **223,16 GiB** (`0x37BD4D3000` = 239.613.255.680 B, da `current-fastboot-getvar.txt:198`) |
| `misc` | `/dev/block/sda3` | `0x100000`; **lo stato slot A/B NON è qui ma nei bit GPT** |
| `rawdump` | `/dev/block/sda11` | 256 MiB; logger `crashlog-dump.sh` via `/dev/rd`; **azzerata al boot → volatile** |
| `modem_a` | `/dev/block/sde6` | vfat → `/vendor/firmware_mnt` |
| `modemst1/2`, `fsg`, `fsc` | `/dev/block/sdf2..5` | EFS del modem (usate da `rmtfs`) |

Note di formato immagine (slot B): **100663296 B fissi** (96 MiB), kernel stock invariato, ramdisk lz4. L'ultimo payload (v180) e' nel file riusato `boot_b-v90-mmwd.img`, md5 `f8d1324fed4c6eb5ddab4e58c5dbc923` (il nome del file non e' la versione: tutti i builder scrivono li').

### 7. Punti di mount del sistema runtime

| Percorso | Cos'è |
|---|---|
| `/owrt` | rootfs OpenWrt (chroot) preparato dal ramdisk; **dentro il chroot `/owrt` NON esiste** (i path OpenWrt sono alla radice) |
| `/owrt/{proc,sys,dev,tmp,run,dev/pts}` | montati da `switch.sh`; i mount interni vanno fatti DALL'INTERNO del chroot |
| `/vendor/firmware_mnt` | `mount -t vfat /dev/sde6`; firmware modem |
| `/lib/firmware` (root vero) | firmware touch iniettato (`/proc/1/root/lib/firmware`) |
| `/dev/rd` | nodo del `rawdump` (via `/proc/1/root/dev/rd`) |
| `/rfs` | root del servitore `tqftpserv` (firmware modem); **volatile** |
| `/tmp/ui-data.txt` | dati della UI (`key=value`, riscritto atomico ogni 3 s, ~217 chiavi) |
| `/tmp/ui-cell.txt` | cache cella+CA (refresh ogni ~30 s) |
| `/tmp/ui-standby` | comando di servizio: commuta lo standby |
| `/` (dentro il chroot) | OpenWrt 25.12.5, `armsr/armv8`, `aarch64_generic` — **volatile** (ram chroot) |

### 8. Endpoint di rete usati

| Endpoint | Contesto |
|---|---|
| **`10.0.0.1`** | device sul gadget **USB NCM `18d1:4ee7`**, `usb0` a `10.0.0.1/24`; host `10.0.0.2/24`. Canale preferito (rtt ~3 ms) |
| SSH | `ssh -i ~/.ssh/nx679j_key … root@10.0.0.1` (dropbear); `scp` **sempre `-O`**; host key cambia a ogni boot → `UserKnownHostsFile=/dev/null` |
| **`192.168.77.1`** | fallback Wi-Fi: AP **`NX679J-TEST`** 5 GHz su `phy0-ap0`, DHCP dnsmasq |
| LuCI | `http://10.0.0.1/` e `http://192.168.77.1/` (uhttpd solo su questi IP) |
| Bearer dati | `rmnet_data0 10.181.52.50/30`; bearer dinamico su `qmapmux2_1`; `ping 1.1.1.1` 0% loss |
| Recupero | con display appeso SSH non esegue: il recupero è il **tasto power fisico ~15-20 s** |

### Da completare

- **IRQ del touch nella build corrente**: `404` (stock) vs `395` osservato — rimisurare.
- **Mappatura stabile `eventN` → device**: **non esiste**; l'ordine cambia a ogni avvio (tasto cercato per nome, touch per capability).
- **Identificazione esatta del modem** (SDX65/SDX65M, revisione firmware, Equipment ID): non trovata; unica stringa `Modem X65`.
- **Fornitore del pannello**: DT/mainline dicono `vdtr6130`/`visionox`; la skill dice "R6130" — conferma del fornitore assente.
- **Geometria delle partizioni logiche dentro `super`**: non trovata.
- **DRAM totale**: solo indizi, valore reale non trovato.

---

## Manuale operativo

> Ricette verificate, **così come eseguite su questa macchina**. Ogni ricetta cita in parentesi il file/script che la contiene.
> `SES/` = `/home/user/nx679j-stock/experiments/20260920-wifi-luci/` (progetto host). Sul device: `/usr/lib/nx679j/modem/` — con caratteri speciali usare **sempre la glob** `ls -d /usr/lib/*/modem` (SKILL.md §Trappole).
> Accesso: `SES/nxssh.sh 'CMD'` oppure `ssh -i ~/.ssh/nx679j_key -o IdentitiesOnly=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=8 root@10.0.0.1` (USB gadget, rtt 3 ms; WiFi `192.168.77.1`). `scp` **sempre con `-O`**. Primo comando di ogni sessione: `SES/nxstate.sh` → `FASTBOOT | ANDROID | OPENWRT | OFFLINE` (nxstate.sh).

---

### 1. Accendere / spegnere lo schermo (standby)

**Progetto v180 — vincolante: lo standby è "un frame NERO + luminosità 0", con il link DSI VIVO.** Mai DPMS-off, mai unprepare: su questo tree uccidono il link command-mode e il pannello non risorge senza riavvio (`failed wait_for_idle: -110`, `wr_ptr_irq wait failed`) (ui-6-main.c `standby_enter()`/`standby_exit()`, STATO-ATTUALE.md §"2026-09-25 (sera) — STANDBY").

**Dal tasto laterale** (pmic_pwrkey, `KEY_POWER` = 116; il nodo può essere `event0` o `event2`, l'ordine cambia a ogni boot) (ui-6-main.c `key_event()`, STATO-ATTUALE.md §"Tasti laterali"):

| gesto | effetto |
|---|---|
| **click singolo** | spegne/accende lo schermo (toggle). Spento → il click risveglia subito; acceso → standby dopo la finestra del doppio click (350 ms) |
| **doppio click** | "attiva" l'elemento a fuoco (navigazione da tasti) |
| **pressione lunga ≥ 1,5 s** | "indietro" |
| **bilanciere volume** | scorre/seleziona (autorepeat limitato a una mossa/120 ms); `VOL UP` non è esposto al kernel su questo telefono (`pmic_resin` dichiara solo il bit 114) |
| `/tmp/ui-swap-vol` | se il verso del bilanciere risultasse invertito: creare il file e scambiare i due tasti (nessuna ricompilazione) |

**Da SSH (senza toccare il telefono)** — comando di servizio, la UI lo consuma al giro successivo del loop:

```sh
SES/nxssh.sh 'touch /tmp/ui-standby'        # un tocco = un toggle
```

**Verifica (obbligatoria, altrimenti è fede):**

```sh
SES/nxssh.sh 'cat /sys/class/backlight/panel0-backlight/brightness'   # 0 = schermo spento; a schermo acceso il valore salvato (misurato 2608)
SES/nxssh.sh 'grep -E "standby:|risveglio:" /tmp/display-late.log | tail -5'
python3 SES/obs_shot.py /tmp/phone.png      # webcam OBS-WebSocket (obs_shot.py) + ispezione visiva
```

Attesi: `standby: frame nero committato (link vivo)` + `standby: luminosita' a 0` (spento), oppure `standby: schermo riacceso` + `risveglio: luminosita' rimessa a N` (acceso) — e webcam **NERA** con dati che continuano ad avanzare (`grep TELEMETRIA`). In standby il battito `standby: loop vivo (nero) (frame N, tasti a/b aperti)` ogni 10 s dice che il loop committa ancora (ui-6-main.c §heartbeat).

**Trappole già pagate:** dopo il risveglio c'è una guardia di **600 ms** (`POWER_WAKE_GUARD_MS`): il click che ha svegliato è consumato e nessun evento può riarmare lo standby — il nodo del tasto consegna **due esemplari** (press+release) per un click solo (ui-6-main.c, STATO-ATTUALE.md §"Tasti laterali"). La funzione di spegnimento **non va armata** se il suo percorso di errore non è provato (lezione dell'incidente v171) (STATO-ATTUALE.md §"INCIDENTE 25/09 (sera)").

---

### 2. Leggere lo stato (telemetria UI, chiavi, bl/dpms)

**Un campione in una riga** (formato `U|ui_vivi|fps|release|retire|gap|irq|ping`) — nx679j-ui-sample.sh:

```sh
SES/nxssh.sh 'sh /usr/lib/*/modem/nx679j-ui-sample.sh'
```

Sano: `ui_vivi=1`, `fps≈45.4` (≈60 con `idle_pc_disable` attivo), release = retire (`done/commit` uguali), `gap` ≤ 25 ms, `irq` `msm_drm` ≈136 (45 fps × 3), `PING_OK`. `pidof` conta anche gli zombie: il campionatore li esclude leggendo lo stato in `/proc/PID/stat` (nx679j-ui-sample.sh; ESITO-v135-v137-ui-display.md §Addendum v138–v140).

**Telemetria della UI** (ogni 600 frame, con pagina corrente e numero di chiavi) — ui-6-main.c, `/tmp/display-late.log`:

```sh
SES/nxssh.sh 'grep TELEMETRIA /tmp/display-late.log | tail -1'
# TELEMETRIA frame=... fps=45.4 gap_max_ms=25 fence_timeout=0 chiavi=217 pagina=MODEM/Stato
```

**Telemetria dei dati della UI** (`/tmp/ui-data.txt`, piatto `key=value`, riscritto atomico ogni 3 s dal fetcher; ~217 chiavi) — nx679j-ui-fetch.sh:

```sh
SES/nxssh.sh 'wc -l /tmp/ui-data.txt'                                  # numero di chiavi (atteso ~217)
SES/nxssh.sh 'grep -E "^(sys|if|modem)\." /tmp/ui-data.txt'            # uptime/load/mem/temp, interfacce, salute modem
SES/nxssh.sh 'grep -E "^(cell|ca)\." /tmp/ui-data.txt; cat /tmp/ui-cell.txt'   # cella + carrier aggregation (cache ogni 10 cicli ≈30 s)
SES/nxssh.sh 'cat /tmp/link-health'                                    # "up <uptime> fails=0" — salute del bearer (nx679j-link-watch.sh)
```

**Schermo: bl e dpms.** La UI **non tocca mai DPMS** (contatore `dpms_calls` deve restare 0 — ui-6-main.c, ui-nav-test.c). Quindi:

```sh
SES/nxssh.sh 'cat /sys/class/backlight/panel0-backlight/brightness'    # "bl": 0 = schermo spento
scp -O -i ~/.ssh/nx679j_key SES/drm-probe root@10.0.0.1:/tmp/          # drm-probe NON e' nel ramdisk: va copiato a mano
SES/nxssh.sh '/tmp/drm-probe DPMS'   # mostra i valori AMMESSI; il valore corrente di DPMS sta sul CONNETTORE 56 (leggerlo via OBJ_GETPROPS) - atteso On SEMPRE
SES/nxssh.sh '/tmp/drm-probe --crtc 152 | grep -A1 idle_pc_state'      # protezione idle collapse: atteso valore 2 (idle_pc_disable)
SES/nxssh.sh 'cat /sys/kernel/debug/dri/0/clients; pidof nx679j-ui'    # master + PID della UI
```

Verifica del risparmio (se serve, non a occhio): in standby i rail DSI/pannello devono risultare off in `regulator_summary` e in `clk_summary` non devono esserci byte/pixel/link clock; al wake **nessuna** occorrenza di `kickoff timed out` / `-110` in dmesg (references/display-standby.md §Verifica — vale per il percorso DPMS, oggi non usato).

---

### 3. Far ripartire la UI / il launcher

**(a) Scambio a caldo della UI (~15 s, senza flash né reboot)** — possibile **solo** grazie a `idle_pc_disable`, che è la rete di sicurezza: il pannello sopravvive alle pause solo con quel flag (references/display-ui-touch.md §"Scambio a caldo", ESITO-v135-v137-ui-display.md §Addendum v138–v140):

```sh
SES/nxssh.sh 'test -f /tmp/ui-idle-pc-disable && echo OK || touch /tmp/ui-idle-pc-disable'  # la proprieta' e' appiccicosa: senza il flag il pannello collassa
SES/nxssh.sh 'kill -9 $(pidof nx679j-ui)'                    # scp del binario in esecuzione da' "Text file busy": si uccide PRIMA
scp -O -i ~/.ssh/nx679j_key SES/nx679j-ui root@10.0.0.1:/usr/lib/nx679j/modem/nx679j-ui    # (o /tmp + cp, path con glob)
SES/nxssh.sh 'setsid /usr/lib/*/modem/nx679j-ui >>/tmp/display-late.log 2>&1 </dev/null &'
SES/nxssh.sh 'sh /usr/lib/*/modem/nx679j-ui-sample.sh'       # verifica: ui_vivi=1, fps che sale, fence che avanzano
```

Rischio dichiarato: su ~8 scambi riusciti **uno** ha ucciso il link (pannello morto fino al reboot). Regola: farlo **solo quando un reboot è possibile subito dopo**, mai mentre si sta verificando altro (references/display-ui-touch.md). `kill -9` **non** uccide una UI bloccata in stato D (commit su CRTC spento): l'unica uscita è il riavvio (STATO-ATTUALE.md §"INCIDENTE 25/09 (sera)").

**(b) Rilancio del launcher** (`nx679j-display-late.sh`): al boot è lui a caricare i moduli vendor → attendere il bind del goodix → ESD kill + disarmo panic SDE → `touch /tmp/ui-idle-pc-disable` → avvio UI (kiosk3 di riserva, drmtest di riserva) (nx679j-display-late.sh). Marcatori/flag:

- `DL-FIRED` senza `DL-CLEAN` nello slot 410 → al boot successivo il launcher **riprova** (fino a 3 tentativi, poi salta il display) (nx679j-display-late.sh).
- Per **riarmare** il display al prossimo boot azzerare lo slot 410: `SES/nxssh.sh 'dd if=/dev/zero of=/proc/1/root/dev/rd bs=32768 seek=410 count=1 conv=notrunc'` (RIPRESA.md §Procedura di flash punto 5; nx679j-display-late.sh `mark()`).
- Flag di servizio: `/tmp/no-ui` (non avviare la UI), `/tmp/no-kiosk3` (niente fallback kiosk3), `/tmp/ui-dump` → la UI scrive **una volta** il frame disegnato in `/tmp/ui-dump.ppm` (ispezione da remoto senza webcam; a schermo nero non esporta nulla) (nx679j-display-late.sh, ui-6-main.c).

Verifica del launcher:

```sh
SES/nxssh.sh 'grep -E "UI ATTIVA|kiosk3|drmtest|idle_pc_disable|panic SDE|ESD KILL" /tmp/display-late.log | tail -12'
```

**(c) La UI non risponde al tocco.** Il controller del touch è guasto **hardware** (IRQ `nubia_goodix_ts` fermo a ~3 anche su Android stock): la UI su display **non è navigabile a dito**, restano i tasti laterali, SSH e LuCI (STATO-ATTUALE.md §"ESITO TOUCH 25/09 (notte)"). Prima di sospettare il tocco guardare il file dati; per provare il percorso input della UI si usa il device sintetico: `/usr/lib/*/modem/uinput-touch <x> <y> --hold 7` (uinput-touch.c, references/display-ui-touch.md §"Verifica da remoto").

---

### 4. Riavviare il telefono in sicurezza + ripristino dello slot A

**Regola d'oro: la prova di un riavvio è l'uptime, non il comando.** Registrare `U1` prima, poi leggere `U2`: riavvio avvenuto ⇔ `U2 < U1` (STATO-ATTUALE.md §Metodo punto 3, references/ui-clock-overflow-and-standby.md §Verifica punto 3).

```sh
SES/nxstate.sh                      # stato in <3s: FASTBOOT|ANDROID|OPENWRT|OFFLINE
SES/nxstate.sh wait OPENWRT 180     # attesa della condizione, senza sleep fissi (nxstate.sh)
SES/nxssh.sh 'cut -d. -f1 /proc/uptime'                     # U1 / U2
```

**Riavvio normale:**

- dal display: pagina **SISTEMA → RIAVVIA** (due tocchi per confermare; esegue `ubus call system reboot`) — ui-5-pages.c `A_REBOOT`, ui-7-menu.c;
- da SSH: `SES/nxssh.sh 'reboot -f'` (`/sbin/reboot -f`; il `reboot` busybox non attraversa il chroot — alternativa `echo b > /proc/sysrq-trigger`) (references/boot-flash-device.md, RIPRESA.md);
- se la UI e' incantata → **`SES/nxssh.sh 'sh -s' < SES/rst.sh`** (lo script e' **device-side**: toglie `/tmp/ui-standby`, `sync`, e dopo 2 s `/sbin/reboot -f` in background) (rst.sh).

**Recupero quando il device è vivo ma non esegue comandi** (hang display: SSH si autentica ma non esegue nulla, ping 100 % perso, nessuna adb/seriale): **solo il tasto power** (~15-20 s di pressione lunga → reset hardware del PMIC). Non insistere con `ssh … reboot -f` (STATO-ATTUALE.md §"recupero remoto impossibile dopo l'hang", references/display-touch.md §8, STATO-ATTUALE.md §"TOUCH MORTO").

**Spegnimento VERO** (taglia i rail delle periferiche; un reset del SoC **non** basta per una periferica incantata) — off-vero.sh:

```sh
sh SES/off-vero.sh                  # adb shell su -c "reboot -p" + verifica che NON risponda piu' (off-vero.sh)
# riaccensione: tasto laterale (azione utente)
```

**Ritorno dallo slot A (Android) allo slot B (OpenWrt)** — torna-b.sh (usa adb; l'oracolo Android serve solo per guardare come il driver/HWC pilotano l'hardware):

```sh
sh SES/torna-b.sh                   # adb reboot bootloader -> fastboot --set-active=b -> fastboot continue -> attesa OpenWrt
# poi verifica: U=..., ui=<pid>, ping=OK, md5 del binario UI, /dev/input, ultima riga TELEMETRIA
```

**Recupero immagine cattiva (fastboot)** — il device va messo in fastboot **da spento con Volume Giù + Power**; attenzione: **EDL NON funziona su questa unità**, la recovery è lo slot A (references/boot-flash-device.md):

```sh
sh SES/ripristino-v175.sh           # gate md5 98ac83f587bd6bb29ae263b48ebd35c7 -> fastboot --set-active=b -> flash boot_b -> reboot
```

---

### 5. Aggiornare l'immagine (build → verifica nel ramdisk → flash con i gate → reboot → verifica)

**1) Build** (inietta l'artefatto **dopo** i tar di persistenza; fail-closed; snapshot documenti automatico) — build-v90.py + doc-backup.sh:

```sh
cd SES && python3 build-v90.py       # prima riga utile: doc-backup.sh -> docs-snapshots/<data-ora>/
# atteso in coda: "... VERIFICA PASS -> /home/user/nx679j-stock/experiments/20260920-wifi-luci/boot_b-v90-mmwd.img"
md5sum SES/boot_b-v90-mmwd.img       # attuale: f8d1324fed4c6eb5ddab4e58c5dbc923
```

Il build **non scarica nulla**: prende i binari già presenti in `SES/` (UI: `SES/nx679j-ui`, con assert su presenza e dimensione >150000 B) e li inietta nel ramdisk (build-v90.py). Gli assert coprono il **payload finale** (UI presente, il launcher la invoca e conserva il fallback kiosk3, gate display a 70 s, `hold=86400` su ≥3 righe di lancio sessione, payload `luci.nx679j-modem`, ecc.). Trappola nota: se un assert di `display-late` fallisce, la copia di riferimento è quella in `SES/` e l'assert va allineato al file giusto (RIPRESA.md §"BUG DEL BUILD").

**2) Verifica la UI DENTRO l'immagine, non quella sul disco** (regola esplicita: un estrattore troncato produce falsi "non trovato") — build-v90.py (`build-v64/check/owrt/...` = payload estratto dal ramdisk), references/ui-clock-overflow-and-standby.md §Verifica punto 2:

```sh
md5sum SES/nx679j-ui                                              # 5a8ca8b812b787e4eed979df4f5c6c1d (v180)
md5sum SES/build-v64/check/owrt/usr/lib/*/modem/nx679j-ui         # DEVE coincidere
stat -c '%s' SES/build-v64/check/owrt/usr/lib/*/modem/nx679j-ui   # 497008 B
```

**3) Flash con i gate** — `nx679j-flash-v128.sh` (script di riferimento, oggi in `~/.hermes/profiles/kernel-re/cache/scratch/`). Catena di gate, nessuno aggirabile:

1. **backup** del boot attuale con lettura fredda (`dd if=/dev/sde41 bs=1M count=96` → `boot_b-current-<label>.img`) e stampa di `BACKUP_MD5`;
2. **gate del backup**: `[ "$BMD5" = "<atteso>" ]` — se non combacia **ci si ferma** e si **aggiorna il valore atteso col contenuto reale del device** (mai aggirare il controllo: è l'unica cosa che ha impedito di scrivere immagini sbagliate) (references/modem-cell-and-ca.md §"Gate del flash", STATO-ATTUALE.md §Metodo punto 5);
3. **gate locale**: `EXPECT_MD5` confrontato con l'md5 dell'immagine su host (oggi `f8d1324f…`);
4. `scp -O` dell'immagine su `/tmp/v128.img` e **ri-verifica dell'md5 sul device**;
5. scrittura: `dd if=… of=/dev/sde41 bs=1M count=96 oflag=direct conv=fsync` + `sync -f`;
6. **due letture fredde** (`iflag=direct`): entrambe devono dare `EXPECT_MD5` → stampa **`WRITE_GATE_PASS`** (altrimenti `WRITE_GATE_FAIL` e si esce);
7. **lo script NON riavvia**: il reboot è un passo separato e deliberato.

```sh
sh /home/user/.hermes/profiles/kernel-re/cache/scratch/nx679j-flash-v128.sh
# in coda deve comparire: BACKUP_MATCHES_<label> OK / REMOTE_IMAGE_MD5=... OK / WRITE_GATE_PASS
```

Protocollo manuale equivalente (SKILL.md §3, RIPRESA.md §Procedura di flash): **il nodo va ricreato a ogni boot** e se esiste come file regolare il `mknod` fallisce in silenzio → `rm -f` prima; `bs=4M oflag=direct conv=fsync`; `sync -f`; due letture fredde; **solo se entrambe coincidono** si riavvia.

**4) Ripristino del marcatore display** (se si vuole che il launcher riparta da zero / se è `DL-FIRED` senza `DL-CLEAN`):

```sh
SES/nxssh.sh 'dd if=/dev/zero of=/proc/1/root/dev/rd bs=32768 seek=410 count=1 conv=notrunc'
```

**5) Reboot e verifica** (uptime + artefatto + telemetria):

```sh
SES/nxssh.sh 'reboot -f'; SES/nxstate.sh wait OPENWRT 180
SES/nxssh.sh 'cut -d. -f1 /proc/uptime'                       # U2 < U1
SES/nxssh.sh 'md5sum /usr/lib/*/modem/nx679j-ui'              # = md5 locale del binario iniettato
SES/nxssh.sh 'sh /usr/lib/*/modem/nx679j-ui-sample.sh'        # ui vivo, fps, release=retire, ping
SES/nxssh.sh 'grep TELEMETRIA /tmp/display-late.log | tail -1'
```

---

### 6. Recupero del modem

**Modello attuale (v130+): ModemManager è DISMESSO.** L'L3 lo porta la **catena** (`qmi-qrtr-observed`: bearer + IP + route) e lo stato lo serve il plugin `luci.nx679j-modem` + il supervisore `nx679j-link-watch.sh`; MM viene neutralizzato a **ogni** boot da `nx679j-boot-services.sh` (ESITO-v130-modem-x65.md, nx679j-boot-services.sh, DIAGNOSI-bearer-1h.md §"SOSTITUIRE ModemManager"). **Con la catena che funziona non si tocca niente**: si interviene solo su regressione riprodotta (references/regole-utente.md §Metodo punto 5).

**Diagnosi (prima di agire):**

```sh
SES/nxssh.sh 'cat /tmp/chain.log | tail -20'      # cerca "STOP bootstrap" / "MSS=offline" / "STOP data (niente bearer)" / "DONE"
SES/nxssh.sh 'cat /tmp/mm-wd.log /tmp/link-watch.log /tmp/link-health 2>/dev/null | tail -20'
```

**Prontezza = risposta QMI REALE, non un nome di servizio in QRTR** (`service=4096` è un **falso positivo**):

```sh
SES/nxssh.sh '/tmp/qmi-qrtr raw 2 0001002d000000 | grep msg_id=0x002d'        # vero = modem pronto
SES/nxssh.sh '/tmp/qmi-qrtr lookup 49 | grep "service=49 version=1 instance=1 node=1 "'   # lookup del servizio (nx679j-modem-prepare.sh)
```

**Rilancio a mano della catena** (serve quando il boot non l'ha avviata o si è fermata) — SKILL.md §4, nx679j-mm-watchdog.sh v97:

```sh
D=$(SES/nxssh.sh 'ls -d /usr/lib/*/modem')                     # staging: senza i symlink in /tmp la catena fallisce IN SILENZIO
SES/nxssh.sh 'for f in /usr/lib/*/modem/*; do b=${f##*/}; [ -e "/tmp/$b" ] || ln -s "$f" "/tmp/$b"; done'
SES/nxssh.sh 'setsid sh /usr/lib/*/modem/chain.sh >>/tmp/chain-early.log 2>&1 &'
SES/nxssh.sh 'grep -c "DONE" /tmp/chain.log'                   # poll dinamico 2-5 s, mai sleep lunghi
```

**Ordine di avvio pulito** (è quello che ha portato la catena da 265 s a 65 s): attendere il **rproc MSS** (`4080000.remoteproc-mss` si registra tardi) → staging symlink `/tmp` → catena; il segnale di salute è la **sessione QMI viva**, non un oggetto di ModemManager (`pidof qmi-qrtr-observed` ≥ 1 → marcatore slot 412 `LINK-OK`) (nx679j-mm-watchdog.sh, nx679j-modem-prepare.sh).

**Se l'MSS non si registra affatto** (dmesg: `releasing 4080000.remoteproc-mss` a ~19 s): **reboot pulito → il MSS torna** (SKILL.md §4). **Attenzione, contraddizione aperta:** `nx679j-mm-watchdog.sh` (v103) fa `unbind/bind` del driver MSS come "unica cura in-place", mentre la SKILL §4 dice **NON fare il rebind del MSS (crasha il device)** → vedi "Da completare".

**Bearer morto / da rinnovare** — nx679j-link-watch.sh (supervisor: probe ICMP legato all'interfaccia ogni ~60 s, 2 evidenze → rinnovo; stesso processo perché su QRTR il CID non attraversa i processi):

```sh
SES/nxssh.sh 'touch /tmp/nx679j-link-renew.request'      # rinnovo su richiesta (LuCI/ubus o a mano)
SES/nxssh.sh 'tail -5 /tmp/link-watch.log; cat /tmp/link-health'
```

Il rinnovo corretto è **sessione QMI + riapplicazione dell'L3 + rimozione dell'indirizzo stantio** (la nuova sessione ottiene una **sottorete diversa**): `dpm-session 4 1 2 23 86400` + `wds-session internet.it 4 1 1 86400`, poi `ip -4 addr add` / `route replace default` dalla nuova sessione (nx679j-link-watch.sh `renew()`, DIAGNOSI-bearer-1h.md §3). Il vecchio "cliff" a 1 ora era il **nostro** `hold=3600`, non il modem: ora è `86400` e la verifica lunga (72 min) non ha più visto il guasto (DIAGNOSI-bearer-1h.md §Chiusura, ESITO-v131-v132.md).

---

### 7. Check cella / carrier aggregation

```sh
SES/nxssh.sh 'sh /usr/lib/*/modem/nx679j-cell.sh'                       # emette cell.* e ca.* (key=value)
SES/nxssh.sh 'cat /tmp/ui-cell.txt; grep -E "^(cell|ca)\." /tmp/ui-data.txt'   # cache del fetcher (~30 s)
```

Comandi diretti (sola lettura, **sempre `2>&1`**: `qmicli` scrive i dati su stderr) — nx679j-cell.sh, references/modem-cell-and-ca.md:

```sh
qmicli -d qrtr://0 --nas-get-rf-band-info  2>&1      # banda attiva, EARFCN, larghezza; PIU' blocchi "Band Information" = CA multi-banda attiva
qmicli -d qrtr://0 --nas-get-cell-location-info 2>&1 # serving cell (PCI, RSRP/RSRQ/RSSI, PLMN, TAC), vicine intra/inter-frequenza
qmicli -d qrtr://0 --nas-get-signal-info 2>&1        # aggregato LTE
qmicli -d qrtr://0 --nas-get-lte-cphy-ca-info 2>&1   # per portante: PCI, EARFCN, larghezza, banda, stato (activated/deactivated/deconfigured)
strings /usr/bin/qmicli | grep -c nas-get           # >11 = collection "full" (la CA c'e'); 11 = minimal/basic (comando assente)
```

Regole: **una query = un processo**, niente `--device-open-sync` / `--device-open-net` (distruggono lo stato della sessione); sul device **non esiste `timeout`** (busybox): i pattern con `timeout` falliscono in silenzio; le query di sola lettura **non** disturbano la sessione dati (verificato: 200 chiamate + 25 letture SIM senza un errore) (references/modem-cell-and-ca.md, ESITO-v130-modem-x65.md §Stress del polling). Semantica: errore QMI **74** `InformationUnavailable` = **nessuna CA allocata** (non è un guasto), **94** `NotSupported` = firmware; la CA si vede **solo con traffico**; l'RSRP per portante si ricava **incrociando il PCI** con la cell-location; il contributo per portante **non è misurabile** dal SO → in UI va etichettato **"stima"**, mai spacciato per misura (references/modem-cell-and-ca.md, STATO-ATTUALE.md §FASE 12).

---

### 8. Regole di sicurezza imparate (vincolanti)

1. **Mai scrivere sui nodi del touch driver** `fwupdate/result`, `get_rawdata`, `esd_info` → **crash del kernel**; mai `unbind` di `nubia_goodix_ts` (kernel crashato e riavvio immediato, verificato) (STATO-ATTUALE.md §"ESITO TOUCH 25/09 (notte)", references/ui-clock-overflow-and-standby.md §Touch).
2. **Mai committare su un CRTC spento/di stato ignoto**: su CRTC inattivo il commit atomico vendor è un **no-op silenzioso** → `kickoff timed out` / `wait_for_idle -110` / watchdog; e il processo resta in **stato D** dove `kill -9` **non** lo uccide (l'unica uscita è il riavvio). Un commit DRM su stato non noto va fatto **NONBLOCK con timeout** (references/display-standby.md §Regole operative, STATO-ATTUALE.md §"INCIDENTE 25/09 (sera)").
3. **Mai spegnere il link DSI**: DPMS-off / unprepare su questo tree uccidono il link command-mode e non risorge senza riavvio; lo standby valido è **frame nero + backlight 0, link vivo** (il modello dello stock Android: `SetDisplayState: state=0, teardown=0`) (STATO-ATTUALE.md §"2026-09-25 (sera) — STANDBY", ui-6-main.c).
4. **Tenere `idle_pc_state=disable`** (proprietà CRTC 166, valore 2) *prima* di qualunque commit: la proprietà è **appiccicosa** (va scritta esplicitamente, `2` o `0`) e senza di essa una pausa >58 ms uccide il pannello (references/display-ui-touch.md §Trappole punto 3, ESITO-v135-v137-ui-display.md §Addendum v138–v140).
5. **Mai lanciare un client DRM quando nessun master è attivo**: `open("/dev/dri/card0")` diventa implicitamente master → `sde_rm_topology_get_topology_def invalid topology`, `sde_fence_signal extra signal attempt`, **154 fault SMMU** e device bloccato (serve power-cycle). Prima di ogni prova: `/sys/kernel/debug/dri/0/clients` + PID master (SKILL.md §8, STATO-ATTUALE.md §FASE 6).
6. **Verificare SEMPRE con la webcam** (pretesa dell'utente): 1920×1080, **raffica di 8-10 frame**, si tiene il più grande (il 4K è corrotto, i frame singoli escono verdi), ritaglio 1:1 per leggere i valori; un artefatto sul vetro non è evidenza. E verificare l'artefatto **dentro l'immagine**, non quello sul disco (references/ui-clock-overflow-and-standby.md §Verifica, references/regole-utente.md §Webcam).
7. **Mai sopprimere stderr** in diagnosi (`qmicli` scrive i dati su stderr: `2>/dev/null` butta via tutto) e mai fidarsi di `rc=$?` dopo una **pipe** (mente: verificare l'effetto — sysfs/netlink/log) (STATO-ATTUALE.md §Metodo punto 1, references/modem-cell-and-ca.md).
8. **Niente scorciatoie sui comandi**: sul device non esistono `timeout`, `stat -c`, `od`, `pkill`, `top` (busybox) → `wc -c`, `strings`, `head -c`; `scp` sempre `-O`; la host key dropbear cambia a ogni boot → `UserKnownHostsFile=/dev/null`; i path con caratteri speciali non passano nel doppio SSH → **glob** (SKILL.md §Trappole).
9. **Panic SDE disarmato**: `echo 0 > /sys/kernel/debug/dri/0/debug/panic` — il launcher lo fa a ogni boot; con `panic_on_err=1` un timeout display chiama `panic()` e il watchdog APSS riavvia il telefono (STATO-ATTUALE.md §FASE 8, nx679j-display-late.sh). Non scrivere gli altri nodi debug (`dump`, `enable`, `esd_trigger`): solo lettura (SKILL.md §6).
10. **Un rimedio automatico va eseguito una volta sola**, o protetto da una condizione che può essere vera **solo** quando il guasto c'è (la regressione del `pmic_glink` ricaricato a ogni boot ha abbattuto la sessione dati) (STATO-ATTUALE.md §FASE 12).
11. **Mai giudicare da un boot solo** (la fase ModemManager varia di ~90 s: v101/v102 annullate perché giudicate sul rumore); **max 1 azione utente per test**, e **i reboot li fa l'utente**; mai fermarsi a chiedere, ma mai dichiarare falsi successi ("verificato" ≠ "plausibile") (references/regole-utente.md, RIPRESA.md §Trap pagate).
12. **Dopo un hang display non riprovare le vie remote**: SSH muto, ping 100 % perso, nessuna adb/seriale → chiedere subito il **power-cycle** (STATO-ATTUALE.md §"recupero remoto impossibile dopo l'hang").

---

## Archivio del progetto: dove sta cosa

Radice dell'albero: `/home/user/nx679j-stock/`. Tutte le path sono relative a questa radice. Le date di questa sezione sono quelle rilevate sull'albero di lavoro originale (lette con `ls -l`/`stat`): nei file di questo pacchetto i mtime sono quelli della copia (28-29/09/2026), quindi quelle date non sono riproducibili qui.

**Cosa è incluso.** Questa sezione descrive l'albero di lavoro *completo*; non tutto è nel pacchetto. L'elenco esatto dei file presenti è in `MANIFEST-files.txt`, ciò che è stato rimosso è nella sezione «Revisione per la pubblicazione». I file citati qui e assenti da entrambi — immagini di boot, catture e media delle prove display, alberi di terzi non
ridistribuibili — non fanno parte del pacchetto, e così le cartelle che esistevano solo per la nota di duplicati
(l'elenco è in `DEDUPLICA.md`).

I **disassemblaggi completi** e i **device tree decompilati** non sono nel pacchetto: si
rigenerano dal proprio dispositivo con le procedure di `ESTRAZIONE-BLOB.md`, mentre le analisi che li
citano restano nel testo.

**Mappa verso questo archivio.** Le path di questa sezione sono dell'albero locale; nell'archivio pubblicato le stesse cose stanno qui:

| Radice originale (`/home/user/nx679j-stock/`) | In questo archivio |
|---|---|
| `NX679J_*.md`, `OPENWRT-PORT-STATUS.md`, `authoritative-bootchain-verification.md`, `WIFI-LUCI-HANDOFF-*.md`, `SESSION-REPORT-*.md`, `V10-BUILD-PLAN.md`, `MODEM-HANDOFF*.md`, `PROMPT-NUOVA-SESSIONE-MODEM.md` | `01-documenti/` |
| `experiments/20260920-wifi-luci/*.md` (stato, esiti, diagnosi, decisioni) | `01-documenti/wifi-luci/` |
| `experiments/20260920-wifi-luci/` (script, builder, tar) | `02-sorgenti/wifi-luci/` |
| `docs-snapshots/` | `01-documenti/snapshots/` |
| `experiments/20260916-*`, `experiments/20260917-*`, `bootloader-re/`, `nx679j-openwrt-clean/`, `edl-recon-20260717/` | `05-bootchain-re/` |
| `port-work/`, `idlepc_research/`, `re-dsi-research/` | `06-ricerche/` |
| `experiments/refs/`, `qrtr/`, `openwrt-rootfs/`, `native-openwrt-usb-build/`, `verified-v311/`, `verified-port/`, `hypervisor-bypass/`, `radice/`, `phase2-failure-analysis/` | `09-albero-originale/` |
| `raw-runtime/` (prove dal device) | `07-evidenze-runtime/` |
| i tar di persistenza iniettati | `04-persistenza/` (sanificati) |

**Artefatti non inclusi**: immagini di boot, partizioni, moduli `.ko`, firmware, DTB/DTBO vendor, ramdisk e `init` vendor, rootfs OpenWrt. Le voci di questa sezione che li nominano restano descrizioni dell'albero locale; per ricostruirli vedi `ESTRAZIONE-BLOB.md`.

**Prefisso dei file di payload.** I file iniettati nel ramdisk stanno in `experiments/20260920-wifi-luci/` e hanno il prefisso `nx679j-` (es. `nx679j-ui.c`, `nx679j-mm-standard-boot.sh`). Se in una copia di questo documento quel prefisso appare troncato o come `nx679j-`, il file reale e' comunque quello con `nx679j-`: si trova con `ls *-ui.c`, `ls *-mm-standard-boot.sh`, ecc.

### 1. Documenti — da dove si comincia

| Path | Cosa contiene | Quando |
|---|---|---|
| `experiments/20260920-wifi-luci/STATO-ATTUALE.md` | Documento **master** (119 KB): stato tecnico completo, storico FASI 1-11, bring-up modem standard, misure di boot. E' il primo file da leggere dopo RIPRESA | riscritto a ogni fase; ultimo 2026-09-25 17:56 |
| `experiments/20260920-wifi-luci/RIPRESA.md` | Punto di ingresso operativo: «dove sta cosa», ultima immagine buona e md5, sorgenti toccati | 23/09 23:33 |
| `experiments/20260920-wifi-luci/ESITO-v129.md` | v129: proto senza route riportata (esito boot dcad575a) | 24/09 20:45 |
| `experiments/20260920-wifi-luci/ESITO-v130-modem-x65.md` | v130: modem X65 senza ModemManager, tempi U (dati 63 s, kiosk 85 s) | 24/09 21:50 |
| `experiments/20260920-wifi-luci/ESITO-v131-v132.md` | v131/v132: campo SIM PIN via ubus + fix del bug introdotto in v131 | 24/09 22:10 |
| `experiments/20260920-wifi-luci/ESITO-v135-v137-ui-display.md` | v135-v137: UI sul display del telefono; immagine in flash v137 + md5 | 24/09 23:24 |
| `experiments/20260920-wifi-luci/DIFETTO-rinnovo-v133.md` | Difetto nel rinnovo automatico del bearer, trovato con caduta simulata e corretto | 24/09 22:23 |
| `experiments/20260920-wifi-luci/DIAGNOSI-bearer-1h.md` | Il bearer QMI dura 1 ora: evidenze FATTO/IPOTESI, ownership di addr e default route | 24/09 20:36 |
| `experiments/20260920-wifi-luci/DIAGNOSI-v121-non-flashata.md` | Correzione di evidenza: v121/v122 costruite ma **non** flashatе, con hash | 24/09 06:58 |
| `experiments/20260920-wifi-luci/SPEC-STATO-MODEM.md` | Specifica dello strato di stato modem per sostituire ModemManager (qmicli su QRTR) | 24/09 19:46 |
| `experiments/20260920-wifi-luci/PROVA-MM-non-passivo.md` | Prova decisiva (boot v128) che ModemManager non e' passivo sul data path | 24/09 19:50 |
| `experiments/20260920-wifi-luci/DECISIONE-no-modemmanager.md` | Decisione: cosa adottare e cosa scrivere eliminando ModemManager | 24/09 19:49 |
| `experiments/20260920-wifi-luci/DECISIONE-cog-vs-native-ui.md` | Decisione A (Cog/WPE + LuCI) vs B (UI nativa C su ubus), numeri misurati | 24/09 22:39 |
| `experiments/20260920-wifi-luci/RICERCA-UI-LAYER-kiosk3.md` | Ricerca: cosa serve per un layer UI interattivo su kiosk3 (atomic commit, tearing, cadenza, touch) | 24/09 22:39 |
| `experiments/20260920-wifi-luci/PIANO-UI-parita-luci.md` | Requisito e piano di copertura della GUI web LuCI nell'interfaccia su display | 24/09 23:50 |
| `experiments/20260920-wifi-luci/RICERCA-anticipo-modem-20260923.md` | Ricerca: anticipare il bring-up del modem sotto i 100 s senza toccare il kernel | 23/09 22:22 |
| `experiments/20260920-wifi-luci/ricerca-qmi-bearer-prior-art.md` | Prior art upstream su bearer QMI longevi (hold window, rinnovo, ownership route) | 24/09 19:00 |
| `experiments/20260920-wifi-luci/VERIFICA-LUNGA-v128.log` | Log della verifica lunga sul boot v128 | 24/09 20:35 |
| `experiments/20260920-wifi-luci/docs-snapshots/` | 4 snapshot datati (dei `.md` del progetto; nell'archivio ne restano i file non duplicati — vedi `DEDUPLICA.md`) (`20260925-174903` … `20260927-225608`). Creati da `doc-backup.sh`, chiamato a ogni build da `build-v90.py`: nasce dall'incidente del 25/09 in cui una scrittura su lettura parziale sovrascrisse RIPRESA.md. In questa revisione ne restano i 4 più recenti | 25/09 -> 27/09 |
| `experiments/20260920-wifi-luci/doc-backup.sh` | Lo script dello snapshot (1 comando annulla il danno) | 25/09 13:49 |

Documenti di handoff piu' vecchi (stessa radice, esperimenti precedenti):

| Path | Cosa contiene | Quando |
|---|---|---|
| `NX679J_OPENWRT_KERNEL_RE_HANDOFF.md` | Handoff del RE kernel OpenWrt (radice dell'albero) | 16/09 10:30 |
| `authoritative-bootchain-verification.md` | Verifica autorevole della catena di boot | 16/09 23:54 |
| `OPENWRT-ACCESSO-TEMPORANEO.txt` | Nota di accesso temporaneo a OpenWrt | 17/09 23:18 |
| `experiments/` … `WIFI-LUCI-HANDOFF-20260920.md` | Handoff della fase wi-fi/LuCI | 20/09 18:40 |
| `experiments/OPENWRT-PORT-STATUS.md` | Stato del port OpenWrt (18 KB) | 18/09 00:10 |
| `experiments/SESSION-REPORT-2026-09.md` | Report di sessione | 20/09 13:40 |
| `experiments/MODEM-HANDOFF.md`, `MODEM-HANDOFF-20260919.md`, `MODEM-HANDOFF-20260919-2.md` | Tre consegne successive sulla fase modem (la terza e' il documento lungo, 75 KB) | 19-20/09 |
| `experiments/PROMPT-NUOVA-SESSIONE-MODEM.md`, `experiments/NX679J-NEW-SESSION-PROMPT.md` | Prompt di ripresa per sessioni successive | 19-20/09 |
| `experiments/V10-BUILD-PLAN.md` | Piano della build v10 | 17/09 14:36 |
| `experiments/20260917-boot-chain/BOOT-CHAIN-NX679J.md` | Analisi della catena di boot (con log e attribuzione cmdline) | 17/09 |
| `experiments/20260917-init-v8/ANALYSIS-BLOCKING-V5-V6.md`, `HARDWARE-TEST-PROTOCOL.md`, `HOW-TO-VERIFY.md` | Analisi del blocco v5/v6, protocollo di test su hardware, come verificare | 17/09 |
| `experiments/qrtr/QMI-STATUS-READER-DESIGN.md` | Disegno del lettore di stato QMI su QRTR | 24/09 |
| `experiments/refs/session5-20260920/HOTPLUG-NETDEV-MECHANICS.md` | Meccanica dell'hotplug netdev | 20/09 |
| `port-work/device-facts-nx679j.md` | Fatti verificati sul device (12 KB) | 10/07 10:35 |
| `port-work/ROOT-CAUSE-900e.md`, `port-work/phase4-minimal-payload.md` | Root cause 900e e payload minimo della fase 4 | 10/07 |
| `bootloader-re/FINAL_REPORT.md`, `FINDINGS.md`, `ACTUAL_RESULTS.md`, `IMMEDIATE_ACTION_PLAN.md`, `penetration_test_plan.md` | Report del RE del bootloader (ABL/XBL) | 16-17/09 |
| `nx679j-openwrt-clean/KEY_AND_BYPASS_ANALYSIS.md`, `KNOWN_FACTS.md` | Analisi chiavi/bypass e fatti noti (con `evidence/`, `SHA256SUMS.json`) | 16/09 |

### 2. Boot e immagini — build e flash

| Path | Cosa contiene | Quando |
|---|---|---|
| `experiments/20260920-wifi-luci/build-v54.py` … `build-v90.py` (37 file) | Un file per versione: la **ricetta completa** (payload, persist-tar, contenitore, offset, md5). `build-v90.py` e' l'ultima e chiama `doc-backup.sh` a ogni build | 18/09 -> 25/09 (ultimo 17:49) |
| `experiments/20260920-wifi-luci/build-v54/` … `build-v64/`, `build-v65/` (12 dir) | Workdir del build di quella versione: `overlay/` (payload iniettato), `check/owrt` (albero del rootfs dopo l'iniezione), `merged.cpio`, `new-ramdisk.lz4`, `vNN-uid0.cpio` | 20/09 -> 25/09; **`build-v64/` e' il workdir della build corrente** (ultimo 25/09 17:49) |
| `experiments/20260920-wifi-luci/boot_b-live.img` (+ `.gz`) | Contenitore boot_b di base (100663296 byte), da cui derivano tutte le immagini | 20/09 17:43 |
| `experiments/20260920-wifi-luci/boot_b-current-v170.img` | Immagine v170 (md5 `cc592be0…`); **NON** ha lo stesso md5 di `boot_b-v90-mmwd.img` (`f8d1324f…`), e `ripristino-v175.sh` asserisce `98ac83f5…` che non corrisponde a nessuno dei due — incoerenza **non sanata al 29/09/2026**: l'md5 atteso dallo script è il riferimento della build v175, che non è nell'archivio; **verificare l'md5 del file prima di usare lo script** | 100663296 byte | 25/09 17:49 |
| `experiments/20260920-wifi-luci/boot_b-v90-mmwd.img` | Output di `build-v90.py` (gemello della v170) | 25/09 17:49 |
| `experiments/20260920-wifi-luci/boot_b-current-v123.img` … `-v170.img` (47 immagini) | Serie delle immagini costruite/flashate, una per iterazione | 24/09 07:15 -> 25/09 17:49 |
| `experiments/20260920-wifi-luci/boot_b-GOOD-v95-155s.img` | Backup di un'immagine buona (155 s), citata in `RIPRESA.md` | 23/09 18:00 |
| `experiments/20260920-wifi-luci/boot_b-v54-wifi.img` … `boot_b-v63-wifi.img` (10 file; la serie completa arriva a v80, 27 file) | Serie wi-fi della prima fase | 20/09 |
| `experiments/20260920-wifi-luci/boot_b-pre-v123.img` | Immagine precedente alla serie v123 | 24/09 07:15 |
| `experiments/20260920-wifi-luci/build-v174.log`, `build-warn.txt` | Log/warning dell'ultimo build: entrambi **vuoti** (0 byte) | 25/09 17:19 / 17:48 |
| `experiments/20260920-wifi-luci/run5-fixed.log` | Output del run del fix | 25/09 16:32 |
| `experiments/20260920-wifi-luci/nxstate.sh` | Stato del NX679J in <3 s: FASTBOOT / ANDROID / OPENWRT / OFFLINE, con modo `wait` | 22/09 00:48 |
| `experiments/20260920-wifi-luci/nxssh.sh` | Helper SSH verso il device (chiave dedicata, host key ignorata) | 20/09 17:48 |
| `experiments/20260920-wifi-luci/torna-b.sh` | Ritorno allo slot B (OpenWrt) dal soggiorno su Android | 25/09 14:36 |
| `experiments/20260920-wifi-luci/ripristino-v175.sh` | Ripristino dell'immagine buona v175 (da FASTBOOT) | 25/09 15:17 |
| `experiments/20260920-wifi-luci/rst.sh` | Ripristino dopo lo stallo dell'interfaccia (reboot pulito, niente standby) | 25/09 14:02 |
| `experiments/20260920-wifi-luci/off-vero.sh` | Spegnimento vero (rail tagliati), guidato dall'host via adb | 25/09 14:25 |
| `experiments/20260920-wifi-luci/switch-v58-live.sh`, `switch-v60-persist.sh`, `live-switch.sh` | Switch di versione a caldo e hand-over v15, con journal degli step | 21-22/09 |
| `experiments/20260920-wifi-luci/check-rawdump.sh`, `probe-gpt.sh`, `scan-v55.sh`, `fix-services-live.sh`, `data-staged-std.sh`, `chain-std.sh` | Sonde della prima fase: confronto sda11/rawdump, GPT, validazione live del fix v56, ripresa della catena dati | 20-21/09 |
| `experiments/20260920-wifi-luci/persist-tars/` | I tar iniettati DOPO i PERSIST (`www/luci-static/…`). In questo archivio stanno in `04-persistenza/` (`etc.tar`, `luci.tar`, `tools.tar`) e sono sanificati (verificato 29/09/2026: `/etc/shadow` senza hash, `authorized_keys` sostituito da un commento, `chap-secrets` vuoto, nessun `uhttpd.key`/`uhttpd.crt`, PSK del Wi-Fi `[REDACTED]`). La cartella `02-sorgenti/wifi-luci/persist-tars/` conteneva copie identiche ed e' stata svuotata (`LEGGIMI.txt`) | 22/09 -> 24/09 19:05 |
| `experiments/20260920-wifi-luci/v54-modules/` | Moduli wi-fi della fase v54: `cnss2.ko`, `cnss_nl.ko`, `cnss_prealloc.ko`, `cnss_utils.ko`, `cnss_plat_ipc_qmi_svc.ko`, `qrtr-mhi.ko`, `qcom_ramdump.ko`, `wlan_firmware_service.ko` | 20/09 17:44 |
| `experiments/20260920-wifi-luci/live-config/` (`etc`, `lib`), `livecfg.tar.gz`, `live-files.tar.gz` | Configurazione live del primo hand-over | 20/09 |
| `experiments/20260920-wifi-luci/owrt-live.tar.gz`, `owrt-live2..19.tar.gz` (18 file) | Le versioni successive dell'albero OpenWrt live (il materiale da cui nascono gli overlay) | 20/09 -> 22/09 16:43 |
| `experiments/20260920-wifi-luci/libqmi-glib.so.5.11.0` | La libqmi-glib patchata (una delle poche patch fuori albero) | 25/09 12:39 |
| `experiments/20260917-init-v9/` | La catena di immagini `boot_b-init-v10.img` … (init pre-OpenWrt) | 17/09 |
| `experiments/20260917-init-v8/boot_b-init-v8.img` + `build-candidate-v8.py` + log | La build v8 con protocollo di test | 17/09 |
| `experiments/20260916-122926-native-baseline/` | Baseline Android: `adb-*.txt` (props, mounts, partitions, pstore, dmesg, network, udc…), `artifacts.sha256` | 16/09 |

### 3. Payload di boot iniettato (script di sistema)

Tutti in `02-sorgenti/wifi-luci/`, prefisso `nx679j-`; nell'albero locale le copie iniettate finiscono in `build-v64/overlay/owrt/…` e nel ramdisk. Quelle copie del rootfs **non sono incluse** nell'archivio; il payload del workdir è conservato in `02-sorgenti/wifi-luci/payload-build-v64/`.

| Path (nome breve) | Cosa contiene | Quando |
|---|---|---|
| `nx679j-boot-services.sh` | Wrapper v3/v94: attende ubus e il wifi-wrapper, poi lancia rcS (rcS durante il bring-up wi-fi = reset hardware) | 24/09 21:44 |
| `chain.sh` | Catena modem X65 completa, con log e marker per step; lanciata detached da S95 | 24/09 19:05 |
| `nx679j-mm-watchdog.sh` | Aspetta il remoteproc MSS (si registra tardi) e poi rilancia la sequenza modem standard | 24/09 21:43 |
| `nx679j-mm-standard-boot.sh` | Bring-up dell'interfaccia **ModemManager standard** dopo che la catena vendor ha completato il data path | 24/09 19:15 |
| `nx679j-observed-bootstrap.sh` | Riproduce i prerequisiti EFS/MCFG/PD su un boot OpenWrt fresco (nessun setter DMS) | 23/09 18:13 |
| `nx679j-link-watch.sh` | Liveness + rinnovo del bearer QMI (modello mwan3track adattato a QRTR), riapplica L3 e `ifup wan_early` | 24/09 23:25 |
| `nx679j-proto-v124.sh`, `nx679j-proto-v61.sh`, `nx679j-proto.sh` | Proto netifd `nx679j` per LuCI: vista read-only/adopt della catena dati X65 | 20/09 e 24/09 20:37 |
| `nx679j-modules.sh` | Carica la catena di moduli vendor che il boot OpenWrt non carica (tasto, alimentazione, Type-C) | 25/09 13:14 |
| `nx679j-display-late.sh`, `nx679j-display-touch.init`, `nx679j-modem.init` | Bring-up ritardato di touch+display; init/regole di servizio | 25/09 13:10 (init 21/09) |
| `nx679j-cell.sh` | Dati della cella via QMI in sola lettura (senza disturbo alla sessione dati) | 25/09 12:45 |
| `nx679j-wan-share.sh`, `nx679j-wan-share-v61.sh` | Condivisione Internet SIM -> LAN Wi-Fi, con verifica | 20/09 |
| `nx679j-blackbox.sh`, `nx679j-blackbox2.sh`, `nx679j-dmcap.sh` | Blackbox: tail di journal/dmesg salvati nel rawdump ogni 5-8 s (sopravvivono al reset HW) | 21/09, 23/09 00:09 |
| `10-nx679j-modem` | Hotplug: quando compare `rmnet_data0`, alza l'interfaccia netifd `modem` (LuCI la mostra attiva) | 20/09 20:02 |
| `luci-proto-nx679j.js`, `luci-app-nx679j-modem.{status.js,menu.json,acl.json}`, `luci-nx679j-modem.rpcd` | Le pagine/ACL/daemon LuCI del modem | 20/09 -> 24/09 |
| `v54-wifi-block.sh`, `v55/v56/v57-wifi-block.sh`, `v56/v57/v58/v59/v60/v61-wifi-services.sh`, `v57-firewall-setup.sh` | Le iterazioni del bring-up wi-fi QCA6490 e dei servizi (la storia dei fix: PATH, wrapper, firewall) | 20/09 |
| `phase0-inventory.sh` + `.log`, `phase0b-deep-probe.sh` + `.log` | Inventario baseline e sonde DT/PCIe/WLAN della fase 0 | 20/09 |

### 4. Modem

| Path | Cosa contiene | Quando |
|---|---|---|
| `experiments/20260920-wifi-luci/mm-final/` | Artefatti finali ModemManager: `ModemManager` (binario), `modemmanager.sh`, `netlink-watch`, `25-modemmanager-net`, `80-mm-nx679j.rules` | 22/09 15:03 (regola 24/09 07:33) |
| `experiments/20260920-wifi-luci/qti-patches/` | 14 patch: 6 su libqmi/ModemManager (QRTR-MHI, BAM-DMUX, BindMuxDataPort, DPM, sio_port, base-modem), 5 di debug netlink (`100-fix-nlmsg-data-hdr.patch` … `104-debug-nlqmi-hexdump.patch`), 3 patch Meizu sul chaining/mux | 22/09 |
| `experiments/20260920-wifi-luci/persist-tars/tools.tar` (+ `.pre-v127.bak`) | Il tar degli strumenti (qmicli/libqmi, `libqmi-glib.so.5.11.0`) iniettato nel ramdisk | 22/09 -> 24/09 19:05 |
| `experiments/qrtr/` | La cassetta degli attrezzi QRTR: `qmi-qrtr.c`, `qmi-qrtr-dpm`, `qmi-qrtr-next`, `qmi-qrtr-observed`, `atcmd.c`, `dspawn.c`, `holdopen.c`, `ipa-trace-capture.c`, `ipa-trigger.c`, `kprobe-write-fixture.c`, `playback.frag` | 19/09 -> 24/09 |
| `experiments/qmi-*.c` (8 sorgenti + binari: `qmi-dial`, `qmi-dial-v2`, `qmi-probe`, `qmi-raw-v3/v4`, `qmi-smd7/8`, `qmi-trace`, `qmi-trace-v2`) | La scala di strumenti QMI, dal dial minimo alla trace completa, usati per capire il modello vendor | 19/09 |
| `experiments/android-*.sh` (14 file: baseline, causality, check-state, crashlog, datatest, dms-observed-check, enum, observed-preserve, probe2-5, radio-capture, trace-run) + `android-*.log` | Sonde di confronto su Android (la controparte che funziona) | 19-20/09 |
| `experiments/ipa-*.sh` (5) + `ipa-status-decode.py`, `test-ipa-trace-capture.py`, `test-ipa-status-decode.py` | Bring-up e tracing IPA (il data path che il modem usa) | 20/09 |
| `experiments/mux-init.sh`, `dpm-init.sh`, `wda-init.sh`, `pipe-init.sh`, `pd-cycle.sh`, `adsp-init.sh`, `build-pd-mapper.sh`, `build-data-tools.sh` | Inizializzatori per mux DPM/WDA/pipe e loro tool | 20/09 |
| `experiments/openwrt-*.sh` (10 file: before-android, cellular-staged/verify, data-observed/staged, dms-observed-check, final, ipa-ingress-diagnostic, ipa-ingress-resume, observed-bootstrap) | Le sonde lato OpenWrt della fase dati/modulo | 20/09 |
| `experiments/rmnet-base-isolation.sh` + `test-rmnet-base-isolation.py` | Isolamento della base rmnet (patches di comportamento) | 20/09 12:55 |
| `experiments/crashwatch.sh` + `crashwatch.log`, `crashlog-dump.sh`, `mode-sampler.sh`, `mode-watch.sh`, `serviceswatch.sh` | Sorveglianza di crash/reboot e campionamento dello stato | 19/09 |
| `experiments/refs/` | Materiale di confronto raccolto: log `android-mode*.log`, `kmsg-crash*.log`, `libqmi-qmi-endpoint*.c`, `linux-5.10-af_qrtr.c`, `extract-playback.py` | 20/09 |

### 5. Interfaccia (UI su display)

| Path | Cosa contiene | Quando |
|---|---|---|
| `experiments/20260920-wifi-luci/ui-1-base.c` … `ui-8-kbd.c` | La UI **modulare** in 8 pezzi: base/impalcatura DRM (1), livello DRM (2), disegno su dumb buffer + palette (3), dati key=value (4), pagine e tocco (5), main + standby pannello (6, DESIGN v176), menu a due livelli (7), tastiera a schermo (8) | 24/09 22:37 -> 25/09 17:47 |
| `experiments/20260920-wifi-luci/nx679j-ui.c` (109 KB) + binario `nx679j-ui` | Il monolite equivalente con tutti i livelli in un file, compilato e iniettato; `nx679j-ui.prefix-16f609ab` e' un residuo di scrittura parziale | 25/09 17:48 |
| `experiments/20260920-wifi-luci/ui-font8x16.h` | Font 8x16 per il disegno in CPU | 24/09 22:37 |
| `experiments/20260920-wifi-luci/nx679j-ui-fetch.sh` | Alimenta la UI con un file piatto key=value (il loop DRM non si ferma mai a fare I/O) | 25/09 12:45 |
| `experiments/20260920-wifi-luci/nx679j-ui-gather.sh` | Raccoglie la superficie di controllo (parita' con LuCI) per `ui-fetch` | 25/09 01:23 |
| `experiments/20260920-wifi-luci/nx679j-ui-sample.sh` | Un campione in una riga (U|ui_vivi|fps|release|retire|gap|irq|ping) per il monitoraggio dall'host | 24/09 23:33 |
| `experiments/20260920-wifi-luci/ui-real.txt`, `ui-sample.txt` | I file di dati prodotti sul device (superficie reale / campione) | 24-25/09 |
| `experiments/20260920-wifi-luci/uinput-touch.c` / `uinput-key.c` (+ binari `uinput-touch`, `uinput-key`), `ui-nav-test.c`, `ui-preview.c`, `ui-preview2.c` (binari senza prefisso) | Gli strumenti di prova: iniezione touch/tasti via uinput, test di navigazione, anteprime della UI | 24-25/09 |
| `experiments/20260920-wifi-luci/ui-preview`, `ui-preview2` (binari) | Render di riferimento della UI | 24-25/09 |
| `experiments/20260920-wifi-luci/prev-*.ppm` / `.png` (8 coppie) e `real-*.ppm` (24) / `.png` (7) | Dump PPM del framebuffer e loro conversione: anteprima disegnata vs render reale sul pannello (modem, rete, sistema, servizi, log, iface, impostazioni) | 24-25/09 |
| `experiments/20260920-wifi-luci/device-*.ppm` / `.png` (6), `new-real-*.png`, `dump-v164/165/166.ppm`, `v164-check.png`, `v166-ca.png` | Catture del device per verificare il layout (tabbar, top, ora, fix) | 24/09 |
| `experiments/20260920-wifi-luci/webcam/` (67 file) | Catture fotografiche/PNG del pannello per la verifica a occhio: serie `v156`, `v157`, `v158`, `v158b`, `v160`, `v163` (6 scatti + uno zoom per serie), `stato_*`, `h_*`, `tabs-zoom*`, `v156-top.png`, `v160-sim.png` | 25/09 11:36 -> 12:21 |
| `experiments/20260920-wifi-luci/EVIDENZA-display-pixel-20260922.png` (8,3 MB) | Evidenza fotografica dei pixel del pannello | 22/09 01:04 |

### 6. Display e touch (sotto il DRM)

| Path | Cosa contiene | Quando |
|---|---|---|
| `experiments/20260920-wifi-luci/nx679j-atom17.c` (e `atom5` … `atom16`) | La scala dei test di commit DRM; `atom17` e' l'ultimo della serie (da `atom11` «il test decisivo»: commit completo ripetuto con fb diverso) | 22-23/09 |
| `nx679j-atomic.c` (+ binari `nx679j-atomic`..`atomic4`, `atomic.c.old-buggy`) | Accensione del pannello via commit atomico (come HWC), con la versione vecchia difettosa conservata | 22/09 |
| `nx679j-drmtest.c`, `nx679j-drm-enum.c` (+ binari) | Accensione DSI con ioctl DRM diretti (senza libdrm) ed enumerazione read-only sicura | 21/09 |
| `nx679j-flip.c`, `nx679j-recolor.c`, `nx679j-paneltest.c`, `nx679j-panelon.c`, `nx679j-kiosktest.c`, `drm-probe.c` | Page-flip legacy, aggiornamento contenuto, mode verbatim, accensione «pulita», probe DRM | 22-24/09 |
| `nx679j-kiosk.c`, `nx679j-kiosk2.c`, `nx679j-kiosk3.c` (+ binario `nx679j-kiosk3`) | Il client kiosk, tre generazioni: la v3 e' la base provata del display e della UI | 23/09 15:38 |
| `nx679j-dirty.c`, `nx679j-guard.c`, `test-kiosk-stride.c` | Test DIRTYFB, fd di guardia con DROP_MASTER, test dello stride | 22-23/09 |
| `nx679j-touchmon.c`, `nx679j-touchpaint.c`, `nx679j-touchsim.c` (+ binari), `touch-selftest.c` | Catena touch: lettura eventi (zero DRM), disegno col dito, self-test completo | 21-24/09 |
| `experiments/20260920-wifi-luci/goodix-fw/` | `goodix_firmware.bin` (182 KB) e `goodix_cfg_group.bin`: il firmware del touch, da mettere dove il kernel lo cerca | 25/09 14:41 |
| `experiments/20260920-wifi-luci/goodix-cmdline-path.py` | Aggiunge `firmware_class.path=<dir>` al cmdline della boot.img Android (parser header v3/v4) | 25/09 15:11 |
| `experiments/20260920-wifi-luci/kernmods/` | I `.ko` vendor necessari a tasto/alimentazione/Type-C: `pm8941-pwrkey.ko`, `pmic_glink.ko`, `pmic-pon-log.ko`, `qcom-pon.ko`, `qti_battery_charger.ko`, `charger-ulog-glink.ko`, `ucsi_glink.ko` | 25/09 13:10 |
| `re-nubia-disp/` | Sorgenti display del vendor estratti dallo stock: `nubia-display-drivers.zip`, `src/msm_drv.c`, `src/msm_drv.h`, `src/msm_atomic.c`, `src/dsi/`, `src/sde/` | 22/09 21:13 |
| `experiments/20260920-wifi-luci/netlink-watch3.c`, `nl-send.c`, `nx679j-reboot-bootloader.c` | Sonde netlink (watch/send) e comando di reboot in bootloader | 22-25/09 |

### 7. Stock estratto e lavoro di port

| Path | Cosa contiene | Quando |
|---|---|---|
| `full_extracted_v311/` | Tutte le partizioni dello stock V311 estratte da payload.bin: `boot.img`, `vendor_boot.img`, `system.img`, `system_ext.img`, `vendor.img`, `product.img`, `modem.img`, `dsp.img`, `dtbo.img`, `abl.img`, `xbl.img`, `tz.img`, `hyp.img`, `uefi.img`, `recovery.img`, `vendor_dlkm.img`, `odm.img`, `vbmeta*.img` … piu' `uefi.img.dump/` (body.bin, info.txt, i 2 file system), `uefi.img.guids.csv`, `uefi.img.report.txt` | partizioni dal 02/07, analisi UEFI dal 03/07 |
| `stock_extracted/`, `extracted/` | Estratti stock di lavoro (contenuto di partizioni e ramdisk) | 16/09 e precedenti |
| `stock-modules/` | I moduli kernel dello stock .ko per .ko (`adsp_loader_dlkm.ko`, `atmel_mxt_ts.ko`, `bam_dma.ko`, `altmode-glink.ko`, `bcl_pmic5.ko` …): la sorgente di verita' su cosa il kernel vendor supporta | 16/09 |
| `port-work/` | Il cantiere del port, con tutta la storia delle fasi: `openwrt-phase1/` (rootfs cpio + `openwrt-router-up.sh`, `openwrt-start.sh`, `99-openwrt-chroot.sh`), `openwrt-phase2-proto1..proto8`, `openwrt-phase4-proto1-rootfsfirst/`, `vboot-dtb-swap/` (decine di `boot_b_612_*.img`), `boot_unpack/`, `vendor_boot_unpack/`, `stock_boot_ramdisk/` (il `stock_vendor_ramdisk/` è stato rimosso: materiale del ramdisk vendor), `DualBootKernelPatcher/` (citato ma **non incluso**: vedi «Revisione per la pubblicazione»), `qtestsign/`, `qc-signature-inspector/`, `ramdump-900e-minimal/` (edlclient, peek-pstore), `uefi-extract-tools/`, `mu-aloha-clean-20260916/`, `rocknix-abl-upstream/` (rimosso in questa revisione) | 02/07 -> 16/09 |
| `bootloader-re/` | RE del bootloader: `abl_a.img`, `hyp_a.img`, `extractions/`, `bootconfig-test/`, `dtb-injection-test/`, `kernel-cmdline-patch/` + i report `.md` | 16-17/09 |
| `verified-v311/` | Materiali verificati del firmware V311: `uefi-fv/`, `partitions/`, `unpacked-boot/`, `unpacked-vendor-boot/`, `abl-fv/`, `ota-metadata/`, `tools-venv/` | 16-18/09 |
| `verified-port/` | Materiali del port: `fv-audit/`, `mu_aloha_platforms-main/`, `native-openwrt/` | 16-18/09 |
| `nx679j-openwrt-clean/` | Albero «pulito» del port OpenWrt con `analysis/`, `bootchain/`, `evidence/`, `research/`, `scripts/`, `uefi/`, `notes/`, `SHA256SUMS.json` | 16/09 |
| `native-openwrt-usb-build/` | Tentativi di kernel/ramdisk costruiti in casa (`bootimg`, `bootimg-stock-kernel`, `boot_b_clang12_kernel.img`, `boot_b_upstream.img`, `headless-upstream/`, `copie .gz` dei ramdisk) | 16-17/09 |
| `edl/`, `edl-recovery/`, `qdl/`, `gpt_dumps/`, `boot_repack*/`, `vendor_boot_repack/`, `magisk_*`, `verify_vb*/`, `phase0-backups/`, `initramfs*/`, `alpine-root/`, `analysis/`, `logs/`, `live-probe/`, `hypervisor-bypass/`, `kernel-patch-test/`, `phase2-failure-analysis/`, `out/`, `tools/`, `runtime-evidence-20260711/`, `recovery-v311-magisk-20260711/`, `recovery-v411-bootchain-20260711/`, `authoritative-android-baseline-20260916-234502/`, `boot-b-analysis-20260916-203316/`, `boot-b-fix-cmdline-20260916-214002/`, `gpt-emergency-20260916-193410/`, `edl-backup-20260916-*` | Gli strumenti e i backup delle prime fasi (EDL/QDL, GPT, repack boot/vendor_boot, Magisk, verifica vbmeta, baseline Android del 16/09) | 11/07 e 16-17/09 |
| `patch-gpt-active-bit.py` (radice) | Patch del bit active nella GPT | 21/07 |

### 8. Ricerca fuori dall'albero (verificata, ma non sotto `nx679j-stock/`)

Documenti e sorgenti citati dai `.md` di progetto ma che vivono fuori dall'albero:

| Path | Cosa contiene | Quando |
|---|---|---|
| `/home/user/re-dsi-research/` | Ricerca DSI/DSI-clock: `atomic-commit-nonblock-probe-5.10.md`, `te-tearcheck-frame-done-5.10.md`, `modeset-vs-idlepc-dsi-byteclk-reparent.md`, `mainline/`, `techpack/`, `articles/` (vuota) | 22-23/09 |
| `/home/user/idlepc_research/` | Ricerca su IDLE_PC/CRTC: `CRTC_PROP_IDLE_PC_STATE_findings.md` + i tar dei sorgenti vendor (`c27_sde.tar.gz`, `ddi_inc.tar.gz`, `hwc_*.tar.gz`, `sde_drm.tar.gz`, `msm.tar.gz`, `branch_head.txt`) | 23/09 15:16-15:25 |
| `/home/user/re-cmdmode/` | Altre copie di sorgenti vendor per confronto (`atomic_510.c`, `dh_510.c`, `cl_510.c`, e la sottocartella `lop_sm8450`) | 22-23/09 |
| `/home/user/linux-6.12/` | L'albero mainline 6.12 usato come riferimento per Documentation e uapi | (citato dalle ricerche) |

### 9. Come ricostruire da zero

Sequenza minima, in ordine di lettura; ogni passo e' un file che esiste (path relative alla radice).

1. **Orientarsi** — `experiments/20260920-wifi-luci/RIPRESA.md`, poi `STATO-ATTUALE.md` (dove sta cosa, ultima immagine buona, FASI 1-11).
2. **Capire il device e la catena stock** — `port-work/device-facts-nx679j.md`, `authoritative-bootchain-verification.md`, `bootloader-re/FINDINGS.md`; per gli artefatti: `full_extracted_v311/` (+ `uefi.img.report.txt`).
3. **Rifare il port OpenWrt** — `port-work/openwrt-phase1/` (`openwrt-start.sh`, `openwrt-router-up.sh`, il rootfs cpio), poi `experiments/20260917-init-v9/` (catena init), poi la prima build con overlay `build-v54.py`, la baseline persistente `build-v64.py`, infine l'ultima ricetta `build-v90.py` (che chiama `doc-backup.sh` a ogni build). Il payload che finisce nel ramdisk e' leggibile in chiaro in `02-sorgenti/wifi-luci/payload-build-v64/` (nell'albero locale era `build-v64/overlay/owrt/`; quella copia del rootfs non è inclusa nell'archivio).
4. **Armeggiare col device** — `nxstate.sh` (stato in <3 s), `torna-b.sh` (torna sullo slot B), `ripristino-v175.sh` (rimette un'immagine buona da FASTBOOT), `off-vero.sh` (spegnimento vero), `nxssh.sh` (accesso).
5. **Modem** — leggere `RICERCA-anticipo-modem-20260923.md`, `SPEC-STATO-MODEM.md`, `DIAGNOSI-bearer-1h.md`; poi i pezzi eseguibili `nx679j-modules.sh`, `nx679j-observed-bootstrap.sh`, `chain.sh`, `nx679j-mm-watchdog.sh`, `nx679j-mm-standard-boot.sh`, `nx679j-link-watch.sh`, `nx679j-proto-v124.sh`; gli esiti li trovi in `ESITO-v129.md`, `ESITO-v130-modem-x65.md`, `ESITO-v131-v132.md`, `DIFETTO-rinnovo-v133.md`, `PROVA-MM-non-passivo.md`, `DECISIONE-no-modemmanager.md`.
6. **Interfaccia** — `RICERCA-UI-LAYER-kiosk3.md` + `DECISIONE-cog-vs-native-ui.md` + `PIANO-UI-parita-luci.md`; poi `ui-1-base.c` … `ui-8-kbd.c` (o il monolite `nx679j-ui.c`), con `nx679j-ui-fetch.sh` e `nx679j-ui-gather.sh` come sorgente dati; esito in `ESITO-v135-v137-ui-display.md`.
7. **Display e touch** — `nx679j-atom17.c` (il test decisivo del commit atomico) -> `nx679j-kiosk3.c` -> `nx679j-display-late.sh`; per il touch `nx679j-touchsim.c` + `goodix-fw/` + `goodix-cmdline-path.py`; per tasto e alimentazione `kernmods/` e `nx679j-modules.sh`.
8. **Se serve la storia** — `docs-snapshots/<timestamp>/` per com'erano i documenti in un dato momento, e `boot_b-current-vNNN.img` per un'immagine specifica.

### Materiale dell'albero non descritto altrove

Aree di lavoro reali che i documenti citano solo di passaggio (nell'archivio consolidato vivono sotto `09-albero-originale/`; le procedure per gli artefatti esclusi sono in `ESTRAZIONE-BLOB.md`):

- `phase2-failure-analysis/` — il corpus della **fase 2**: set di crash-capture Android (`dmesg.txt`, `logcat_kernel.txt`, `logcat_main.txt`), `harness/`, `dropbox/`, `init.rc.current`; è la fonte delle righe del touch della scheda hardware;
- `native-openwrt-usb-build/` — l'ambiente **headless**: una build per variante di prova (`lz4-v4`, `dwc3-quirk-v5`, `panic30-v6`, `gunyah-wdt-v7`, `gunyah-early-v8`, `entry-fdt-v9`, `stock-mm-v10`, `abl-compat-v12`, …) con script e log dei tentativi di boot;
- `verified-v311/`, `verified-port/` — le **verifiche incrociate**: dell'OTA V311 (`uefi-fv`, `abl-fv`, `unpacked-boot`, `unpacked-vendor-boot`, `partitions`) e del port (`fv-audit`, `native-openwrt`);
- `edl-recovery/` — l'**archivio EDL/QDL** completo: analisi dei loader, gli script di restore verificati, i log delle sonde Sahara (raccontati nella Fase 0b);
- `hypervisor-bypass/` — `hyp_attack.c` e `kexec_injector.c`: le prove di agosto 2026 sul bypass (binario `kexec_injector.ko` del 14/09);
- `gpt-emergency-20260916-193410/`, `boot-b-fix-cmdline-20260916-214002/`, `boot-b-analysis-20260916-203316/` — gli interventi del 16/09: GPT d'emergenza, azzeramento del BCB, analisi di `boot_b`;
- `experiments-extra/` (dall'albero: `experiments/qrtr/`, `experiments/refs/`, `experiments/openwrt-rootfs/`, i `qmi-*.c` di radice) — i **tool QMI/QRTR sorgente** e il materiale di confronto;
- contorno: `radice/` (script di radice), `initramfs_build/`, `vendor_rd_build/`, `verify_vb/`, `verify_vb2/`, `magisk_boot_verify/`, `out/`, `live-probe/`, `logs/`.

---

## Revisione per la pubblicazione (29/09/2026)

Oltre alla sanificazione di dati personali e credenziali (descritta nel `README.md`), da questo pacchetto sono stati **rimossi**:

| Categoria | File | Peso | Perché |
|---|---|---|---|
| Intestazioni di riservatezza del produttore | 24 | 2,5 MB | sorgenti QMI/qcril, `init .rc` di partizione e dump marcati `Qualcomm Confidential and Proprietary` / `Quectel Proprietary and Confidential` |
| Firmware e DTS vendor | 80 | 54,9 MB | `.mbn` (mcfg modem), DTB/DTBO, ramdisk e `init` vendor: artefatti proprietari del dispositivo |
| Copie del rootfs/ramdisk OpenWrt | 8.894 | 565,7 MB | binari GPL/LGPL ridistribuiti senza licenza né sorgente corrispondente |
| Binari GPL/LGPL sciolti | 7 | 14,6 MB | `strace-static`, `pd-mapper-static`, `rmtfs`, `tqftpserv`, `libqmi-glib.so`, `ModemManager`, `qrtr-ns` |
| Documentazione di terzi scraped | 746 | 8,5 MB | AliExpress Open Platform, OneProvider, Warp, Webshare: non pertinenti |
| File che erano risposte HTTP 404 | 6 | — | presentati come sorgenti/evidenza, contenevano `404: Not Found` |
| Chiave privata di test | 1 | — | `testkey_rsa4096.pem` (`edl-recovery/e1-sig-boot/keys/`), provenienza non documentata nell'archivio |
| Virtualenv Python e `__pycache__` | 2.625 | 30,3 MB | rigenerabili |
| Estrazioni UEFI del firmware vendor (`*.dump/` con albero GUID) | 1.214 | 0,3 MB | dump del dispositivo, path fino a 332 caratteri (illeggibili su Windows); i report testuali restano nell'archivio |
| Artefatti vendor/terzi residui (`dtb` senza estensione, `boot_signature`, `magiskboot`, `magisk`) | 55 | 50,3 MB | sfuggiti alla prima passata (il filtro cercava l'estensione `.dtb`, non i file chiamati esattamente `dtb`); i `magiskboot` sono binari Magisk (GPL-3.0) |
| Copie duplicate dello stesso contenuto (dedup) | 1.453 | 76,2 MB | di ogni contenuto resta una sola copia; `DEDUPLICA.md` elenca cosa è stato tolto, con md5 |
| Materiale residuo del ramdisk vendor (`stock_vendor_ramdisk/`) | 10 | 86 KB | gemelli identici a quelli già rimossi con l'albero del rootfs |
| Animazioni e sorgenti HTML (`10-animazioni/`) | 65 | 31,9 MB | rimossi su richiesta dell'autore |
| Artefatti residui della seconda passata (`rmtfs`, `pd-mapper`, `tqftpserv`, `busybox-nc`, `ld-musl`, `libgcc`, `stub` Magisk, header di boot vendor, `qmicli`) | 14 | 7,3 MB | binari di terzi sfuggiti alla prima passata |
| File temporanei di sessione (`x-ping/dms-lookup.tmp`) | 1 | 27 B | scratch |
| Disassemblaggi completi e device tree decompilati dal firmware vendor | 26 | 19.0 MB | riproduzioni estese di opera di terzi: rigenerabili col manuale, l'analisi resta |
| Residui di terzi nella cartella di ricerca CVE (note con estratti dell'articolo, dump di un loader di altro produttore, copie ROCKNIX, report su SM8550) | 5 | 96 KB | non pertinenti al dispositivo; vedi THIRD_PARTY §3 |
| Analisi derivate da firmware di altri dispositivi (Oppo / Samsung Odin2 / SDM845) | 19 | 3.8 MB | non pertinenti al NX679J e non ridistribuibili |
| Snapshot ridondanti e tool di terzi valutati | 399 | 22,9 MB | 10 snapshot datati oltre ai 4 più recenti; `DualBootKernelPatcher/` (MIT, citato ma non usato) e i binari di `qrtr/` (i sorgenti restano) |
| **Totale** | **15594** | **865.3 MB** | |

Conseguenze sul contenuto:

- il payload iniettato che stava nel workdir di build è conservato in `02-sorgenti/wifi-luci/payload-build-v64/` (i file `nx679j-*`); sorgenti e binari del progetto restano in `02-sorgenti/wifi-luci/`;
- le voci dei **crediti** e dell'**archivio** che descrivono gli artefatti vendor restano come descrizione del tree di lavoro locale: non sono file di questo pacchetto. Per riottenerli dal proprio dispositivo: `ESTRAZIONE-BLOB.md`;
- i due log di sessione che citavano l'intestazione di riservatezza Qualcomm sono stati **redatti** (il log resta, la stringa no);
- cinque file con nome corrotto dalla redazione automatica (`«redacted-vault-secret»-…`) sono stati rinominati in `nx679j-ui-legacy.c`, `nx679j-ui-legacy` e `qti-patches/{101,102,103}-netlink-*.patch`;
- `MANIFEST-files.txt` e `MANIFEST-md5.txt` sono stati **rigenerati** su questo contenuto; le licenze di terzi incluse sono in `THIRD_PARTY_LICENSES.md`;
- **deduplica**: dove lo stesso contenuto compariva più volte (log di esperimenti successivi, immagini di lavoro, gli stessi tar) è rimasta **una sola copia**, e nella cartella interessata c'è una nota `DEDUPLICA.md` con l'elenco, l'md5 e la copia canonica. Nessuna informazione unica è stata persa: il contenuto rimosso è integralmente nell'archivio di revisione.

**Licenza (risolta il 29/09/2026):** il codice del progetto è sotto **MIT** (`LICENSE`), la documentazione sotto **CC BY 4.0** (`LICENSE-docs`).
