# ESPERIMENTO PARTI STANDARD (2026-09-20) — qmicli / ip-full / ModemManager

Tutti i test eseguiti LIVE sul NX679J (boot con catena up). Nessuna modifica distruttiva.

## 1. qmicli 1.36 (libqmi + libqrtr-glib + glib2 + libmbim)
- Installato daapk (feed 25.12, aarch64_generic) con deps risolte a mano.
- `qmicli -d qrtr://0 --dms-get-operating-mode` → `Mode: 'online'` ✓
- `qmicli -d qrtr://0 --dms-get-model` ✓, `--dms-get-ids` (IMEI) ✓
- `qmicli -d qrtr://0 --nas-get-signal-info` → RSSI -63 / RSRP -105 / RSRQ -14 / SNR -0.8 ✓
- `qmicli -d qrtr://0 --nas-get-serving-system` → registered, PLMN 222-88 WINDTRE, RAT=lte ✓
- **Conclusione: il controllo in lettura è standardizzabile. Le SCRITTURE (DMS set, DPM, WDS) da testare al prossimo ciclo catena.**

## 2. ip-full 6.18 (iproute2) — il comando rmnet upstream
- `ip link add link rmnet_ipa0 name rmnet_test type rmnet mux_id 15` → **rc=0** ✓
  - netdev creato: `rmnet mux_id 15 <INGRESS_DEAGGREGATION>` (il vendor rmnet = API netlink upstream!)
- `ip link del rmnet_test` → rc=0, pulito ✓
- **Conclusione: sostituisce il nostro `rmnet-link` per la creazione dei mux.**

## 3. ModemManager 1.24 (+dbus, lua, mm-rpcd)
- `ModemManager --debug` (40s):
  - `[filter] qrtr devices allowed: yes`
  - `loaded builtin plugin 'qcom-soc'` ✓
  - `[qrtr] added server on 0:1..0:41` = TUTTI i servizi del modem visibili (incl. `unknown [0x0049]` = IPA)
  - `[qrtr0/probe] port is QMI-capable` → `found best plugin: qcom-soc`
  - `creating modem with plugin 'qcom-soc' and '1' ports`
  - **`[wrn] couldn't create modem for device 'qcom-soc': Failed to find a net port in the QMI modem`** ← STOP
- **Causa: il plugin qcom-soc (via `peek_port_qmi_for_data_ipa`) cerca un netdev con driver `ipa`/`bam-dmux` (mainline); il nostro `rmnet_data0` ha driver vendor `rmnet` → nessun net port associato al device qcom-soc.**
- **Per adottare MM serve una patch locale del plugin** (o aliasing driver): lavoro futuro annotato.
- Nota: MM su OpenWrt è `-Dudev=false`; il qrtr-bus-watcher ha funzionato; i nodi 1/7 "not control nodes" = normali (altri nodi).

## Prossimi passi (in ordine)
1. Catena standard: mux con `ip link add type rmnet` (sostituzione già verificata); testare le scritture qmicli (DMS online, DPM open, WDS start) al prossimo ciclo.
2. Patch MM qcom-soc (net port match) per l'automazione totale — richiede build locale di MM o wrapper.
3. Immagine v65: includere qmicli+libs, ip-full, MM+dbus (già installati live; da congelare nel tar).
