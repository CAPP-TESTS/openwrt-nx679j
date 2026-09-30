---
name: driver-protocol-recovery
description: Ricostruisce il modello di programmazione di un dispositivo a partire da un driver vendor, un blob binario, un firmware o tracce di bus — mappa dei registri MMIO, ABI ioctl, descrittori e sequenze USB, BAR PCI, transazioni I2C/SPI, anelli DMA, sequenze di init e reset. Usa questa skill quando serve capire "come si parla" a un dispositivo prima di scrivere un driver Linux, quando si analizza un driver Windows o Android per portarlo, o quando l'hardware non ha datasheet pubblico. Produce una specifica verificabile che il driver poi implementa.
version: 1.0.0
license: MIT
metadata:
  hermes:
    tags: [reverse-engineering, hardware, mmio, ioctl, usb, pci, dma, protocol]
---

# Driver protocol recovery

Questo è il passaggio che trasforma il reverse engineering in ingegneria: da "so cosa fa questo codice" a "so come si programma questo dispositivo".

Il prodotto non è una raccolta di annotazioni sul disassemblato. È una **specifica**: un documento che descrive il dispositivo abbastanza bene da poterci scrivere un driver senza riaprire il binario. Se la specifica è buona, chi scrive il driver non ha più bisogno del blob originale — ed è anche la forma corretta dal punto di vista legale, perché il driver implementa una specifica ricostruita, non ricalca il codice altrui.

## Fase -1 — Prior art: il protocollo esiste già altrove?

**Regola assoluta dell'utente (2026-09-20): mai più lavoro su cose che esistono già.** Un protocollo che stai per ricostruire è quasi sempre già implementato in qualche progetto open source: se esiste un'implementazione adattabile al target, il lavoro di RE scende da giorni a ore, e va cercata PRIMA di aprire il disassemblatore.

Dove cercare, sempre tutte e cinque:

1. **Progetti upstream del protocollo**: es. per QMI/MBIM: `libqmi`/`libmbim` (freedesktop) e `ModemManager`; per Qualcomm SoC: `linux-msm`, postmarketOS, i fork community (device page + pmaports).
2. **Package del target**: indici dei feed (apk/opkg). Attenzione alle alternative vicine: un tool integrato può non supportare il trasporto che serve mentre un altro sì (`uqmi` non parla QRTR; `libqmi`/`qmicli` sì — scoperto tardi e a caro prezzo).
3. **Sorgenti kernel del target e upstream**: il driver che il vendor ha scritto ha un equivalente upstream, e le API (netlink, ioctl, sysfs) sono documentate lì.
4. **Implementazioni vendor di altri prodotti**: es. l'IPACM di AOSP per la configurazione IPA, i port di altri SoC (meizu-m2172, XEC), le mailing list.
5. **Specifiche pubbliche**: le specifiche dei comandi (es. QMI) sono pubbliche — il protocollo non va inventato, va solo applicato al target.

**Output**: elenco FATTO/IPOTESI con URL e versione, e decisione: *adotto l'esistente* | *adatto (con N modifiche documentate)* | *scrivo custom perché X* (prova che X non esiste o non è utilizzabile sul target: kernel vendor, trasporto assente, ecc.). Un custom scritto senza questo passaggio è un difetto del processo.

**Caso reale (NX679J, 2026-09-20):** client QMI-over-QRTR custom scritto da zero mentre `qmicli`/`libqmi` col supporto QRTR era a un `apk add` di distanza (e ha funzionato al primo comando); parte dell'orchestrazione (DPM endpoint, WDS bind, link rmnet) duplicava ciò che fa `ModemManager` dal 1.18. Il risultato custom resta valido per la parte vendor-specifica (config IPA userspace, sequenze osservate) — ma il contesto andava verificato prima.

## Audit di un rilascio kernel/DT vendor

- Fissa il repository vendor e il commit immutabile prima di citare il codice; un branch `main` da solo non è una fonte riproducibile.
- Verifica separatamente la versione in `Makefile` e il build config del device: un config `NX679J` prova che il tree supporta quel target, non che corrisponda al kernel installato.
- Se i driver display sono distribuiti come ZIP nel repo, leggi l'archivio dal blob del commit (`git show <commit>:display-drivers.zip`) senza estrarlo nel worktree; cita URL del blob, membro ZIP e linee interne, perché GitHub non dà permalink di riga dentro un binario.
- Confronta il `Makefile` del rilascio con la versione runtime. Usa il tag upstream della stessa versione per le semantiche DRM core stabili, ma marca callback vendor più nuovi come evidenza di famiglia, non come prova byte-per-byte del kernel in uso.
- Per correlare un reset a una chiusura DRM, risali da `do_exit()` (`exit_files()` prima di `EXIT_ZOMBIE`) al `.release` reale, poi separa `preclose`, `master_drop`, `postclose` e `lastclose` (vincolato a `open_count == 0`). Controlla se il preclose invoca commit KMS; non equiparare la scomparsa da `drm_clients` all'assenza di commit già eseguiti. Traccia in un ramo distinto il full atomic modeset e il percorso runtime-PM/RSC/clock-parent; pinna repository e commit SHA, e mantieni separate le ipotesi di causalità.
- Per un aggiornamento piano DRM, segui i flag old/new `mode_changed`, `active_changed` e `connectors_changed` separatamente da `plane atomic_update/flush`; poi traccia il kickoff vendor, idle-PC/runtime-PM, e callback di clock-parent. Controlla anche i flag privati di seamless mode switch: un fork Qualcomm può avere un percorso `mode_set` specifico che non equivale al modeset DRM completo. Usa sorgenti di SoC/fork affine come evidenza di famiglia, mai come prova del target.
- Per ioctl DRM atomiche, separa il return ABI dalla coda del commit e dalla conferma di scanout: traccia `NONBLOCK`, il punto `swap_state()`, l'esecuzione del commit tail, le callback `void` (`atomic_flush`/kickoff) e la propagazione reale degli errori. Distingui `hw_done`, `flip_done`/eventi e `OUT_FENCE_PTR`; un `FB_ID` nello stato software dopo lo swap non prova quale buffer legge l'hardware. Nei fork vendor verifica per commit/versione se `.atomic_commit` usa l'helper upstream o una coda worker custom e se il percorso blocking attende il worker senza propagare gli errori; non trasferire questa semantica tra upstream e sorgenti di famiglia.
- Cerca separatamente DT/DTBO e `init*.rc`/ramdisk: il parser generico non determina il comando ESD o i valori del pannello senza il DT board-specifico, e il sorgente kernel non stabilisce l'ordine dei servizi se gli script non sono nel rilascio. Riporta lo scope della ricerca negativa e rimanda a FDT/boot image live.

## Il documento da produrre

```
# Specifica: <dispositivo>

## Identificazione
Vendor/device ID, bus, revisioni note, firmware richiesto

## Risorse
BAR / range MMIO / IRQ / clock / GPIO / regolatori

## Mappa dei registri
offset | nome | accesso | reset | campi (bit) | evidenza

## Sequenze
init, reset, start/stop, sospensione/ripresa, upload firmware
ogni passo con la sua evidenza

## Trasferimento dati
polling / IRQ / DMA — formato descrittori, allineamenti, coerenza cache

## Zone d'ombra
cosa resta ignoto e perché conta
```

La colonna **evidenza** non è burocrazia. Quando il driver a metà lavoro non aggancia l'interrupt, la prima domanda utile è "questo bit da dove viene?", e la risposta deve essere consultabile in due secondi.

## Da dove entrare

Il punto di partenza cambia in base a cosa hai in mano:

- **Driver vendor Linux `.ko`** — il caso più semplice: i simboli kernel dichiarano il bus, e `probe()` contiene la sequenza di init.
- **Driver Windows `.sys` / macOS kext** — la logica c'è tutta, ma tradotta in un'altra API. Cerca gli handler IOCTL e le funzioni di accesso al bus (`READ_REGISTER_ULONG`, `WdfUsbTargetDeviceFormatRequestForControlTransfer`).
- **Libreria userspace + device generico** — spesso il caso migliore: il "driver" è in userspace su `libusb` o su un chardev, e le sequenze sono leggibili senza kernel debugging.
- **Solo firmware del dispositivo** — l'altro lato del dialogo: il firmware mostra cosa il dispositivo *si aspetta* di ricevere.
- **Hardware in mano e nessun codice** — resta l'osservazione diretta del bus: `usbmon`, analizzatore logico, `i2c-tools`. Più lento, ma i fatti sono di prima mano.

## Registri MMIO

Il pattern da riconoscere: nel codice vendor le funzioni di accesso sono wrapper sottili attorno a una base più un offset costante.

```c
/* forma tipica nel codice decompilato */
writel(value, dev->base + 0x40);
tmp = readl(dev->base + 0x44);
```

Nel disassemblato ARM/x86 appaiono come load/store con offset immediato rispetto a un registro che è stato inizializzato da `ioremap`. Il metodo:

1. Trova la funzione che chiama `ioremap` / `pci_iomap` / `MmMapIoSpace`: da lì nasce la base.
2. Segui il puntatore base e raccogli tutti gli accessi con il loro offset costante.
3. Per ogni offset, annota: si legge, si scrive, o entrambi? Con quali valori? In quale ordine rispetto agli altri?
4. Rinomina appena hai un'ipotesi (`REG_CTRL`, `REG_STATUS`, `REG_INT_MASK`): un nome sbagliato ma esplicito è più utile di `0x44`, perché rende falsificabile l'ipotesi.

I significati emergono dal comportamento, non dai nomi:

- Un registro **scritto per primo, con un bit alzato e poi abbassato dopo un delay** è quasi certamente un reset.
- Un registro **letto in un loop con maschera e timeout** è uno status con un bit "ready" o "busy".
- Un registro **scritto con una maschera all'avvio e letto nell'handler di interrupt** è la coppia mask/pending degli interrupt.
- Una **coppia di registri a 32 bit scritti in sequenza** con valori che sembrano indirizzi è quasi sempre un indirizzo DMA a 64 bit (parte bassa e parte alta).

Gli **ordini e i delay contano**: `writel` consecutive con `udelay()` in mezzo indicano requisiti temporali reali dell'hardware, non pigrizia del programmatore. Vanno nella specifica come vincoli, con il valore del delay.

Le **barriere** vanno annotate: in Linux `writel`/`readl` includono già ordering rispetto alla memoria, ma `writel_relaxed` no. Se il vendor usa esplicitamente barriere o versioni relaxed, il dispositivo ha requisiti di ordering che il tuo driver deve rispettare.

## ABI ioctl

Il punto d'ingresso è la struttura `file_operations`, in cui `unlocked_ioctl` punta all'handler. La forma è sempre uno switch sul comando.

Da estrarre per ogni comando:

- Il **numero**, e la sua decomposizione: `_IOC_DIR`, `_IOC_TYPE`, `_IOC_NR`, `_IOC_SIZE`. La dimensione codificata nel comando rivela la dimensione della struttura anche senza vederne la definizione.
- La **struttura** dell'argomento, ricostruita dagli offset usati dopo `copy_from_user`. Attenzione al padding: un campo a offset 8 dopo un `u32` a offset 0 implica un `u64` allineato, non tre byte mancanti.
- La **validazione** applicata, che dice quali valori sono legali.
- Il **flusso**: quali registri tocca, quale sequenza scatena.

Ricostruisci la struttura in C con offset espliciti in commento e verificala con `pahole` o con `_Static_assert(sizeof(struct x) == N)` — la dimensione totale deve corrispondere a `_IOC_SIZE`. È un controllo di coerenza che smaschera subito un layout sbagliato.

## USB

Il protocollo è più leggibile che altrove, perché i descrittori sono autodescrittivi e il traffico è catturabile.

Con il dispositivo presente:

```bash
lsusb -v -d VID:PID                    # descrittori completi: interfacce, endpoint, classi
cat /sys/kernel/debug/usb/devices      # vista alternativa
```

Cattura del traffico (richiede privilegi, quindi va proposta all'utente):

```bash
modprobe usbmon
# poi cattura del bus corrispondente, es. con tcpdump su usbmon<N> o Wireshark
```

Da ricostruire:

- **Control transfer**: `bmRequestType`, `bRequest`, `wValue`, `wIndex`, `wLength`. Le richieste vendor-specific (`bmRequestType & 0x40`) sono il vero protocollo del dispositivo.
- **Endpoint**: indirizzo, tipo (bulk/interrupt/isoc), `wMaxPacketSize`, intervallo di polling.
- **Sequenza di init**: cosa manda il driver prima che il dispositivo diventi utilizzabile — spesso include upload di firmware, poi un reset, poi la ri-enumerazione con VID/PID diversi.
- **Formato dei pacchetti dati**: header, lunghezza, checksum, numeri di sequenza.

Il segnale della ri-enumerazione è tipico: il dispositivo appare con un ID, riceve un blob, sparisce e riappare con un altro ID. Un driver che ignora questa doppia identità non funzionerà mai.

## PCI/PCIe

```bash
lspci -nnvvv -s <slot>                 # BAR, capabilities, IRQ, link
lspci -xxx -s <slot>                   # config space grezzo
```

Da mappare: quali BAR sono usate e per cosa (registri vs memoria di frame vs coda), il tipo di interrupt (legacy, MSI, MSI-X e quanti vettori), le capability rilevanti (power management, AER), e le finestre di config space vendor-specific.

## DMA

È la parte in cui gli errori diventano corruzione di memoria, quindi va ricostruita con precisione:

- **Descrittori**: layout esatto della struttura, dimensione, allineamento richiesto, se formano un anello o una lista.
- **Ownership**: quale bit segnala che il descrittore appartiene al dispositivo e quale al driver. È il cuore del protocollo.
- **Indici**: registri di head e tail, e chi li aggiorna.
- **Coerenza**: il vendor usa buffer coerenti (allocazione una tantum) o streaming (map/unmap per transazione)? Determina l'API Linux corretta: `dma_alloc_coherent()` contro `dma_map_single()` con `dma_sync_*`.
- **Maschera degli indirizzi**: 32 o 64 bit. Sbagliarla su un sistema con molta RAM produce fallimenti che si manifestano solo quando l'allocazione capita sopra i 4 GB.

## Verifica prima di scrivere il driver

Una specifica non verificata è un'ipotesi lunga. Prima di passare all'implementazione, chiudi almeno questi punti:

1. **La somma torna?** Ogni registro nella mappa ha almeno un accesso osservato. Ogni passo di init ha una fonte.
2. **Il modello spiega l'errore?** Se il vendor gestisce un caso di errore, la tua specifica deve poter spiegare quando quel caso si verifica.
3. **Prova a costo zero, se possibile.** Se il dispositivo è presente e il bus è accessibile da userspace (I2C con `i2ctransfer`, USB con `libusb`, MMIO in sola lettura via `devmem` su una macchina di test), esegui una parte della sequenza fuori dal kernel. Un errore scoperto qui costa un secondo; lo stesso errore dentro il kernel costa un reboot.
4. **Zone d'ombra dichiarate.** Ciò che resta ignoto va scritto: nel driver diventerà un commento `/* TODO: significato non determinato, valore preso dalla sequenza vendor */`, che è onesto e utile a chi legge dopo.

## Il passo successivo

Con la specifica in mano, `linux-driver-authoring` la trasforma in codice. La specifica resta il documento di riferimento: se durante l'implementazione emerge una contraddizione, si aggiorna la specifica, non si aggiusta il codice a tentativi.
