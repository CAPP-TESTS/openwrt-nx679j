# Regole e richieste dell'utente — NX679J (verbatim dove possibile)

> Raccolte dalle sessioni 16–22/09/2026. Sono VINCOLANTI: l'utente si è ripetutamente irritato quando non applicate.

## Ricerca prima del codice (LA regola n.1)

- *"mi sono rotto il cazzo di ripeterti ogni volta di cercare online prima di metterti a scrivere codice perdendo inutilmente tempo"*
- *"questa cosa di aver lavorato su delle cose che già esistevano non deve succedere mai più... Bisogna sempre cercare online prima di iniziare a scrivere noi soluzioni."*
- *"ma prima di scrivere altro codice, come ti ho detto 2 milioni di volte, cerca le info online sguinzagliando anche 20 agenti simultaneamente"*
- *"ok che non sono upstream su github quelle patch, ma sei sicuro che non si trovano da NESSUNA PARTE SU TUTTO INTERNET!? sguinzaglia una decina di agenti... anche su forum, reddit, siti cinesi, russi, asiatici, ecc. Anche community di hacker e modder."*

→ Skill dedicata: `block-research-fanout` — invocarla OGNI volta prima di scrivere codice. Documentare le fonti.

## Subagenti

- *"ricorda sempre che puoi spawnare tutti i subagenti che vuoi, così facciamo il lavoro più velocemente"*
- *"puoi usarli per finire il compito"* / *"tenerli attivi tutto il tempo che vuoi"*
- MA: **verificare SEMPRE i numeri/dati che i subagenti riportano** — mai fidarsi del summary.

## No attese passive

- *"ma che segnale stai aspettando? è acceso da diversi minuti sullo splash screen di redmagic! ma fai un cazzo di controllo ogni tanto sulla webcam per capire invece di aspettare come un idiota!"*
- *"ma perché metti questi sleep lunghissimi? fai le cose dinamiche non aspettando ogni volta 2 ore per una cosa da 10 secondi"*

→ Check attivi (webcam via `obs_shot.py` + vision_analyze), polling dinamico 2-5s con condizione e break. MAI sleep fissi lunghi.

## Canali e indirizzi

- *"tu hai sempre l'usb gadget per connetterti, ed è quello è veloce"* → preferire USB (10.0.0.1, rtt 3ms).
- *"ma che cazzo dici!? il wifi ti deve rispondere su 192.168.77.1"*
- *"ma adesso tu vuoi dirmi che non puoi entrare ssh né da gadget né da wifi!? ma su! lo vedo io connesso il gadget e vedo la rete wifi del telefono!"* → non accettare "non raggiungibile" senza riprova.

## Persistenza

- *"tutta la roba che sappiamo che ci serve e che funziona mettiamolo in modo persistente, questo telefono ha tantissimo spazio"*

## Standard OpenWrt

- *"noi dovevamo usare i componenti di OpenWrt standard, non creare un protocollo nostro che non ha nessuna funzione"* → proto custom rimosso; si usa `proto modemmanager`.

## Android come oracolo

- *"possiamo sempre avviare la parte android per capire come viene pilotato il display"*
- *"per andare su android devi andare su fastboot, settare il boot a e farlo partire... sulla parte android hai magisk con il root"*

## Webcam

- *"ho collegato una webcam che guarda lo smartphone, perché non trovi online un modo per poterla usare così da avere tu stesso degli input visivi senza il mio supporto?"*
- *"ti ricordo che hai la webcam per vedere la situazione"* → usare `obs_shot.py`/vision_analyze, con controlli ATTIVI periodici.
- *"quella che tu stai decifrando come linea 'ciano' in realtà è solo una linea glitchata"* → attenzione a non scambiare artefatti per colori.

## Lavoro e modalità

- *"continua allora fino al goal"* → lavoro autonomo, mai fermarsi a chiedere.
- *"mai interrompere il lavoro"*; max 1 azione utente per test; i reboot li faccio io.
- *"allora mi raccomando lavora in autonomia"*
- Ottimismo zero: *"non dichiarare mai falsi successi"* (implicito: distinguere "verificato" da "plausibile").
- Preferenza: una sola riga copia-incolla per l'utente quando serve un'azione sua; lui esegue i comandi privilegiati.
- Comunicazione in italiano.

## Metodo (lezioni validate sul campo)

1. **`rc=$?` dopo una pipe mente** — verificare l'effetto (sysfs/netlink/log), non l'exit code.
2. **Un tentativo si ripete SOLO con un dato nuovo** (STATO-ATTUALE.md = fonte unica anti-loop).
3. **Ogni voce con comando + output reale**; se incerto scrivere "DA CONFERMARE".
4. **Prima di un flash**: backup, checksum, doppia lettura fredda, recovery documentato.
5. **Non toccare la catena modem quando funziona** — solo su regressione riprodotta.
6. **Niente subagenti per il lavoro meccanico** (build, download): farli io con script.
7. **Analisi esaustive**: tutte le differenze, mai fermarsi alla prima.
