---
name: boot-timing-optimization
description: Use when optimizing a device boot sequence timing.
version: 1.0.0
author: kernel-re
license: MIT
platforms: [linux]
metadata:
  hermes:
    tags: [boot, timing, embedded, optimization, measurement]
    related_skills: [nx679j-openwrt, kernel-debug-validate, block-research-fanout]
---

# Ottimizzare la sequenza di boot (embedded / telefoni con kernel vendor)

## When to Use

- Un device si riavvia per ogni prova e devi far comparire prima qualcosa (modem, display, rete, UI).
- Devi attribuire un cambiamento di tempo a una modifica senza farti ingannare dal boot "buono".
- Stai per fare un esperimento sull'ordine di avvio che puo' resettare o bloccare la board.

Classe di lavoro: "far partire X prima" su un device che si riavvia per ogni prova (modem, display,
servizi di rete). Ogni tentativo costa un ciclo build+flash+riavvio, quindi il metodo vale piu' del
codice. Per l'istanza NX679J/OpenWrt vedi la skill `nx679j-openwrt` e `RIPRESA.md` nel progetto.
Per la depth OpenWrt (ModemManager/rmnet, criteri di prontezza, UI LuCI inaffidabile, storage
condiviso dei marker): `references/openwrt-modemmanager-luci.md`.
Per la guardia anti-boot-loop (ricetta, marker da chiudere, slot condivisi):
`references/anti-bootloop-guard.md`.

## Regola zero: prima il criterio, poi il cronometro

Il criterio di successo deve essere **cio' che l'utente vede**, non un proxy comodo.
Caso reale: `ping 8.8.8.8` riusciva a 63 s (bearer a basso livello gia' pronto) mentre l'utente
vedeva ancora «carrier absent» perche' l'interfaccia di rete gestita **non esisteva**
(`ifstatus <iface>` → *"Interface not found"*). Ottimizzare il ping sarebbe stato ottimizzare la
cosa sbagliata. ⇒ Scrivi il test di accettazione nella forma esatta in cui l'utente lo percepisce
(stato dell'interfaccia, cosa appare a schermo) e usalo in ogni misura.

## Procedura di misura (4 passi)

1. **Strumenta i passi, non solo l'evento finale.** Ogni script del boot logga `uptime` accanto
   al passo (`[65] DONE`, `[97] inject`, `[135] iface UP`). Senza questo non attribuisci nulla.
2. **Prima lo strumento, poi il cronometro: il polling dall'host NON arriva durante il boot.**
   Il device e' occupato (user, rete, script) e SSH non risponde: nella pratica il primo campione
   utile cade a **100-145 s**, quindi i tempi che interessano di piu' (gate, primi servizi) restano
   *invisibili*. Ogni conclusione presa da li' e' un artefatto dello strumento, non del sistema — e
   si finisce per inseguire per ore un tempo che lo strumento non puo' vedere.
   ⇒ Fonte primaria: i **log con uptime che il device scrive da se'** (`[63.40] cell ping rc=0`,
   `uptime 93: parto`), letti **dopo** il boot. Il polling dall'host serve solo come conferma, quando
   il device e' raggiungibile.
   - **I log possono essere contaminati dal boot precedente.** Se il sistema ripristina i log da uno
     storage che sopravvive al riavvio (rawdump/misc), le righe del boot PRECEDENTE finiscono in testa
     al file nuovo: **timestamp identici in boot diversi = contaminazione**. Leggi solo la coda
     (`tail` / `grep … | tail`), verifica con `boot_id` e con gli uptime, e non attribuire a questo
     boot una riga che non porta il suo `boot_id`.
   - **Se un numero non torna, sospetta del log.** Un messaggio che stampa un multiplo sbagliato
     (passo reale 2 s, log `E*5`) fa stimare decine di secondi di ritardo inesistenti: il log di
     avanzamento e' codice come gli altri e va verificato.
   **Polling fine (2-5 s) del primo uptime in cui la condizione diventa vera**, non un campione a
   caso: `U=$(cut -d. -f1 /proc/uptime)` + il test del criterio, in loop, fermandosi al primo
   `true`. Un campione singolo a "≤143 s" non e' una misura: non dice nulla sulla coda.
   Pronto all'uso: `scripts/boot-timeline-poll.sh <host>` — asserisce il reset di `uptime`, stampa
   solo le transizioni e il primo uptime in cui ciascun campo diventa vero.
   **Asserisci il reset di `uptime` prima di credere a un campione**: se il primo campione ha uptime
   alto e crescente il device **non si e' riavviato** e stai misurando il boot vecchio (successo: un
   poll lanciato subito dopo `reboot -f` ha registrato una timeline inesistente, sprecando il ciclo).
   **Non parsare l'output a offset fissi** (`${R:0:3}`, `${R:3:1}`): basta un campo che cambia
   larghezza per leggere `iface` come una cifra dell'uptime. Metti un separatore esplicito tra i campi
   e leggili uno alla volta.
3. **N campioni prima di attribuire un effetto.** Fasi diverse hanno varianze diverse (osservati
   ~90 s di spread su una fase di probe): un boot "buono" puo' far sembrare vincente una modifica
   che peggiora. Tre esperimenti giudicati da un boot solo sono stati annullati, pagando un ciclo
   di build+flash ciascuno.
4. **Prima di rimuovere un workaround, capisci cosa lo rende necessario.** Un workaround
   documentato (`kill`+restart di un daemon per forzare un re-probe) sembrava un costo puro;
   rimuoverlo ha **peggiorato** (186 s contro 135 s). Il costo vero era altrove: una notifica di
   evento kernel che **fallisce** e fa ricadere il daemon sul proprio ciclo di scansione. Cerca il
   sintomo nel log del daemon (`Couldn't report kernel event`), non nel daemon.

## Le domande di configurazione si risolvono dal vivo, non con un flash

Quando devi decidere *come* configurare qualcosa (interfacce, route, priorita' fra servizi), la
risposta spesso non e' nella documentazione e **non vale un ciclo build+flash**: provala sul device
vivo, in modo reversibile. `uci set … && uci commit && ubus call network reload` + il test del
criterio + il log del demone (`netifd: has link connectivity` → `is now up`) costa un minuto e
risponde. E' cosi' che si e' scoperto che un'interfaccia `static` sopra un device che ha **gia'**
indirizzo e route si alza subito, senza errore `File exists` — cosa che nessuna fonte diceva.
**Attenzione: la prova "si alza" NON e' la prova "va bene".** Con `static` netifd diventa
**proprietario** dell'L3 e un reload successivo (`ifup <altro>` = reload globale) **cancella
indirizzo e route** del bearer: misurato come interfaccia `up` con `ping=KO` fino a quando l'altro
servizio non reinstallava le sue. Per un L3 che gestisci tu la scelta e' `proto none` (nessun IP in
UI) o l'handler `address-external` (up + IP, kernel mai toccato) — vedi
`references/openwrt-modemmanager-luci.md`.

- Asserisci i fatti **prima e dopo** (indirizzo sul device, tabella route, ping), non solo lo stato
  dell'interfaccia: e' cio' che distingue "up" da "up senza aver rotto niente".
- Usa una sezione di configurazione **tua** (nome nuovo) e annullala subito dopo; con rootfs in ram
  sparisce al reboot, ma non lasciare stato che confonda la misura successiva.
- Solo quando la **forma** e' provata la si scrive nell'immagine: allora il ciclo di flash misura un
  effetto, non un'idea.

## Un guasto intermittente e' quasi sempre un identificatore fisso in un ordine variabile

Prima di accusare l'hardware: **se il difetto si presenta circa a meta' dei tentativi, cerca un nome
o un indice FISSO dentro un ordine che varia ad ogni boot.** Caso reale: lo script del modem leggeva
`/sys/class/remoteproc/remoteproc3/state`; l'ordine di enumerazione dei remoteproc cambia ad ogni
avvio (in un boot il modem e' `remoteproc2`, in un altro `remoteproc3`), quindi in meta' dei boot
leggeva lo stato di un ALTRO processore remoto: `MSS=offline` fasullo, ramo di attesa mai preso,
bootstrap tentato nel momento sbagliato — **e nessun guasto hardware dietro**. Regola: **risolvi il
nodo per nome, mai per indice** (`/sys/class/remoteproc/*/name`, `/sys/class/input/*/device/name`,
`/sys/class/net/*/address`, `by-id`/`by-path` per i dischi): l'indice e' un dettaglio di questo boot,
non un'identita'. Ricetta, tavola degli errno e contromisure: `references/intermittent-bringup-triage.md`.

Conseguenza per il metodo: un guasto al ~50% si diagnostica **leggendo i log del driver e il codice
che decide**, non allungando i timeout. E il suo effetto tipico e' proprio quello che si nota di piu':
un percorso che a volte non parte affatto.

## Prima la deterministica, poi i secondi

Il tempo del boot **riuscito** e' la seconda domanda. La prima e': **quanti boot falliscono del tutto?**
Caso reale: la stessa immagine dava "tutto su" a ~144 s in un boot e **niente** in un altro
(`iface=0 modem=0 ping=0` da 144 a 370 s) perche' il bring-up del firmware del modem falliva a meta'
degli avvii e **non si recupera senza reboot**. Ottimizzare i secondi di un percorso che a volte non
parte e' lavoro sprecato: prima si caratterizza il fallimento, poi si lima.

- **Misura il tasso di successo, non solo la mediana.** Registra per N boot l'esito booleano del
  criterio + l'uptime, e cercalo in correlazione con `dmesg` (bus, probe, firmware) e con i log dei
  tuoi script. 4-5 boot bastano per distinguere "lento" da "a volte morto".
- **Un fallimento non recuperabile in-place va dichiarato come tale.** Se l'unico rimedio e' il
  reboot, la mitigazione utile non e' limare i secondi ma trovare il trigger (ordine di probe, timing
  del bus, servizio che deve essere pronto prima, risorsa non ancora rilasciata).
- Su quel trigger **non improvvisare codice**: fan-out di ricerca sul sintomo esatto
  (`block-research-fanout`) — un guasto hardware/firmware intermittente non si risolve a tentativi.

## Misurato ≠ risolto: annota i tentativi a guadagno zero

Un'ipotesi con prova nei log puo' comunque valere zero. Caso reale: la notifica di evento che
falliva (`Couldn't report kernel event`) spiegava bene ~38 s di ritardo, ma **sistemarla non ha
spostato nulla** (137 s contro 134-142 di baseline). Regole:

- Ogni modifica misurata si registra con l'esito e la baseline: "provata, nessun guadagno" e' un
  risultato e va scritto nel file di continuita', altrimenti la sessione successiva la ritenta.
- Una modifica a guadagno zero si **ritira**, non si tiene "per sicurezza": allunga il codice e
  confonde la diagnosi successiva.
- Il guadagno atteso si dichiara **prima** (ipotesi + baseline + criterio); a misuratori spenti vale
  solo il numero.

## Una variabile per flash; se l'utente chiede "tutti i fix", progetta la bisezione PRIMA

Ogni ciclo build+flash+riavvio misura **una** modifica. Applicarne quattro insieme (una causa nota,
due ipotesi e un fix di UI) ha reso impossibile attribuire la regressione comparsa subito dopo: due
boot peggiori del baseline e nessun modo di sapere quale delle modifiche di merito l'avesse causata.
Il costo non e' il doppio del lavoro: e' che **tutti** i fix finiscono sotto sospetto e non si puo'
piu' tenere quello buono con fiducia.

- Raggruppa solo modifiche **indipendenti e senza interazione possibile** (file diversi che non si
  parlano), dichiarando per ognuna l'effetto atteso e come si annulla.
- Se il batch e' imposto (l'utente vuole "tutto applicato subito"), decidi **in anticipo** l'ordine di
  bisezione e rendi ogni candidato revertibile con una patch di una riga: il primo giro dopo la
  regressione deve togliere **il sospetto numero uno**, non "qualcosa".
- Un indizio che restringe il campo vale quasi una misura: un passo che cambia stato solo dopo la
  modifica X (`open=1` → `open=0`) indica X, ma non lo prova — serve il boot di controllo.
- Dopo una regressione non attribuita: ripristina la baseline **misurata buona** e riparti da li',
  invece di insistere con il batch dentro.
- Corollario di attribuzione: un fix "teoricamente corretto" che misura peggio si **ritira** e si
  annota come tale, anche quando la fonte autorevole lo indicava come la via giusta. La teoria non
  batte il cronometro sul setup reale.
- La misura di controllo di un batch va fatta **dopo il revert dei sospetti**: se gira con il batch
  ancora dentro, l'esito non e' interpretabile.

## Guardia per esperimenti che possono resettare la board

Un esperimento sull'ordine di avvio puo' mandare la board in reset o in boot-loop: serve una guardia
che **si auto-annulla** dopo N fallimenti, scritta in uno storage che sopravvive al riavvio (settore
 dedicato, slot rawdump/misc, U-Boot env). Ricetta pronta (codice della guardia, gestione del marker
lasciato aperto, inventario degli slot condivisi): `references/anti-bootloop-guard.md`.

**Regola sempre valida: chiudi il marker con un esito a OGNI uscita normale**, e se lo trovi aperto
riprova contando i tentativi (1, 2, 3) arrendendoti solo dal terzo. Un marker "tentato, nessun esito"
che resta aperto spegne il boot SUCCESSIVO — l'utente vede schermo nero e la diagnosi sembra hardware,
mentre nel log del launcher c'e' scritto *skip*. E il contatore sopravvive al flash: dopo una serie di
boot interrotti a meta' la guardia passa in prudente **in silenzio** (percorso a 93 s invece di 37 s,
senza nessuna modifica che lo giustificasse) — quando i tempi peggiorano senza causa nuova, leggi e
azzera il marker prima di indagare il percorso critico. Il primo passo e' **inventariare** la mappa
degli slot: sono condivisi con altri componenti.

## Trappole che costano un ciclo intero

- **`cmd | tail -2` maschera l'exit code.** Un build fallito (assert) non ferma la catena `&&` e si
  flascha l'**immagine vecchia**, spendendo un riavvio per misurare una modifica che non c'e'.
  Dopo la build confronta SEMPRE l'hash dell'artefatto prima/dopo; non fidarti dell'exit della pipeline.
- **Quando una patch rimuove una riga che un assert della build controlla, aggiorna l'assert nello
  stesso giro.** Altrimenti la build aborta e (col punto sopra) si flascha il vecchio.
- **`ps` di busybox tronca i nomi a 15 caratteri** (`{nx679j-mm-watch}`): `ps | grep <script>` come
  test di esistenza da' falsi negativi → `pgrep -f` sul path completo.
- **`mknod <nodo> b <maj> <min>` fallisce se il path esiste come file regolare**: `rm -f` prima,
  altrimenti si scrive nel file sbagliato.
- **Una campagna staccata dal wrapper non finisce con la notifica.** Se lanci `nohup <campagna> &`
  dentro un comando eseguito in background, la notifica di completamento e' del **wrapper**: la
  campagna continua — e se riavvia il device continua a riavviarlo — dopo il "completed". Conta i
  processi prima di concludere, e **mai flashare mentre una campagna riavvia il device**: un reboot
  durante il `dd` sulla partizione di boot la lascia scritta a meta'.
- **`pkill -f <pattern>` puo' uccidere la tua stessa shell** quando il pattern compare nella tua riga
  di comando (basta citare il percorso del file di log o dell'artefatto nello stesso comando): la shell
  muore con SIGTERM e il comando sparisce. Usa una pipeline che esclude se stessa:
  `ps w -eo pid,args | grep -F <nome> | grep -vF grep | grep -vF <tuo-marcatore> | awk '{print $1}' | xargs -r kill`.
- **Lo storage dei marker e' condiviso con altri componenti.** Uno slot dato per libero puo' essere
  riscritto da un sottosistema (check-point, coldboot reason, log di crash): se il valore letto non
  ha il tuo formato, il marker e' stato sovrascritto. Verifica la mappa degli slot, e progetta la
  logica perche' un valore estraneo significhi "procedi" e non "bloccati".
- **Una modifica a un workaround necessario e' una regressione mascherata da pulizia.** Misura
  sempre contro la baseline e ritira la modifica se peggiora (v. sezione sulla deterministica).
- **Log del daemon come fonte primaria.** Il tempo attribuito a un daemon e' spesso il tempo in cui
  il daemon *non riceve* qualcosa. Cerca la riga di notifica/evento prima di toccare la sua config.
- **Non ripetere la notifica/il restart "per sicurezza".** I daemon di discovery hanno una finestra di
  probe: ogni evento in piu' la riazzera e **peggiora** il tempo (misurato: 184 s contro 135-142 di
  baseline). Un evento, poi silenzio e poll. Se un singolo tentativo non basta, cambia *cosa* o
  *quando* notifichi, non quante volte.
- **Una fix va verificata NELL'ARTEFATTO, non nella fonte.** Aver patchato un file nella radice di build
  non garantisce che sia quello impacchettato: la build passa e il bug resta sul device. Dopo il flash,
  `grep` il pattern nuovo **sul device**; con rootfs volatile applica il fix ad ogni boot dallo script
  di avvio (e loggalo), perche' nel filesystem non sopravvive.
- **Criterio di "pronto" = risposta reale, non presenza del servizio.** Un servizio che compare in un
  registry (QRTR, D-Bus, bus di sistema) e' un **falso positivo** frequentissimo: il test valido e'
  una richiesta reale che ritorna il dato atteso.

## Il tuo retry puo' distruggere un successo (e il sintomo punta altrove)

Un passo di boot che si rilancia da solo (waiter, watchdog, retry difensivo) e' pericoloso quanto un
indice fisso: se riparte **dopo** che il percorso ha raggiunto uno stato buono, lo **smonta**.
Osservato: catena di bring-up rieseguita a ~95 s mentre i dati andavano dalle 63 s; il secondo giro
falliva agli stage di indirizzo/route e lasciava il device con `ping=KO` per tutto il resto del boot.
Il sintomo si legge pero' come **"a volte non parte"** — e si finisce per accusare l'hardware.

- **Rendi idempotente ogni passo rilanciabile**: se il run precedente ha lasciato il marker di
  **completamento**, esci senza toccare nulla; se ha lasciato un marker di **fallimento**, ritenta.
  Il discriminante sta nello stato scritto dal run stesso (`DONE` nel suo log, marker dedicato), non
  in un timeout.
- Non appiattire il retry "per sicurezza": la distinzione *successo → non toccare*,
  *fallimento → ritenta* va implementata, non disattivata.
- **Chi ha toccato per ultimo lo stato buono?** Prima di incolpare l'hardware, guarda l'ultimo
  scrittore: watchdog, campagna di test, script lanciato due volte.
- **Il guard di idempotenza va misurato con lo STESSO criterio di sopravvivenza, e in entrambe le
  direzioni.** Bloccare un run che riparte puo' togliere un **recupero** oltre che un distruttore:
  si e' aggiunto un "esci se il run precedente ha lasciato DONE" e da li' la risorsa ha smesso di
  restare viva — coerente con l'ipotesi che il run bloccato la **ristabilisse**. Non lo si afferma
  senza il boot di controllo: si annota come sospetto e si misura il criterio di sopravvivenza
  (esistenza della risorsa a +X s) prima e dopo il guard. Morale: **non difendere il proprio fix
  contro i dati** — e' il fix, non la misura, che va messo in dubbio.

## Il criterio include la SOPRAVVIVENZA, non solo la comparsa

Un tempo misurato al momento in cui lo stato *appare* non e' il risultato: se un passo successivo
puo' togliere la risorsa, la vittoria e' effimera. Osservato: dati a 64-65 s **deterministici su 3
boot** e obiettivo dichiarato raggiunto — poi la campagna ha mostrato `ping=KO` a 165 s in **2 boot
su 3**, perche' uno script piu' avanti **uccideva la sessione dati della catena per passare la porta
all'altro proprietario** (il cui bring-up arriva 130-330 s dopo, variabile). Il tempo era perfetto:
non era quello il problema.

- **Misura il criterio in DUE momenti**: alla prima transizione E dopo i servizi che possono
  rubare/rottamare la risorsa (es. +100 s). Il secondo momento va nel criterio di accettazione,
  scritto prima di ottimizzare.
- **Un handover deliberato e' distruttivo quanto un retry.** Se una risorsa ammette un solo
  proprietario (una sessione dati, un port, un lock), liberarla per un componente il cui bring-up
  dura minuti **spegne il percorso veloce**: chi l'ha fatto funzionare se la tiene, oppure l'handover
  va reso non distruttivo (attesa che il nuovo proprietario sia pronto prima di mollare).
- **"Tutto funziona" richiede N boot con ENTRAMBI i tempi buoni.** Un solo boot, o un boot giudicato
  solo dalla comparsa, non basta: la dichiarazione di obiettivo raggiunto si fa sulla campagna, non
  sul boot migliore. Se anche un solo boot perde il criterio, l'obiettivo non e' chiuso — e va detto.
- **Misura la RISORSA, non solo il sintomo.** "Connettivita' assente" e "device e route distrutti"
  portano a diagnosi diverse: raccogli a ogni campione anche `ip -o -4 addr` (il device esiste?) e
  `ip route` (la default c'e'?), non solo il ping. `dev=0 default=0` accusa un **distruttore**;
  sintomo che sparisce da solo al reboot accusa invece un handover o un timeout.
- **Se togliere il distruttore che hai trovato non ripristina la sopravvivenza, i distruttori sono
  almeno due.** Osservato: rimossi sia il kill della sessione dati sia il passo che faceva salire il
  servizio con il cleanup dei bearer, la risorsa moriva lo stesso. Non fermarsi al primo colpevole
  plausibile: **elenca tutti i passi che scrivono su quella risorsa** (kill/watchdog, cleanup di
  bearer del gestore, teardown dello script di dati, retry rilanciato, handler di hotplug) e provali
  uno per volta con lo stesso criterio ai due momenti.
- Corollario: prima di attribuire un guasto intermittente al modem/hardware, cerca il passo del
  TUO codice che tocca lo stato dopo il successo (handover, teardown, cleanup, "libera la risorsa").

## Dove atterra il codice e' parte del fix

Avere il fix giusto nel file giusto non basta: in uno script di boot conta **dove** atterra nella
sequenza. Due volte un blocco corretto e' stato inefficace perche' eseguiva nel momento sbagliato:

- un blocco messo **prima** degli stage che configurano indirizzo e route leggeva un device ancora
  inesistente (`nessun device dati trovato`) e non faceva nulla;
- un launcher messo in **fondo** a uno script di boot (dopo il resto dell'init) partiva a ~93 s invece
  che a ~38 s, e la gate di sicurezza che conteneva era di fatto inutile.

Regole: fai **loggare al blocco il proprio uptime** quando esegue, e verifica quel numero dopo il
flash; se un passo dipende da un altro, mettilo **dopo** quello (o rendilo un'attesa attiva); e
quando sposti del lavoro fuori da una coda di init **seriale** (rcS/S*, dove i servizi girano in
ordine e l'ultimo paga l'attesa di tutti), la protezione che avevi — gate, condizione, verifica —
deve restare dentro il passo spostato.

**Audit degli `sleep` fissi: sono i secondi piu' facili.** Nella sequenza di bring-up ogni attesa a
tempo fisso e' o (a) una prudenza legata a una condizione, e allora va **convertita in poll con lo
stesso tetto** — `i=0; while [ $i -lt N ] && ! <condizione>; do sleep 1; i=$((i+1)); done` — o (b)
tempo puro, e allora va tolta. Due casi misurati, entrambi invisibili finche' non si legge il codice
con i timestamp a fianco: un gate che interrogava la condizione **ogni 15 s** (fino a 15 s di
ritardo per pura granularita') e un `sleep 20` fisso prima di verificare che un client fosse vivo
(20 s di attesa pura: −18 s sostituendolo con un poll a 1 s). Stesso trattamento per i passi di
caricamento moduli (`sleep 2` ×4 = 8 s fissi). Cercali con `grep -n sleep` negli script di boot e
chiediti, per ognuno: *questa attesa e' una condizione o e' tempo perso?*

## Quando smettere di limare

Separa sempre il pavimento **fisico** (enumerazione bus, probe del driver, boot del kernel) dalle
attese **nostre** (sleep, gate, ordini di avvio prudenti, workaround). Le seconde si comprano
indietro; le prime no. Di' esplicitamente qual e' il pavimento misurato, cosi' l'utente sa dove
finisce l'ottimizzazione dello script e comincia un lavoro di natura diversa (e piu' rischioso).

## Comunicare i risultati

- Prima il numero misurato, poi il perche'. Tabella `fase → uptime → cosa la determina`.
- Distingui **misurato** da **stimato** in ogni risposta: un guadagno non misurato va etichettato
  come stima, non come risultato.
- **Il file di continuita' (STATO/RIPRESA) serve a NON fermarsi.** Se il contesto si esaurisce si
  scrive lo stato nel file e si continua a lavorare; non si chiude il turno citando l'esaurimento
  del contesto — l'utente lo considera un modo di non lavorare.
