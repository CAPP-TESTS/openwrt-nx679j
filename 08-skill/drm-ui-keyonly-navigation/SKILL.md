---
name: drm-ui-keyonly-navigation
description: Use when a touchless C UI must be driven by keys only.
version: 1.0.0
author: Hermes Agent
license: MIT
metadata:
  hermes:
    tags: [ui, drm, kms, navigation, accessibility, harness, c]
---

# Navigazione a soli tasti per una UI C su DRM (touch morto)

## When to Use (quando usarla)

Una UI C single-thread che disegna su dumb buffer con ioctl DRM grezzi deve
essere guidata senza touch (touchscreen guasto, pannello bare, kiosk): un tasto
cicla gli elementi, un tasto conferma, la pressione lunga torna indietro.

## Regola 1 — la lista degli elementi si raccoglie MENTRE si disegna

Non scrivere una seconda funzione che ricalcola i rettangoli degli elementi:
due formule parallele (una per disegnare, una per selezionare) divergono al primo
ritocco del layout, e l'evidenza cade dove non c'e' nulla. Le funzioni di disegno
(`draw_btn_at`, `draw_tab_at`, le righe di lista) chiamano `focus_add(id, kind,
x, y, w, h, label)` sulle coordinate che stanno gia' usando. Chi disegna =
chi definisce la zona sensibile.

- Registra **un solo punto per frame**: alza il flag di raccolta attorno al
  PRIMO dei due buffer; il secondo disegna lo stesso contenuto e raddoppierebbe
  la lista (chiavi di selezione che si spostano di un posto a ogni frame).
- Serve un tetto (`FOCUS_MAX` 320 circa con struct da ~70 byte): una pagina con
  77 voci + tastiera aperta resta largamente sotto.

## Regola 2 — la selezione si identifica con una CHIAVE, non con un indice

Quando l'elemento attivato cambia pagina, l'indice non significa piu' nulla.
Salva l'id dell'elemento (o una chiave negativa progressiva per le righe, che un
id non ce l'hanno) e a ogni frame cerca la chiave nell'elenco; se non c'e' piu',
riparti dal primo elemento visibile. Cosi' la selezione resta "agganciata"
all'elemento anche quando l'etichetta cambia (NASCONDI SIM -> MOSTRA SIM).

## Regola 3 — l'evidenza si disegna per ULTIMA e ATTORNO

Disegna la cornice di selezione in fondo alla funzione di disegno del frame,
dopo ogni overlay (tastiera compresa): un elemento disegnato dopo copre la
cornice dei precedenti. Cornice di ~6 px ATTORNO al rettangolo, mai sopra:
l'etichetta resta leggibile. Se la cornice viene disegnata su due buffer, la sua
scelta dev'essere **deterministica** (stessa lista + stesso stato modale),
altrimenti i due buffer mostrano due evidenze diverse per un frame.

## Regola 4 — overlay modale: il fuoco non esce

Con la tastiera a schermo aperta solo i suoi tasti sono selezionabili. Filtra in
un unico punto (`focus_visible(i)`) usato sia dallo spostamento sia dal disegno
della cornice. Attenzione al primo elemento disegnato di un overlay: se e' la "X"
di annulla, la prima pressione butta via la modifica — preferisci come punto di
riparto un tasto carattere.

## Regola 5 — pressione lunga: guarda ANCHE la scadenza, non solo il rilascio

Il rilascio puo' non arrivare (loop occupato, evento perso): senza un controllo
a tempo il tasto resta "giu'" per sempre e la pressione successiva viene letta
come lunga. Tieni `pwr_down/pwr_fired/t0`, agisci **al rilascio** se la durata e'
< soglia, e fai scadere la soglia anche a eventi finiti (chiamata a ogni giro di
loop). Accetta la ripetizione (value 2) dei tasti di scorrimento con un limite
(una mossa ogni ~120 ms): su una pagina da 77 voci senza ripetizione servono 77
pressioni.

## Come si VERIFICA senza il device (obbligatorio prima di consegnare)

Cosi' si prova il codice che gira davvero, non una copia:

```
# stessa concatenazione dei frammenti che va sul device, main() rinominato
cat ui-1… ui-6-main.c ui-nav-test.c > nav-test-full.c
gcc -I. -O1 -Wall -Wextra -Dmain=ui_real_main -o nav-test nav-test-full.c
```

Ogni caso di regressione va provato PRIMA contro il codice vecchio: il file di
prova nuovo piu' il `ui-6-main.c` pre-correzione (estratto dalla concatenazione
vecchia, se il progetto non e' sotto git) deve FALLIRE, altrimenti il collaudo
non sta misurando niente.

- Il file di prova comincia con `#undef main` e definisce il proprio `main`: il
  main vero (che apre `/dev/dri`) resta definito e **non viene chiamato**.
- Si chiama `key_event()` (l'ingresso vero, con `struct input_event` sintetici) e
  `poll_key_timeout()`: la pressione lunga si prova davvero con `usleep(1.6 s)`.
- `draw_page()` scrive su un buffer `malloc` — il DRM non si tocca affatto.
- Asserzioni che vale la pena scrivere, in quest'ordine:
  1. ogni pagina ha elementi e tutti i rettangoli stanno dentro il pannello;
  2. un giro completo di pressioni riporta sulla stessa selezione e tocca
     **tutti** gli elementi (nessuno irraggiungibile);
  3. l'evidenza si verifica A PIXEL: i quattro punti medi della cornice hanno il
     colore dell'evidenza, e il bordo inferiore del vecchio elemento non ce l'ha
     piu' (controllare un bordo laterale darebbe falsi negativi: la cornice del
     vicino lo copre);
  4. giro del menu con i soli tasti: per ogni categoria e ogni pagina si attiva
     la linguetta e si verifica che la pagina sia davvero cambiata;
  5. ciclo di modifica: apri la tastiera su un campo, PULISCI, scrivi due
     caratteri, APPLICA — e controlla l'avviso di conferma;
  6. modale: a tastiera aperta gli elementi dietro non sono selezionabili.
- I dump PPM dei frame (`P6`, poi `PIL`/`convert`) si guardano con gli occhi: la
  cornice arancione si vede. Sul device esiste di solito un flag tipo
  `/tmp/ui-dump` che scrive il frame su file: chiedilo invece di indovinare.

## Trappole che sono costate tempo

- **Il nodo del tasto consegna DUE esemplari dello stesso click.** Misurato: un
  click solo produce `press+release`, poi un secondo `press+release`. Se il
  primo ha svegliato il pannello (standby off), il secondo trovava lo schermo
  acceso e il suo rilascio armava la finestra del click: 350 ms dopo lo schermo
  tornava nero, e nel log manca il secondo "risveglio" (la riga e' solo
  `standby: schermo OFF`). Regola: **chi consuma un evento deve azzerare lo stato
  che quell'evento ha mosso** (`pwr_pending`, `pwr_pend_t0`, `pwr_down`,
  `pwr_fired`) **e armare una guardia piu' lunga della finestra del click**
  (~600 ms) che blocchi il riarmo; il flag di guardia va tenuto per TUTTA la
  pressione (`pwr_guard = now < t_wake_ignore` sulla press) se la guardia puo'
  scadere col dito ancora giu'. La pressione LUNGA deve restare valida dentro la
  guardia (un dito tenuto 1.5 s vuole "indietro").
- Un click rimasto in attesa puo' essere armato anche dall'ALTRO canale di
  standby (comando di servizio/flags file): il risveglio deve azzerarlo, non solo
  il ramo del tasto. E serve anche il controllo simmetrico sulla scadenza: un
  click in attesa dentro la guardia va CONSUMATO e scartato, non lasciato li'
  (altrimenti spara appena la guardia scade).
- Le `#define` di una soglia usata da `standby_exit()` vanno PRIMA della
  funzione: la concatenazione dei frammenti non perdona l'ordine.
- **Warning preesistenti**: la concordanza "zero warning" si misura PRIMA di
  scrivere. Codice morto tenuto di proposito (innesco disattivato) fa
  `-Wunused-function`: si marca `__attribute__((unused))` invece di cancellarlo,
  cosi' la conoscenza resta e il gate torna verde.
- Un `for (int y = 0; y < H; y++)` con `H` unsigned e' un `-Wsign-compare`.
- Il bilanciere volume puo' essere esposto su un **secondo** nodo input
  (`pmic_resin`) e il verso dei codici non e' verificabile senza device: si
  aprono entrambi i nodi cercati PER NOME e si lascia un flag
  (`/tmp/ui-swap-vol`) che scambia i due versi senza ricompilare.
- Un avviso temporaneo all'avvio ("VOL = seleziona, POWER = attiva") e' l'unico
  modo perche' l'utente scopra una navigazione che non ha un cursore visibile.
