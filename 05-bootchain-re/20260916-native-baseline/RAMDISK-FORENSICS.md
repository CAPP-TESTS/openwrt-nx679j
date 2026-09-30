# Analisi forense delle ramdisk — NX679J / SM8450

Data: 2026-09-17. Metodo: parsing byte-level dei contenitori e degli archivi, confronto campo per campo, **piu' lettura del sorgente del kernel 5.10 (msm-kernel) e del `/proc/config.gz` del kernel realmente in esecuzione**. Nessuna regola di accettazione e' dedotta: e' letta.

## 1. Livello contenitore (byte)

| | Android (parte, 2200983 B) | nostra v5 (1147147 B) | nostra v6 (901952 B) |
|---|---|---|---|
| magia | `02214c18` = **LZ4 legacy** | `1f8b0800` = **gzip/deflate** | `1f8b0800` = **gzip/deflate** |
| blocchi | 1 blocco, COMPRESSED | flusso deflate | flusso deflate |
| consumo | 2200983 B, **residuo 0** | tutto consumato, residuo 0 | tutto consumato, residuo 0 |
| decompresso | 2306300 B | 3598688 B | 3237608 B |

Nota: `lz4 -l -9` produce LZ4 legacy con blocchi **compressi**; quello Android e' un singolo blocco compresso. Entrambi i formati sono supportati (vedi §3). La variante `lz4 -9` (senza `-l`) produce invece il **formato frame** (`04224d18`), che **non** e' quello usato dal device: `vendor_ramdisk00` e' anch'esso legacy `02214c18`.

## 2. Livello archivio cpio (campo per campo)

| | Android | nostra v5 |
|---|---|---|
| archivi cpio | **1** (nessun concatenato) | **1** |
| voci totali | **30** | **86** |
| residuo dopo il TRAILER | **0 B** | **0 B** |
| `ino` | 300000..300029 (tutti distinti, **mai 0**) | **0**..85 (0 sulla prima voce) |
| `nlink` | tutti **1** | tutti **1** |
| `mtime` | tutti 0 | tutti 0 |
| campo `check` | 0 su tutte | 0 su tutte |
| devmajor/devminor | 0 | 0 |
| tipi usati | dir `0o40000`, file `0o100000`, TRAILER `0o0` | dir, file, **symlink `0o120000`**, **chardev `0o20000`**, TRAILER |
| padding dei NOMI | tutti nulli | tutti nulli |
| padding dei DATI | tutti nulli | tutti nulli |
| anomalie | **nessuna** | 1: `ino=0` sulla prima voce (innocua con `nlink=1`) |

Voci comuni (le sole 5): `dev`, `init`, `proc`, `sys`, `TRAILER!!!`. Differenze campo per campo:
- `init`: Android `mode 0o100750`, size 263928, sha `8e26e33c…` (ELF statico AArch64); nostra `mode 0o100755`, size 10507, sha `4ebac295…` (script `#!/bin/sh`);
- `TRAILER!!!`: Android `mode 0o755 ino 300029`; nostra `mode 0 ino 0` (**entrambe accettate**: il kernel confronta solo il nome, riga 328);
- `dev`, `proc`, `sys`: identiche salvo `ino`.

**Conclusione del livello archivio:** nessuna violazione della specifica newc, nessun residuo, nessun archivio concatenato, nessun hardlink (`nlink=1` ovunque), padding corretto, `check=0`.

## 3. Il kernel reale: configurazione (letta dal device, `/proc/config.gz`)

```
CONFIG_RD_GZIP=y            CONFIG_RD_LZ4=y          CONFIG_RD_ZSTD=y
CONFIG_BLK_DEV_INITRD=y     CONFIG_INITRAMFS_SOURCE=""
CONFIG_BINFMT_SCRIPT=y      CONFIG_BINFMT_ELF=y
CONFIG_PANIC_TIMEOUT=-1     CONFIG_PANIC_ON_OOPS=y
CONFIG_ANDROID=y            CONFIG_ANDROID_BINDERFS=y
(nessuna riga CONFIG_DEVTMPFS -> non impostato)
```

Conseguenze dirette, che **falsificano per config** alcune ipotesi:
1. **gzip e lz4 sono entrambi supportati**: la compressione della ramdisk non puo' essere la causa, ne' per v5/v6 (gzip) ne' per probe3 (lz4 legacy). Ipotesi chiusa alla radice.
2. **`CONFIG_BINFMT_SCRIPT=y`**: il kernel puo' eseguire uno script `#!/bin/sh` come init. L'ipotesi "lo script non e' eseguibile" e' falsificata anche per config.
3. **`CONFIG_PANIC_TIMEOUT=-1`**: per default **un panic NON fa riavviare il kernel, resta appeso per sempre**. Da qui due cose: (a) un panic e' indistinguibile da un hang per l'host; (b) **la mia deduzione precedente "il contatore A/B non si e' mosso, quindi il kernel non e' andato in panic" NON e' valida** e va ritirata. Vale solo per le immagini con `panic=10` nella cmdline (v4), non per probe5/probe6 (cmdline vuota => `panic_timeout=-1`).
4. **Nessun `CONFIG_DEVTMPFS`**: non c'e' devtmpfs, quindi i nodi in `/dev` possono arrivare solo dalla ramdisk (o essere creati dall'init di turno). Android non ne mette nessuno nel ramdisk; noi si.

## 4. Il kernel reale: regole di accettazione (msm-kernel 5.10, `init/initramfs.c`)

Lette nel codice:
| regola | riga | effetto sulla nostra ramdisk |
|---|---|---|
| richiede la magia `070701` | 251 `memcmp(collected,"070701",6)` -> `error("no cpio magic")` | ok (entrambe) |
| allineamento: `next_header = this_header + N_ALIGN(name_len) + body_len`, poi a 4 byte | 256-257 | ok |
| `name_len` in (0, PATH_MAX] | 259 | ok |
| **hardlink** attivato solo se `nlink >= 2` (`maybe_link`) | 309-312 | ok: `nlink=1` su tutte le 86 voci |
| **nodi device** (`S_ISBLK/S_ISCHR/S_ISFIFO/S_ISSOCK`) -> `init_mknod` | 355-361 | **supportati**: i nostri 6 chardev sono legittimi |
| **symlink** -> `init_symlink` | 391-395 | **supportati**: i nostri ~20 symlink sono legittimi |
| dati non-header dopo il trailer -> `error("junk within compressed archive")` | 442 | ok: residuo 0 in entrambe |
| init di default | `main.c:163` `ramdisk_execute_command = "/init"` | ok: entrambe hanno `/init` |
| `clean_path()` sui percorsi | 332, 394 | ok: percorsi relativi semplici |

**Nessuna regola del parser e' violata dalla nostra ramdisk.**

## 5. Cosa resta in piedi (e cosa e' caduto)

| Ipotesi | Esito | Fonte |
|---|---|---|
| firma AVB / ABL | falsificata | Magisk su slot B avvia |
| `signature_size=0` | falsificata | probe2 con 4096 fallisce |
| cmdline nostra | falsificata | probe1 (Magisk + nostra cmdline) avvia |
| dimensione ramdisk | falsificata | v5 da 1,09 MiB fallisce |
| formato compressione (gzip) | falsificata **due volte**: empiricamente (probe3 lz4) e per config (`RD_GZIP=y`) | probe3 + `/proc/config.gz` |
| script `/init` non eseguibile | falsificata **due volte**: QEMU lo esegue, `BINFMT_SCRIPT=y` | QEMU + config |
| uscita di PID 1 | non testata in modo pulito; v6 non esce mai e fallisce | v6 |
| catena moduli / softdep | corretta in v6, fallisce ugualmente | v6 |
| contenitore/padding/coda AVB | falsificata | probe5 |
| **imballaggio** (cpio+lz4+contenitore) | **falsificata: e' sano** | **probe6 avvia Android** |
| nodi `/dev` e symlink nella ramdisk | falsificati come causa | sorgente 355-395 |
| "il contatore A/B prova che non c'e' panic" | **ritirata** (PANIC_TIMEOUT=-1) | config |
| **il contenuto della ramdisk** | **unica variabile rimasta** | probe2 vs probe6, stesso contenitore |

## 6. Stato della domanda fondamentale

Tutto cio' che il kernel *valuta* della nostra ramdisk e' conforme. Restano due possibilita', e solo una sonda le distingue:
1. il kernel **non arriva** a eseguire `/init` (a monte del parser: handoff ABL->kernel, DTB/DTBO, o il kernel si ferma prima);
2. il kernel **esegue** `/init` e il nostro init si blocca **senza lasciare traccia** — il che e' possibile perche' l'unico canale di evidenza (USF/rawdump, USB) dipende proprio dai moduli che l'init deve caricare.

Le sonde v7 (nostra ramdisk + init che riavvia) e v7b (contenuto Android + init che riavvia) decidono quale delle due, **senza bisogno di alcun canale dipendente da UFS o USB**.
