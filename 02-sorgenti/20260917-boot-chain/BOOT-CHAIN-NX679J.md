# NX679J (Nubia RedMagic 7, Qualcomm SM8450 "Waipio") — catena di boot completa, dal Boot ROM agli userspace

**Documento di ricostruzione dei meccanismi.** Obiettivo: descrivere *come funziona davvero* la catena
di boot che parte su questa unità, stadio per stadio, con i campi, i bit e i valori reali letti dai file
presenti sul dispositivo, e con l'interfaccia con cui ogni stadio cede il controllo al successivo.

| | |
|---|---|
| Dispositivo | Nubia NX679J (RedMagic 7), SM8450/Waipio, ADB seriale `0123456789ABCDEF` |
| Slot attivo durante la raccolta | `_a` (`ro.boot.slot_suffix=_a`, `ro.boot.slot_suffix` da bootconfig) |
| Kernel in esecuzione | `5.10.66-android12-9-00005-gf6e6376090be-ab8060604 #1 SMP PREEMPT Fri Jan 7 14:51:36 UTC 2022` |
| Data della raccolta | 17 settembre 2026, ore ~20:00–21:00 (più i readback del 16/09 e i log di esperimenti del 17/09) |
| Vincolo rispettato | **solo letture** sul telefono: `cat`, `ls`, `readlink`, `dmesg`, `getprop`, `grep -a`, `sha256sum`, `dd if=… | base64` (nessuna scrittura su partizione, nessun `fastboot flash`, nessun `insmod`/`rmmod`, nessun `set_active`, nessun riavvio) |
| Livello di dettaglio | campi di struttura, bit di attributo, offset misurati, hash e dimensioni reali |

### Come leggere le affermazioni

Ogni affermazione porta la sua fonte e una di queste etichette:

- **[VERIFICATO]** — letto direttamente da un file/partizione del dispositivo o da un sorgente
  disponibile, con il comando e il valore accanto. Riproducibile.
- **[IPOTESI]** — deduzione coerente con i dati, ma non osservata direttamente; è indicato cosa servirebbe
  per verificarla.
- **[APERTO]** — il dato non è ricavabile da ciò che è disponibile offline (o non è stato possibile
  leggerlo in sola lettura). Elencato di nuovo in §13 con l'esperimento che lo chiuderebbe.
- **[RIPORTATO]** — proviene da documenti dell'handoff precedente e **non** è stato riverificato in questa
  sessione.

Il registro completo dei comandi eseguiti è in `live/commands.log`; ogni file citato ha il percorso
completo. Le abbreviazioni di percorso usate nel testo:

| Sigla | Percorso reale |
|---|---|
| `EXP/` | `/home/user/nx679j-stock/experiments/20260916-122926-native-baseline/` |
| `RB/` | `EXP/current-readback/` (readback del 16/09, 12:43–12:47) |
| `UNC/` | `EXP/unpacked-current/` (risultato di `unpack_bootimg` sui readback) |
| `RT2/` | `EXP/runtime-v2/` (telemetria del 16/09, 12:45–12:46) |
| `NEW/` | `/home/user/nx679j-stock/experiments/20260917-boot-chain/` (questa sessione) |
| `NEW/live/` | raccolta in sola lettura del 17/09 |
| `NEW/work/` | artefatti estratti in questa sessione (con hash) |
| `DIAG/` | `EXP/diag-gpt-bootchain-20260917-034017/` (dump di xbl/abl + GPT, 17/09 03:40) |
| `KSRC/` | `/home/user/nx679j-kernel/kernel_platform/msm-kernel/` (sorgente 5.10.101, la famiglia del kernel in uso) |
| `AOSP12/` | sorgenti Android 12 `android-12.0.0_r1` di `platform/system/core` (URL in §14) |
| `CAF/` | `/home/user/android-tv24-excluded/hardware-qcom-caf/bootctrl/` (implementazione Qualcomm di bootctl) |

---

## 1. Vista d'insieme: chi esegue cosa, e con quale interfaccia passa il controllo

| # | Stadio | Codice eseguito, da dove | Interfaccia verso lo stadio successivo | Stato |
|---|---|---|---|---|
| 0 | Boot ROM (PBL) | maschera ROM del SoC, non leggibile | carica un'immagine **MBN firmata** dalla partizione `xbl_a`/`xbl_b` (LUN `sdb`/`sdc`, partizione 1) | [IPOTESI] sui dettagli, [VERIFICATO] la collocazione |
| 1 | XBL (SEC/PE) | `xbl_a` = `sdb1`, 3.670.016 B, sha256 `685f0a75…` | cede a un'immagine ELF64 AArch64 presa da `uefi_a` (5.242.880 B) | [VERIFICATO] il file, [IPOTESI] il ruolo |
| 2 | UEFI (DXE) + applicazione ABL | `uefi_a` (`ELF64`, contiene la stringa `LinuxLoader`); `abl_a` = `sde10`, 1.048.576 B | il bootloader **riempie la RAM**, sceglie DTB+DTBO, compone `cmdline` e `bootconfig`, verifica (o non verifica) l'immagine e avvia il kernel con `x0 = puntatore al DTB` e `initrd` in memoria | [VERIFICATO] gli effetti, [APERTO] il codice |
| 3 | Kernel 5.10.66 | `boot_a` (header v4, kernel 49.108.324 B) | estrae l'initrd (due membri compressi concatenati), popola `rootfs`, esegue `/init` | [VERIFICATO] |
| 4 | Primo stadio (Magisk) | `/init` del ramdisk di `boot_a` = **magiskinit** (263.928 B) | esegue il vero init Android estratto da `.backup/init.xz` (2.523.880 B) | [VERIFICATO] |
| 5 | Primo stadio "vero" (init Android 12) | il binario di cui sopra, come PID 1 | monta `/dev`, `/proc`, `/sys`; carica i moduli; monta `/system`, `/vendor`, … via `dm-verity`; `SwitchRoot("/system")` | [VERIFICATO] dal sorgente AOSP12 + effetti sul device |
| 6 | Secondo stadio (stesso binario, argomento `second_stage`) | `/system/bin/init second_stage` (PID 1) | parsifica gli `.rc`, avvia i servizi, carica i moduli vendor (in parallelo) | [VERIFICATO] da `/proc/1/cmdline`, `.rc`, dmesg |
| 7 | Userspace Android | `zygote`/`servicemanager`/… | — | [VERIFICATO] |

Il kernel non riceve mai il controllo "da un file": l'ultimo attore che legge partizioni è il bootloader
(stadio 2). Tutto ciò che il kernel "sa" gli arriva **in memoria** (immagine kernel, initrd, DTB) più una
riga di comando; da lì in poi le partizioni le leggono `init` (stadio 5) e i servizi (stadio 6).

---

## 2. La mappa reale delle partizioni della catena (GPT letta dal device)

Comando (solo lettura), eseguito su `sde`, la LUN che contiene tutte le partizioni di boot Android:

```
adb exec-out su -c 'dd if=/dev/block/sde bs=4096 skip=2 count=3 2>/dev/null | base64 -w0'
```

`NEW/live/gpt_sde_all.txt` (12.288 byte = **96 entry da 128 byte**) decodificato con
`NEW/decode-gpt-full.py` → `NEW/gpt-sde-full.log`. La header GPT dichiara `entries=96`,
`entry_size=128`, `table_lba=2`, settore logico **4096** (`/sys/block/sde/queue/logical_block_size`
→ `4096`, `…/size` → `12582912` settori). Header: `signature='EFI PART'`, `revision=0x10000`,
`header_size=92`, `crc=0x34245d17`, `cur=1`, `alt=1572863`, `first=6`, `last=1572858`,
`table_crc=0x434f4f43`. **[VERIFICATO]**

Mappa `by-name` → device (`/dev/block/by-name/`, `NEW/live/byname.txt`):

| Partizione | Device | Partizione | Device |
|---|---|---|---|
| `uefi_a` | `sde1` | `uefi_b` | `sde29` |
| `abl_a` | `sde10` | `abl_b` | `sde38` |
| `boot_a` | `sde13` | `boot_b` | `sde41` |
| `vbmeta_a` | `sde16` | `vbmeta_b` | `sde44` |
| `dtbo_a` | `sde17` | `dtbo_b` | `sde45` |
| `vendor_boot_a` | `sde24` | `vendor_boot_b` | `sde52` |
| `recovery_a` | `sde27` | `recovery_b` | `sde54` |
| `xbl_a` | **`sdb1`** | `xbl_b` | **`sdc1`** |
| `xbl_config_a` | `sdb2` | `xbl_config_b` | `sdc2` |
| `misc` | `sda3` | `devinfo` | `sde56` |

### 2.1 Gli attributi GPT degli slot (bit 48-63) e la loro lettura

I bit del campo `attributes` (offset 48 di ogni entry, 8 byte little-endian) sono, per costruzione
Qualcomm, **tutti a partire dal bit 48**:

```
CAF/gpt-utils/gpt-utils.h:
  #define AB_FLAG_OFFSET (ATTRIBUTE_FLAG_OFFSET + 6)      // = byte 6 del campo attributi = bit 48..55
  #define AB_PARTITION_ATTR_SLOT_ACTIVE   (0x1<<2)        // = bit 50
  #define AB_PARTITION_ATTR_BOOT_SUCCESSFUL (0x1<<6)      // = bit 54
  #define AB_PARTITION_ATTR_UNBOOTABLE    (0x1<<7)        // = bit 55
  #define AB_SLOT_ACTIVE_VAL   0x3F                       // "slot attivo" = 0x3F nei bit 48..53
  #define AB_SLOT_INACTIVE_VAL 0x0
```

**[VERIFICATO]** (il file è nel tree locale; la citazione di `BOOT_SLOT_PROP` è nella tabella sopra).

Valori **reali** osservati nei tre snapshot di cui si conserva il dump (tabella generata da
`NEW/gpt-timeseries.py` → `NEW/gpt-timeseries.log`):

| Snapshot | `boot_a` | `boot_b` | `vbmeta_a` | `vbmeta_b` |
|---|---|---|---|---|
| 16/09 12:45 — Android su A (`RT2/sde-primary-entries.bin`) | `0x007f000000000000` | `0x003a000000000000` | `0x107f000000000000` | `0x107b000000000000` |
| 17/09 12:13 — Android su B (`EXP/probe-v7-devstate/gpt-sde-now.bin`) | `0x007a000000000000` | `0x007f000000000000` | `0x107b000000000000` | `0x107f000000000000` |
| 17/09 ~20:00 — Android su A (`NEW/live/gpt_sde_all.txt`) | `0x007f000000000000` | `0x003a000000000000` | `0x107f000000000000` | `0x107b000000000000` |

Lettura di quei byte con la semantica CAF (byte 6 del campo attributi):

| Valore | bit 50 `SLOT_ACTIVE` | bit 54 `BOOT_SUCCESSFUL` | bit 55 `UNBOOTABLE` | lettura |
|---|---|---|---|---|
| `0x7f` | 1 | 1 | 0 | **slot attivo e marcato "successful"** (`0x3F` attivo + `0x40`) |
| `0x7b` | 1 | 0 | 0 | slot attivo, mai marcato successful |
| `0x3a` | **0** | 0 | 0 | slot **non attivo**, non successful |
| `0x7a` | 0 | 1 | 0 | non attivo ma successful (stato transitorio del 17/09 12:13) |
| `0xc4` (`xbl_a`) | 1 | 1 | **1** | attivo+successful; il bit 55 non ha qui il senso di "unbootable" |
| `0xc0` (`xbl_b`) | 0 | 1 | **1** | non attivo |

**Conseguenze verificate:**

1. Lo slot da cui si avvia ha byte `0x7f`; l'altro `0x3a` oppure `0x7a`. **[VERIFICATO]**
2. Il valore `0x3a` **non** è "`0x3f` con il solo bit 50 azzerato" (`0x3b`): perché valga `0x3a` è
   azzerato anche il bit 48. Chi scriva i tre stati `0x7f`/`0x7a`/`0x3a` **non è determinabile dai
   dump**: [APERTO] → §13, domanda D1.
3. `slot-retry-count` non è un campo a sé: **il bootloader di questa unità non decrementa** i bit
   51-53 (valgono `7` = `0b111` in **tutti** gli snapshot, anche dopo ~10 riavvii, `EXP/HANDOFF-20260917.md`
   §"fatti"). [RIPORTATO] per i 10 riavvii, [VERIFICATO] che in tutti e tre gli snapshot del 16-17/09
   valga 7.
4. **Chi decide lo slot non è Android.** `CAF/boot_control.cpp` ricava lo slot corrente da una
   *proprietà*:

   ```
   #define BOOT_SLOT_PROP "ro.boot.slot_suffix"      // CAF/boot_control.cpp, riga 55
   ```

   e `ro.boot.slot_suffix` è prodotta **dal bootloader** in `/proc/bootconfig`
   (`androidboot.slot_suffix = "_a"`, `NEW/live/bootconfig.txt`). La logica di scelta sta quindi a monte
   del kernel: nei bit della GPT, oppure in una variabile persistente del bootloader
   (`devinfo` `sde56` = 4096 B, `uefivarstore` = `sde65`, 524.288 B); [APERTO] quale delle due → §13, D2.

### 2.2 Partizioni che contano per la catena (dimensioni reali)

| Nome | LBA first–last | Byte | attributi |
|---|---|---|---|
| `uefi_a` | 6–1285 | 5.242.880 | `0x107f…` |
| `tz_a` | 1542–2565 | 4.194.304 | `0x107f…` |
| `hyp_a` | 2566–4613 | 8.388.608 | `0x007f…` |
| `abl_a` | 91654–91909 | 1.048.576 | `0x107f…` |
| `devcfg_a` | 132998–133029 | 131.072 | `0x007f…` |
| `boot_a` | 108422–132997 | 100.663.296 | `0x007f…` |
| `vbmeta_a` | 133050–133065 | 65.536 | `0x107f…` |
| `dtbo_a` | 133066–139209 | 25.165.824 | `0x007f…` |
| `imagefv_a` | 139722–140233 | 2.097.152 | `0x007f000000000001` (bit 0 = "required partition" UEFI) |
| `vendor_boot_a` | 207663–232238 | 100.663.296 | `0x007f…` |
| `qmcs` (non-A/B) | 232239–239918 | 31.457.280 | `0x0` |
| `recovery_a` | 239983–265582 | **104.857.600** | `0x007f…` |
| `devinfo` / `dip` / `limits` / `toolsfv` / `storsec` / `secdata` / `uefivarstore` | 524504–666661 | 4 KiB … 1 MiB | `0x10…` (bit 60) |

Numerazione completa delle 74 entry non vuote: `NEW/gpt-sde-full.log`. **[VERIFICATO]**

> Nota di provenienza: nel readback del **16/09** `vendor_boot_b` aveva sha256 `ba64d163…` (con un `dtb`
> diverso, `d6891d5e…`), mentre alle **10:17 del 17/09** (`EXP/control-slotb-20260917-101754/pre-vendor_boot_b.img`)
> e **adesso** (`NEW/live/hashes_boot.txt`) vale `6db6d4d7…`, cioè *identico ad A*. Nessuno script dei probe
> scrive `vendor_boot_b` (`grep -rn "of=/dev/block/by-name/vendor_boot" NEW/EXP → 0`), quindi la
> variazione non è attribuibile a un'azione documentata: [APERTO] → §13, D3. Per l'analisi di layout qui
> sotto si usa la versione corrente (letto dal device e incrociato con `avbtool`, §6.5).

---

## 3. Stadio 0 — Boot ROM (PBL)

**Cosa si può affermare, e cosa no.** Il PBL è in maschera ROM del SoC: non è leggibile né con `dd` né
offline, e su questa unità il modo alternativo (EDL/Sahara) non apre sessione
(`EXP/HANDOFF-20260917.md`). Le affermazioni che seguono sono perciò di collocazione e di formato.

- **[VERIFICATO]** XBL *non* vive nella GPT di `sde`: `xbl_a` è la **partizione 1 della LUN `sdb`**
  (`/dev/block/by-name/xbl_a → /dev/block/sdb1`) e `xbl_b` la partizione 1 di `sdc`; `sdb` e `sdc` hanno
  `size = 16384` settori da 4096 = 64 MiB ciascuna, e contengono solo `xbl_*`, `xbl_config_*`,
  `multiimgqti_*`, `multiimgoem_*`, `apdp*`, `last_parti`. La tabella GPT di `sdb`/`sdc` è stata letta
  separatamente (`NEW/live/gpt_sdb_entries.txt`, `gpt_sdc_entries.txt`) e decodificata: entry 1 = `xbl_a`
  (896 settori × 4096 = **3.670.016 B**, attributi `0x10c4000000000000`), entry 1 di `sdc` = `xbl_b`
  (stessa dimensione, `0x10c0000000000000`).
- **[IPOTESI]** il PBL, dopo l'inizializzazione minima (DDR, clock, UFS), legge l'immagine del bootloader
  da una posizione **fissa** del dispositivo di boot (per UFS: una partizione/LUN predefinita, non
  derivata dalla GPT del sistema), ne verifica la firma MBN contro l'hash della chiave OEM bruciato nei
  fuse QFPROM (Secure Boot), e salta al suo entry point. Coerente con: la mappa by-name (§2), la presenza
  della catena di certificati OEM **"Ztemt"** dentro i dump `xbl`/`abl` (§4), e `secure: yes` riportato dal
  bootloader (`EXP/control-slotb-20260917-101754/control.log`, output di `fastboot getvar secure`).
- **[APERTO]** non si può leggere né lo stato dei fuse OEM (secure boot enabled/disabled, anti-rollback)
  né le variabili che il PBL passa a XBL.

---

## 4. Stadio 1 — XBL (e la domanda "perché `xbl_a != xbl_b`")

### 4.1 Il file

| | `xbl_a` | `xbl_b` |
|---|---|---|
| Device | `sdb1` | `sdc1` |
| Dimensione (GPT) | 3.670.016 B | 3.670.016 B |
| sha256 (letto dal device, 17/09) | `685f0a75fbff851bb6ef55ba158b7f86ea14a44c527c5388605ece94f47d637a` | **identico** |
| Header | ELF32, `e_machine = 0x0001` (marcatore Qualcomm "WE32100"), `e_entry = 0x2211c000`, `phnum = 2` | identico |
| Contenuto reale | PT_LOAD #0: offset `0x74`, **90.112 B**, vaddr `0x2211c000` (entropia 5,706 bit/byte); PT_LOAD #1: offset `0x16074`, **640 B**, vaddr `0x22143000` (entropia 7,032); **dal byte 90.900 alla fine: zeri** | identico |

Comandi: `sha256sum` su device (`NEW/live/hashes_bl.txt`), `cmp -l`, analisi ELF e
`strings` su `DIAG/xbl_a.img` (`NEW/elf-images.log`).

**Risposta alla domanda "perché `xbl_a != xbl_b`": non lo sono.** I due dump attuali sono
**byte-identici** (`cmp -l xbl_a.img xbl_b.img` → 0 differenze; hash uguali), così come `abl_a`/`abl_b`
(`443d1956…`). La voce "`xbl_a != xbl_b`" proviene dai **dump di luglio** ed è già stata ritirata in
`EXP/HANDOFF-20260917.md` riga 119 ("Smentita: il contenuto attuale è stock V311, byte-identico
all'estrazione dall'OTA ufficiale e con catena di firma valida"). **[VERIFICATO]** in questa sessione,
per il contenuto attuale. Una spiegazione strutturale del perché *possano* differire è che XBL è una
partizione **per-slot** (`xbl_a` e `xbl_b` sono due immagini separate, con attributi GPT indipendenti
`0xc4`/`0xc0`): un aggiornamento parziale del bootloader le renderebbe diverse. **[IPOTESI]**

### 4.2 Cosa contiene e cosa non contiene

`strings -n 6` su `DIAG/xbl_a.img` (3,6 MB) → **1.786 stringhe**, fra cui:

- `SEQ_FW_BUILD_TYPE_STRING=RELEASE`, `SEQ_FW_RELEASE_BUILD_VERSION_STRING=r79`;
- i *subject* della catena di certificati OEM: `Generated Ztemt Root CA`, `General Use Ztemt Key`,
  `CDMA Technologies`, `San Diego`, `SecTools`, con validità `211009012008Z` / `411004012008Z`;
- stringhe della libreria di provvigionamento chiavi (`CHIP_PROD_PROV_K_LBL`, `ENTITY_OTA_PROV_K_LBL`).

E **zero** occorrenze di: `ANDROID!`, `VNDRBOOT`, `boot.img`, `dtbo`, `vbmeta`, `slot_suffix`,
`fastboot`, `avb`, `AVB0`, `LinuxLoader`, `UEFI` (`NEW/elf-images.log`). **[VERIFICATO]**

**Conseguenza:** i circa 90 KB non-zero di `xbl_a` sono **una struttura MBN**: intestazione + hash +
catena di certificati. Il codice vero non è leggibile come testo → o è nella parte cifrata/compressa, o
XBL è solo lo *stage* di verifica e il codice sta altrove (§5). Entrambe le cose sono **[IPOTESI]**;
l'unico dato certo è che nel dump non c'è testo utile.

### 4.3 Cosa passa a chi

- **[IPOTESI]** XBL verifica e avvia l'immagine UEFI/ABL; l'unico file del dispositivo che contiene la
  stringa `LinuxLoader` (nome dell'applicazione Android Boot Loader di Qualcomm) è **`uefi_a`** (§5).
- **[VERIFICATO]** la stringa `UEFI` compare 7 volte in `uefi_a` e 0 volte in `xbl_a`, `abl_a`,
  `xbl_config_a`, `imagefv_a` (`NEW/live/string-map.txt`).
- **[APERTO]** con quale interfaccia esatta (UEFI PI/DXE handoff, protocollo con `gBS->StartImage`?).
  Non leggibile senza il binario decodificato.

---

## 5. Stadio 2 — UEFI e ABL: dove sta il codice che legge `boot.img`, e cosa si può dimostrare

### 5.1 I due candidati, misurati

| Partizione | Dimensione | Genere | Stringhe di boot |
|---|---|---|---|
| `uefi_a` (`sde1`) | 5.242.880 B | **ELF64 AArch64** (`7f454c46 02010100 … 02002800`), `e_machine=0x28` | `UEFI` ×7, **`LinuxLoader` ×1**; `_FVH`×0, `PE32+`×0, `fastboot`×0, `avb`×0, `dtbo`×0 |
| `abl_a` (`sde10`) | 1.048.576 B | **ELF32 ARM** (`e_machine=0x28`), `e_entry=0x9fa00000`, `phnum=3` | nessuna |
| `xbl_config_a` (`sdb2`) | 307.200 B | (config del bootloader) | nessuna |
| `imagefv_a` (`sde19`) | 2.097.152 B | — | nessuna |
| `recovery_a` (`sde27`) | 104.857.600 B | — | **`ANDROID!` ×1** (contiene una vera immagine di boot) |

Struttura di `abl_a` (`od` + parser ELF, `NEW/elf-images.log`):

```
offset 0x00000  ELF32 header (52 B) + 3 program header (96 B)
offset 0x00094  segmento "intestazione" da 148 B (phdr di tipo NULL)
offset 0x01000  PT_LOAD: 163.840 B, vaddr 0x9fa00000, entropia 7,724 bit/byte   <-- payload
offset 0x29000  segmento da 3.416 B, entropia 5,688 bit/byte                      <-- catena certificati
primi 16 byte del payload: 00 ×16, poi dati ad alta entropia
dopo 0x29d58: zeri fino a 1.048.576
```

- **[VERIFICATO]** `abl_a` **non contiene alcuna stringa di boot image** (`ANDROID!`, `VNDRBOOT`,
  `dtbo`, `slot_suffix`, `vbmeta`, `avb`, `fastboot`, `boot.img`, `bootconfig`, `fstab.qcom`,
  `modules.load` → tutte 0 occorrenze), e il suo payload è **ad alta entropia** (7,724 bit/byte): quindi
  **non è leggibile come testo**, e la conclusione "questo file non è il componente che legge le immagini
  di boot" va scritta così come sta: *come testo non lo è*. **[VERIFICATO]**
- **[IPOTESI]** il payload da 160 KiB è l'applicazione ABL **compressa/cifrata** (l'entropia alta, la
  presenza di un'intestazione MBN e di una coda di certificati OEM sono coerenti). In tal caso il codice
  esiste ma non è ispezionabile con `strings`.
- **[IPOTESI]** la logica che legge `boot.img`/`vendor_boot`/`dtbo` e applica AVB sia dentro `uefi_a`
  (unico file con `LinuxLoader` e con `UEFI`). Non ci sono prove testuali oltre a quelle due stringhe.
- **[APERTO]** identificare con certezza il *binario* che compie la selezione dello slot e l'apertura di
  `boot_*`. Non si risolve con `strings` perché il testo non c'è → §13, domanda D4.

### 5.2 Cosa il bootloader fa, dedotto dagli **effetti** (tutto verificato a valle)

Poiché il codice non è leggibile, la descrizione del suo comportamento si ancora ai risultati che esso
lascia, tutti leggibili in sola lettura:

1. riempie `/proc/bootconfig` con 22 chiavi, di cui **19 che il file `vendor_boot` non contiene** (§6.4);
2. sceglie 1 DTB su 9 e 1 overlay su 44 (§6.1, §6.2);
3. scrive nel DTB `/chosen/bootargs` la riga di comando finale (§6.3);
4. appende il `bootconfig` all'initrd (§6.4);
5. avvia il kernel con initrd + DTB (§7);
6. **non** applica l'hash descriptor di AVB su `boot` (§6.5);
7. **non** decrementa i bit "tries" della GPT (§2.1).

---

## 6. Stadio 3 — Le decisioni del bootloader (DTB, DTBO, cmdline, bootconfig, AVB)

Qui sta il cuore del "come funziona": il bootloader è l'unico attore che legge le partizioni di boot e
che traduce lo stato del dispositivo in dati che il kernel e `init` troveranno pronti.

### 6.1 La scelta del DTB: il campo `dtb` di `vendor_boot` contiene **nove** alberi

`vendor_boot` header v4 (`unc/…`) — campi reali:

| Campo (offset) | Valore |
|---|---|
| `magic` (0) | `VNDRBOOT` |
| `header_version` (8) | 4 |
| `page_size` (12) | 4096 |
| `kernel_addr` (16) | `0x00008000` |
| `ramdisk_addr` (20) | `0x01000000` |
| `vendor_ramdisk_size` (24) | 10.041.284 |
| `cmdline[2048]` (28) | 83 byte utili (v. §6.3) |
| `tags_addr` (2076) | `0x00000100` |
| `name[16]` (2080) | vuoto |
| `header_size` (2096) | 2128 |
| `dtb_size` (2100) | 3.879.360 |
| `dtb_addr` (2104) | `0x01f00000` |
| `vendor_ramdisk_table_size` (2112) | 108 |
| `vendor_ramdisk_table_entry_num` (2116) | 1 |
| `vendor_ramdisk_table_entry_size` (2120) | 108 |
| `bootconfig_size` (2124) | 85 |

**Layout misurato** (offset calcolati dalla struttura e **verificati cercando le firme nei byte**,
`NEW/vendor-boot-layout.log`):

```
0x00000000  header (2128 B)
0x00001000  vendor_ramdisk00  10.041.284 B   firma 02 21 4c 18 (LZ4 legacy)
0x00995180  dtb                3.879.360 B   firma d0 0d fe ed (FDT)
0x00D48000  vendor_ramdisk_table      108 B
0x00D49000  bootconfig                 85 B     (fine immagine = 0x00D49055)
```

- **[VERIFICATO]** i byte subito dopo l'header (offset 2128) sono **tutti zero**: la tabella vendor
  ramdisk **non** sta lì ma **dopo il dtb**, a offset 13.930.496 — è l'ordine previsto dalla struttura
  `vendor_boot_img_hdr_v4` (header → ramdisk → dtb → tabella → bootconfig). Decodificata:
  `entry0: ramdisk_size=10041284, ramdisk_offset=0, ramdisk_type=0x1 (PLATFORM), ramdisk_name="", board_id[0..3]=0`.
- **[VERIFICATO]** cross-check indipendente con AVB: il *hash descriptor* di `vendor_boot` in `vbmeta_a`
  dichiara `Image Size: 13938688` = `3403 × 4096`, cioè la mia fine-immagine (13.934.677) arrotondata alla
  pagina → il layout calcolato è confermato da un descrittore firmato.
- **[VERIFICATO]** il campo `dtb` (3.879.360 B) **non** è un contenitore `mkdtimg`: contiene **9 FDT
  concatenati senza padding**, ciascuno con la propria intestazione a dimensione `totalsize`:

| idx | offset | totalsize | `model` | `qcom,msm-id` |
|---|---|---|---|---|
| 0 | 0 | 431.264 | Cape LTE Only SoC | `0x212 0x10000` |
| 1 | 431.264 | 431.252 | Cape SoC | `0x212 0x10000` |
| 2 | 862.516 | 347.467 | CapeP SoC | `0x213 0x10000` |
| 3 | 1.209.983 | 298.995 | Diwali HSP SoC | `0x1fa 0x10000` |
| 4 | 1.508.978 | 387.498 | Diwali SoC | `0x1fa 0x10000` |
| **5** | **1.896.476** | **496.030** | **Waipio v2 SoC** | **`0x1c9 0x20000`** |
| 6 | 2.392.506 | 495.598 | Waipio SoC | `0x1c9 0x10000` |
| 7 | 2.888.104 | 495.630 | WaipioP v2 SoC | `0x1e2 0x20000` |
| 8 | 3.383.734 | 495.626 | WaipioP SoC | `0x1e2 0x10000` |

(dopo l'ultimo FDT: `0` byte non-zero, `NEW/fdt-scan.log`). **[VERIFICATO]**

**Meccanismo (e verifica incrociata):** il dispositivo espone `ro.boot.dtb_idx = 5`
(`NEW/live/identity.txt`, `NEW/live/bootconfig.txt` → `androidboot.dtb_idx = "5"`). L'indice 5 corrisponde
all'albero *Waipio v2* (`msm-id 0x1c9, 0x20000`), che è la revisione "v2" del SoC in questa unità; e
l'overlay applicato (§6.2) dichiara a sua volta `qcom,msm-id = <0x1c9 0x20000 …>`. **[VERIFICATO]** la
coerenza indice↔board; **[IPOTESI]** il criterio con cui il bootloader confronta `qcom,msm-id` con il
"platform id" letto dai registri del SoC (non osservabile senza codice).

### 6.2 Il DTBO: 44 overlay, e quale viene applicato

`dtbo_a` e `dtbo_b` sono **identici** (sha256 `9ee95463abce517d140eae3c5c892c2b38fd1344d6fb0a6e2da2af76a86de6a1`,
25.165.824 B ciascuno, `NEW/live/hashes_boot.txt`). Tabella DTBO (`NEW/parse2.log`):

| Campo | Valore |
|---|---|
| `magic` (0) | `0xd7b7ab1e` |
| `total_size` (4) | 9.447.224 |
| `header_size` (8) | 32 |
| `dt_entry_size` (12) | 32 |
| `dt_entry_count` (16) | 44 |
| `dt_entries_offset` (20) | 32 |
| `page_size` (24) | 4096 |
| `version` (28) | 0 |

**Verifica aritmetica:** `header_size` + `entry_count × entry_size` = 32 + 44×32 = **1.440**, e la somma
dei `dt_size` delle 44 voci = **9.445.784**; 1.440 + 9.445.784 = **9.447.224 = `total_size`** ✔
(§6.5: lo stesso numero è dichiarato come `Image Size` dell'hash descriptor AVB di `dtbo`: 9.447.224 ✔).
**[VERIFICATO]** su due fonti indipendenti.

Esempi di voci decodificate e decompilate con `dtc`:

| idx | dt_offset | dt_size | `model` | `qcom,msm-id` | `qcom,board-id` |
|---|---|---|---|---|---|
| 0 | 1.440 | 109.475 | Cape ATP with PM8008 | `0x213 0x10000 0x21c 0x10000 0x212 0x10000` | `0x10021 0x0` |
| 5 | 588.458 | 121.695 | Cape MTP with PM8010 | `0x213 0x10000 0x21c …` | `0x10008 0x0` |
| **35** | **6.679.236** | **334.200** | **Waipio MTP with PM8010** | **`0x1c9 0x20000 0x1e2 0x20000`** | **`0x10008 0x0`** |
| 43 | 9.143.387 | 303.837 | WaipioP HDK with PM8010 | `0x1e2 0x20000 0x1e2 0x10000` | `0x1001f 0x0` |

**Quale overlay è stato applicato, e come si sa:** `androidboot.dtbo_idx = "35"` (§6.4); il modello
dell'albero **in esecuzione** è `Qualcomm Technologies, Inc. Waipio MTP with PM8010`
(`/proc/device-tree/model`, `NEW/live/dt_model.txt`) **uguale** al modello della voce 35, non a quello del
DTB base ("Waipio v2 SoC", che è un albero di *SoC* senza board); e il nodo del pannello
`qcom,mdss_dsi_r6130_1080_2400_amoled_cmd` compare sia nella voce 35 sia nell'albero in esecuzione.
**[VERIFICATO]**

Dimensione dell'albero in esecuzione: **748.187 B** (`EXP/current-device/running_fdt_20260916.dtb`,
sha256 `0cdf7536cbf08a13f445bf1dbc0be9afcaa1986def7ff163cbf7e3de7e16fee3`) contro **496.030 B** del DTB
base idx 5 → l'overlay è stato **fuso nell'albero** (+252.157 B). **[VERIFICATO]** il fatto; **[IPOTESI]**
il meccanismo di fusione (overlay `libfdt`/`libufdt` sui fragment `/fragment@N/__overlay__`, come da
sintassi visibile nel DTS della voce 35: `target-path = "/fragment@94/__overlay__/…"`).

> **Da dire esplicitamente:** l'albero che il kernel riceve **non è** il DTB di `vendor_boot`: è
> *DTB scelto (idx 5) + overlay applicati*. La `dtbo_a.img` da sola (9,4 MB) contiene 44 overlay, e in
> questo caso ne è stato selezionato uno.

### 6.3 La costruzione della `cmdline`: tre segmenti, e chi li mette

`/proc/cmdline` (717 B utili, 718 nel file; `NEW/live/cmdline.txt`) e la proprietà `/chosen/bootargs`
dell'albero in esecuzione (**605 B**, letta dal DTS salvato `EXP/current-device/running_fdt_20260916.dts`
riga 16) sono **quasi identici**: la differenza è un prefisso di **111 byte** più un separatore, presenti
solo in `/proc/cmdline` (verifica: `seg1 + " " + bootargs == /proc/cmdline` → vero).
Scomposizione dei **605 B** di `chosen/bootargs`, verificata con `NEW/cmdline-attribution.log`:

| # | Segmento | Lunghezza | Fonte provata |
|---|---|---|---|
| 1 | `stack_depot_disable=on kasan.stacktrace=off kvm-arm.mode=protected cgroup_disable=pressure cgroup.memory=nokmem` | **111 B** | **`CONFIG_CMDLINE` del kernel**, valore letto da `/proc/config.gz`: `CONFIG_CMDLINE "stack_depot_disable=on kasan.stacktrace=off kvm-arm.mode=protected cgroup_disable=pressure cgroup.memory=nokmem"`, con `CONFIG_CMDLINE_EXTEND=y` → il kernel **appende** la cmdline del bootloader a quella compilata |
| 2 | `console=ttyMSM0,115200n8 loglevel=6 kpti=0 log_buf_len=256K kernel.panic_on_rcu_stall=1 swiotlb=noforce loop.max_part=7 cgroup.memory=nokmem,nosocket pcie_ports=compat service_locator.enable=1 msm_rtb.filter=0x237 allow_mismatched_32bit_el0 cpufreq.default_governor=performance pelt=8 kasan=off rcupdate.rcu_expedited=1 rcu_nocbs=0-7 irqaffinity=0-3 ftrace_dump_on_oops pstore.compress=none fsa4480_i2c.async_probe=1 can.stats_timer=0` | **434 B** | **`/chosen/bootargs` del DTB base idx 5**: il DTS estratto da `vendor_boot_a/dtb` (FDT #5) contiene **esattamente** questa stringa (`NEW/cmdline-attribution.log`) |
| 3 | ` video=vfb:640x400,bpp=32,memsize=3072000 qcom-dload-mode.download_mode=1 bootconfig` | **83 B** (85 nel segmento: +2 spazi separatori) | **cmdline del `vendor_boot`** (offset 28, 83 byte utili; §6.1) — **[VERIFICATO]** identica byte per byte |
| 4 | `␣␣msm_drm.dsi_display0=qcom,mdss_dsi_r6130_1080_2400_amoled_cmd: rootwait ro init=/init` | **85 B** (2 spazi + 83) | **[IPOTESI] generato dal bootloader**: non è in nessuna delle 44 voci DTBO (ricerca esaustiva di `msm_drm.dsi_display0` → **0** occorrenze) né nella cmdline di `vendor_boot`; il valore però **coincide con il `compatible` del nodo pannello** presente nell'albero finale (`running_fdt…dts` riga 27088: `qcom,mdss_dsi_r6130_1080_2400_amoled_cmd`), e `rootwait ro init=/init` sono i default Android. **[VERIFICATO]** l'assenza nelle fonti testuali; **[IPOTESI]** che sia composto dal bootloader dal pannello selezionato |

Dettagli fini (utili come orma digitale del meccanismo): fra il segmento 3 e il 4 ci sono **due spazi**
consecutivi (`…bootconfig  msm_drm.dsi_display0=…`), in entrambe le copie (live e `chosen/bootargs`
salvato il 16/09): la concatenazione avviene aggiungendo un separatore anche quando il campo aggiunto è
vuoto. **[VERIFICATO]** il fatto, **[IPOTESI]** la causa.

Ordine finale nel kernel: `CONFIG_CMDLINE` **+** `chosen/bootargs` (che è già la fusione di 2+3+4). Lo
prova il confronto byte per byte: `/proc/cmdline` = segmento 1 + `chosen/bootargs`, dove
`chosen/bootargs` è *identico* alla stringa nel nodo `/chosen/bootargs` dell'albero in esecuzione.
**[VERIFICATO]**

### 6.4 Il `bootconfig`: formato, contenuto, e come arriva al kernel

**Il blob in `vendor_boot`** (85 B, offset 13.934.592; sha256
`fbab70e49b81a2c679a7802d8e27712323ed79a2a7116bab037cf29b5198cba6` in entrambi gli slot) è testo
in formato **`chiave=valore`** terminato da `\n`:

```
androidboot.hardware=qcom
androidboot.memcg=1
androidboot.usbcontroller=a600000.dwc3
```

**[VERIFICATO]** (`od`/`base64` del campo; `NEW/vendor-boot-layout.log`).

**Il blob che vede il kernel** è invece più grande: `/proc/bootconfig` = **887 byte, 22 chiavi**
(sha256 `1fe3616812e8fd3673cfa82832d34f9ba4bd0d8592f8bcdd27012b8c0fd6ee33`). Le prime 3 coincidono con
quelle di `vendor_boot`; le altre **19 non esistono in nessuna partizione di boot** e possono venire solo
dal bootloader, perché sono esattamente le informazioni di stato che solo lui possiede:

| Chiave aggiunta dal bootloader | Valore osservato | Perché è significativa |
|---|---|---|
| `androidboot.bootdevice` | `1d84000.ufshc` | dispositivo di boot |
| `androidboot.boot_devices` | `soc/1d84000.ufshc` | idem, per `init` |
| `androidboot.serialno` | `3dbd****` | — |
| `androidboot.baseband` | `msm` | — |
| **`androidboot.dtb_idx`** | `5` | risultato della scelta §6.1 |
| **`androidboot.dtbo_idx`** | `35` | risultato della scelta §6.2 |
| `androidboot.force_normal_boot` | `1` | **decide il percorso dell'init** (§9) |
| `androidboot.fstab_suffix` | `default` | sceglie `fstab.<suffix>` |
| **`androidboot.verifiedbootstate`** | `orange` | stato AVB: bootloader sbloccato |
| `androidboot.keymaster` | `1` | — |
| `androidboot.vbmeta.device` | `PARTUUID=ef598a96-a359-bf7a-9361-16bf46ef6b3c` | partizione vbmeta verificata |
| `androidboot.vbmeta.avb_version` | `1.0` | — |
| **`androidboot.vbmeta.device_state`** | `unlocked` | **perché AVB non blocca** (§6.5) |
| `androidboot.vbmeta.hash_alg` | `sha256` | — |
| `androidboot.vbmeta.size` | `11904` | **dimensione esatta dell'immagine vbmeta** (§6.5) |
| `androidboot.vbmeta.digest` | `030f49bd051dbd2c4b6f33560db51a1e4e13f7817115a4a484a780c0a1bf33c2` | digest vbmeta calcolato dal bootloader |
| `androidboot.vbmeta.invalidate_on_error` | `yes` | — |
| `androidboot.veritymode` | `enforcing` | dm-verity attiva per le partizioni con hashtree |
| **`androidboot.slot_suffix`** | `_a` | **slot corrente deciso dal bootloader** |

**[VERIFICATO]** (`NEW/live/bootconfig.txt`, confronto delle chiavi in `NEW/vendor-boot-layout.log`).

Nota di formato **[VERIFICATO]**: `/proc/bootconfig` **non** è una copia del blob ma la *resa* del kernel:
le chiavi sono stampate con spazi (`androidboot.memcg = "1"`, virgolette per i valori stringa). Quindi il
confronto va fatto sulle chiavi, non sui byte (verificato: il blob a 85 byte **non** è un prefisso
letterale di `/proc/bootconfig`).

**Trasporto al kernel — meccanismo letto nel sorgente del kernel in uso** (`KSRC/init/main.c`):

```c
static int __init bootconfig_params(char *param, char *val, …)   // riga 392
{  if (strcmp(param, "bootconfig") == 0) bootconfig_found = true;  return 0; }

static void __init setup_boot_config(const char *cmdline)        // riga 401
{  data = get_boot_config_from_initrd(&size, &csum);  …  }
```

e `get_boot_config_from_initrd()` (riga 313):

```c
data = (char *)initrd_end - BOOTCONFIG_MAGIC_LEN;      // il trailer sta IN CODA all'initrd
for (i = 0; i < 4; i++) { if (!memcmp(data, BOOTCONFIG_MAGIC, 12)) goto found; data--; }
found:
hdr  = (u32 *)(data - 8);      size = le32(hdr[0]);   csum = le32(hdr[1]);
data = ((void *)hdr) - size;   initrd_end = (unsigned long)data;   // e lo STACCA dall'initrd
```

con (`KSRC/include/linux/bootconfig.h`):

```c
#define BOOTCONFIG_MAGIC        "#BOOTCONFIG\n"
#define BOOTCONFIG_MAGIC_LEN    12
/* csum = somma di tutti i byte del bootconfig (xbc_calc_checksum) */
```

**Catena causale completa, e cosa è verificato:**

1. il `vendor_boot` porta 85 B di bootconfig (§6.1) e la sua cmdline contiene il token **`bootconfig`**
   → verificato nei byte;
2. il kernel ha `CONFIG_BOOT_CONFIG=y` (`/proc/config.gz`) → verificato;
3. il bootloader appende al termine dell'initrd: `[bootconfig][csum u32][size u32]["#BOOTCONFIG\n"]`
   e la cmdline che consegna al kernel conserva il token `bootconfig` → **verificato a valle**: il kernel
   ha effettivamente parsato un bootconfig (esiste `/proc/bootconfig`), e *nessuna* delle 19 chiavi del
   bootloader compare in `/proc/cmdline` come `androidboot.*=…` (verificato: in `/proc/cmdline` non c'è
   una sola occorrenza di `androidboot.`), quindi **le 19 chiavi non sono arrivate per cmdline**;
4. il trailer è stato **tagliato** dall'initrd prima di estrarre il ramdisk (`initrd_end = data`), cosa
   che spiega il conto di §7.2. **[VERIFICATO]** per il sorgente, **[IPOTESI]** che il produttore del
   trailer sia l'ABL (il testo non è più leggibile: l'initrd è stato liberato).

### 6.5 AVB: cosa c'è dentro `vbmeta`, cosa **non** viene verificato, e perché

`vbmeta_a` e `vbmeta_b` sono identici (sha256
`cc0de687c935714a9b6de4bf3492391474e640d2e23f752f6c626f6413b07e22`, 65.536 B di partizione). `avbtool
info_image` (`NEW/avb-crosscheck.log`):

| Campo AVB | Valore |
|---|---|
| `magic` @ offset 0 | `AVB0` |
| Header / Auth / Aux block | 256 / 576 / 5824 byte |
| `Algorithm` | `SHA256_RSA4096` |
| **`Flags`** | **0** → *nessun* flag di "verification disabled" inciso nell'immagine |
| `Rollback Index` / location | 0 / 0 |
| Chiave pubblica (sha1) | `f43904b57d85ac17dfbfa33e632c3cdf2875fa96` |
| `Release String` | `avbtool 1.2.0` |
| Chain descriptor | `recovery` (loc. 1, chiave `2597c218…`), `vbmeta_system` (loc. 2, chiave `cdbb7717…`) |
| Hash descriptor | `boot` (Image Size **50.499.584**, salt `aecce954…`, digest `f0abae29b198534160dc5c0123a28bf6ae392bb50ada970b4a5d041003407ece`), `dtbo` (9.447.224, digest `f6d55496…`), `vendor_boot` (13.938.688, digest `5220a76b…`) |
| Hashtree descriptor | `odm`, `vendor` (1.621.602.304 B, root digest `33472435…`), `system`, … |
| Prop | `com.android.build.boot.fingerprint = nubia/NX679J-UN/NX679J-UN:12/SKQ1.211113.001/eng.nubia.20220308.204459:user/release-keys`, `…security_patch = 2021-10-05` |

**Verifiche numeriche fatte in questa sessione** (`NEW/avb-crosscheck.log`):

| Controllo | Esito |
|---|---|
| Dimensione immagine vbmeta = `androidboot.vbmeta.size` | **11.904 B** dichiarati dal bootloader = 256 + 576 + 5.824 (header+auth+aux) **+ 5.248 di metadati di chiave pubblica**; i byte fino a 11.904 sono l'immagine, quelli dopo sono zero **[VERIFICATO]** |
| `sha256(boot_a[0:50.499.584])` vs digest dichiarato per `boot` | `ec823514…` ≠ `f0abae29…` → **l'immagine presente non è quella descritta** |
| `sha256(salt‖boot_a[0:50.499.584])` vs digest | `9dbd5737…` ≠ `f0abae29…` |
| Immagine `boot` scritta da Magisk | 51.317.499 B utili (header 4096 + kernel 49.108.324 + ramdisk 2.200.983 + firma 4096) ≠ 50.499.584 dichiarati |
| Coda firma del `boot.img` (ultimi 4096 B) | **13 byte non-zero**, nessun `AVB0` → il `boot.img` **non ha** footer AVB; il campo `signature_size=4096` è area riservata non usata |
| `AVB0` dentro `boot_a.img` | presente a offset 51.318.784 e 51.322.880 (dentro il contenuto del ramdisk Magisk, non è un footer) |
| md5 equivalente `sha256(vbmeta_a[0:11904])` vs `androidboot.vbmeta.digest` | `bc224f44…` ≠ `030f49bd…` → [APERTO]: l'algoritmo libavb esclude il blocco di autenticazione e concatena i vbmeta incatenati (`recovery`, `vbmeta_system`); la ricostruzione esatta non è fatta (D5) |

**Conclusione meccanica [VERIFICATO]:** il dispositivo avvia Android da `boot_a` **anche se**
il contenuto di `boot_a` non corrisponde al digest AVB di `boot`, e il bootloader dichiara
`androidboot.vbmeta.device_state = "unlocked"` + `androidboot.verifiedbootstate = "orange"`
(in `/proc/bootconfig`, letto a runtime). Quindi la verifica AVB della catena `boot`/`dtbo`/`vendor_boot`
**non viene applicata** su questa unità; **non** perché l'immagine vbmeta abbia dei flag (i `Flags` sono
0), ma per lo stato "unlocked" del bootloader. **[IPOTESI]** il bootloader chiami
`avb_slot_verify()` con i flag di tolleranza degli errori previsti per i dispositivi sbloccati (e
riporti comunque digest e dimensione). **[APERTO]** il dato esatto di quei flag (D5).

**Interfaccia con cui il bootloader consegna l'esito AVB a userspace** — nodi nel DT, letti dal device:

```
/proc/device-tree/firmware/android/compatible          "android,firmware"
/proc/device-tree/firmware/android/name                "android"
/proc/device-tree/firmware/android/vbmeta/compatible   "android,vbmeta"
/proc/device-tree/firmware/android/vbmeta/parts        "vbmeta,boot,system,vendor,dtbo,recovery"
/proc/device-tree/firmware/android/fstab/vendor/…      compatible="android,vendor"
      dev="/dev/block/platform/soc/1d84000.ufshc/by-name/vendor"
      type="ext4"  mnt_flags="ro,barrier=1,discard"  fsmgr_flags="wait,slotselect,avb"
      status="disabled"
```

**[VERIFICATO]** (`NEW/live/avb_dt.txt`, `dt_fstab_vendor.txt`). È questa la "cintura" con cui il
bootloader e `init` si scambiano il permesso di montare (`android,fstab`) e l'elenco delle partizioni AVB.

### 6.6 Perché l'immagine non valida **non** impedisce il boot — prova diretta già in archivio

`EXP/control-slotb-20260917-101754/control.log` (17/09 10:17): scrittura di un `boot.img` Magisk
(sha256 `0cd94d56…`) su `boot_b`, readback coincidente, `fastboot set_active b`, riavvio →
USB `05c6:908c` ricompare a **+26 s** e Android parte da B ("*** ANDROID E' PARTITO DA SLOT B ***").
Quell'immagine non è firmata AVB e non ha footer. **[VERIFICATO]** dall'esito riportato nel log
(esperimento eseguito in precedenza, qui non ripetuto).

---

## 7. Handoff al kernel, e kernel 5.10.66

### 7.1 L'interfaccia

- **[VERIFICATO]** il DTB che il kernel ha in mano è l'albero fuso (748.187 B, §6.2) e contiene
  `/chosen/bootargs` con la cmdline completa, oltre a `/chosen/linux,initrd-start` e `/chosen/linux,initrd-end`:

  ```
  /proc/device-tree/chosen/linux,initrd-start = 0x00000000b7452000
  /proc/device-tree/chosen/linux,initrd-end   = 0x00000000b7fff094
  ```

- **[IPOTESI]** (convenzione, non osservabile): il bootloader salta all'entry point del kernel con
  **`x0` = indirizzo fisico del DTB** (standard arm64 Linux boot protocol); `x1..x3` azzerati o non usati.
  Non c'è modo di leggerlo a posteriori.
- **[VERIFICATO]** il kernel in esecuzione è `5.10.66-android12-9-00005-gf6e6376090be-ab8060604 #1 SMP
  PREEMPT Fri Jan 7 14:51:36 UTC 2022` (`NEW/live/uname.txt`), AArch64, `CONFIG_ARM64_VA_BITS=39`,
  `CONFIG_ARM64_4K_PAGES=y`.
- **[VERIFICATO]** la console è quella dichiarata nel DT: `/chosen/stdout-path =
  /soc/qcom,qup_uart@99c000:115200n8` e la cmdline ha `console=ttyMSM0,115200n8 loglevel=6`.

### 7.2 L'initrd, misurato: **due ramdisk concatenati + il trailer del bootconfig**

| Voce | Byte |
|---|---|
| `initrd_end - initrd_start` | **12.243.092** |
| ramdisk del `boot.img` (LZ4 legacy, §8) | 2.200.983 |
| vendor ramdisk (`vendor_ramdisk00`, LZ4 legacy) | 10.041.284 |
| somma dei due | 12.242.267 |
| **differenza** | **825** |

**[VERIFICATO]** (lettura di `/proc/device-tree/chosen/linux,initrd-{start,end}`, `NEW/initrd-size.log`).
La differenza di 825 B è dell'ordine di grandezza del trailer di §6.4 (12 magic + 4 size + 4 csum +
~805 byte di dati): con l'allineamento a 4 ammesso dal kernel, la parte dati del bootconfig risulta di
**802–805 byte** contro gli 887 byte della resa `/proc/bootconfig` (la resa aggiunge spazi e virgolette).
**[VERIFICATO]** la dimensione totale; **[IPOTESI]** la ripartizione esatta 20 + ~805 + eventuale padding
(→ D6).

**Meccanismo di estrazione (sorgente del kernel in uso, `KSRC/init/initramfs.c`):**

```c
decompress = decompress_method(buf, len, &compress_name);     // riga 484: sceglie dal MAGIC in testa
if (decompress) { res = decompress(buf, len, NULL, flush_buffer, NULL, &my_inptr, error); … }
…
case Reset:  … /* riga 411-440: dopo un archivio completo lo stato torna a Start */
             error("junk within compressed archive");        /* riga 442: dati NON-CPI0 dentro il flusso */
```
e la selezione dei metodi disponibili viene dal `.config` reale: `CONFIG_RD_GZIP=y`, `CONFIG_RD_LZ4=y`,
`CONFIG_RD_ZSTD=y` (`CONFIG_RD_XZ`, `RD_BZIP2`, `RD_LZO` non impostate). Quindi **il kernel accetta
entrambi i formati** e processa **membri compressi concatenati**, tornando in `Start` dopo ciascun
archivio completato: è così che due ramdisk (boot + vendor) in un unico initrd finiscono nella stessa
`rootfs`, e la magia `02 21 4c 18` di **entrambi** (§8, §9) conferma che qui i due membri sono
**LZ4 legacy**. Nel gergo degli errori già visti nelle prove precedenti ("broken padding") questo è il
punto esatto in cui un membro non allineato viene rifiutato. **[VERIFICATO]** per il sorgente;
**[VERIFICATO]** che entrambi i membri sono LZ4 legacy; **[IPOTESI]** il dettaglio dell'avanzamento di
`inptr` fra i membri.

### 7.3 La configurazione reale del kernel (letta dal device)

`/proc/config.gz` (39.300 B compressi; 180.823 B decompressi; sha256 del file compresso
`f4bc378e7daf728096f4bf45f43a402700014dad1beee668d621defc300c14e4`), estratto con
`CONFIG_IKCONFIG_PROC=y`, `NEW/config-and-dt.log`:

| Opzione | Valore | Conseguenza meccanica |
|---|---|---|
| `CONFIG_BLK_DEV_INITRD` | `y` | l'initrd viene usato |
| `CONFIG_INITRAMFS_SOURCE` | `""` | nessun initramfs **integrato** nel kernel: tutto ciò che si vede in `rootfs` viene dall'initrd esterno |
| `CONFIG_RD_GZIP` / `RD_LZ4` / `RD_ZSTD` | `y`/`y`/`y` | accetta gzip, LZ4, zstd |
| `CONFIG_BOOT_CONFIG` | `y` | abilita il bootconfig (§6.4) |
| `CONFIG_CMDLINE` / `CMDLINE_EXTEND` | stringa di **111 B** / `y` | la cmdline finale è compilata + quella del bootloader |
| `CONFIG_DEVTMPFS` | **non impostata** | `/dev` **non** viene popolato dal kernel: i nodi devono arrivare dal ramdisk o da `mknod` di `init` |
| `CONFIG_BINFMT_ELF` / `BINFMT_SCRIPT` | `y`/`y` | `/init` può essere un ELF (qui lo è) o uno script |
| `CONFIG_MODULES` | `y` | moduli caricabili |
| `CONFIG_MODULE_SIG` / `MODULE_FORCE_LOAD` | non impostate | nessuna firma obbligatoria dei `.ko` |
| `CONFIG_DM_VERITY` / `DM_VERITY_FEC` | `y`/`y` | dm-verity disponibile (usata da `init`, §9/§11) |
| `CONFIG_PANIC_TIMEOUT` | **-1** | un panic **non** riavvia: il dispositivo resta appeso (nessun watchdog software) |
| `CONFIG_PANIC_ON_OOPS` | `y` | un oops diventa panic → e quindi, con timeout -1, un blocco silenzioso |
| `CONFIG_DETECT_HUNG_TASK` | `y` | un task bloccato in modo non interrompibile verrebbe segnalato (ma vedi §10.6) |
| `CONFIG_ANDROID` / `ANDROID_BINDERFS` | `y`/`y` | — |
| `CONFIG_USB_GADGET` / `USB_CONFIGFS` / `USB_F_NCM` | `y`/`y`/`y` | il gadget NCM è **integrato**, non modulo: si può configurare via `configfs` senza caricare nulla |
| `CONFIG_LZ4_DECOMPRESS` | `y` | — |
| `CONFIG_KALLSYMS` / `DEBUG_INFO` | `y`/`y` | simboli presenti (utile per debug) |
| `CONFIG_UEVENT_HELPER` | non impostata | nessun helper `uevent` in userspace |
| `log_buf_len` (cmdline) | `256K` | **il ring buffer è piccolo**: i messaggi dei primi secondi vengono sovrascritti (§10.6) |

### 7.4 L'avvio di `/init`

```c
KSRC/init/main.c:163   static char *ramdisk_execute_command = "/init";
KSRC/init/main.c:1429  if (ramdisk_execute_command) { ret = run_init_process(ramdisk_execute_command); … }
KSRC/init/main.c:1530  if (init_eaccess(ramdisk_execute_command) != 0) ramdisk_execute_command = NULL;
```

Nessuno `rdinit=` né `init=` nella cmdline del kernel… se non **`init=/init` in coda** alla cmdline (§6.3,
segmento 4): è il bootloader a dichiararlo esplicitamente, ed è coerente con il default del sorgente.
**[VERIFICATO]** la stringa in `/proc/cmdline`; **[VERIFICATO]** che `/init` esiste in entrambi i ramdisk
(§8) e che il processo finale è un binario ELF.

---

## 8. Stadio 5 — Primo stadio: `/init` del `boot.img` è **magiskinit**

### 8.1 Il contenitore `boot_a` (header v4 campo per campo)

| Campo (offset) | Valore |
|---|---|
| `magic` (0) | `ANDROID!` |
| `kernel_size` (8) | 49.108.324 |
| `ramdisk_size` (12) | 2.200.983 |
| `os_version` (16) | 12.0.0 |
| `os_patch_level` | 2022-02 |
| `header_size` (20) | 1584 |
| `header_version` (40) | 4 |
| `cmdline[1536]` (44) | **vuota** (0 byte utili) |
| `signature_size` (1580) | 4096 |
| kernel sha256 | `f0aa949c95d1421381850cb1982bd2a505e96fcd7565543ec28d5b1f7d81a0cc` |
| ramdisk sha256 | `adb917c51de2b9a58a74053d2aa39c2762abaeac29a7f605f5f4eda2ba692b84` |
| sha256 dell'intera partizione | `0cd94d5627501f3c65c94542093ad18b1ec6c1c215dc6a7ecab4f941057e8364` |

**[VERIFICATO]** (`UNC/boot_a.log`, `RT2/current-boot-headers.json`, `NEW/live/hashes_boot.txt`; la
partizione è 100.663.296 B = 96 MiB, l'immagine utile finisce a 51.317.499 B, il resto è zero).

> La cmdline del `boot.img` è **vuota**: la cmdline utile è tutta del DTB e del `vendor_boot` (§6.3).

### 8.2 Il ramdisk del `boot.img`: cosa contiene

LZ4 legacy (`02 21 4c 18`), 2.200.983 B → **2.306.300 B** decompressi, **29 voci cpio**
(`NEW/parse2.log`, `NEW/work/magisk-ramdisk.cpio`):

```
drwxr-x---  overlay.d/           overlay.d/sbin/{init-ld.xz 1.556, magisk.xz 181.004, stub.xz 963.636}
drwx------  .backup/             .backup/.magisk (143 B)  .backup/.rmlist (99 B)  .backup/init.xz (891.140 B)
-rwxr-x---  init                 263.928 B   <-- magiskinit
drwxr-xr-x  first_stage_ramdisk/{dev,metadata,mnt,proc,second_stage_resources,sys,debug_ramdisk}
drwxr-xr-x  proc/ sys/ dev/ mnt/ metadata/ system/etc/ramdisk/
-rw-r--r--  system/etc/ramdisk/build.prop   914 B
```

**[VERIFICATO]** (elenco cpio completo).

`.backup/.magisk` (configurazione usata dal patcher, letta integralmente):

```
KEEPVERITY=true          KEEPFORCEENCRYPT=true   RECOVERYMODE=false
VENDORBOOT=false         PREINITDEVICE=metadata
SHA1=a787c66d8085f5cc55210c97c42065158eec3f58      <-- sha1 dell'immagine stock originale
```

`.backup/.rmlist`: `overlay.d`, `overlay.d/sbin`, `overlay.d/sbin/init-ld.xz`,
`overlay.d/sbin/magisk.xz`, `overlay.d/sbin/stub.xz` — cioè **l'elenco dei file che Magisk ha aggiunto**
al ramdisk (usato per poterlo ripristinare). **[VERIFICATO]**

### 8.3 `/init`, identificazione

| | |
|---|---|
| Dimensione | 263.928 B |
| sha256 | `8e26e33c3db284823f5d083588df26781d032ecd9818b8a116490d2f488f75dd` |
| Genere | `ELF 64-bit LSB executable, ARM aarch64, statically linked, stripped`, `BuildID[sha1]=564de200…` |
| Identità | **magiskinit** di Magisk **30.7:MAGISK:D** (`magisk -v`); il file `/data/adb/magisk/magiskinit` sul device ha **la stessa dimensione, 263.928 B** [VERIFICATO dimensione+versione] |

Stringhe interne (743 con len ≥ 6) che descrivono il suo comportamento, tutte lette dal binario
(`NEW/backup-init.log`):

| Stringa | Significato |
|---|---|
| `First Stage Init`, `keyvalue magiskinit::ffi::MagiskInit::mount_overlay magiskinit::ffi::inject_magisk_rc magiskinit::ffi::switch_root` | è un init di **primo stadio** scritto in Rust, con `mount_overlay`, `inject_magisk_rc`, `switch_root` |
| `/.backup/init`, `/.backup/init.xz`, `.backup/.magisk`, `.backup/.rmlist` | sa dov'è l'init originale e lo **estrae** |
| `androidboot.fstab_suffix`, `android,fstab`, `Patch @ %08zX [android,fstab] -> [xxx]` | **riscrive** l'`fstab` nel DT patchando i byte (il messaggio stampa offset e old→new) |
| `/first_stage_ramdisk`, `/first_stage_ramdisk/storage/self/primary` | lavora sul percorso di primo stadio |
| `force_normal_boot`, `selinux_setup`, `second_stage` | conosce le fasi di Android |
| `PREINITDEVICE`, `.magisk/preinit`, `Mount preinit %s failed with %d: %s` | monta la partizione `metadata` come "preinit" |
| `magisk`, `magisk_file`, `magisk_log_file`, `sepolicy`, `load_policy`, `zygote`, `service_manager` | inietta regole SELinux e un `.rc` per lo userspace |
| **`modules.load` / `modprobe` / `/lib/modules`** | **0 occorrenze → magiskinit NON carica moduli** [VERIFICATO] |

### 8.4 Cosa fa, in ordine (meccanismo)

1. viene eseguito dal kernel come PID 1 sull'unico argomento previsto (`/init` del ramdisk);
2. monta i filesystem minimi, applica la propria politica SELinux e inietta le sue righe `.rc`;
3. **estrae l'init originale** da `.backup/init.xz` e lo esegue (`.backup/init` è il nome che compare
   nelle sue stringhe);
4. prima di cedere il controllo, riscrive l'entry `android,fstab` nel DT per il prefisso `fstab_suffix`
   (`fstab_suffix=default` → `fstab.qcom`), **a livello di byte** nel DTB in memoria.

**[VERIFICATO]** per 2-4: le stringhe; e a valle perché l'`fstab` che `init` legge è
`/first_stage_ramdisk/fstab.qcom` (§9). **[IPOTESI]** l'ordine preciso delle operazioni.

### 8.5 L'init originale (quello che il kernel non ha mai eseguito per primo)

`.backup/init.xz` = 891.140 B (sha256 `1d847b79980a57c56f2cc75d4224db7334fae19136a22bc303a42f1f71b839a3`,
magic `fd 37 7a 58 5a 00` = XZ) → decompresso **2.523.880 B**, sha256
`a143fadf2eb30fc627aa6d8f880139e67e02ad2e4d23311a04bfc909fdf2cd91`, `ELF 64-bit ARM aarch64,
statically linked, for Android 31 (= Android 12), stripped` (`NEW/work/original-init`).

Stringhe che ne provano il ruolo (`NEW/backup-init.log`):

| Stringa | Righe/funzione |
|---|---|
| `system/core/init/first_stage_mount.cpp`, `first_stage_mount` | montaggio di primo stadio |
| `system/core/libmodprobe/libmodprobe.cpp`, `libmodprobe_ext.cpp`, `Loading module `, `/lib/modules`, `Unable to open /lib/modules, skipping module loading.`, `modules.load`, `modules.load.recovery` | **è lui che carica i moduli** (§10) |
| `/system/bin/init`, `avb`, `dm-verity`, `Enabling dm-verity for …` | verifica e montaggio delle partizioni |
| `selinux_android_seapp_context_reload`, `/system/etc/selinux/plat_file_contexts` | SELinux |

**[VERIFICATO]**

---

## 9. Stadio 6 — Primo stadio "vero": cosa monta, e il doppio `switch_root`

Il meccanismo è ricostruito sul sorgente della stessa major Android del device
(`AOSP12/init/first_stage_init.cpp`, `first_stage_mount.cpp`, `switch_root.cpp`, URL in §14) e
**verificato sugli effetti** osservabili a runtime.

| Passo | Cosa fa | Evidenza |
|---|---|---|
| 1 | monta `tmpfs` su `/dev`, `devpts` su `/dev/pts`, `proc` su `/proc`, `sysfs` su `/sys`, `selinuxfs`, `tmpfs` su `/mnt`, `/debug_ramdisk`, `second_stage_resources`; crea i nodi `/dev/{kmsg,random,urandom,ptmx,null,zero,full}` con `mknod` (necessario: `CONFIG_DEVTMPFS` **non** impostata, §7.3) | `/debug_ramdisk` esiste ed è un `tmpfs` montato da "magisk" (`NEW/live/mounts.txt`); assenza di `DEVTMPFS` nel config |
| 2 | legge `/proc/cmdline` **e `/proc/bootconfig`** (li rende `0440`) e ne ricava le proprietà `ro.boot.*` | `getprop` mostra **tutte** le chiavi del bootconfig (§6.4) come `ro.boot.*`: `ro.boot.dtbo_idx`, `ro.boot.vbmeta.size`, `ro.boot.slot_suffix`, `ro.boot.veritymode`… |
| 3 | `ForceNormalBoot()`: se `/proc/bootconfig` contiene `androidboot.force_normal_boot = "1"` (oppure la cmdline `androidboot.force_normal_boot=1`), allora `mkdir /first_stage_ramdisk` + `SwitchRoot("/first_stage_ramdisk")` **prima** di montare `/system` | bootconfig contiene `androidboot.force_normal_boot = "1"` [VERIFICATO]; la directory `first_stage_ramdisk/` esiste nel ramdisk reale (§8.2) [VERIFICATO] |
| 4 | `DoFirstStageMount()`: legge l'`fstab` (`/first_stage_ramdisk/fstab.qcom`, 5.497 B, dal vendor ramdisk di `vendor_boot`) e monta le partizioni con `first_stage_mount`/`avb`/`logical`; le chiavi AVB GSI stanno in `/avb/{q,r,s}-gsi.avbpubkey` | contenuto reale dell'fstab: `system … avb=vbmeta_system,logical,first_stage_mount,avb_keys=/avb/q-gsi.avbpubkey:/avb/r-gsi.avbpubkey:/avb/s-gsi.avbpubkey`; `vendor … avb,logical,first_stage_mount`; `metadata … check,formattable` (`NEW/parse2.log`) |
| 5 | `TrySwitchSystemAsRoot()` → `SwitchRoot("/system")` dopo aver montato `/system` via dm-verity | **[VERIFICATO]** la radice finale **è** un device mapper: `/proc/mounts` riga 1 = `/dev/block/dm-9 / ext4 ro,seclabel,relatime,discard` |
| 6 | `execv("/system/bin/init", ["selinux_setup"])` e poi `second_stage` (stesso binario, altro argomento) | **[VERIFICATO]** `/proc/1/cmdline` = `"/system/bin/init second_stage"`, `/proc/1/exe → /system/bin/init`, `/proc/1/comm = init` |

**Conseguenza importante e verificabile:** dopo il passo 5 la `rootfs` iniziale (quella costruita con i
due ramdisk) **non è più la radice**, e con essa spariscono i `.ko` e le liste dei moduli:
`ls /lib/modules` sul sistema avviato → **"No such file or directory"** (`NEW/live/lib_modules.txt`).
Perciò i moduli di primo stadio sono già in memoria (kernel) e i loro file non sono più raggiungibili a
quel percorso. **[VERIFICATO]**

Timing reali dei due stadi (`getprop ro.boottime.*`, `NEW/live/boottime.txt`):

| Proprietà | Valore | Significato (da `AOSP12/init/init.cpp:904-930`) |
|---|---|---|
| `ro.boottime.init` | 330.826.093 | ns dall'avvio del kernel al primo stadio |
| `ro.boottime.init.first_stage` | 640.173.802 | ns del primo stadio fino a SELinux |
| `ro.boottime.init.selinux` | 159.984.219 | ns della fase SELinux |
| **`ro.boottime.init.modules`** | **359** | **millisecondi** spesi a caricare i moduli (`kEnvInitModuleDurationMs`); *non* è un conteggio di moduli |
| `ro.boottime.init.mount_all.*` | `.default=429`, `.late=…` | fasi di mount |
| `ro.boottime.kernel-boot` / `kernel-post-boot` | 14.758.753.430 / … | ~14,76 s fra avvio e fine del boot del kernel |

---

## 10. I moduli, come meccanismo: chi decide, da dove, con quale syscall, con quale ordine

Questa sezione è il punto su cui la documentazione precedente ha sbagliato di più, quindi ogni
affermazione ha la sua fonte.

### 10.1 Le liste e i loro consumatori

| File | Righe/blocchi | Chi lo consuma | Modalità |
|---|---|---|---|
| `lib/modules/modules.load` (nel **vendor ramdisk** di `vendor_boot`) | **98 righe** | **init di primo stadio** (il binario di §8.5) | **in-process, bloccante** |
| `lib/modules/modules.load.recovery` | **329 righe** | lo stesso codice, **solo** in modalità recovery | in-process, bloccante |
| `lib/modules/modules.dep` / `modules.softdep` / `modules.alias` / `modules.options` / `modules.blocklist` | — | libreria `libmodprobe` dentro init | risoluzione dipendenze |
| `lib/modules/*.ko` (piatti, nessuna sottodirectory di versione) | **327 file** | idem | `finit_module` |
| `lib/modules/avb/{q,r,s}-gsi.avbpubkey` | 3 file | `init` (primo stadio) per le GSI keys | — |
| `/vendor/lib/modules/modules.load` | **285 righe** | **`vendor_modprobe.sh`** (secondo stadio) | **fork + exec, in parallelo** |
| `/vendor_dlkm/lib/modules/modules.load` | **285 righe** (identiche, verificato differenza = 0) | idem | idem |
| `/vendor/lib/modules/*.ko` / `/vendor_dlkm/lib/modules/*.ko` | **283 .ko ciascuna** (288 file) | idem | idem |
| `/proc/sys/kernel/modprobe` | **vuoto** | il kernel (`request_module`) | **autoload disabilitato** |

**[VERIFICATO]**: conteggi delle liste dal readback (`NEW/parse2.log`), i conteggi vendor e il confronto
delle due liste letto dal device (`NEW/live/*.txt`), la scrittura del valore vuoto in
`/system/etc/init/hw/init.rc` righe 20-22 ("…disable it by setting modprobe to the empty string…,
`write /proc/sys/kernel/modprobe \n`").

### 10.2 Primo stadio: **c'è una sola lista attiva, ed è `modules.load` (98)**

Meccanismo (AOSP12 `init/first_stage_init.cpp`, stessa struttura del binario del device):

- base dir **fissa**: `MODULE_BASE_DIR = "/lib/modules"`;
- se `opendir("/lib/modules")` fallisce → messaggio `Unable to open /lib/modules, skipping module
  loading.` e **non è fatale** (si prosegue);
- altrimenti scorre le **sottodirectory** che corrispondono alla versione del kernel in esecuzione
  (`<major>.<minor>*`, es. `5.10`), in ordine alfabetico; se nessuna corrisponde, usa direttamente
  `/lib/modules`. **Questo è il caso del device**: il vendor ramdisk ha i `.ko` **piatti** in
  `lib/modules/` (nessuna sottodirectory `5.10/`), quindi `base_paths = {"/lib/modules"}`;
- la lista usata è `modules.load` in boot normale, **`modules.load.recovery` solo se `IsRecoveryMode()`**;
  il flag è passato dal chiamante come `IsRecoveryMode() && !ForceNormalBoot(...)` — e su questo device
  `androidboot.force_normal_boot = "1"` (§6.4) **forza il boot normale**;
- le righe vengono caricate con `Modprobe`, che risolve prima le dipendenze (`modules.dep`,
  `modules.softdep`, `modules.alias`) e chiama `finit_module` **nel processo di `init`** (nessun fork,
  nessun `exec`, nessun helper);
- l'argomento `strict = !want_console`: senza console (il caso normale) **un modulo che non si carica è un
  errore fatale**; il conteggio e la durata in ms finiscono in `ro.boottime.init.modules` (misurato: 359).

**[VERIFICATO]** per il meccanismo (sorgente) e per i valori (config, liste, `ro.boottime.init.modules`).
**[VERIFICATO]** che i 231 moduli "solo recovery" **non** vengono caricati in boot normale: le due liste
sono disgiunte su questi nomi:

| modulo | in `modules.load` (98) | in `modules.load.recovery` (329) |
|---|---|---|
| `icnss2.ko`, `cnss2.ko` (WiFi) | assente | presente |
| `cdsp-loader.ko`, `cdsprm.ko` (DSP) | assente | presente |
| `mhi_cntrl_qcom.ko`, `mhi_dev_uci.ko`, `mhi_dev_netdev.ko`, `mhi_dev_dtr.ko`, `mhi_dev_drv.ko`, `mhi_dev_net.ko` | assente | presente |
| `atmel_mxt_ts.ko` (touch), `aw9620x.ko`, `fsa4480-i2c.ko` | assente | presente |
| `qcom_q6v5.ko`, `qcom_q6v5_pas.ko` | assente | presente |

**[VERIFICATO]** (`NEW/parse2.log`, tabella generata dal confronto riga per riga delle due liste).
La lista normale contiene invece il "nocciolo" necessario a montare lo storage e lo storage stesso:
`gh_virt_wdt.ko`, `qcom_wdt_core.ko`, `clk-rpmh.ko`, `gcc-waipio.ko`, `icc-rpmh.ko`, `qcom_ipcc.ko`,
`qcom-pdc.ko`, `rpmh-regulator.ko`, … (prime 10 di 98).

### 10.3 Secondo stadio: i moduli vendor, in **parallelo**, con `fork`+`exec`

`/vendor/etc/init/hw/init.qti.kernel.rc` (letto dal device, integrale):

```
on early-init
    start vendor.modprobe
…
service vendor.modprobe /vendor/bin/vendor_modprobe.sh
    class main
    user root
    group root system
    oneshot
    seclabel u:r:vendor_modprobe:s0
    stdio_to_kmsg
on init
    wait_for_prop vendor.all.modules.ready 1
```

`/vendor/bin/vendor_modprobe.sh` (letto dal device, parti essenziali):

```sh
MODPROBE=/vendor/bin/modprobe
POSSIBLE_DIRS="/vendor_dlkm/lib/modules /vendor/lib/modules"      # ordine reale
for dir in $POSSIBLE_DIRS; do
  MODULES=$(cat ${dir}/modules.load | grep -v "^#" | grep -v "^$")
  if [ -f ${dir}/modules.blocklist ]; then
      BLOCKLIST=$(sed -n 's/blocklist \(.*\)/\1/p' ${dir}/modules.blocklist | tr '-' '_')
  fi
  for module in ${MODULES}; do echo "$module" | grep -q -w "$BLOCKLIST" && continue; …
  # primo modulo: BLOCCANTE, e se fallisce si salta l'intera directory
  ${MODPROBE} -b -s -d ${dir} -a ${first_module} > /dev/null || { continue; }
  # tutti gli altri: in una subshell in background
  ( ${MODPROBE} -b -s -d ${dir} -a ${module} > /dev/null ) &
  …
  wait
done
setprop vendor.all.modules.ready 1
```

Conseguenze meccaniche (tutte dalla forma dello script e dalle fonti sopra):

1. **chi decide**: il secondo stadio di `init`, via la direttiva `on early-init → start vendor.modprobe`;
2. **da dove**: `/vendor_dlkm/lib/modules` e `/vendor/lib/modules` (partizioni `vendor_dlkm` e `vendor`,
   montate in `dm-verity` — `/proc/mounts`: `/dev/block/dm-13 → /vendor_dlkm`, `dm-12 → /vendor`);
3. **quali**: le 285 righe di `modules.load` di ciascuna, meno la `modules.blocklist` (`8250_of`,
   `dummy_hcd`, `llcc_perfmon`, `tda18250`, `tda9887`, `tuner-simple`, `mt2266`, `tea5767`);
4. **con quale meccanismo**: `/vendor/bin/modprobe`, che è un **symlink a `toolbox`**
   (`/vendor/bin/modprobe → toolbox`) → **un processo separato, `fork`+`exec`, uno per modulo**, in
   parallelo (`&` + `wait`). Un modulo che non ritorna blocca **il suo** processo, non `init`;
5. **nessun timeout per modulo**: l'unico sincronismo è il `wait` finale della shell; l'attesa verso il
   resto di `init` è `wait_for_prop vendor.all.modules.ready 1`, che viene eseguita solo quando la
   proprietà arriva (quindi `init` **può** restare in attesa se lo script non termina);
6. **prova a runtime**: dmesg mostra `modprobe` eseguiti come *servizi* `exec` da `init`, con tempi reali:

```
[ 7.893224] init: starting service 'exec 38 (/vendor/bin/modprobe -r -d /vendor/lib/modules rmnet_shs)'…
[ 7.894383] init: SVC_EXEC service 'exec 38 (…)' pid 2770 (uid 0 gid 0+0 context u:r:vendor_modprobe:s0) started; waiting…
[ 7.926775] init: Service 'exec 38 (…)' (pid 2770) exited with status 0 waiting took 0.032000 seconds
```

e `ro.boottime.init.modules = 359` misura in **ms** il solo blocco di primo stadio.

Ulteriore riscontro temporale dallo stesso `getprop` (`NEW/live/boottime.txt`): le voci
`ro.boottime.<servizio>` sono **istanti assoluti in ns** dall'avvio del kernel — `ro.boottime.vendor.modprobe`
= `1610926874` → il servizio che carica i moduli vendor parte a **1,61 s**, dopo che il primo stadio ha
già caricato i suoi 98 (che erano finiti a `[0,359 s]` di durata dentro un primo stadio lungo 640 ms).
**[VERIFICATO]**

### 10.4 Caricamenti on-demand dagli `.rc` (terzo canale)

Oltre ai due caricamenti in blocco, alcuni servizi caricano moduli **quando servono**, sempre con
`modprobe` (fork/exec, dominio SELinux dedicato `vendor_modprobe`):

| File:riga | Direttiva |
|---|---|
| `/vendor/etc/init/hw/init.qcom.rc:56` | `exec u:r:vendor_modprobe:s0 -- /vendor/bin/modprobe -a -d /vendor/lib/modules msm_11ad_proxy` |
| `/vendor/etc/init/hw/init.qcom.rc:654` | `insmod /system/lib/modules/pronto/pronto_wlan.ko con_mode=5` (forma "insmod" legacy, mai eseguita su questa build: il file non esiste) |
| `/vendor/etc/init/netmgrd.rc:16-77` | 20+ direttive `exec u:r:vendor_modprobe:s0 -- /vendor/bin/modprobe [-r|-a] -d /vendor/lib/modules[/5.4-gki] rmnet_ctl|rmnet_core|rmnet_shs|rmnet_perf|rmnet_offload …` (carica **e scarica** a runtime) |

**[VERIFICATO]** (`NEW/live/rc_grep_modules.txt` + dmesg).

### 10.5 L'autoload del kernel è **disattivato**

`/proc/sys/kernel/modprobe` è **vuoto** (`NEW/live/kernel_modprobe_path.txt`) e
`/system/etc/init/hw/init.rc` lo scrive così di proposito. Quindi `request_module()` — cioè il percorso per
cui il kernel chiede un modulo a userspace quando incontra un alias (es. `MODALIAS` di un device) — **non
ha nessun helper**: nessun modulo può essere auto-caricato. Aggiuntivo: `CONFIG_UEVENT_HELPER` non è
impostata (§7.3). **[VERIFICATO]**

### 10.6 Cosa succede se un `probe` non ritorna (analisi per configurazione)

| Percorso | Comportamento | Fonte |
|---|---|---|
| modulo caricato dal **primo stadio** (`finit_module` nel processo di `init`) | `init` è **bloccato in modo non interrompibile** dentro la syscall; nessun timeout, nessun watchdog software (`CONFIG_PANIC_TIMEOUT=-1` → nessun riavvio; `CONFIG_PANIC_ON_OOPS=y` non si applica: non c'è oops) → il dispositivo **resta appeso** | `KSRC` + config (§7.3) |
| evidenza del blocco | i log dei primi secondi **non esistono più**: `log_buf_len=256K` e il ring buffer ha inizio a `[2.157307]` (`NEW/live/dmesg_head.txt`); `pstore` è vuoto (`/sys/fs/pstore/` senza file), quindi nessuna console-ramoops persistente | [VERIFICATO] |
| rilevatore kernel | `CONFIG_DETECT_HUNG_TASK=y` farebbe stampare un warning dopo 120 s **nel ring buffer**, utile solo se letto in tempo | config |
| moduli **vendor** (secondo stadio) | ogni `modprobe` è un **processo separato**: un probe bloccato blocca **quel** processo; la shell aspetta il `wait` e `init` aspetta `vendor.all.modules.ready` → l'effetto pratico è comunque un blocco del boot, ma **senza** bloccare il resto dei modprobe già lanciati in parallelo | forma di `vendor_modprobe.sh` + dmesg |
| moduli on-demand | solo i servizi che li richiedono vengono bloccati | `.rc` §10.4 |

**[VERIFICATO]** per config e per la forma degli script; **[IPOTESI]** il dettaglio dell'attesa di
`wait_for_prop` nei confronti delle azioni successive di `init` (D7).

### 10.7 Lo stato a runtime (misure)

| Misura | Valore | Fonte |
|---|---|---|
| righe in `/proc/modules` | **376** | `NEW/live/modules_count.txt` |
| voci in `/sys/module` | **502** | `NEW/live/modules_count2.txt` |
| `icnss2`, `cnss2`, `mhi_dev_drv`, `mhi_dev_netdev`, `atmel_mxt_ts`, `aw9620x`, `fsa4480_i2c`, `ipa_fmwk`, `smem`, `socinfo`, `qrtr`, `boot_stats`, `gh_virt_wdt`, `qcom_wdt_core` | `initstate = live` (caricati e vivi) | `NEW/live/initstate_sample.txt` |
| `cdsp-loader` | `ASSENTE` (non caricato) | idem |
| `/lib/modules` | **non esiste** nel sistema avviato (§9) | `NEW/live/lib_modules.txt` |

---

## 11. Stadio 7 — Secondo stadio e userspace

| Fatto | Valore osservato | Fonte |
|---|---|---|
| PID 1 | `/system/bin/init second_stage` | `/proc/1/cmdline`, `/proc/1/exe` |
| Radice | `/dev/block/dm-9` (ext4, `ro`) = `/system` montato con dm-verity | `/proc/mounts` |
| Partizioni di sistema | `dm-10 /system_ext`, `dm-11 /product`, `dm-12 /vendor`, `dm-13 /vendor_dlkm`, `dm-14 /odm`, `metadata → /dev/block/by-name/metadata` | `/proc/mounts` |
| Tracce di Magisk nello userspace | `magisk /debug_ramdisk tmpfs`, `devpts /debug_ramdisk/.magisk/pts` | `/proc/mounts` |
| SELinux | `Enforcing` | `getenforce` |
| Proprietà di boot | 22 chiavi del bootconfig → `ro.boot.*` (inclusi `ro.boot.dtbo_idx=35`, `ro.boot.dtb_idx=5`, `ro.boot.vbmeta.size=11904`, `ro.boot.vbmeta.digest=030f49bd…`, `ro.boot.slot_suffix=_a`) | `getprop`, `NEW/live/selinux.txt` |
| `ro.boot.veritymode` | `enforcing` | `getprop` |
| Ring buffer dmesg | inizia a `[2.157307]`: **i primi ~2 s non sono più presenti** | `dmesg | head -3` |
| `pstore` | vuoto (nessuna traccia di crash precedenti) | `ls -R /sys/fs/pstore` |
| Impronta della build | `nubia/NX679J-UN/NX679J-UN:12/SKQ1.211113.001/eng.nubia.20220308.204459:user/release-keys` (dai *Prop* di `vbmeta`) | `avbtool info_image` |

---

## 12. Appendice — Dove, e come, si innesta OpenWrt (solo conseguenze dei meccanismi sopra)

Il punto di innesto è **una scelta di conseguenza**, non una scoperta new: derivano tutti dai meccanismi
descritti. In sintesi i quattro punti, con quello che *serve* e quello che *si rompe*.

| Punto di innesto | Cosa cambiare | Cosa si rompe | Cosa serve |
|---|---|---|---|
| **A. Ramdisk del `boot.img`** (slot B) | sostituire `/init` (magiskinit) con un proprio init, lasciando kernel/header/cmdline identici. Il ramdisk è **LZ4 legacy**, cpio `newc`, e viene **concatenato** al vendor ramdisk dal bootloader: il proprio init convive con `/lib/modules` del vendor ramdisk | niente a livello di bootloader: il bootloader non verifica (AVB non applicata, §6.5) e non serve toccare GPT; si perde Magisk nell'istanza B | un `boot.img` con header v4 identico (1584 B) e ramdisk LZ4 legacy; la prima cosa da non fare è caricare i 329 moduli "recovery" (§10.2) |
| **B. `vendor_boot` (slot B)** | sostituire `vendor_ramdisk00`: cambiare `modules.load` (98) e i `.ko`, e/o `first_stage_ramdisk/fstab.qcom` | si rompono **anche** i driver di storage: `init` di primo stadio deve montare `/system`, `/vendor` ecc.; un errore qui è fatale (`strict`) | padronanza del layout v4 (offset 4096/13.930.496/13.934.592, §6.1) e del fatto che l'AVB non blocca |
| **C. Partizione dedicata** | scrivere un rootfs in una partizione che nessuno verifica: la più grande e già "di boot" è **`recovery_b` = 104.857.600 B** (§2.2); altre disponibili: `logdump` 512 MiB, `rawdump` 256 MiB, `qmcs` 31 MB | `recovery` è incatenata in `vbmeta` (chain descriptor con chiave propria, §6.5): le sue immagini smettono di essere AVB-valide, cosa che **su questa unità non blocca il boot** ma rende il recovery non più utilizzabile come tale | un init che monti quella partizione o ne usi il contenuto come rootfs |
| **D. Secondo stadio / hook su Android** | innestarsi dove `init` già passa: overlay `.rc`, o un init proprio che faccia da "primo stadio" e poi ceda | si dipende da tutte le decisioni del bootloader (§6) e dalla SELinux policy | conoscere il percorso `force_normal_boot → /first_stage_ramdisk → SwitchRoot("/system")` (§9) |

**Canale USB:** non dipende da nulla della catena di boot: `CONFIG_USB_CONFIGFS=y` e
`CONFIG_USB_F_NCM=y` sono **integrati** nel kernel 5.10.66 di questa unità (§7.3), il controller gadget è
`a600000.dwc3` (`androidboot.usbcontroller` in `/proc/bootconfig`) e la configurazione di un gadget NCM
si fa da `configfs` a runtime, **senza caricare moduli**. Sul sistema Android in esecuzione il gadget è
già configurato (`/config/usb_gadget/g1/UDC`; UDC fisico `a600000.dwc3`, oltre a `dummy_udc.0`),
quindi il percorso esiste già: la parte da costruire è l'init che lo configura. **[VERIFICATO]** per
config e nodi; **[IPOTESI]** per il funzionamento su uno userspace non-Android.

**Strada minima con massima probabilità di arrivare a uno userspace OpenWrt con canale USB:** punto **A**
(ramdisk del `boot.img` sullo slot B), perché (i) è l'unico punto in cui si controlla **cosa diventa PID
1** senza toccare la `vendor_boot` (e quindi senza rischiare i driver di storage che il primo stadio deve
caricare), (ii) sfrutta il fatto verificato che il bootloader non applica AVB, (iii) lascia intatto lo
slot A come via di rientro.

---

## 13. Domande ancora aperte, e l'esperimento che le chiuderebbe

| # | Domanda aperta | Perché è aperta | Esperimento che la chiude |
|---|---|---|---|
| **D1** | Chi scrive i tre valori degli attributi GPT (`0x7f` / `0x7a` / `0x3a`) e perché `0x3a` non è `0x3b` | il bootloader non è leggibile (testo assente, payload ad alta entropia, §5.1); il bootctl Android **scrive** solo i casi `0x3F` e "clear bit 50" (§2.1) | su slot B: scrivere un valore noto con `set_active`, riavviare, rileggere la GPT con `dd` (dopo ogni passo) e confrontare con i valori attesi dai due modelli. Alternativa non invasiva: misurare i byte GPT dopo un `fastboot reboot bootloader` + `set_active a` senza boot |
| **D2** | Lo slot è scelto **solo** dai bit GPT, o anche da una variabile persistente (`devinfo`, `uefivarstore`)? | `ro.boot.slot_suffix` è prodotto dal bootloader (§2.1) e non si può leggere la sua logica | leggere `devinfo` (`sde56`, 4096 B) e `uefivarstore` (`sde65`, 524.288 B) **prima e dopo** una `set_active`, e verificare quali byte cambiano |
| **D3** | Chi ha riscritto `vendor_boot_b` fra il 16/09 12:44 (sha `ba64d163…`) e il 17/09 10:17 (sha `6db6d4d7…`) | nessuno script documentato scrive quella partizione; restano due ipotesi: scrittura fuori log, oppure readback del 16/09 invalido | confrontare `RB/vendor_boot_b.img` con `EXP/control-slotb-20260917-101754/pre-vendor_boot_b.img` (differiscono) e cercare nei log di sistema del PC i comandi `fastboot flash vendor_boot` di quella finestra temporale |
| **D4** | Quale binario legge `boot.img`/`vendor_boot`/`dtbo` e prende le decisioni di §6 | `abl_a` e `xbl_a` non contengono testo utile; `uefi_a` contiene solo `LinuxLoader` e `UEFI` | decomprimere/decifrare il payload da 163.840 B di `abl_a` (o cercare il formato "MBN compresso" noto) e rileggere `uefi_a` per intero con analisi di sezioni PE/COFF; in alternativa, `strings` mirate sui dump **completi** di `uefi_a`, `imagefv_a`, `uefisecapp_a` |
| **D5** | Con quali flag esatti il bootloader chiama `avb_slot_verify()` (perché "unlocked" tollera l'errore) e come è calcolato `androidboot.vbmeta.digest` (030f49bd…) | libavb esclude il blocco di autenticazione e concatena i vbmeta incatenati; il calcolo esatto non è stato riprodotto (tentativi in `NEW/avb-crosscheck.log`: nessuno coincide) | leggere `libavb` (`avb_slot_verify.c`, funzione che calcola il digest) e riprodurre il calcolo includendo `recovery` e `vbmeta_system`; l'esperimento offline è sufficiente e non richiede il telefono |
| **D6** | Ripartizione esatta degli 825 byte in più nell'initrd (dati bootconfig + padding) | il trailer è stato tagliato dal kernel e la `initrd` è stata liberata: non è più in memoria leggibile | avviare un kernel di test che **non** stacchi il bootconfig (`CONFIG_BOOT_CONFIG` off, oppure un proprio init che legga `/sys/kernel/bootconfig`) e dumpare i byte del trailer; oppure ricostruire il trailer a partire da `/proc/bootconfig` e dalla somma di controllo |
| **D7** | `wait_for_prop vendor.all.modules.ready 1` blocca **tutte** le azioni successive di `init` o solo la coda di quel trigger? | non verificato sul sorgente in questa sessione | leggere `AOSP12/init/builtins.cpp` (azione `wait_for_prop`) e la semantica della action queue; conferma empirica: rinominare la `setprop` in un'immagine di test e misurare il ritardo del boot |
| **D8** | Il significato dei bit 55 e 60 del campo attributi (`xbl`=0xC0/0xC4, molte partizioni `_a` con bit 60) | le fonti CAF nominano solo i bit 50/54/55 e non spiegano il bit 60; i valori osservati non sono compatibili con "unbootable" per gli XBL | raccogliere altri dispositivi Qualcomm della stessa generazione e confrontare i pattern; oppure cercare la documentazione Qualcomm del "GPT partition attribute map" |
| **D9** | Perché i bit "tries" (51-53) non vengono mai decrementati su questa unità | osservato (valore 7 costante in tre snapshot e in ~10 riavvii riportati) | riavvii controllati contando `slot-retry-count` da fastboot **prima** di ogni boot (senza toccare le immagini), per N=10, registrando la GPT ad ogni passaggio |
| **D10** | Chi aggiunge `rootwait ro init=/init` e il parametro del pannello alla cmdline | nessuna delle 44 voci DTBO contiene quelle stringhe; il `vendor_boot` non le contiene | `strings` sui payload decodificati di `abl_a`/`uefi_a` (dipende da D4); oppure cercare le stringhe esatte in tutte le partizioni del dispositivo con `grep -a` (fattibile in sola lettura) |
| **D11** | Il vendor ramdisk contiene 327 `.ko` ma `modules.load` ne elenca 98 e `modules.load.recovery` 329: la differenza di conteggio (327 vs 329) è dovuta a file mancanti o a righe duplicate? | non verificato riga per riga in questa sessione | confronto insiemistico tra i nomi elencati nelle due liste e i nomi dei file presenti nel ramdisk (offline, dal readback) |
| **D12** | `recovery_a` contiene `ANDROID!` ×1: è un `boot.img` completo e quale kernel/ramdisk? | solo la presenza della firma è stata verificata | estrarre l'header a offset 0 del dump di `recovery_a` e confrontarlo con `boot_a` (offline) |

---

## 14. Indice delle evidenze

Tutte le letture di questa sessione sono in `NEW/live/` con il comando corrispondente in `NEW/live/commands.log`.

| File/evidenza | Contenuto chiave | Hash o valore chiave |
|---|---|---|
| `NEW/live/cmdline.txt` | cmdline completa del kernel in esecuzione | 718 B |
| `NEW/live/dt_chosen.txt` | nodi di `/chosen` (bootargs **troncati a 300 B** dal comando) + `stdout-path` | 661 B di file |
| `EXP/current-device/running_fdt_20260916.dts` riga 16 | `/chosen/bootargs` **completo** dell'albero in esecuzione | 605 B |
| `NEW/live/bootconfig.txt` | `/proc/bootconfig` | 887 B, 22 chiavi |
| `NEW/live/identity.txt` | slot, stato AVB, dtb/dtbo idx, uptime | `_a`, `orange`, `enforcing`, 5, 35 |
| `NEW/live/gpt_sde_all.txt` | entry GPT di `sde` (96 entry) | 12.288 B |
| `NEW/live/gpt_sdb_entries.txt`, `gpt_sdc_entries.txt` | GPT delle LUN con `xbl_a`/`xbl_b` | 4096 B |
| `NEW/live/hashes_boot.txt` | hash delle partizioni di boot | `vendor_boot_a` = `vendor_boot_b` = `6db6d4d7…`; `boot_b` attuale `b099bf13…` |
| `NEW/live/hashes_bl.txt` | hash di `xbl_a/b`, `abl_a/b` | `685f0a75…`, `443d1956…` |
| `NEW/live/string-map.txt` | mappa delle stringhe di boot per partizione | `LinuxLoader` solo in `uefi_a` |
| `NEW/live/modules_list.txt`, `modules_count.txt` | `/proc/modules` | 376 righe |
| `NEW/live/initstate_sample.txt` | `initstate` dei moduli critici | tutti `live`, tranne `cdsp-loader` |
| `NEW/live/rc_grep_modules.txt` | direttive sui moduli negli `.rc` | `init.qti.kernel.rc`, `netmgrd.rc`, `init.qcom.rc` |
| `NEW/live/vendor_modprobe_sh.txt` | lo script vendor integrale | caricamento parallelo |
| `NEW/live/mounts.txt` | `/proc/mounts` | radice = `/dev/block/dm-9` |
| `NEW/live/boottime.txt` | timing di init | `init.modules=359` |
| `NEW/live/avb_dt.txt`, `dt_fstab_vendor.txt` | interfaccia `android,firmware`/`vbmeta`/`fstab` | `parts = vbmeta,boot,system,vendor,dtbo,recovery` |
| `NEW/live/dt_model.txt` | modello dell'albero in esecuzione | `Waipio MTP with PM8010` |
| `NEW/vendor-boot-layout.log` | layout e campi di `vendor_boot`, tabella, bootconfig | offset 4096 / 13.930.496 / 13.934.592 |
| `NEW/fdt-scan.log` | i 9 DTB del campo `dtb` | idx 5 = Waipio v2 |
| `NEW/parse2.log` | DTBO, magiskinit, moduli del vendor ramdisk, fstab | 44 overlay; 327 `.ko`; 98/329 righe |
| `NEW/avb-crosscheck.log` | `avbtool info_image` + confronti digest | `boot` digest ≠ immagine presente |
| `NEW/backup-init.log` | `original-init` e stringhe di magiskinit | `Loading module`, `modules.load` |
| `NEW/elf-images.log` | analisi ELF/entropia/stringhe di `xbl_a`, `abl_a` | payload 163.840 B, entropia 7,724 |
| `NEW/gpt-timeseries.log` | i tre snapshot degli attributi | `0x7f` / `0x7a` / `0x3a` |
| `NEW/initrd-size.log` | initrd misurato e conti | 12.243.092 = 2.200.983 + 10.041.284 + 825 |
| `NEW/config-and-dt.log` | estratto di `/proc/config.gz` | `CONFIG_CMDLINE`, `RD_LZ4=y`, `PANIC_TIMEOUT=-1` |
| `NEW/work/magiskinit` | `/init` del `boot.img` | `8e26e33c3db28482…`, 263.928 B |
| `NEW/work/original-init` | init Android estratto da `.backup/init.xz` | `a143fadf2eb30fc6…`, 2.523.880 B |
| `NEW/work/magisk-ramdisk.cpio` | ramdisk del `boot.img` decompresso | `6fd2630909eab545…`, 2.306.300 B |
| `RT2/config.gz` | `/proc/config.gz` dal device | sha256 `f4bc378e7daf7280…` |
| `RT2/sde-primary-entries.bin` | GPT del 16/09 | attributi slot |
| `RB/*.img` + `RB/SHA256SUMS` | readback completi del 16/09 | 13 partizioni |
| `EXP/control-slotb-20260917-101754/control.log` | boot di un'immagine non firmata da B | USB a +26 s |
| `EXP/probe-v7-devstate/gpt-sde-now.bin` | GPT del 17/09 12:13 | attributi slot |
| `EXP/HANDOFF-20260917.md` | stato delle ipotesi precedenti | `xbl_a != xbl_b` smentita |

**Sorgenti esterni usati come riferimento normativo** (scaricati e citati, non eseguiti):

- `platform/system/core` android-12.0.0_r1: `init/first_stage_init.cpp`,
  `init/first_stage_mount.cpp`, `init/switch_root.cpp`, `init/init.cpp`, `libmodprobe/libmodprobe.cpp`
  (`https://raw.githubusercontent.com/aosp-mirror/platform_system_core/android-12.0.0_r1/…`).
- CAF Qualcomm `bootctrl`: `gpt-utils/gpt-utils.{h,cpp}`, `boot_control.cpp` (in locale, tree Android 14).
- Kernel: `msm-kernel` 5.10.101 in locale (`init/main.c`, `init/initramfs.c`, `include/linux/bootconfig.h`)
  — la stessa famiglia del kernel in esecuzione (5.10.66).

---

### Nota finale di onestà

Tutti i valori in questo documento provengono da letture sul dispositivo o da file già presenti nel
workspace; dove non è stato possibile leggere (PBL, codice del bootloader, trailer del bootconfig ormai
consumato dal kernel, fuse OEM) è scritto **[APERTO]** o **[IPOTESI]** e la domanda è ripetuta in §13 con
l'esperimento che la chiuderebbe. Nessuna scrittura è stata eseguita sul telefono per produrre questo
documento.
