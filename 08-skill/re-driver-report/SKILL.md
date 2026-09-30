---
name: re-driver-report
description: Produce il report finale di un lavoro di reverse engineering o di sviluppo driver — riassunto esecutivo, tabella delle evidenze che lega ogni valore del codice alla sua fonte, stato di verifica, zone d'ombra e lavoro residuo. Usa questa skill quando l'utente chiede di documentare, riassumere o consegnare i risultati di un'analisi binaria o di un driver, quando serve una specifica hardware condivisibile con altri, o alla chiusura di una sessione di analisi che ha prodotto conclusioni da conservare.
version: 1.0.0
license: MIT
metadata:
  hermes:
    tags: [reporting, documentation, reverse-engineering, driver]
---

# Report RE e driver

Il valore di un lavoro di reverse engineering svanisce se resta nella testa di chi lo ha fatto. Sei mesi dopo, davanti al proprio stesso driver, nessuno ricorda perché in `probe()` c'è un `udelay(50)` — e senza quella risposta nessuno osa toccarlo.

Il report esiste per rendere il lavoro modificabile da altri, e da sé stessi nel futuro.

## Struttura

```markdown
# <dispositivo o artefatto> — analisi e driver

## Sintesi
Cosa è il dispositivo, cosa è stato ottenuto, cosa funziona oggi.
Cinque righe. Chi legge solo questo deve sapere se il lavoro gli serve.

## Ambito e metodo
Artefatti analizzati, con hash. Strumenti usati. Cosa NON è stato esaminato.

## Prior art — cosa esisteva già
La ricerca dell'esistente va fatta PRIMA di scrivere codice (regola assoluta dell'utente), e va tracciata qui perché il report serva anche a chi verrà dopo. Per ogni componente custom: le fonti cercate (upstream, package dei feed del target, progetti community, fork), cosa è stato trovato con URL/versione, e l'esito: *adottato* | *adattato (modifiche documentate)* | *scritto custom perché X* (con la prova dell'assenza). Elenca anche ciò che si è scelto di NON usare e perché.
Un prior art onesto in questo report è ciò che permette a chi legge di capire, sei mesi dopo, se una parte del lavoro custom si può oggi dismettere in favore di qualcosa nel frattempo maturato.

## Il dispositivo
Modello di programmazione ricostruito: bus, risorse, registri, sequenze.
Rimanda alla specifica completa se è un documento separato.

## Il driver
Sottosistema scelto e perché. Struttura. Interfaccia esposta a userspace.
Scelte di progetto non ovvie, con la loro motivazione.

## Evidenze
La tabella che lega ogni valore non ovvio alla sua fonte.

## Fonti archiviate e interfacce debugfs

Quando il repository distribuisce il driver dentro un archivio, ancora la citazione al commit immutabile e allo SHA dell'oggetto archivio; riporta il member path esatto e i numeri di riga interni. Non inventare permalink GitHub per file che la UI non espone singolarmente. Per gli attributi debugfs/sysfs, traccia in ordine creazione, permessi, `file_operations`, callback di lettura/scrittura, offset `ppos`, byte copiati e byte restituiti, newline, parsing esatto e percorsi di errore/feature disabilitata: la sola presenza di `.read` non descrive il contenuto osservabile. Separa sempre la versione dichiarata dal `Makefile` sorgente dall'identità di un binario live, che richiede provenienza indipendente.

## Verifica
Cosa è stato testato, come, con quale esito. E cosa non è stato testato.

## Zone d'ombra
Ciò che resta ignoto, con l'impatto pratico di ogni lacuna.

## Lavoro residuo
Ordinato per priorità, non per comodità.
```

## La tabella delle evidenze

È la parte che distingue un report utile da un riassunto. Ogni costante, offset, maschera, timeout e sequenza che compare nel codice deve avere una riga:

```markdown
| Elemento nel codice | Valore | Fonte | Confidenza |
|---|---|---|---|
| `MYDEV_REG_CTRL` | `0x00` | `vendor.ko` `mydev_init+0x24`, `writel` su base+0 | certa |
| `CTRL_RESET` | `BIT(31)` | stessa funzione: set, `udelay(50)`, clear | certa |
| ritardo post-reset | `50 µs` | `udelay(50)` a `mydev_init+0x3c` | certa |
| `REG_STATUS` bit 0 | ready | loop di polling con timeout 100 ms a `+0x58` | alta |
| `REG_UNKNOWN` | `0x2c = 0x0f` | scritto una volta in init, mai riletto | ignota |
```

La colonna **confidenza** è quella che fa lavorare bene chi verrà dopo:

- **certa** — osservata direttamente e senza interpretazione.
- **alta** — dedotta dal contesto, coerente con tutto il resto.
- **ipotesi** — plausibile ma non verificata; il report deve dire come si verificherebbe.
- **ignota** — il valore serve perché senza il dispositivo non funziona, ma il significato non è stato determinato. Dichiararlo è più utile che inventare un nome.

Le righe "ignota" non sono un fallimento del lavoro: sono la mappa di dove guardare quando qualcosa si romperà.

## Onestà sullo stato di verifica

La sezione più facile da annacquare, e quella su cui il report viene giudicato. Distingui in modo netto:

- **Verificato** — c'è un test riproducibile che lo dimostra. Scrivi quale.
- **Osservato** — funziona sull'hardware provato, senza un test formale.
- **Non testato** — implementato secondo la specifica ma mai eseguito su quel percorso.
- **Noto non funzionante** — con la ragione, se è nota.

Un percorso "non testato" dichiarato vale molto più di un "funziona" generico: dice esattamente dove cercare al primo problema.

## Nota legale

Nei report destinati a uscire dalla propria macchina, una riga sul contesto è opportuna:

> Analisi svolta ai fini di interoperabilità, per lo sviluppo di un driver libero per hardware legittimamente posseduto. Il driver è una reimplementazione a partire dalla specifica ricostruita e documentata in questo report; non contiene codice derivato dal software analizzato.

Deve essere vera. Se durante l'implementazione hai copiato struttura e ordine di funzioni dal disassemblato, quella frase non descrive il tuo lavoro — e la cosa da correggere è il codice, non il report.

## Stile

- Ogni affermazione tecnica porta la sua evidenza: indirizzo, comando, riga di log.
- Niente aggettivi al posto dei numeri: "veloce" non significa nulla, "latenza 200 µs misurata con ftrace" sì.
- Le tabelle vincono sui paragrafi, per i dati.
- Lunghezza proporzionata al lavoro. Un'analisi di due ore non produce venti pagine, e un driver da tremila righe non si riassume in mezza pagina.
- Il lettore immaginario è un collega competente che non ha seguito il lavoro: sa cos'è un `probe()`, non sa nulla di questo dispositivo.

## Vedi anche

- Catena di evidenza strutturata (E→F→Path, soglie di validazione, gate di scope) → skill `re-toolchain-routing`, reference `evidence-and-scope.md`.
