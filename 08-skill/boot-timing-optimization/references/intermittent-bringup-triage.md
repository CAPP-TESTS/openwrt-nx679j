# Triage di un bring-up intermittente (firmware sottosistema su kernel vendor)

Contesto: un sottosistema (modem/DSP) che in alcuni boot **non sale affatto** e non si recupera senza
riavvio. Ordine di lavoro: (1) misura il tasso, (2) leggi il log del driver, (3) distingui le cause
con gli errori esatti, (4) solo dopo prova una contromisura. Tutto cio' che segue e' verificato su un
kernel vendor 5.10 con remoteproc PAS di Qualcomm, ma le regole sono di metodo.

## 1. Risolvere il nodo per NOME, mai per indice

L'ordine di enumerazione cambia ad ogni boot: un indice fisso dentro un ordine variabile produce un
guasto intermittente che sembra hardware.

```sh
# esempio remoteproc: la stessa logica vale per input, net, mhi, pci
mss_path() { for r in /sys/class/remoteproc/remoteproc*; do
  case "$(cat "$r/name" 2>/dev/null)" in *<identificatore-del-sottosistema>*) echo "$r"; return;; esac
 done; }
mss_state() { p=$(mss_path); [ -n "$p" ] && cat "$p/state"; }
```

Dopo la correzione, logga il **path risolto** accanto allo stato: e' la prova che la risoluzione
avviene per nome e non per indice, e rende immediato accorgersi se un giorno il nome cambia.

## 2. `echo start > .../state` che "non funziona": quali errno contano

Dalla sequenza di `remoteproc_sysfs`: scrittura di `start` su un rproc gia' `RPROC_RUNNING` ⇒ errore
**senza alcun log** (spesso `-EBUSY`), e da quel momento **ogni** scrittura resta in errore finche'
qualcuno non lo ferma ⇒ il sintomo "serve il reboot". Firme da distinguere in `dmesg`:

| firma | significato | rimedio |
|---|---|---|
| errore, **nessun log** | gia' `RUNNING` (`-EBUSY`) | `echo stop > state`, poi `start`; altrimenti reboot |
| `Boot failed: -110` / `start timed out` | il firmware non si presenta entro il timeout | riprova una volta; se persiste, il peripheral lato secure e' in stato incoerente |
| `panic` immediato nel cleanup | il vendor non esegue lo shutdown del peripheral sul timeout (upstream si') ⇒ stato secure "autenticato" residuo | **reboot**: e' l'unico rimedio in quel ramo |
| `recovery_disabled = true` (default di molte drop vendor) | un crash del sottosistema diventa `panic`, non recovery | nessun self-heal da attendersi: prevedere il riavvio |

Triage in una riga (distingue le cause senza toccare nulla):
```sh
dmesg | grep -E "q6v5|Boot failed|PAS Shutdown|start timed out|auth and reset|load_state|watchdog received|releasing"
```
**Lo stato letto da sysfs non e' una prova**: e' un campione istantaneo e un bit di "ready" puo'
restare settato dopo lo stop. Il segnale buono e' una **richiesta reale** che ritorna il dato atteso
(transazione sul bus, ping sul canale), non la presenza del nodo o dello stato `running`.

## 3. A/B su come si esce: reboot sporco vs stop pulito

Se il sospetto e' che il sottosistema sopravviva al riavvio in uno stato incoerente, il test e' un
A/B alternato nella stessa campagna: boot pari = `reboot -f`, boot dispari = **stop pulito del
sottosistema** prima di uscire (`echo stop > <rproc>/state`, dopo aver fermato i consumatori).
Registra per ogni boot: stato prima, esito, stato dopo, e le righe `dmesg` del bus/firmware. Se
l'esito non correla col modo di uscita, l'ipotesi e' morta: cerca altrove (tipicamente
nell'identificatore fisso del punto 1) invece di insistere.

## 4. Il layer di integrazione e' spesso il vero collo di bottiglia

Il sottosistema puo' essere pronto molto prima di quando l'utente lo vede: separa sempre "hardware
pronto" da "integrazione pronta" e misura entrambe. Fatti riutilizzabili su OpenWrt + modem Qualcomm:

- **`mmcli --scan-modems` su OpenWrt non fa nulla**: la build e' senza `udev` e la scansione manuale
  risponde "unsupported". La via corretta per far notare subito un device appena creato e'
  `mmcli --report-kernel-event="action=add,subsystem=net,name=<iface>"` **piu' i port del device**
  (interfaccia di rete + port di controllo QMI: `cdc-wdm*`/`wwan*qmi*`). Una "fix" basata sulla
  scansione non produce guadagni e non va tenuta: non ha effetto, non peggiora.
- **Il daemon apre il port QMI attraverso un proxy** con un **timeout di decine di secondi**: se il tuo
  script tiene il nodo QMI occupato mentre il daemon parte, l'apertura si blocca. E' una causa
  plausibile dei tempi lunghi attribuiti al "probe", e si verifica guardando l'ordine degli accessi,
  non il daemon.
- **Versioni recenti riprovano da sole sui port aggiunti in ritardo** (teardown+ricreazione dopo
  ~2 s): su quelle il `kill`+restart forzato e' inutile. Su versioni precedenti il restart forzato
  resta **necessario** (rimuoverlo ha peggiorato: 186 s contro 135 s) perche' il primo probe avviene
  quando l'interfaccia dati non esiste ancora e il device viene scartato. Verifica la versione prima
  di toccare il workaround.
- **I messaggi "checking if the daemon is available" possono essere di uno script di shell**, non del
  daemon: su OpenWrt e' un loop `n=60 × 1 s` di replay degli eventi in cache. Se il daemon non compare
  entro quel tempo, gli eventi **non vengono mai consegnati** e il device resta invisibile. Non
  attribuire al daemon un tempo che e' dello script.
- Una notifica di evento inviata **prima** che il daemon esista fallisce (socket non ancora creata) e
  l'evento va perso: il daemon ricade sul proprio ciclo di scansione. Re-inviare l'evento dopo l'avvio
  e' corretto ma **puo' non spostare nulla** (misurato): registralo come tentativo a guadagno zero.
- **La notifica si manda UNA volta, poi si lascia correre la finestra di probe.** Un daemon di discovery
  e' una macchina a stati **a finestra**: ogni evento o restart in piu' la riazzera. Misurato: ripetere
  la notifica ogni 2 s per 16 s ha portato l'interfaccia a **184 s contro 135-142 di baseline** — peggio
  di non fare nulla. Dopo la notifica si **aspetta** il ciclo di poll dello script (`sleep 5`), non si
  insiste. Vale identico per i restart (`kill`+start ripetuti = stesso danno). "Forzare il re-probe" a
  martello non e' una strategia: se il primo tentativo non basta, il problema e' **cosa** notifichi e
  **quando**, non quante volte.

## 5. Quando l'interfaccia utente mente

Un errore JavaScript della UI puo' mascherare un problema di rete e viceversa. Se la pagina di
controllo mostra un sintomo di rete, **verifica prima il dato con lo strumento di stato**
(`ifstatus <iface>`, `ubus call network.interface.<iface> status`) e controlla la console del
browser: in LuCI e' noto un `ReferenceError: View is not defined` in `ui.js` (presente in piu'
release stabili, corretto solo su master) che rompe le pagine basate su viste JS, lasciando la pagina
bloccata sull'errore invece di mostrare i dati. Si mitiga con una patch di una riga in
`/www/luci-static/resources/ui.js` + svuotando la cache del browser — e vale la regola generale:
**non inseguire la rete su un sintomo che viene da un errore di rendering.**

La patch upstream sostituisce il guard rotto con un duck-type check sul metodo che ogni vista valida
implementa (`!(view instanceof View)` → `typeof view?.render !== 'function'`). Due trappole di
applicazione, entrambe costate un ciclo:

- **Verifica che la patch sia DAVVERO nell'artefatto.** Un file patchato a mano dentro una radice di
  build puo' non essere quella impacchettata: la build passa senza errori e il device resta col bug.
  Dopo il flash, `grep` il pattern nuovo **sul device**; se manca, la radice di build e' un'altra.
- **Con rootfs volatile (chroot/ramdisk/overlay rigenerato) la patch va applicata a OGNI boot** dallo
  script di avvio — `[ -f "$F" ] && grep -q <vecchio> "$F" && sed -i "s/<vecchio>/<nuovo>/" "$F"` —
  perche' nel filesystem non sopravvive. Subito dopo svuota la cache delle viste
  (`/tmp/luci-indexcache*`) e **logga l'esito**: senza il log non saprai se ha attecchito.

## 6. Acceleratori del firmware: guarda cosa fa il sistema di riferimento

Un pezzo di hardware puo' richiedere un'azione userspace per essere inizializzato (firmware caricato
con una scrittura su un nodo dedicato, un servizio che deve girare *prima* del bring-up). Confronta
con il sistema funzionante di riferimento (lo stock Android dello stesso device): se li' l'init scrive
su un nodo o avvia un servizio in un ordine preciso, quel passaggio e' parte del contratto — e la sua
assenza e' un candidato per i fallimenti intermittenti.
