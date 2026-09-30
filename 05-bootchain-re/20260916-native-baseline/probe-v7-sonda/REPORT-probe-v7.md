# Sonda v7 / v7b — "il kernel raggiunge lo userspace quando la ramdisk e' nostra?"

Domanda unica, risposta binaria. `/init` e' un ELF statico che scrive una riga di
marcatura e poi chiede al kernel un `RESTART`, per sempre, ogni ~10 s. Se la sonda
gira, il telefono si riavvia da solo; se non gira, il telefono resta fermo sul logo
come con v5 e v6. Non c'e' un terzo esito possibile.

## TL;DR

- Due immagini pronte, verificate (28/28) e **provate in QEMU col kernel del
  telefono: 12/12**, in entrambe `/init` parte come PID 1 e il kernel si riavvia.
- La sonda non carica moduli, non tocca UFS/USB: un solo file cambia rispetto a
  v6 (v7) o all'Android di probe6 (v7b).
- **Unica ipotesi NON verificata: che il contatore A/B si consumi.** Non e'
  refutata (i fallimenti precedenti non riavviavano il telefono, quindi quel caso
  non si e' mai presentato) ma nemmeno provata: se non scatta, la sonda gira in
  loop e il telefono va recuperato a mano (POWER ~15 s, poi POWER+VOL- →
  `fastboot set_active a`). Dettaglio e prove in fondo.

## Deliverable

| immagine | percorso | sha256 | ramdisk | header |
|---|---|---|---|---|
| **v7** | `probe-v7-sonda/boot_b-probe-v7.img` | `77d7675eb54a65d9b3ca45042731b8180d1d927d6006a5c80e8f9775e1c3c2a5` | 1.014.155 B | kernel 49108324, ramdisk 1014155, header 1584, sig 4096, v4 |
| **v7b** | `probe-v7-sonda/boot_b-probe-v7b.img` | `7424a599a6a9b7e105ad1ed59b53b9dfe6763238ca16e88f9d7ad9adaba48e20` | 2.352.880 B | kernel 49108324, ramdisk 2352880, header 1584, sig 4096, v4 |

Entrambe: 100.663.296 B esatti (partizione piena), kernel byte-identico a quello
che oggi avvia Android, coda originale identica agli stessi offset assoluti.

- v7 = ramdisk v6 (65 voci + TRAILER, gli stessi 47 moduli) con **solo `/init`
  sostituito** dalla sonda. Delta vs v6: il contenuto di `/init` + la
  compressione (v6 era gz via mkbootimg, qui lz4-legacy nel contenitore Magisk,
  cioe' esattamente l'imballaggio di probe6 che avvia Android in 26 s).
- v7b = ramdisk Android **esatta** (`inputs/magisk-android-ramdisk.cpio`,
  2.306.300 B, sha `6fd26309…`, 30 voci, la stessa di probe6) con il suo `/init`
  (che e' l'init di Magisk, 263.928 B, modo 0100750) sostituito dalla sonda;
  tutto il resto invariato voce per voce.
- `/init` sonda: 532.352 B, sha256 `a1376c22ac8a02fbdf7e7e1e3b8bb2fe0b7888c9c6b4d7dcf97da36dbaf51dd7`
  — **ELF aarch64, EXEC, statico, PT_INTERP = 0, DT_NEEDED = 0**.
- Sorgente: `probe-v7-sonda/candidate-init-sonda.c` (adattato da
  `candidate-init-v6.c`, canali `/dev/kmsg` → `/dev/console` → fd, con
  `mknod(2)` di `/dev/kmsg` se manca: la ramdisk Android ha `/dev` vuota e
  senza quel fallback non ci sarebbe nessuna marcatura in v7b — in QEMU la
  sonda di v7b ha riportato `marker channel /dev/kmsg`, cioe' il fallback ha
  funzionato sul kernel vero).

**Toolchain** (identica a v6):
`aarch64-linux-gnu-gcc (GCC) 16.1.0` con
`-static -Os -s -Wall -Wextra -Wno-unused-result` (zero warning).
Compressore: `lz4 v1.10.0`, `lz4 -l -9`; **prova che e' lo stesso compressore di
probe6**: `lz4 -l -9` su `/tmp/p6.cpio` riproduce byte per byte il frame
`c90e61fb…` presente nell'immagine che ha avviato Android.

## Il trapianto nel contenitore (misurato, non dedotto)

Origine: `/home/user/nx679j-stock/magisk_patched-30700_Zx2eF.img`
(sha `0cd94d56…`, l'immagine del test di controllo che ABL ha avviato da B).
Metodo provato, riprodotto qui per **patch in loco**: si scrive la nuova ramdisk
a offset 49115136 e si aggiornano i 4 byte di `ramdisk_size` (offset 12); **ogni
altro byte del file resta quello dell'origine allo stesso offset assoluto**.

Verifica indipendente del fatto che questo e' il metodo di probe6: probe6
differisce dall'origine **solo** in `{12,13} ∪ [49115136, 51316450)` — 0 byte
diversi nel kernel, 0 nel blocco da 4096 B dopo la ramdisk e 0 nella coda.
Il "blocco firma" quindi non viene riscritto: e' letteralmente invariato, e
`signature_size` resta 4096.

Un dettaglio da conoscere (non nascosto): in **v7 la ramdisk nuova e' piu' corta**
di quella originale, quindi restano 1.186.828 B della ramdisk Android di prima
fra la fine della nuova ramdisk e la fine della vecchia. Sono fuori dal
`ramdisk_size` dichiarato: il kernel non li legge mai. In v7b il residuo e' 0.
Se v7 fallisce e v7b no, questo residuo e' una delle differenze da eliminare per
prima (v7c = v7 con quella zona azzerata: una riga nella build).

## Verifiche (28/28, `verify-probe-v7.py`, strumenti diversi dalla build: bsdtar + `cpio -itv` + readelf + objdump)

| verifica | esito |
|---|---|
| sha256 delle immagini = manifest | PASS (entrambe) |
| lunghezza == 100.663.296 | PASS (entrambe) |
| header: magic ANDROID!, kernel 49108324, ramdisk == len, header 1584, v4, sig 4096 | PASS (entrambe) |
| area kernel byte-identica al contenitore (49.111.040 B, sha `f7ea707e…`) | PASS (entrambe) |
| coda identica agli stessi offset assoluti | PASS (50.534.005 B / 49.195.280 B) |
| **ogni** byte diverso dall'origine e' nel campo size o nella ramdisk | PASS (0 fuori) |
| ramdisk salvata = frame lz4 legacy (`02214c18`) e decomprime allo sha atteso | PASS (entrambe) |
| albero estratto == albero sorgente tranne `/init` (nomi, contenuti, tipi) | PASS (`content differs: ['init']`) |
| metadati di ogni voce (mode/nlink/uid/gid, anche i device node) identici al sorgente | PASS (59 e 29 voci) |
| `/init` nell'immagine == la sonda (sha, modo 0755 in v7 / 0750 in v7b) | PASS |
| il sorgente non contiene chiamate exit/_exit/abort (commenti rimossi) | PASS |
| `main()` **non puo' tornare**: nessuna istruzione `ret`, nessun ramo verso exit/abort, ultima istruzione ramo incondizionato | PASS (`b 40056c <main+0xb4>`, 137 istruzioni) |

Nota sul metodo: il file spedito e' compilato con `-s`, quindi per disassemblare
`main()` ricompilo lo stesso sorgente senza `-s` e verifico che `.text` sia
identico e che le differenze siano solo nell'header ELF/build-id/tabella sezioni
(0 byte diversi in ogni altra sezione allocata). La disassemblata descrive quindi
il binario spedito, non un altro.

## Prova d'esecuzione offline (`qemu-probe-v7.py`, 12/12) — kernel del telefono, ramdisk spedite

QEMU `-machine virt`, kernel estratto dall'immagine, `-initrd` = la ramdisk
spedita, `-no-reboot` (il riavvio guest fa uscire QEMU: esito osservabile).

```
[    0.615094][    T1] nx679j-sonda: init reached userspace as pid 1 attempt=1 uptime_ms=605   <- v7
[    0.615494][    T1] nx679j-sonda: marker channel /dev/kmsg attempt=1 uptime_ms=606
[    0.615699][    T1] nx679j-sonda: calling reboot(2) LINUX_REBOOT_CMD_RESTART attempt=1 uptime_ms=606
[    0.617437][    T1] reboot: Restarting system                                              <- il kernel si riavvia
```
e per v7b le stesse tre righe a `uptime_ms=603/605/605` poi `reboot: Restarting system`.
Nessun panic, nessun "Attempted to kill init", il tag `T1` prova che e' PID 1.
(Log: `qemu/boot-v7.log`, `qemu/boot-v7b.log`, `qemu-probe-v7.json`.)

Cosa **non** dice: niente su ABL/AVB, niente sul contatore A/B, niente su cosa fa
il bootloader di Qualcomm con un riavvio pulito.

## Procedura di flash e test

```bash
cd /home/user/nx679j-stock/experiments/20260916-122926-native-baseline/probe-v7-sonda
./test-probe-v7-slotb.sh v7b 240      # v7b per primo: contenuto di probe6, rischio minimo
./test-probe-v7-slotb.sh v7  240      # poi v7, se serve isolare il NOSTRO contenuto
```
Lo script (log in `test-slotb-<img>-<ts>/test.log`) fa, in quest'ordine:
1. legge i metadati A/B **prima** (dump read-only della GPT di `sde` e decodifica
   dei bit: 48-49 priority, 50 successful, 51-53 tries) — nessuna scrittura;
2. `adb push` su `/data/local/tmp` + `sha256sum` sul device: se non combacia, stop;
3. `dd if=… of=/dev/block/by-name/boot_b bs=1M && sync` + **readback sha256**:
   se non combacia, nessun riavvio e la partizione non e' stata toccata;
4. `fastboot getvar` di `current-slot`, `slot-successful:a/b`,
   `slot-unbootable:b`, `slot-retry-count:b/a`;
5. `fastboot set_active b` + `fastboot reboot`;
6. monitor USB 240 s (`monitor-boot.sh`) e attesa di Android per altri 120 s;
7. valutazione + (se Android e' tornato) lettura GPT "dopo" per il confronto del
   contatore, pstore, rawdump; alla fine ripristina lo slot A (lascia il telefono
   avviabile; `--no-restore` per non farlo).

### Cosa osservare (in ordine di forza)

1. **Il telefono parte da solo, ripetutamente**: logo RedMagic che si ripete
   ~ogni 10 s → la sonda gira. Su host USB **non si vede nulla** in nessuno dei
   due casi: la sonda non espone USB e ABL non espone USB in un boot normale
   (lo stesso "USB=none" dei test v5/v6). Il segnale primario e' lo schermo.
2. **Il telefono torna da solo ad Android entro ~2-3 minuti** → userspace
   raggiunto **e** contatore consumato fino al fallback (esito migliore).
3. **Prova del consumo**, come chiesto:
   `adb reboot bootloader && fastboot getvar slot-retry-count:b && fastboot getvar slot-unbootable:b`
   (atteso se il meccanismo funziona: retry < 7 e/o `slot-unbootable:b: yes`).
   In alternativa, senza riavviare, la stessa cosa si legge dalla GPT: lo script
   lo fa gia' nella fase 7 (`gpt-after.bin`).
4. **Logo fermo una volta sola, nessun riavvio, contatore intatto** → userspace
   NON raggiunto: il blocco e' a monte di `/init` (kernel/handoff), non nel
   contenuto della ramdisk. E' l'esito che distingue v7 da v7b.

### Matrice delle conclusioni

| v7 | v7b | conclusione |
|---|---|---|
| gira | gira | il kernel raggiunge lo userspace; il problema dei v5/v6 era l'init che non arrivava a compiere nulla |
| fermo | gira | il blocco e' nel **nostro contenuto** (47 moduli / device node / directory) |
| fermo | fermo | lo userspace non e' mai raggiunto: problema a monte (kernel/handoff), non nella ramdisk |

## NON verificato (da dire chiaramente)

1. **Che il contatore A/B si consumi su un riavvio pulito.** Non ho potuto
   provarlo sulla macchina, e i dati disponibili non bastano a chiudere la
   domanda in nessuna delle due direzioni:
   - i metadati A/B di questa unita' **non** stanno in `misc` (letto: niente
     `BLOC`/BCB nei primi 64 KiB) ma nei **bit di attributo GPT** di ogni
     partizione; la decodifica l'ho verificata contro i `fastboot getvar` reali
     (09-16 12:47 `current-slot:a` → boot_a priority=3 successful=1,
     boot_b priority=2 successful=0; oggi `current-slot:_b` → boot_b
     priority=3 successful=1, tries=7 in entrambi i casi);
   - tre letture da fastboot in momenti diversi del 2026-09-17 (02:48, 10:17,
     10:44) hanno dato **sempre** `slot-retry-count:b: 7` con
     `slot-successful:b: no`, anche subito dopo boot falliti di B. Attenzione
     pero': quei fallimenti **non** provocavano un riavvio (il kernel restava
     appeso), quindi e' possibile che ABL non sia mai stato rieseguito nel modo
     che fa scattare il decremento. La sonda e' esattamente l'esperimento che
     crea quel caso: la premessa non e' confutata, e' **non ancora testata**;
   - se il meccanismo non scatta, il loop **non si ferma da solo** e serve il
     recupero manuale (POWER ~15 s → POWER+VOL- → `fastboot set_active a`).
   Questo e' il rischio numero uno del test, non un dettaglio: oggi lo slot B e'
   `successful=1` con priority 3, e un ABL che decrementa solo gli slot non
   marcati come riusciti non decrementerebbe nulla.
2. Il comportamento di ABL/AVB verso una data immagine: qui l'unico dato reale
   resta probe6 (che ha avviato Android da B) e il controllo Magisk.
3. Nessuna esecuzione sulla macchina fisica: niente flash, niente test. Il
   telefono e' rimasto intatto (boot_b contiene ancora probe6,
   `06b52815…`, verificato ora; slot attivo `_b`).
4. La marcatura su `/dev/kmsg` arriva a `/sys/fs/pstore` solo se il ramo
   console di ramoops esiste: nei test precedenti pstore era vuoto, quindi il
   canale "leggi la marcatura dopo" e' best-effort, non una prova.

## Se il contatore non si consuma (prossimo passo da 1 riga)

In `candidate-init-sonda.c` c'e' un solo punto di chiamata: si puo' passare da
`LINUX_REBOOT_CMD_RESTART` a `LINUX_REBOOT_CMD_RESTART2` con argomento
`"bootloader"` (il kernel passa il reason al bootloader, e' cosi' che funziona
`reboot bootloader` su Android). Se ABL lo onora, il telefono entra in **fastboot
dopo il primo riavvio**: segnale visibile all'host e recupero immediato con
`fastboot set_active a`. E' l'unica variante che rende il test leggibile senza
guardare lo schermo; va provata solo se v7b gira e il contatore non si muove.

## File prodotti

```
probe-v7-sonda/
  candidate-init-sonda.c          sorgente della sonda
  build-probe-v7.py               build + autoverifica (build-probe-v7.log)
  boot_b-probe-v7.img             v7   (sha 77d7675e…)
  boot_b-probe-v7b.img            v7b  (sha 7424a599…)
  manifest.json                   hash, header, residui, toolchain
  verify-probe-v7.py              riverifica indipendente (VERIFY-probe-v7.log/.json, 28/28)
  qemu-probe-v7.py                prova d'esecuzione con il kernel del telefono (12/12)
  qemu/boot-v7.log, qemu/boot-v7b.log
  test-probe-v7-slotb.sh          procedura di flash/test (fa il readback prima di riavviare)
  test-gpt-decoder.py             prova a secco del decodificatore dei bit A/B della GPT
  work/                           init-sonda, ramdisk cpio/lz4, objdump di main()
  inputs/magisk-android-ramdisk.cpio   ramdisk Android di riferimento (sha 6fd26309…)
../probe-v7-devstate/             stato del device prima del test (GPT, misc) — letto, non scritto
```
