# OpenWrt: ModemManager e LuCI in un boot ottimizzato (note misurate)

Depth per chi lima il boot di un OpenWrt con modem Qualcomm/rmnet (o simile) misurando con
ModemManager (MM) e guardando il risultato in LuCI. Tutto quanto segue e' stato **misurato** su un
setup reale; cio' che e' ipotesi e' marcato come tale.

## MM su OpenWrt: cosa non funziona come ti aspetti

- **`mmcli --scan-modems` non fa nulla**: OpenWrt compila MM senza udev, quindi la D-Bus
  `ScanDevices` risponde *unsupported*. E' innocua — puo' restare nello script — ma **non e' una
  leva**: non aspettarti che scopra il dispositivo.
- **`mmcli --report-kernel-event` puo' essere controproducente.** In MM 1.24 ogni report innesca un
  ciclo teardown+reprobe: misurato **184 s** e un boot con modem mai visto (336 s) contro **135-142 s**
  di baseline. Non usarlo "perche' e' la via documentata": prima misura.
- **Il restart forzato del daemon puo' essere necessario.** Se il tuo script di bring-up crea
  l'interfaccia di rete *dopo* che MM ha gia' fatto il probe, MM non la vede piu': il workaround
  `kill -9` + `sleep 10` + `/etc/init.d/modemmanager start` era **necessario** (rimuoverlo: 186 s).
  MM vuole **un** restart e poi quiete nella sua finestra di probe.
- **Non ripetere notifiche o restart a intervalli brevi**: ogni evento in piu' riazzera la finestra di
  probe e **peggiora** il tempo. Notifica una volta, poi poll.
- **"Checking if ModemManager is available..." non e' MM**: e' lo shell di OpenWrt
  (`mm_report_events_from_cache` in `/usr/share/ModemManager/modemmanager.common` + il monitor), un
  loop fisso 60×1 s. Il tempo che attribuisci a MM puo' essere di quel loop: leggi *chi* logga.
- **`qmi-proxy` (ipotesi, coerente con i numeri):** MM apre i port QMI attraverso qmi-proxy, e quello
  step ha un timeout lungo (~45 s). Se una tua catena tiene il nodo QMI in esclusiva mentre MM parte,
  l'open puo' bloccarsi per decine di secondi. Se il ritardo di MM oscilla molto, questo e' il primo
  sospetto da escludere con i log di MM a livello debug.
- **Finestra di probe senza udev:** MM attende ~2 s prima di iniziare e ~4 s dopo l'ultimo port
  aggiunto prima di concludere "non ci sono altri port". Il tuo bring-up deve far comparire **tutti**
  i port (net + controllo QMI) prima di quella chiusura, o concludera' con un dispositivo incompleto.

## Criterio di prontezza

- Un servizio che **compare** in un registry (QRTR, D-Bus, bus di sistema) e' un falso positivo
  frequentissimo: il test valido e' una **richiesta reale** che ritorna il dato atteso.
- Per l'interfaccia gestita, il dato autorevole e' `ifstatus <iface>` / `ubus call network.interface.<iface>.status
  ("up": true) — **non** il ping, **non** la UI.

## LuCI: la UI non e' una fonte attendibile

- Le pagine LuCI con vista JS possono morire con **`ReferenceError: View is not defined`** (l'utente
  vede una pagina vuota o messaggi di stato inventati, es. "carrier absent" su una rete sana).
  Il bug e' in `/www/luci-static/resources/ui.js`, funzione `instantiateView`:
  `if (!(view instanceof View))` → **`if (typeof view?.render !== 'function')`**.
- **La rootfs e' volatile** in un boot da ramdisk: applica il fix **ad ogni boot** dallo script di
  avvio (sed idempotente, poi `rm -f /tmp/luci-indexcache*`), e verifica con `grep` **sul device**
  dopo il flash — patchare la radice di build non garantisce che finisca nell'immagine.

## Firmware IPA (Qualcomm)

- Se `ipam` non carica (`Unknown symbol gsi_*`), il firmware IPA resta giu': la via userspace e'
  `echo 1 > /dev/ipa` (come fa Android). Applicato e innocuo, ma su un boot gia' funzionante
  **non ha dato guadagno misurato**: non attribuirgli meriti in una diagnosi futura.

## L'interfaccia di rete: perche' aspetta MM e le tre vie per anticiparla

Il proto `modemmanager` e' lento **per costruzione**, non per colpa del modem: esegue in serie
`mmcli --modem … --enable` (che attende l'enable **e la registrazione**), l'eventuale registrazione a
operatore e `--simple-connect`, ognuno con `option timeout` (default **120 s**). Somma tipica
osservata 135-330 s. Nessun ritocco di opzioni lo porta a ~70 s: se serve quel tempo, l'interfaccia
deve uscire dal percorso di MM.

Se il bearer e' **gia' pronto** (creato da te con libqmi, con IP e route installati), le vie per far
risultare l'interfaccia `up` senza aspettare MM:

| via | effetto in LuCI | rischio |
|---|---|---|
| `proto none` + `option auto '1'` | up, **"Unmanaged"**, nessun IP | minimo: netifd non tocca l'L3 |
| `proto static` con gli stessi valori | up **con IP** | netifd diventa **proprietario**: a un `ifdown`/reload/teardown rimuove indirizzo e route |
| proto custom con `address-external` | up **con IP** e route | va scritto l'handler (poche righe) |

- **`proto static` sugli stessi indirizzi non da' "File exists"** (netifd usa
  `NLM_F_CREATE|NLM_F_REPLACE`) e le route esistenti sopravvivono con `option defaultroute '0'` —
  **verificato dal vivo**: up in ~4 s dal reload, default route e ping intatti. **Ma l'ownership
  resta, e il conto arriva dopo**: un **reload globale** successivo (`ifup <altro>` = reload +
  down/up) rimuove indirizzo e route del bearer. Misurato: interfaccia `up` con `ping=KO` fino a
  quando MM non ha reinstallato le sue (bearer a 130-330 s). Sintomo tipico ⇒ **`up` + `ping KO`**
  significa che qualcun altro ha preso possesso dell'L3: per un L3 gestito da te usa `proto none`
  (nessun IP in UI) o `address-external` (up + IP, kernel mai toccato).
- **Una sola sessione dati per porta embedded: non regalarla a un servizio piu' lento.** Se liberi la
  sessione del tuo bring-up (kill del processo) perche' l'altro proprietario possa connettersi, il
  percorso veloce muore e riappare quando il lento arriva — con la sua varianza. Tenere la sessione
  e' cio' che rende la connettivita' stabile; il servizio lento serve solo per la UI.
- **Il proto `modemmanager` fa cleanup dei bearer quando sale**: `--simple-disconnect` + `--delete-bearer`
  di **tutti** i bearer conosciuti dal modem, prima di crearne uno proprio. Un bearer creato da te con
  libqmi non e' "adottabile" (l'interfaccia D-Bus `Modem` non ha un metodo di import) e viene quindi
  distrutto, con device e route che se ne vanno.
- **NON basta togliere il kill della sessione** per conservare il percorso veloce: misurato su 3 boot
  con `ifup modem` rimosso (MM ridotto a osservatore) e il kill disattivato, la risorsa moriva lo
  stesso a 155-255 s (`dev=0 default=0`). ⇒ il cleanup NON e' l'unico distruttore: il sospetto
  successivo e' il ciclo di vita della sessione WDS/rmnet del tuo stesso bring-up (holder, TTL,
  teardown a fine stage), e va cercato **prima** di toccare di nuovo MM.
- **`ifup <iface>` e' un reload GLOBALE**, non locale a quell'interfaccia: e' il motivo per cui un
  passo che ne esegue uno a meta' boot puo' cancellare indirizzo e route di un'interfaccia che andava
  bene. Se hai un percorso dati tuo, non eseguire `ifup` dell'interfaccia gestita dal gestore lento.
- **La via pensata per questo caso** e' `address-external`: in un proto handler minimale
  `proto_init_update "$ifname" 1 1` (terzo argomento = L3 esterno) fa **riportare** a netifd up,
  indirizzi e route **senza mai modificare il kernel**. E' l'unico modo di avere "up + IP" in LuCI
  senza diventare proprietari. Verifica che il meccanismo esista prima di scriverlo
  (`grep address-external /lib/netifd/netifd-proto.sh`).
- **Una sola interfaccia dati per device**: quando netifd crea una default route ne **cancella** la
  precedente, anche con metric diverse. Non progettare la coppia "una veloce + una gestita da MM":
  una sola interfaccia sul bearer, con `metric` fissato.
- **La pagina "Cellular Network" di LuCI e' indipendente dal proto dell'interfaccia**: vive in
  `luci-proto-modemmanager` e richiede solo MM installato/in esecuzione (+ ACL). Togliere l'interfaccia
  dati da MM **non** fa perdere la pagina del modem; si perdono il form del proto e la riconnessione
  guidata da MM (che va rifatta a mano).
- **`option device` del proto NON e' un netdev**: per i modem su QRTR l'UID e' la costante
  `qcom-soc` (il selettore che `mmcli --modem=` accetta), e il netdev reale viene risolto a runtime.
  Non cercare quel nome in `/sys/class/net`: e' un'identita' logica, ed e' anche il motivo per cui
  l'interfaccia "non ha un device fisso" in configurazione.
- **Il nome del device dati e' volatile per colpa di MM**: `qmapmux<dbus_id>_<mux_id-1>`, dove il
  numero che cambia e' il contatore interno di ModemManager (osservati `qmapmux2_1` → `qmapmux34_1`
  sullo stesso device). Nessuna configurazione UCI puo' stabilizzarlo: **risolvi il device a runtime**
  (come fa il proto di OpenWrt, che legge `bearer.status.interface` da mmcli) e non scriverlo mai in
  un file di configurazione.
- **MM tiene la porta QMI**: se MM gira, un `qmicli` diretto deve usare `-p` (via qmi-proxy),
  altrimenti i due si contendono il nodo.

## Storage condiviso (rawdump/misc/UBoot env)

- Gli slot "liberi" **non sono tuoi**: su un device reale sono scritti da altri componenti
  (check-point, coldboot reason, log di crash). Prefissa i tuoi valori (`DL-…`, `RC-…`) e progetta la
  lettura perche' un valore **estraneo** significhi *procedi*, mai *bloccati*.
- Il contatore di una guardia anti-boot-loop **sopravvive al flash**: una serie di boot interrotti a
  meta' la fa passare in modalita' prudente **in silenzio** (osservato: percorso a 93 s invece di 37 s,
  senza nessuna modifica che lo giustificasse). Quando i tempi peggiorano senza causa nuova, leggi e
  azzera il marker prima di indagare il percorso critico.
