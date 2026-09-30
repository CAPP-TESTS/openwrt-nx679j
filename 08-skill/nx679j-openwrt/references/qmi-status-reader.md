# QMI status reader su NX679J (read-only, QRTR)

Spec completo: `experiments/qrtr/QMI-STATUS-READER-DESIGN.md`.
Sampler v1 verificato: `experiments/qrtr/qmi-status.sh`.

## Fatti QRTR che cambiano il design

- Il modem è **node 0**; ogni servizio QMI è una **porta QRTR** separata su quel node
  (verificato: 48 servizi su node 0, WDS a porta 61, nodo locale 1).
- **Non esiste un servizio CTL su QRTR**: libqmi lo implementa in locale
  (`qmi-endpoint-qrtr.c`, "We implement the CTL service here") e alloca il client id
  da un contatore locale. Quindi **nessun round trip di Allocate CID** verso il modem,
  e `ctl_port=-1` è normale (non un guasto).
- "Aprire un servizio" = NS lookup (`qrtr_node_lookup_port`) + un socket per servizio.
- QRTR **multiplexa**: ModemManager e qmicli convivono su `qrtr://0` (verificato).
  `qmi-proxy` è inutile qui. Ma due scrittori sulla WDS si pestano: il reader deve
  essere **read-only** (mai Start/Stop Network, Bind Mux Data Port, Set IP Family).
- `uqmi` **non è utilizzabile** qui: vuole `/dev/cdc-wdm*`/MBIM, e la porta è QRTR-only.

## Messaggi minimi (id dalle spec `experiments/refs/qmi-service-*.json`)

| Servizio | Msg | id |
|---|---|---|
| NAS `0x03` | Get Serving System | `0x0024` |
| NAS | Get Signal Info | `0x004F` |
| DMS `0x02` | Get Operating Mode | `0x002D` |
| WDS `0x01` | Get Packet Service Status | `0x0022` |
| DMS | Get Model / IDs / Revision | `0x0022`/`0x0025`/`0x0023` (una volta) |

Indicazioni preferibili al polling: NAS `Set Event Report 0x0002` +
`Register Indications 0x0003`. MM disabilita il poll generico quando il modem le
supporta (`MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED`); la nostra unità le
supporta (`extended signal capabilities supported` in `mm.log`).

Cadenza MM di riferimento: poll segnale 30 s (iniziale 3 s), registrazione 30 s.

## Errori che sono STATI, non guasti

- `--wds-get-packet-statistics` -> QMI error 70 `InvalidOperation` (nessuna sessione dati).
- `--wds-get-current-settings` -> QMI error 15 `OutOfCall` (chiamata giù).

## Opzioni NON compilate nella libqmi dell'unità

`--dms-uim-get-state`, `--dms-get-power-state` -> "Unknown option".
Per lo stato SIM usare `--uim-get-card-status` (UIM `0x002F`, funziona).
Probe delle capability all'avvio, non a ogni poll.

## Pitfall shell (ash busybox)

- **Non c'è `timeout`** di coreutils sull'unità.
- Un watchdog in subshell `( sleep N; kill $pid ) &` usato dentro `$( ... )`
  **blocca la command substitution** per tutto N: la subshell eredita il fd della
  pipe e `$( )` aspetta che *tutti* gli scrittori chiudano. Serve
  `( ... ) >/dev/null 2>&1 &`. Sintomo: ogni chiamata qmicli costa esattamente il
  timeout (4 s) invece di ~5 ms.
- `sleep` dell'unità non accetta frazioni di secondo.

## Costo misurato

Ciclo completo (4 invocazioni qmicli + shell): **30 ms**. A 30 s -> 0.10% di un core
vs 3.00% a 1 s. Costo vero = round trip verso l'ADSP: 8/min a 30 s contro 240/min a 1 s.
**Nessuna telemetria di potenza su questa immagine**: `/sys/class/power_supply/` è
vuoto (niente `current_now`); `soc:qcom,pmic_glink` esiste -> caricare `qcom_battmgr`
o usare un power meter USB per i watt.
