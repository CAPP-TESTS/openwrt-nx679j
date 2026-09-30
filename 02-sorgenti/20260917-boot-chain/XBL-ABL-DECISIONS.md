# NX679J — Decisioni della catena di boot ricostruite dai binari (XBL / ABL)

- **Data dell'analisi**: 2026-09-17, 12:40–13:05 CEST
- **Device**: Nubia RedMagic 7 **NX679J** (`0123456789ABCDEF`), SM8450/Waipio, Android avviato, `adb` + `su`
- **Oggetto**: ricostruire **quali decisioni prende la catena di boot che funziona** (chi sceglie slot, DTB, dtbo, chi legge boot/vendor_boot, cosa fa AVB) leggendo i **binari** presenti sul telefono.
- **Vincoli rispettati**: solo letture (`dd` in sola lettura verso file locali, `sha256sum`, `strings`, `readelf`, `objdump`, `ls -l /dev/block/by-name`). Nessuna scrittura su partizioni, nessun flash, nessun `set_active`, nessun reboot deliberato. L'unica scrittura sul device è stata uno script temporaneo di scansione in `/data/local/tmp/` (rimovibile, nessuna partizione toccata).
- **Legenda**: **[VERIFICATO]** = misurato/disassemblato in questa sessione; **[IPOTESI]** = inferenza coerente con le evidenze ma non dimostrata riga per riga; **[NON DETERMINATO]** = non risolto.

Evidence locale prodotta in `experiments/20260917-boot-chain/re/` (dump in `re/bl/`, immagini estratte in `re/out/`, log in `re/logs/`, DTS in `re/work/fdt/`).

---

## 0. Risposte in breve

| # | Domanda | Risposta |
|---|---------|----------|
| 1 | Quali partizioni contengono il codice che legge le immagini di boot | **`abl_a`/`abl_b`** (= Android Bootloader, modulo EDK2 chiamato **`LinuxLoader`**). `xbl_a/xbl_b` contengono solo XBL/SBL1, `uefi_a/uefi_b` solo una firmware volume UEFI con DXE core. **[VERIFICATO]** |
| 2 | Dove sta il codice che conosce `ANDROID!` / `VNDRBOOT`, in che forma | In **`abl_a`** (partizione `sde10`, 1 MiB): non è testo visibile, è **LZMA compresso dentro una UEFI Firmware Volume** dentro un contenitore ELF32. Le due stringhe stanno nel **payload decompresso (602.312 B) a offset 0x632c4 e 0x632cd**; il payload è un **PE32+ AArch64** (`machine=0xAA64`) = l'applicazione UEFI `LinuxLoader`. **[VERIFICATO]** |
| 3 | Come viene scelto uno dei nove DTB | Per confronto (memcmp) delle proprietà **`qcom,msm-id`** e **`qcom,board-id`** del candidato con i valori locali della piattaforma. Sul device il risultato è **indice 5** (`androidboot.dtb_idx=5`) = *"Qualcomm Technologies, Inc. Waipio v2 SoC"*, `qcom,msm-id=<0x1c9 0x20000>` — l'**unico** dei nove con msm-id `0x1c9/0x20000`. I nove DTB base hanno tutti `qcom,board-id=<0 0>` (wildcard): il board-id reale `0x10008` del telefono **non** viene da lì ma dall'overlay dtbo. **[VERIFICATO]** |
| 4 | Come viene applicato il dtbo | Dall'ABL via **libufdt** statico: legge il `dt_table` della partizione `dtbo`, sceglie una voce e fa `ufdt_apply_overlay`/`ufdt_apply_multi_overlay` (=`ApplyOverlay`). Sul device ha applicato la **voce 35** (`androidboot.dtbo_idx=35`) = overlay *"Qualcomm Technologies, Inc. Waipio MTP with PM8010"*, che è esattamente il modello/compatible visti nel DT vivo del kernel. **[VERIFICATO]** |
| 5 | Come viene scelto lo slot A/B, cosa si legge/scrive per i contatori | Si leggono/scrivono gli **attributi (8 byte) della entry GPT** delle partizioni con suffisso slot, non il `misc`. Bit: **48-49 = Priority, 50 = Active, 51-53 = Retry, 54 = Success, 55 = Unbootable**. Slot attivo = quello con il bit **Active**; sul device `_a` ha `0x107f…` (Active=1) e `_b` `0x007b…` (Active=0), entrambi **Success=1** → i retry non vengono consumati (spiega `slot-retry-count` fermo a 7). **[VERIFICATO]** (la regola esatta di decremento è **[IPOTESI]**) |
| 6 | Cosa fa AVB e perché un'immagine non firmata parte | libavb **è presente e viene eseguito** (versione 1.0, `vbmeta.size=11904`), ma il device è **unlocked**: `androidboot.vbmeta.device_state="unlocked"`, `verifiedbootstate="orange"`, e il codice ABL ha i rami *"Device is unlocked, Skipping boot verification"* / *"State: Unlocked, AvbSlotVerify returned %a, continue boot"* → la verifica non è bloccante. **[VERIFICATO]** |
| 7 | Perché `boot`, `oem edl`, `reboot edl` non esistono | Il fastboot dell'ABL registra una **tabella statica di 23 comandi** (ciclo a `0x5038c`, 0x170/0x10 = 23 voci, tabella a VA `0x6f4b8`). L'elenco **non contiene** `boot`, `oem edl`, `reboot edl` → qualunque altro comando produce la stringa **`unknown command`** (VA `0x6dd2c`, usata a `0x4f32c`). EDL esiste solo come voce del **menu di boot** ("Boot to edload"), non come comando fastboot. **[VERIFICATO]** |

---

## 1. Inventario: chi contiene davvero il codice che legge le immagini di boot

### 1.1 Ri-lettura ORA dal device

Metodo: `adb exec-out su -c 'dd if=/dev/block/by-name/<p> bs=1M' > re/bl/<p>.img` + `sha256sum` locale; per `xbl_*` anche `sha256sum` **sul device** (misura indipendente dal trasporto adb).

| partizione | device node | dimensione (byte) | sha256 |
|---|---|---|---|
| `xbl_a` | `/dev/block/sdb1` | 3.670.016 | `685f0a75fbff851bb6ef55ba158b7f86ea14a44c527c5388605ece94f47d637a` |
| `xbl_b` | `/dev/block/sdc1` | 3.670.016 | `685f0a75fbff851bb6ef55ba158b7f86ea14a44c527c5388605ece94f47d637a` |
| `abl_a` | `/dev/block/sde10` | 1.048.576 | `443d1956e836aea3fe30ac13113dae787d88223c1e24f875c6831f69d86acea3` |
| `abl_b` | `/dev/block/sde38` | 1.048.576 | `443d1956e836aea3fe30ac13113dae787d88223c1e24f875c6831f69d86acea3` |
| `uefi_a` | `/dev/block/sde1` | 5.242.880 | `d32139de305aeb2aba1dc8c12b4996c49b09a09a6032307bf100ecb1ecb2d312` |
| `uefi_b` | `/dev/block/sde29` | 5.242.880 | `d32139de305aeb2aba1dc8c12b4996c49b09a09a6032307bf100ecb1ecb2d312` |
| `vbmeta_a` / `vbmeta_b` | `sde16` / `sde44` | 65.536 | `cc0de687c935714a9b6de4bf3492391474e640d2e23f752f6c626f6413b07e22` |
| `dtbo_a` | `/dev/block/sde17` | 25.165.824 | `9ee95463abce517d140eae3c5c892c2b38fd1344d6fb0a6e2da2af76a86de6a1` |
| `dtbo_b` | `/dev/block/sde45` | 25.165.824 | `9ee95463abce517d140eae3c5c892c2b38fd1344d6fb0a6e2da2af76a86de6a1` |
| `misc` | `/dev/block/sda3` | 1.048.576 | `6c93debe281b2edb75e7733391b65d8a0dd64e349eb9a229a58867a3956f341d` |
| `vendor_boot_a` / `vendor_boot_b` | `sde24` / `sde52` | 100.663.296 | `6db6d4d76059a9ef3a6bcc904911aac53a20044224a675014a6396a8f5d9d168` |

Note: **`vendor_boot_a` e `vendor_boot_b` sono la `vendor_boot` stock della V311** (sha256 identico a `full_extracted_v311/vendor_boot.img` e a `verified-v311/partitions/vendor_boot.img`) → le conclusioni su dtb_idx/dtbo_idx valgono per immagini stock, non per repack di prova.

### 1.2 La questione `xbl_a` / `xbl_b` incrociati: **risolta**

**Sono IDENTICI** e **non** incrociati. Tre misure indipendenti:

1. lettura locale `dd bs=1M` di `xbl_a` e `xbl_b` → stessa hash `685f0a75…`;
2. **seconda** lettura locale con `bs` diverso e in **ordine invertito** (`xbl_b` prima, poi `xbl_a`) → di nuovo `685f0a75…` per entrambe (controllo contro un eventuale artefatto di ordine/caching del trasporto);
3. `sha256sum /dev/block/by-name/xbl_a` e `…/xbl_b` **eseguito sul telefono** (nessun passaggio adb dei dati) → `685f0a75…` per entrambe.

Conclusione: la partizione XBL è la stessa immagine su entrambi gli slot; l'osservazione precedente ("dump diversi/incrociati") **non è riproducibile**. Non esiste, nei dump attuali, alcun offset in cui `xbl_a` differisca da `xbl_b`. Lo stesso vale per `abl_a==abl_b`, `uefi_a==uefi_b`, `vbmeta_a==vbmeta_b`, `dtbo_a==dtbo_b` (misurate in questa sessione). **[VERIFICATO]**

### 1.3 Chi contiene il codice che legge le immagini

**`abl_a`/`abl_b` = sì. `xbl_*` e `uefi_*` = no.**

Metodo: per ogni immagine, prima `strings`, poi (visto che `strings` non trovava nulla) scansione di magie di compressione/container e **decompressione ricorsiva** di firmware volume EDK2 (`re/fv-unpack.py`).

- **`xbl_a`** — contenitore ELF32 con **3 immagini ELF annidate** (`0x0`, `0x162f4`, `0x439f4`):
  - ELF#1 (32 bit, `e_machine=1`, entry `0x2211c000`) = segmento di hash/certificati (stringhe: `CHIP_PROD_PROV_K_LBL`, `ENTITY_DEV_FACT_PROV_K_LBL`, `Generated Ztemt Attestation CA`, `General Use Ztemt Key`), non codice applicativo;
  - ELF#2 (186.112 B) = payload **senza stringhe leggibili e non decodificabile come AArch64** (disassemblaggio all'entry `0x20412800` produce `.inst/undefined`) → compresso/cifrato, **[NON DETERMINATO]**;
  - ELF#3 = codice **XBL/SBL1 AArch64** (stringhe: `SBL1, End`, `sbl1_mc.c`, `sbl1_hw_pre_ddr_init`, `CDT not found in any storage media`, …).
  - Ricerca su **tutta** la partizione: `ANDROID!` **0**, `VNDRBOOT` **0**, `fastboot` **0**, `avb` **0**, `dtbo` **0**; nessuna magia di compressione (l'unico match LZMA a `0x60748` è un falso positivo: i byte `5d 00 00 94` sono l'istruzione `bl`). → **XBL non legge le immagini di boot**.
- **`uefi_a`** — ELF64 → FV da 2.752.512 B a `0x1000` con DXE core (`d6a2cb7f-6a18-4e2f-b43b-9920a733700a`), PEI core, driver, e **due sezioni gzip** (a `0x49f28` e `0x180fe0`) che si decompongono in 3.764.232 B e 2.773.000 B (altre FV con decine di moduli PE32: `QcomCharger`, driver seriale, …). Grep sui dati **decompressi**: `ANDROID!` 0, `VNDRBOOT` 0, `fastboot` 0, `libavb` 0 → **non è il componente che carica boot/vendor_boot**.
- **`abl_a`/`abl_b`** — contiene **tutto**: `ANDROID!`, `VNDRBOOT`, `libavb` (11 stringhe), `ufdt*` (18), `fastboot` (23), `slot_*`, `unbootable`, `qcom,msm-id`/`qcom,board-id`, e il percorso di build Nubia completo (vedi §2).

### 1.4 Controprova sul device (scansione letterale di tutte le partizioni)

Script read-only (`re/sigscan.sh`, eseguito come root, scrittura solo del file di output in `/data/local/tmp`): per ogni `/dev/block/by-name/*` non-LUN con dimensione ≤ 300 MB, `grep -abo 'ANDROID!'` e `grep -abo 'VNDRBOOT'`.

**Risultato**: le uniche occorrenze **non compresse** sono:
- `boot_a` → `ANDROID!` @ 0; `boot_b` → `ANDROID!` @ 0;
- `vendor_boot_a` e `vendor_boot_b` → `VNDRBOOT` @ 0 (verificato anche con lettura diretta dei primi 8 byte);
- **nessun'altra partizione** contiene le due firme.

Interpretazione: le copie "di codice" delle due stringhe stanno **solo** nell'ABL, e lì sono **compresse** (quindi invisibili a `strings`/`grep`) — è esattamente il motivo per cui nella sessione precedente l'ABL era stato giudicato "senza stringhe di boot image". Limite della prova: lo scan esclude i device LUN grezzi (`sda…sdf`) e le partizioni > 300 MB (in pratica `super`, `userdata`), che non fanno parte della catena di boot.

---

## 2. `ANDROID!` e `VNDRBOOT`: dove, a che offset, in che forma

### 2.1 Catena di contenitori dentro `abl_a` (1.048.576 B, sha256 `443d1956…`)

```
abl_a (flash)
 └─ ELF32 @0x0           e_machine=ARM(0x28), entry 0x9fa00000, 1 LOAD: file 0x1000 len 0x28000, vaddr 0x9fa00000
     │                   + segmento "hash" (p_type=0) a 0x29000 len 0xd58, resto della partizione = 0x00
     └─ UEFI Firmware Volume @0x1000   "_FVH", FvLength=0x28000, HeaderLength=72, BlockMap 320×512
         └─ FFS file @0x1048   GUID f536d559-459f-48fa-8bbc-43b554ecae8d
             └─ Sezione GUID_DEFINED @0x1060   GUID ee4e5898-3914-4259-9d6e-dc7bd79403cf  (EDK2 LZMA), DataOffset=24
                 └─ flusso .lzma @0x1078, 150.051 B
                    header LZMA: props=0x5D (lc3/lp0/pb2), dict=0x01000000 (16 MiB), dimensione = 0x000930C8 = 602.312 B
                    └─ DECOMPRESSO = 602.312 B  (sha256 0d7786715ccacc2f396e21dbf75f02bc89f95e3d5fcaec8d320c76967f9870e5)
                        = flusso di sezioni: [RAW 4 B][FV_IMAGE 602.308 B]
                        └─ FV interna @+8   HeaderLength=72
                            └─ FFS file @+0x78, type=0x09 FIRMWARE_VOLUME_IMAGE
                                ├─ sezione USER_INTERFACE @+0x90 → nome modulo **"LinuxLoader"** (UTF-16)
                                └─ sezione PE32 @+0xac, 602.116 B
                                    └─ MZ @+0xb0, PE header @+0xf08: machine=0xAA64 (AArch64), PE32+, ImageBase=0
                                         sezioni: .text VA 0x1000 (file 0x1000, 0x71000 B), .data VA 0x72000, .reloc VA 0x92000
                                         EntryPoint RVA 0x1000, Subsystem 10 (EFI application)
```

**Forma**: non è un ELF e non è un semplice PE su flash — è un **PE32+ AArch64 (UEFI application) compresso LZMA dentro una FFS/FV, dentro un contenitore ELF32 che il PBL/XBL carica**. La catena di boot che *esegue* queste decisioni è quindi il PE32+ decompresso.

Path di build presente nel binario (stringa a offset decompresso `0x608d2` ca.):
`/home/nubia/SCMWork/nrom/NX679J_Z69_UN_ZML1S_V311/20220308142115/app/out/target/product/taro/obj/ABL_OBJ/Build/DEBUG_CLANG35/AARCH64/QcomModulePkg/Application/LinuxLoader/LinuxLoader/DEBUG/LinuxLoader.dll`
e per libavb: `.../app/bootable/bootloader/edk2/QcomModulePkg/Library/avb/libavb/avb_slot_verify.c` (e `avb_util.c`, `avb_vbmeta_image.c`, `avb_chain_partition_descriptor.c`, `avb_property_descriptor.c`, `avb_descriptor.c`) → **[VERIFICATO]** l'origine esatta (Nubia, NX679J_Z69_UN_ZML1S_V311, target `taro`, clang35).

### 2.2 Offset delle due stringhe e codice che le usa

Nel payload decompresso (mappa VA = offset − 0xb0, perché il PE inizia a 0xb0 e le sezioni hanno `PointerToRawData == VirtualAddress`):

| stringa | offset nel payload decompresso | VA |
|---|---|---|
| `ANDROID!` | **0x632c4** | 0x63214 |
| `VNDRBOOT` | **0x632cd** | 0x6321d |

Sono adiacenti (`ANDROID!\0VNDRBOOT\0`, precedute da `ERROR: Failed to load image header: %r`), cioè la **tabella delle magie di immagine** con cui l'ABL riconosce il tipo di boot image.

Riferimenti nel codice (decodifica ADRP+ADD, `re/xref2.py`):

- `ANDROID!` ← `0x0dab4`, `0x17b88`, `0x313ec`
- `VNDRBOOT` ← `0x0dacc`, `0x17cf4`, `0x180e8`

Disassemblaggio (estratto, `re/logs/abl_text.asm`):
```
  dab4: adrp x1,0x63000 ; add x1,x1,#0x214   ; -> VA 0x63214 = "ANDROID!"
  dab8: mov  w2,#8
  dabc: bl   0x56aa0                          ; confronto byte (memcmp) su 8 byte
  dac8: ldur x0,[x23,#-184]
  dacc: adrp x1,0x63000 ; add x1,x1,#0x21d   ; -> VA 0x6321d = "VNDRBOOT"
  dad0: mov  w2,#8
  dad4: bl   0x56aa0                          ; stesso confronto
```
Le stesse stringhe sono anche usate nella logica di *vendor boot* (`0x17b88/0x17cf4/0x180e8`) e nella validazione del boot image header v3/v4 (offset 0x313ec).

**Da notare**: le offset valide sono quelle **nel payload decompresso**; sulla partizione `abl_a` le stringhe non esistono in chiaro. La VA di runtime (dove il PE viene caricato) non è determinabile staticamente: PE con `ImageBase=0` e rilocazioni, caricato dal decompressore di FV → **[NON DETERMINATO]**.

---

## 3. Come viene scelto **quale dei nove DTB** usare

### 3.1 I nove candidati (misurati, con `dtc`, sulla copia byte-identica al device)

Area DTB di `vendor_boot` (header v4: `header_size=2128`, `page_size=4096`, `vendor_ramdisk_size=10041284`, `dtb_size=3879360`, `dtb_addr=0x1f00000`, area a **offset 10047488** = 0x995000: `align(2128,4096)+align(10041284,4096)`):

| # (indice) | offset | size | model | compatible | qcom,msm-id | qcom,board-id |
|---|---|---|---|---|---|---|
| 0 | 0 | 431264 | Qualcomm Technologies, Inc. Cape LTE Only SoC | qcom,cape-v2 | <0x212 0x10000> | <0x00 0x00> |
| 1 | 431264 | 431252 | …Cape SoC | qcom,cape | <0x212 0x10000> | <0x00 0x00> |
| 2 | 862516 | 347467 | …CapeP SoC | qcom,capep | <0x213 0x10000> | <0x00 0x00> |
| 3 | 1209983 | 298995 | …Diwali HSP SoC | qcom,diwali | <0x1fa 0x10000> | <0x00 0x02> |
| 4 | 1508978 | 387498 | …Diwali SoC | qcom,diwali | <0x1fa 0x10000> | <0x00 0x00> |
| **5** | **1896476** | **496030** | **…Waipio v2 SoC** | **qcom,waipio** | **<0x1c9 0x20000>** | **<0x00 0x00>** |
| 6 | 2392506 | 495598 | …Waipio SoC | qcom,waipio | <0x1c9 0x10000> | <0x00 0x00> |
| 7 | 2888104 | 495630 | …WaipioP v2 SoC | qcom,waipiop | <0x1e2 0x20000> | <0x00 0x00> |
| 8 | 3383734 | 495626 | …WaipioP SoC | qcom,waipiop | <0x1e2 0x10000> | <0x00 0x00> |

(comando: `re/fdt-enum.py` + `dtc -I dtb -O dts`; la catena dei `totalsize` è coerente: 0+431264=431264, 431264+431252=862516, …; l'hash del DTB #5 `45304e9c2f43d7…` coincide con quello registrato nello scan precedente `fdt-scan.log`).

### 3.2 Chi decide, e come: il codice nell'ABL

Stringhe nell'ABL (offset nel payload decompresso) e xref nel codice:

| stringa (offset) | significato | xref (VA) |
|---|---|---|
| `qcom,msm-id` (0x66eb2) | nome proprietà SOC | 0x1b220, 0x1c728 |
| `qcom,board-id` (0x66e13) | nome proprietà board | 0x1b044, 0x1c944 |
| `qcom, msd-id … not a multiple of (%d)` / `qcom,board-id (%d) … not a multiple of (%d)` | validazioni di forma dei valori | — |
| `qcom,msm-id entry not found` | nessuna proprietà | — |
| `Best match DTB tags %u/%08x/0x%08x/%x/%x/%x/%x/%x/(offset)0x%08x/(size)0x%08x` (0x66934) | stampa della scelta | 0x1a978 |
| `Exact DTB match found. DTBO search is not required` (0x66a84) | match esatto → non serve cercare oltre | 0x1c3b8 |
| `Delete don't fit DTB entry %u/%08x/0x%08x/%x/%x/%x/%x/%x/%x/%x` (0x66d66) | scarto dei candidati non compatibili | 0x1e258, 0x1e5f4, 0x1e874 |
| `DT Total number of entries: %d, DTB version: %d` (0x66d02) | numerosità/tabella | — |
| `model does not exist in device tree`, `DTB Image not present: DTB Size=%u`, `Device Tree Load Address: 0x%x` | percorso di caricamento DTB | — |

Disassemblaggio del confronto (cuore della decisione):
```
 1c944: adrp x2,0x66000 ; add x2,x2,#0xd63   ; x2 = "qcom,board-id" (VA 0x66d63)
 1c948: bl   0x58d28                          ; lettura proprietà dal FDT candidato
 1c95c: ldr  x0,[sp,#360]  (valore locale)
 1c960: ldur w2,[x8,#-140]  (numero di celle del valore letto)
 1c964: bl   0x1f538                          ; confronto -> restituisce il match
```
e la `0x1f538` è un helper di confronto (prologo `sub sp,sp,#0x50`, salvataggio di `x0/x1/w2`, controllo puntatori nulli e log d'errore). Analogamente a `0x1b220/0x1c728` viene letto `qcom,msm-id`.
Nel codice compare anche una costante SOC hard-coded: `0x5ba4: movk w0,#0x1c9,lsl#16` (SOC Waipio) — **[IPOTESI]** è il percorso che usa l'id SOC da SMEM/registri quando il valore non arriva dal DT.

### 3.3 Quale dei nove è stato scelto **su questo telefono** (prova runtime)

`/proc/bootconfig` (letto dal device, Android avviato):
```
androidboot.dtb_idx = "5"
androidboot.dtbo_idx = "35"
androidboot.verifiedbootstate = "orange"
androidboot.vbmeta.device_state = "unlocked"
androidboot.slot_suffix = "_a"
```
e `/proc/device-tree`:
```
model      = "Qualcomm Technologies, Inc. Waipio MTP with PM8010"
compatible = "qcom,waipio-mtp", "qcom,waipio", "qcom,mtp"
qcom,board-id = 00 01 00 08 00 00 00 00   -> <0x00010008 0x00000000>
qcom,msm-id   = 00 00 01 c9 00 02 00 00  00 00 01 e2 00 02 00 00  -> <0x1c9 0x20000>, <0x1e2 0x20000>
```

**Lettura dei dati**: l'ABL ha scelto il DTB **#5** (`Waipio v2`, `qcom,msm-id=<0x1c9 0x20000>`) perché è l'**unico** dei nove con l'msm-id/SOC-revision che combacia con la piattaforma (gli altri Waipio hanno `rev 0x10000`, i Cape/Diwali hanno id diversi). Poiché **tutti** i nove DTB base dichiarano `qcom,board-id=<0 0>` (jolly), il board-id del telefono **non** discrimina in questa fase. Il board-id `0x10008` che si vede nel DT vivo **non viene dal DTB base** ma dall'overlay dtbo (§4): **[VERIFICATO]** che esso è presente nella voce 35 del dtbo (byte pattern `00 01 00 08 00 00 00 00` a offset `0x120` della voce) e assente nei nove base.

> Nota sulla cattura runtime: `androidboot.slot_suffix="_a"` indica che **questa** cattura è stata fatta con il device avviato dallo **slot A**, coerente con il bit *Active* impostato su tutte le entry `_a` della GPT (§5.1). Le conclusioni su DTB/dtbo/A-B non dipendono dallo slot: `vendor_boot_a`/`vendor_boot_b` e `dtbo_a`/`dtbo_b` sono identici (hash in §1.1).

**Non determinato**: la semantica completa della funzione di *scoring* (i campi stampati da `Best match DTB tags %u/%08x/0x%08x/%x/%x/%x/%x/%x/(offset)/(size)` e la regola di tie-break), e la provenienza esatta dei "tag" locali (SMEM vs FDT passato da XBL): ho verificato i *nomi* delle proprietà confrontate, la chiamata al confronto e il *risultato* sul device, non l'intera funzione riga per riga.

---

## 4. Come viene applicato il dtbo

### 4.1 La tabella dtbo sul device

`dtbo_a` = `dtbo_b` = 25.165.824 B, sha256 `9ee95463…`. Header `dt_table` (big-endian): magic `0xd7b7ab1e`, `total_size=9447224`, `header_size=32`, `dt_entry_size=32`, **`dt_entry_count=44`**, `dt_entries_offset=32`, `page_size=4096`, `version=0`. Ogni voce (32 B) = `{dt_size, dt_offset, id, rev, custom[4]}` con i DT in chiaro (magic `d00dfeed`).

### 4.2 La voce effettivamente applicata: **indice 35**

- `androidboot.dtbo_idx = "35"` (dal bootconfig vivo, cioè l'ABL lo comunica ad Android perché lo usi per la coerenza A/B).
- Voce 35: `size=334200`, `offset=0x65eac4`, magic `d00dfeed`.
- Contenuto (estratto in `re/work/dtbo_entry35.dtb`, sha256 `5d4d23a6c5922d4a69dd55716c3e3750b225ddd557d4b75ddf75ae233716e054`): stringhe `Qualcomm Technologies, Inc. Waipio MTP with PM8010`, `qcom,waipio-mtp`, `qcom,waipio`, `qcom,mtp`, nodi `regulator-pm8010i-l1…`, `qcom,ufs-phy-qmp-v4-waipio`, e 59 occorrenze di `nubia`; al byte `0x120` il pattern `00 01 00 08 00 00 00 00` = `qcom,board-id = <0x10008 0x0>`.
- **Confronto con il DT vivo**: il `model` e i tre `compatible` della voce 35 coincidono **esattamente** con quelli del kernel in esecuzione → prova che il DT finale = DTB #5 di vendor_boot **+ overlay della voce 35**, e che le proprietà *root* dell'overlay finiscono nel root del DT finale.

### 4.3 Il codice che applica l'overlay

Simboli/stringhe presenti nell'ABL (`libufdt` statico): `ufdt_install_blob`, `ufdt_apply_overlay`, `ufdt_apply_multi_overlay`, `ufdt_overlay_apply`, `ufdt_overlay_local_ref_update`, `ufdt_overlay_do_fixups`, `ufdt_do_one_fixup`, `ufdt_get_fixup_location`, `ufdt_apply_fragment`, `ufdt_get_node_by_path_len`, `ufdt_node_add_child`, `ufdt_node_dict_add/resize`, `Failed to dump the device tree to out_fdt_header`, `Failed one fixup in ufdt_do_one_fixup`; più il livello ABL:
`ApplyOverlay: Invalid input parameters`, `ApplyOverlay: Overlay DT is NULL`, `ApplyOverlay: Install blob failed`, `ApplyOverlay: ufdt apply overlay failed` (← `0x19d78`), `ApplyOverlay: Apply overlay DTB size exceeded than supported`, `Apply Overlay total time: %lu ms` (← `0x19ec8`), `Error: Dtb overlay failed`, `Unable to Allocate buffer for Overlay DT`, `Error: Board Dtbo blob not found` (← `0x16c4c`), `Dtbo count = %u LocalBoardDtMatch = %x` (← `0x1dae8`), `Override DTB: GetBlkIOHandles failed loading user_dtbo!`, `Override DTB: No proper DtTable`, `Override DTB: Exceeding maximum supported dtb count in Image`.
Inoltre vengono gestiti overlay separati per l'hypervisor: `HypInfo: Not overlaying hyp dtbo`, `Failed to allocate memory for HypDtbo %d`, `Unable to Allocate buffer for HypOverlay DT num: %d`.

Flusso ricostruito **[VERIFICATO nelle sue parti, IPOTESI nel dettaglio]**: (a) l'ABL legge la partizione `dtbo` dello slot corrente, ne valida la `dt_table`; (b) filtra/seleziona la voce sulla base del match con il DTB base e con il "LocalBoardDtMatch" (il valore `0x10008` presente nell'overlay è coerente con questa funzione: `Dtbo count = %u LocalBoardDtMatch = %x`); (c) installa il blob (`ufdt_install_blob`) e applica l'overlay sul DTB base (`ufdt_apply_overlay`/`multi`), verificando la dimensione finale; (d) esporta `androidboot.dtbo_idx` (35) e `androidboot.dtb_idx` (5) nel bootconfig passato al kernel.
**Non determinato**: la funzione esatta di match delle voci dtbo (come si passa da 44 voci alla #35) — verificato il *risultato*, non l'algoritmo completo.

---

## 5. Scelta dello slot A/B e contatori

### 5.1 Dove sta lo stato: attributi GPT (non `misc`)

- L'ABL legge/scrive gli **attributi della GPT**: stringhe `GetActiveSlot: Slot attr: Priority %ld, Retry %ld, Active %ld, Success %ld, unboot %ld` (← `0x37358`), `Error writing partition entries array for Primary Table: %x` (← `0x39c34`), `Updated Partition Table Successfully` (← `0x39db4`), `Error writing partition entries array for Secondary Table: %x`.
- Nel codice gli attributi (8 byte) sono ricostruiti caricando i due dword a `+48` e `+52` della entry GPT e unendoli (`bfi x8,x9,#32,#32` a `0x37348`).
- Gli attributi sono stati **riletti ORA dal device**: `dd if=/dev/block/sde bs=4096 skip=2 count=4` (entry GPT primarie, 128 × 128 B; file `re/work/sde-gpt-entries.bin`, sha256 `0d2fe55da5afbdec6682db5c38860e06a76ed6e3cc519419e5f491008c42310f`, letto 13:04) e decodificati con `re/gpt-slots.py`.
- Sul device gli attributi sono su **sde** (le partizioni slot-suffixed stanno tutte su `sde`, vedi §1.1). Risultato (32 partizioni con attributi non nulli): **tutte** le `_a` hanno `attr = 0x007f…` o `0x107f…` → nel layout di §5.2: Priority 3, **Active 1**, Retry 7, Success 1, Unbootable 0; **tutte** le `_b` hanno `attr = 0x007b…` o `0x107b…` → Priority 3, **Active 0**, Retry 7, Success 1, Unbootable 0. Esempi: `abl_a = 0x107f000000000000`, `boot_a = 0x007f000000000000`, `vendor_boot_a = 0x007f000000000000`, `abl_b = 0x107b000000000000`, `boot_b`/`vendor_boot_b = 0x007b000000000000`. Coerente con `androidboot.slot_suffix="_a"` letto a runtime.
- Il `misc` (1 MiB su `sda3`) contiene in offset 0 la **BCB AOSP**: campo `command` = `bootonce-bootloader\0` (hexdump). Però nell'ABL **non esiste** la stringa `bootonce-bootloader`, e i suoi usi di `misc` sono altri (`Error reading virtualab msg from misc partition: %r`, `Error Reading FFBM info from misc: %r`, `Error in loading Data from misc partition`) → **la scelta dello slot non passa dalla BCB di `misc`**, ma esclusivamente dagli attributi GPT. **[VERIFICATO]** (assenza della stringa nel binario + contenuto di misc letto ora).

### 5.2 Layout dei bit (estratto dal codice, non dalla documentazione)

| bit (u64 attributo) | campo | evidenza (istruzione) |
|---|---|---|
| 48-49 | **Priority** | `0x36fd8: ldrh w8,[x8,#54]` + `and x8,x8,#0x3` → salvato e passato come 1° argomento della stampa `…Priority %ld, Retry %ld, Active %ld, Success %ld, unboot %ld` (`0x37358`) |
| 50 | **Active** | `0x3734c: ubfx x4,x9,#18,#1` (bit 18 del dword a `+52` = bit 50 del u64) come 3° argomento; e `0x1f9d0: orr x8,x8,#0x4000000000000` = **marca attivo** il bit 50 |
| 51-53 | **Retry / tries** | `0x36fec: ubfx x8,x8,#19,#3` (bit 19-21 del dword a `+52`) come 2° argomento |
| 54 | **Success** | `0x37350: and x5,x8,#0x40000000000000`; testato a `0x51c1c: tst x9,#0x40000000000000` + `0x51c28: tbnz x9,#54,…` |
| 55 | **Unbootable** | `0x37354: and x6,x8,#0x80000000000000`; testato a `0x51ccc: tst x9,#0x80000000000000` |

Applicando il layout ai valori misurati: `_a` = Priority 3, **Active 1**, Retry 7, Success 1, Unbootable 0; `_b` = Priority 3, **Active 0**, Retry 7, Success 1, Unbootable 0.

### 5.3 Funzioni e comportamento

Nomi ricavati dalle stringhe d'errore interne (quindi affidabili) e loro riferimenti nel codice: `GetActiveSlot` (`0x36f04`: `GetActiveSlot: found active slot %s, priority %d`; `GetActiveSlot: First boot: set default slot _a`; `GetActiveSlot: Slot attr: …`), `SetActiveSlot` (`SetActiveSlot: %s already active slot`, `Alternate slot %s, New slot %s`), `IsCurrentSlotBootable` (`Slot suffix %s Part Attr 0x%lx`, `Slot %s is unbootable`, ← `0x39ec8`), `FindBootableSlot` (`Active Slot %s is bootable, retry count %ld`, `Slot %s is unbootable, trying alternate slot`), `ClearUnbootable`, `HandleActiveSlotUnbootable` (`FirstBoot, skipping slot Unbootable`, `Alternate Slot %s is bootable`, `HandleActiveSlotUnbootable: Rebooting`), `ValidateSlotGuids` (`Boot lun: %x and BootableSlot: %s do not match`), più i fallback `No bootable slots found enter fastboot mode`, `Non Multi-slot: Unbootable entering fastboot mode`, `IsCurrentSlotBootable: no active slots found!`, `Multi Slot boot is supported` / `Multi Slot boot is not supported`.

**Regola ricostruita**: lo slot da avviare è quello la cui entry GPT ha il bit **Active** (e non Unbootable); se non ce n'è nessuno, al primo boot l'ABL imposta il default `_a` (`GetActiveSlot: First boot: set default slot _a`).
**Perché i contatori non calano** (osservazione: `slot-retry-count` fermo a 7 dopo ~10 riavvii): entrambe le entry hanno **Success=1** (bit 54). Nei pressi delle due verifiche dei bit 54/55 (`0x51c1c`, `0x51ccc`) sta la logica di aggiornamento/marcatura; la lettura più coerente è che il decremento dei retry e/o la marcatura **Unbootable** avvengano solo per slot **non** marcati *successful* → **[IPOTESI]**, supportata da: (a) i due `tst` sui bit 54/55 nello stesso blocco, (b) i valori GPT attuali, (c) il comportamento osservato.

Interfaccia fastboot dei contatori (stringhe presenti): `has-slot:boot/system/modem`, `current-slot` (← `0x51e60`), `slot-retry-count`, `slot-unbootable`, `slot-successful`, `slot-suffixes`, `slot-count`, `partitions with slot`, `Partition %s has slot`, `Nullifying A/B info`, `No change in Ptable`, `set_active _a or _b should be entered` / `set_active failed` (← `0x50790`, handler `set_active` = `0x505d8`).
Lato output verso Android: `androidboot.slot_suffix=` (presente nell'ABL; sul device vale `_a`).

---

## 6. Verifica AVB: cosa fa e perché un'immagine non firmata parte comunque

### 6.1 Cosa è presente

libavb è **linkato staticamente** nell'ABL (path sorgente Nubia in §2.1). Stringhe rilevanti:
`AVB version %d`, `avb_sha256_init/update/final`, `avb_abort!`, `AvbSlotVerify`, `AvbGetSizeOfPartition/Image`, `ValidateVbmetaPublicKey PublicKeyLength %d,…`, `AvbGetUniqueGuidForPartition`, `ERROR: Failed to allocate AvbOps`, `: Error loading vbmeta data.`, `: Error verifying vbmeta image: invalid vbmeta header`, `: VERIFICATION_DISABLED bit is set.`, `: success: Image verification completed`, `vbmeta rollback_index: %d`, `system rollback_index: %d`, `Not updating rollbackindex as current slot is unbootable`.
Esiti e rami decisionali:
- `Slot: %a, allow verification error: %a`
- `State: Unlocked, AvbSlotVerify returned %a, continue boot` (← `0x68f8`)
- `Device is unlocked, Skipping boot verification` (← `0x5bec`)
- `ERROR: Device State %a,AvbSlotVerify returned %a` (percorso di errore)
- `ERROR: AvbSlotVerify slot data error: num of loaded partitions %d, requested %d`
- stampa dello stato: `Unlocked` / `Locked` (VA `0x608c1` / `0x608ca`, selezionate con `csel` a `0x6874`)
- UX di verified boot presente ma non raggiunta qui: `Your device is corrupt. It can't be trusted and will not boot`, `The boot loader is unlocked and software integrity cannot be guaranteed…`, `ORANGE`, `YELLOW`, `GREEN`, `RED`.

Parametri esportati (dal bootconfig, quindi prodotti dall'ABL/libavb): `androidboot.vbmeta.device="PARTUUID=ef598a96-a359-bf7a-9361-16bf46ef6b3c"`, `.avb_version="1.0"`, `.device_state="unlocked"`, `.hash_alg="sha256"`, `.size="11904"`, `.digest="030f49bd…33c2"`, `.invalidate_on_error="yes"`, `androidboot.verifiedbootstate="orange"`, `androidboot.veritymode="enforcing"`.

### 6.2 Perché un'immagine non firmata viene avviata

**[VERIFICATO]** Lo stato del device è **unlocked** (`vbmeta.device_state=unlocked`, `verifiedbootstate=orange`), e l'ABL contiene un percorso esplicito che in quello stato **prosegue l'avvio**: `Device is unlocked, Skipping boot verification` e `State: Unlocked, AvbSlotVerify returned %a, continue boot`. Quindi la verifica può anche *fallire* (o essere saltata) senza bloccare il boot: è esattamente il comportamento osservato (Android avviato da `_b` con firma non valida). La verifica **viene comunque eseguita** per estrarre i metadati (`vbmeta.size=11904`, digest) e per popolare `androidboot.vbmeta.*`; inoltre `Slot: %a, allow verification error: %a` mostra che l'errore è *tollerato* per policy.

**Non determinato**: da dove l'ABL legga lo stato "unlocked" (fuse QFPROM vs partizione `devinfo`/`frp`) — nel binario esistono `Error Reading FRP partition: %r`, `IsAllowUnlock is %d`, `Device unlocked: %a`, `NUBIA_NX679J`, `START update nubia fastboot unlock flag!!!`, `unlock password check fail!!`, `get_unlock_ability: %d`, ma non ho disassemblato l'implementazione di `read_is_device_unlocked`. Non ho nemmeno verificato se il *kernel* applichi `dm-verity` su `vbmeta_system` (`veritymode=enforcing` è passato, ma l'enforcement è del kernel/init, fuori dall'ABL).

---

## 7. Perché `fastboot boot`, `oem edl`, `reboot edl` non esistono

### 7.1 La tabella dei comandi (decodificata dal binario)

Metodo: (a) ricerca dei puntatori alle stringhe-nome dentro il payload decompresso, (b) decodifica dell'array di struct da 16 byte `{const char *name; handler}`, (c) disassemblaggio del ciclo che le registra.

Ciclo di registrazione (`0x5038c`):
```
5038c: adrp x20,0x6f000
50390: mov  x19,xzr
50394: add  x20,x20,#0x4a8
50398: cmp  x19,#0x170            ; 0x170 = 368 = 23 voci * 16
5039c: b.eq 0x503b4               ; fine tabella
503a0: add  x8,x20,x19
503a4: ldp  x0,x1,[x8,#16]        ; x0 = nome, x1 = handler
503a8: bl   0x50548               ; registra la voce (alloca e concatena in lista)
503ac: add  x19,x19,#0x10
503b0: b    0x50398
```
Tabella a VA `0x6f4b8` (payload decompresso `0x6f568`), **23 voci**:

| # | nome | handler VA | # | nome | handler VA |
|---|---|---|---|---|---|
| 0 | `flash:` | 0x5354c | 12 | `oem enable-charger-screen` | 0x50bb0 |
| 1 | `erase:` | 0x53de4 | 13 | `oem disable-charger-screen` | 0x50c08 |
| 2 | `set_active` | 0x505d8 | 14 | `oem off-mode-charge` | 0x50c60 |
| 3 | `flashing get_unlock_ability` | 0x50b20 | 15 | `oem select-display-panel` | 0x50db0 |
| 4 | `flashing unlock` | 0x50ba0 | 16 | `oem device-info` | 0x51108 |
| 5 | `flashing lock` | 0x50ba8 | 17 | `continue` | 0x511e8 |
| 6 | `oem nubia_unlock` | 0x5410c | 18 | **`reboot`** | 0x51298 |
| 7 | `oem nubia_lock` | 0x54148 | 19 | `reboot-bootloader` | 0x51384 |
| 8 | `oem nubia_device-info` | 0x54154 | 20 | `reboot-recovery` | 0x512d4 |
| 9 | `oem nubia_unlock_critical` | 0x542a4 | 21 | `reboot-fastboot` | 0x5132c |
| 10 | `oem nubia_lock_critical` | 0x542e0 | 22 | `getvar:` | 0x513c8 |
| 11 | *(non presente: salto nell'indice)* | — | 23 | `download:` | 0x51670 |

Le **23** voci sono esattamente quelle sopra (indici interni della tabella 0…22; la numerazione di destra è progressiva per comodità di lettura).

**Assenti**: `boot` (avvio da RAM), `oem edl`, `reboot edl`, `flashall`, `powerdown`, `upload`, `fetch`. Il dispatcher dei comandi USB stampa `Handling Cmd: %a` (`0x4f09c`), `Invalid input command` (`0x4f15c`) e, per i comandi non riconosciuti, **`unknown command`** (VA `0x6dd2c`, usata a `0x4f32c`): è letteralmente la risposta ottenuta sul campo per `fastboot boot`, `oem edl` e `reboot edl`.

### 7.2 Conseguenze operative

- `fastboot boot <img>` **non è implementabile** su questa unità: il comando non esiste nel firmware (non c'è un percorso "avvia da RAM"), quindi il nostro percorso deve passare da `flash:` su `boot`/`vendor_boot` (o dallo slot B) e non da un boot temporaneo.
- EDL: nell'ABL esiste la stringa **`Boot to edload`** (in `.data`, payload decompresso `0x75090`) accanto a `Reboot system now`, `Reboot to recovery mode`, `Power off`: sono le voci del **menu di boot** (volume/power). Con la ricerca ADRP+ADD **non** ho trovato riferimenti diretti a quella stringa → **[IPOTESI]** l'ingresso in EDL dal firmware avviene via menu/flag, non via fastboot. Non esiste alcun comando fastboot `edl`/`oem edl`.
- `getvar` disponibili (stringhe presenti): `getvar:partition-type`, `kernel`, `max-download-size`, `is-userspace`, `snapshot-update-status`, `product`, `serialno`, `secure`, `variant`, `logical-block-size`, `erase-block-size`, `version-bootloader`, `version-baseband`, `battery-voltage`, `battery-soc-ok`, `charger-screen-enabled`, `hw-revision`, `parallel-download-flash`, `has-slot`, `current-slot`, `slot-retry-count`, `slot-unbootable`, `slot-successful`, `slot-suffixes`.
- **[NON DETERMINATO]** la semantica esatta del matching dell'input (prefissi `flash:`/`getvar:`/`download:`, argomenti dei `reboot-*` e del menu `reboot`), che ho ricavato per lettura delle stringhe/registrazione, non da un disassemblaggio completo del matcher; e cosa offra in più il *fastbootd* userspace di Android (fuori dal bootloader).

---

## 8. Elenco di ciò che **non** è stato possibile determinare

1. **Indirizzi di runtime dell'ABL**: il PE32+ ha `ImageBase=0`; il payload da 602.312 B viene decompresso dal codice FV. Le offset citate sono nel *payload decompresso*, non indirizzi assoluti di RAM.
2. **`xbl_a` ELF#2** (186.112 B): payload senza stringhe e non decodificabile come AArch64 → compresso/cifrato con schema proprietario; non ho potuto escluderne al 100 % il contenuto (ma l'intera partizione non contiene `ANDROID!`/`VNDRBOOT`/`fastboot`/`avb`).
3. **Algoritmo di scoring della scelta DTB** (`Best match DTB tags …`) e origine esatta dei tag locali (SMEM vs FDT di XBL).
4. **Funzione di match delle voci dtbo** (da 44 voci alla #35): risultato verificato, algoritmo no.
5. **Regola esatta di decremento dei `tries` / marcatura `Unbootable`** (bit 54/55 testati, regola inferita).
6. **Implementazione di `read_is_device_unlocked`** (fuse vs partizione) e relazione con le stringhe Nubia (`nubia_unlock`, password di unlock).
7. **Matcher completo del fastboot** (prefissi e argomenti) e comportamento del fastbootd userspace.
8. **Significato del bit 60** dell'attributo GPT (presente su tutte le entry `_a` e su `uefi_b`, non usato dal codice slot che ho disassemblato).
9. **Decodifica della bootconfig da 85 B dentro `vendor_boot`**: la lettura del settore a 13930496 (`dd skip=3401`) mostra una struttura binaria e non il testo atteso → **[NON DETERMINATO]** (probabilmente l'offset indicato nel lavoro precedente è l'inizio di una struttura, non del testo; ho usato la bootconfig *unita* letta a runtime da `/proc/bootconfig`, 885 B, che contiene tutte le voci `androidboot.*`).

---

## 9. Metodo, comandi, artefatti

**Strumenti**: `adb` + `su` (letture `dd`), `sha256sum` (host e device), `strings`, `readelf`, `aarch64-linux-gnu-objdump` (binutils 2.47), `dtc`, Python 3 (script in `re/`): `fv-unpack.py` (parsing FV/FFS/sezioni + LZMA/gzip ricorsivo), `fv-struct.py`, `scan-magic.py`, `xref2.py` (xref ADRP+ADD), `fb-table3.py` (tabella fastboot), `dtbo-parse.py`, `fdt-enum.py`, `asmdump.py` (finestre di disassemblaggio + ricerca `ubfx`/maschere), `final-check.py`, `sigscan.sh` (scan on-device).

Comandi chiave (riproducibili, sola lettura):
```
adb -s 0123456789ABCDEF exec-out "su -c 'dd if=/dev/block/by-name/xbl_a bs=1M 2>/dev/null'" > xbl_a.img
adb -s 0123456789ABCDEF shell "su -c 'sha256sum /dev/block/by-name/xbl_a /dev/block/by-name/xbl_b'"
python3 fv-unpack.py out bl/abl_a.img            # FV -> FFS -> LZMA -> FV -> PE32
aarch64-linux-gnu-objdump -D -b binary -m aarch64 -EL --adjust-vma=0x1000 abl_text.bin
adb -s 0123456789ABCDEF shell "su -c 'cat /proc/bootconfig'"
adb -s 0123456789ABCDEF shell "su -c 'cat /proc/device-tree/model /proc/device-tree/compatible'"
adb -s 0123456789ABCDEF shell "su -c 'cat /proc/device-tree/qcom,board-id /proc/device-tree/qcom,msm-id'"
```

**Artefatti principali** (tutti locali, nessun firmware esportato):
- `re/bl/{xbl_a,xbl_b,abl_a,abl_b,uefi_a,uefi_b,vbmeta_a,vbmeta_b,misc,dtbo_a}.img` (dump freschi, con hash)
- `re/out/abl_a.img@1000_f1048_gd1060_fvi_4.bin` — **ABL decompresso** (602.304 B, sha256 `0d7786715ccacc2f…`)
- `re/work/abl_text.bin`, `re/logs/abl_text.asm` — `.text` dell'ABL + disassemblaggio (115.593 righe)
- `re/work/sde-gpt-entries.bin` — entry GPT primarie di sde rilette ora (attributi slot), sha256 `0d2fe55da5afbdec6682db5c38860e06a76ed6e3cc519419e5f491008c42310f`
- `re/logs/`: `abl-strings.txt` (tutte le stringhe dell'ABL con offset), `fb-table-decoded.log` (tabella fastboot), `fdt-enum.log` + `fdt-root-props.log` (i nove DTB), `gpt-slots.log` (attributi slot), `xref-key.log`/`xref-key2.log` (xref ADRP+ADD), `scan-magic.log`, `fv-*.log` (decompressioni), `sigscan-device.txt` (scan letterale on-device), `live-cmdline.txt`, `live-bootconfig.txt`
- `re/work/fdt/b00..b08.dtb` (i nove DTB base), `re/work/dtbo_entry35.dtb` (overlay applicato)
