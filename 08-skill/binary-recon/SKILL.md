---
name: binary-recon
description: Triage iniziale di un artefatto binario sconosciuto — eseguibile, libreria, modulo kernel .ko, immagine firmware, dump di flash o blob vendor. Usa questa skill ogni volta che l'utente presenta un file binario da capire, chiede "cosa fa questo binario", fornisce un .ko o un firmware senza sorgenti, o inizia una sessione di reverse engineering, anche se non usa la parola "reverse". Produce una scheda dei fatti con architettura, layout, simboli, stringhe e punti di ingresso, prima di qualunque analisi approfondita.
version: 1.0.0
license: MIT
metadata:
  hermes:
    tags: [reverse-engineering, binary, firmware, kernel-module, triage]
---

# Binary recon

Primo contatto con un artefatto opaco. L'obiettivo non è capire tutto: è costruire in pochi minuti una base di fatti verificati che renda sensata ogni analisi successiva.

Il motivo per cui questo passo esiste separato è che l'errore più costoso in RE si commette nei primi cinque minuti: disassemblare con l'architettura sbagliata, analizzare lo stub UPX invece del payload, o studiare la partizione di recovery invece di quella applicativa. Ore di lavoro perfettamente coerente su un oggetto sbagliato.

## Fase -1 — Prior art: cerca l'esistente PRIMA di scrivere qualunque cosa

**Regola assoluta dell'utente (2026-09-20, lezione pagata sul campo): mai più lavoro su cose che esistono già.** Prima di progettare o scrivere un tool, un driver, uno script o una sequenza, la ricerca di ciò che esiste già è un passo obbligatorio, non un'opzione. "Non l'ho trovato" non è una conclusione: è un inizio di ricerca incompleta.

Copri almeno queste cinque fonti, e mettici il tempo che serve:

1. **Il progetto upstream naturale del pezzo.** Per Qualcomm: `linux-msm` e postmarketOS (pmaports, wiki devices). Per il networking/modem Linux: i progetti freedesktop (libqmi, ModemManager).
2. **I package del sistema target** — non solo il nome ovvio: cerca i vicini e le alternative negli indici dei feed (apk/opkg). Un daemon assente dai feed è un lavoro di packaging legittimo; un daemon presente va usato, non reimplementato.
3. **I sorgenti del kernel** del target e upstream (`drivers/...`): per modem `drivers/net/ipa`, `net/qrtr`, `rmnet`.
4. **Community e port esistenti**: device page pmOS, fork GitHub, wiki, issue tracker, mailing list.
5. **Le implementazioni "ovvie" vicine**, anche contro l'intuizione. In OpenWrt: `uqmi` NON parla QRTR, ma `libqmi`/`qmicli` sì — e la scoperta è arrivata solo dopo aver scritto un client QMI custom completo. Testa l'alternativa vicina PRIMA di concludere che il custom serve.

**Output della fase = elenco FATTO/IPOTESI** di cosa esiste (con URL + versione) e una decisione motivata: *usare l'esistente* | *usare con N adattamenti* | *scrivere custom perché X* (con la prova che X è assente o inutilizzabile sul target). Custom senza questa fase = difetto, non scelta.

**Caso reale (NX679J, SDX65/SM8450):** `rmtfs`/`tqftpserv`/`pd-mapper` esistevano upstream ma non impacchettati in OpenWrt (compilarli è stato legittimo — ma la verifica andava fatta prima); `qmicli` con QRTR esisteva, funzionava e ha parlato col modem al primo colpo — scoperto DOPO aver scritto un client custom; ModemManager 1.24 col plugin qcom-soc era già nel feed. Costo dell'omissione: giorni di lavoro duplicato sul layer di orchestrazione.

## Scorciatoia

Per le fasi 0-2 esiste uno script che le esegue tutte e produce direttamente la scheda dei fatti:

```bash
scripts/triage.sh <file>
```

Riconosce ELF, moduli `.ko` e contenitori firmware, decodifica gli alias hardware e deduce i sottosistemi kernel usati dai simboli non definiti. Solo lettura: non esegue il target.

Leggi il resto di questa skill quando lo script segnala qualcosa che merita approfondimento, o quando l'artefatto non rientra nei casi che copre.

## Output atteso

Una **scheda dei fatti** in questo formato, che diventa il punto di riferimento per il resto del lavoro:

```
## Artefatto
path, dimensione, sha256

## Identificazione
tipo, architettura, endianness, bitness, ABI
strip/simboli, statico/dinamico, PIE, packer

## Layout
sezioni o partizioni rilevanti, con offset e dimensioni

## Superficie
simboli esportati/importati, stringhe significative, tabelle di funzioni

## Ipotesi aperte
ognuna con l'esperimento che la verifica
```

Ogni riga deve poter essere ricondotta a un comando. Se una voce non ha una fonte, appartiene alle ipotesi, non ai fatti.

## Fase 0 — Identificazione

Prima di ogni altra cosa, stabilisci *cosa* stai guardando:

```bash
file <target>
sha256sum <target>
stat -c '%s bytes' <target>
```

Se `file` dice ELF, il quadro completo è in due comandi:

```bash
readelf -h <target>     # architettura, endianness, tipo (EXEC/DYN/REL), entry point
readelf -SW <target>    # sezioni con indirizzi e dimensioni
```

Punti di attenzione:

- `Type: REL` con sezione `.modinfo` significa modulo kernel: vai alla sezione dedicata più sotto.
- `Type: DYN` può essere sia libreria condivisa sia eseguibile PIE. Distingui con la presenza di `DT_SONAME`.
- Un'entropia alta e sezioni assenti indicano packing o cifratura. Verifica: `binwalk -E <target>`.
- Se `file` dice "data" e non ELF, non è necessariamente firmware raw: potrebbe essere un container. Vai alla fase firmware.

L'architettura letta qui vincola tutto il resto. Se la sbagli, ogni disassemblato successivo sarà plausibile e falso.

## Fase 1 — Superficie

Simboli e stringhe sono la mappa più economica che esista.

```bash
nm -C --defined-only <target> 2>/dev/null | head -50    # cosa espone
nm -uC <target> 2>/dev/null | head -50                  # cosa richiede
strings -n 8 -t x <target> | head -100                  # con offset, servono dopo
```

Cosa stai cercando davvero:

- **Import** che rivelano le capacità: `ioctl`, `mmap`, `open("/dev/...")`, `libusb_*`, `pthread_*`, syscall di rete.
- **Path e nomi di device**: `/dev/`, `/sys/`, `/proc/`, nomi di file firmware.
- **Stringhe di formato e messaggi di errore**: sono il modo più rapido per capire l'intento del codice, perché il programmatore le ha scritte per sé stesso.
- **Versioni e nomi di chip**: spesso identificano il datasheet da cercare, e con quello il lavoro cambia scala.

Se il binario è strippato, `nm` non produce nulla: passa alle stringhe e alla tabella delle relocation (`readelf -rW`).

## Fase 2 — Il caso specifico: modulo kernel

Un `.ko` è un ELF relocatable con metadati che raccontano molto prima di qualsiasi disassemblato:

```bash
modinfo <target>.ko                                  # licenza, autore, alias, parametri, vermagic
readelf -p .modinfo <target>.ko                      # gli stessi metadati grezzi
readelf -SW <target>.ko | grep -E 'init|exit|text|data'
nm <target>.ko | grep -E ' (T|t) '                   # funzioni definite
nm -u <target>.ko                                    # simboli kernel usati: la firma più informativa
```

Gli **alias** sono la scoperta più preziosa: dicono a quale hardware il modulo si lega.

- `pci:v0000XXXXd0000YYYY...` → vendor e device ID PCI.
- `usb:vXXXXpYYYY...` → vendor e product ID USB.
- `of:N*T*Cvendor,device` → compatible string del Device Tree.
- `acpi*:XXXX` → identificativo ACPI.

I **simboli non definiti** classificano il driver senza leggere una riga di codice. `usb_register_driver` significa USB core; `spi_register_driver` significa SPI; `devm_regmap_init_i2c` significa I2C con regmap; `netdev_alloc_skb` significa driver di rete; `iio_device_register` significa sensore.

`vermagic` dice per quale kernel è stato compilato: se non combacia con quello locale, il modulo non caricherà — informazione da annotare subito, non da scoprire più tardi.

## Fase 3 — Il caso specifico: immagine firmware

Un'immagine firmware è quasi sempre un contenitore, non un blob monolitico.

```bash
binwalk <image>                       # mappa delle firme: header, filesystem, kernel, compressione
binwalk -E <image>                    # entropia: le zone piatte ad alta entropia sono cifrate o compresse
```

Dall'estrazione:

```bash
binwalk -e --run-as=$(whoami) -C /tmp/fw-extract <image>
```

Due avvertenze concrete: l'estrazione va sempre in una directory dedicata sotto `/tmp`, perché produce molti file; e il contenuto va trattato come non fidato — nessuno script estratto va eseguito, i filesystem si ispezionano in sola lettura.

Struttura tipica da riconoscere: header vendor con checksum, un bootloader, uno o più kernel, un rootfs (SquashFS, JFFS2, UBIFS, CramFS), e una partizione di configurazione. Il codice interessante di solito sta nel rootfs, nei binari sotto `/usr/sbin` o `/usr/bin`, e nei moduli `.ko` sotto `/lib/modules`.

Se l'immagine è per un microcontrollore senza filesystem — `file` dice "data", `binwalk` non trova firme, l'entropia è uniforme — allora è codice raw. Serve determinare architettura e base address prima di disassemblare: cerca la vector table (su ARM Cortex-M la prima word è lo stack pointer iniziale, la seconda il reset handler; entrambe puntano a indirizzi coerenti con la base di flash) e verifica che le stringhe abbiano offset coerenti con quella base.

## Fase 4 — Disassemblato mirato

Solo ora, e solo su punti specifici identificati nelle fasi precedenti.

```bash
objdump -d --start-address=0xADDR --stop-address=0xADDR2 <target>
r2 -A <target>          # afll per la lista funzioni, pdf @ sym.name per il disassemblato
```

Non disassemblare tutto per leggerlo tutto: parti dai punti d'ingresso (`init_module`, `probe`, l'entry point ELF, gli handler di ioctl) e segui solo i percorsi che rispondono alla domanda che ti stai ponendo.

Se `radare2` sbaglia l'architettura su un blob raw, impostala esplicitamente:

```bash
r2 -a arm -b 32 -m 0x08000000 <blob>
```

## Cosa NON fare in questa fase

- Non eseguire il binario. Il triage è statico. L'esecuzione, se serve, è una decisione separata e va presa in un ambiente isolato.
- Non montare filesystem estratti in scrittura.
- Non passare oltre finché architettura ed endianness non sono certe: sono le fondamenta di tutto.

## Readback Android e acquisizione binaria

Per backup via Android/Magisk, segui [references/android-readback.md](references/android-readback.md): quoting a due shell, separazione stdout/stderr, dimensioni reali dei block device e verifica dei digest remoti.

## Il passo successivo

Con la scheda dei fatti in mano, la domanda successiva determina la direzione:

- Serve capire come si programma il dispositivo? → `driver-protocol-recovery`.
- Serve già scrivere il driver perché il modello è chiaro? → `linux-driver-authoring`.
- L'artefatto era solo da identificare? → il lavoro è finito, consegna la scheda.
- L'artefatto non è un binario (APK, PCAP, bytecode) o serve scegliere il toolchain e il percorso a fasi? → `re-toolchain-routing`.
