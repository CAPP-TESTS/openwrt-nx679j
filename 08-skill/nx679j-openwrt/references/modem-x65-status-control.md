# Modem X65 — strato di stato e controllo senza ModemManager (v130/v131)

Sostituisce ModemManager sul NX679J. Cinque file, nessun daemon nuovo.

## File (in immagine, iniettati DOPO i persist-tar)

| sorgente nel progetto | destinazione sul device |
|---|---|
| `luci-«redacted»-modem.rpcd` | `/usr/libexec/rpcd/luci.«redacted»-modem` (0755) |
| `luci-app-«redacted»-modem.acl.json` | `/usr/share/rpcd/acl.d/luci-app-«redacted»-modem.json` |
| `luci-app-«redacted»-modem.menu.json` | `/usr/share/luci/menu.d/luci-app-«redacted»-modem.json` |
| `luci-app-«redacted»-modem.status.js` | `/www/luci-static/resources/view/«redacted»-modem/status.js` |
| `«redacted»-link-watch.sh` | `/usr/lib/«redacted»/modem/«redacted»-link-watch.sh` (0755) |

rpcd ricarica con `/etc/init.d/rpcd reload`; l'ACL e' globale (l'oggetto ubus e' `luci.«redacted»-modem`).

## Metodi ubus

- `getStatus` — una chiamata, sei query `qmicli`: registrazione, tecnologia, operatore, RSSI/RSRP/RSRQ/SNR,
  IMEI, revision, slot SIM, ICCID, IMSI + percorso dati (ip/route/sessione/uptime) dal kernel.
- `getImsi` — solo IMSI.
- `doReconnect` — scrive `/tmp/«redacted»-link-renew.request`: il supervisor esegue il rinnovo.
- `doPin {pin}` — `qmicli --uim-verify-pin=PIN1,<pin>`. Non testato sul device di proposito:
  un PIN errato consuma uno dei 3 tentativi.

## Parser verificati sul device (qmicli 1.36)

- registrazione/tecnologia: riga `Registration state: 'x'`, campo `Radio interfaces` POI la riga `[0]: 'lte'`
- ICCID: `--uim-get-slot-status`, riga `ICCID:` **senza virgolette** (diverso dagli altri campi)
- slot: `--uim-get-slot-status`, `Card status: present|absent` (1a e 2a occorrenza = slot 1/2)
- IMSI: `--dms-uim-get-imsi` NON e' supportato (NotSupported). Serve `--uim-read-transparent=0x3F00,0x7FFF,0x6F07`,
  poi BCD (scambio nibble, salta il primo byte di lunghezza) e **prendi le ultime 15 cifre**.
- `qmicli` accetta UNA sola azione per invocazione (`too many NAS actions`).

## Supervisor `«redacted»-link-watch.sh`

- attende `DONE` in `/tmp/chain.log` prima di contare guasti (altrimenti rinnova contro la catena)
- ogni 30 s: processo sessione vivo? ogni **2** tick (60 s): `ping -I rmnet_data0` (legato all'interfaccia:
  `ping -I <addr>` NON lega)
- **Cadenza dei probe (v141, misurata)**: era ogni 5 tick = 150 s, quindi due evidenze =
  fino a **300 s solo per accorgersi** del guasto (osservato: ~3 minuti offline su un boot
  reale), più il rinnovo. Ora: probe ogni 60 s e, alla PRIMA evidenza, una conferma a 5 s
  (secondo ping) — la politica resta a 2 evidenze, ma non si aspetta un ciclo intero.
  Misurato con guasto iniettato (indirizzo tolto, sessione QMI viva): rilevato in ~38 s,
  rinnovo completato in 4 s → **recupero ~42 s** (caso peggiore ~65 s, era ~300 s).
  Costo: un probe ogni 60 s a link sano; il ping extra c'è solo quando qualcosa è sospetto.
  Trade-off accettato: una transiente di ~8-10 s può far partire un rinnovo non necessario.
- 2 evidenze di guasto -> `renew()`: kill sessioni, dpm+wds nuovi con hold 86400, L3 dalla sessione
  nuova (addr + route replace + del vecchia), `ifup wan_early`, scrive `/tmp/link-health`
- **v133 — trappola già pagata**: l'attesa del rinnovo NON deve basarsi sulla sola riga `HOLDING`.
  Su un file di log condiviso può contenere contenuto VECCHIO, il ciclo esce subito e la lettura
  dell'indirizzo fallisce -> **sessione ricreata ma L3 non applicato, device offline**.
  Serve: `: > file` prima dell'avvio (poi append) + attesa di SIA `^  IPv4 addr: ` SIA `HOLDING`
  (fino a 90 s) + su fallimento NON azzerare il contatore (ritenta al giro dopo).
- verifica del rinnovo: uccidere i processi `qmi-qrtr-observed` e osservare che l'indirizzo
  CAMBIA (prova che è stato letto dalla sessione nuova) e che il ping torna
- **v181 — TARGET DEL PROBE (27/09)**: `ping 1.1.1.1` NON risponde all'ICMP da questa rete
  (misurato: 5 target, solo 1.1.1.1 KO; 8.8.8.8 / 8.8.4.4 / 9.9.9.9 / 208.67.222.222 tutti OK).
  Con il probe su 1.1.1.1 il watcher vedeva guasto a internet perfetto → renew ogni ~74 s →
  **1641 rinnovi in ~2 giorni** → il firmware del modem si è INCASTRATO (`dpm/wds` vivi ma muti,
  `qrtr_tx_wait` nel kernel) e il `doReconnect` del pulsante non poteva più funzionare.
  Sintomo "il riconnetti non fa nulla" ⇒ è il modem, non la UI: controllare WDS/DMS.
  Fix v181: probe → `8.8.8.8` (in `«redacted»-link-watch.sh` E in `«redacted»-ui-sample.sh`,
  che mostrava PING_KO falso nella UI).
- stato leggibile: `/tmp/link-health` = `up|down <uptime> fails=N`

## Neutralizzazione di ModemManager (in `«redacted»-boot-services.sh`, prima dell'rcS)

Binari rinominati `.disabled`, init stop, `uci delete network.modem`, rimossi menu+pagina di MM.
Revert = togliere il blocco `v130`.

## Perche' MM e' stato rimosso (prove)

- tenta `reset with data interface 'rmnet_ipa0'` a ogni probe e si salva solo perche' tratta QRTR come
  `/dev/qrtr0` inesistente (log: `couldn't reset ... No such file or directory`)
- non deterministico: in un boot non crea mai l'oggetto modem (122+ `cleaning up port`)
- pagina falsa: due SIM attive con lo stesso ICCID mentre lo slot 2 e' `absent`
- 179 s contro <1 s

## Verifica rapida (dopo un boot)

```sh
ubus -v list luci.«redacted»-modem
ubus call luci.«redacted»-modem getStatus
cat /tmp/link-health ; ps w | grep -c '[M]odemManager'
```
Stress del polling (gia' eseguito, rifare se si cambia cadenza): 200 `qmicli` in 1 s, 25 letture EF_IMSI in <1 s,
nessun errore, bearer intatto.
