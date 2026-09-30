# Cella e carrier aggregation (modem X65) — dati via QMI

Tutto quanto segue è verificato sul device (NX679J, libqmi 1.36, modem su QRTR
`qrtr://0`), salvo dove indicato.

## Trappole del device (costano ore se non si sanno)

- **OpenWrt 25.12 usa `apk`, non `opkg`**: `opkg: not found`. Installare/elencare
  con `apk update`, `apk search`, `apk info -v`, `apk add`.
- **Sul device NON esiste il comando `timeout`**: ogni `timeout N comando` fallisce
  in silenzio. Se lo si usa in uno script, lo script sembra "non ricevere risposta".
- **`qmicli` scrive i DATI su stderr**: `2>/dev/null` butta via tutto. Catturare
  sempre con `2>&1`.
- Le query NAS di sola lettura **non disturbano la sessione dati**: verificato
  (indirizzo e ping invariati dopo più interrogazioni). Usare UN comando per
  processo, senza `--device-open-sync` e senza `--device-open-net` (quelle sì
  distruggono lo stato della sessione: SYNC invalida i client CID allocati).

## Dati della cella (chiavi `cell.*`)

Script: `/usr/lib/nx679j/modem/nx679j-cell.sh` (emette key=value per la UI).
Comandi:

- `qmicli -d qrtr://0 --nas-get-rf-band-info` → `Active Band Class` (es. eutran-1),
  `Active Channel` (EARFCN), `Bandwidth` (MHz). **Se la CA multi-banda è attiva,
  compaiono PIÙ blocchi "Band Information"** — è il modo più semplice per vederla.
- `qmicli -d qrtr://0 --nas-get-cell-location-info` → cella servente (Serving Cell
  ID = PCI, EARFCN, RSRP/RSRQ/RSSI in decimi di dB), celle vicine intrafrequenza,
  e le interferquenza con le loro celle misurate.
- `qmicli -d qrtr://0 --nas-get-signal-info` → aggregato LTE (RSSI/RSRQ/RSRP/SNR).

## Carrier aggregation per portante (`--nas-get-lte-cphy-ca-info`)

Messaggio QMI NAS **0x00AC**. Dà per ogni portante: **PCI, EARFCN (RX Channel),
larghezza DL, banda, stato** (`activated` / `deactivated` / `deconfigured`) e
indice. **Non dà RSRP/RSRQ per portante.**

- La CA si vede **solo con traffico attivo**; in idle le secondarie risultano
  deactivate/deconfigured.
- Errore QMI 74 `InformationUnavailable` = **nessuna CA allocata** (non è un guasto);
  94 `NotSupported` = comando non supportato dal firmware.
- **RSRP per portante si ricava incrociando i PCI** del CA-info con quelli di
  `--nas-get-cell-location-info`.

### Il comando manca nei pacchetti OpenWrt (risolto ricostruendo)

`qmicli` di OpenWrt è compilato con una *collection* ridotta:

```
-Dcollection=$(if $(CONFIG_LIBQMI_COLLECTION_MINIMAL),minimal,
              $(if $(CONFIG_LIBQMI_COLLECTION_BASIC),basic,full))
```

Con `minimal`/`basic` il binario ha **solo 11 comandi `--nas-get-*`** e NON conosce
la CA (`error: Unknown option`); lo si verifica con
`strings /usr/bin/qmicli | grep -c nas-get`. Il default di libqmi, **se nessuna
delle due opzioni è attiva, è `full`** → basta ricostruire il pacchetto con l'SDK
senza toccare `.config` (via `./scripts/feeds update -a && ./scripts/feeds install
libqmi && make package/libs/libqmi/compile`).

## Contributo di ogni portante: cosa è onesto dire

**Non è misurabile dal sistema operativo.** L'aggregazione avviene dentro il
modem: il kernel vede una sola `rmnet_dataX` per bearer/PDN e i contatori
(rmnet, IPA, QMI WDS) sono aggregati — verificato su tre fonti indipendenti.
Non esiste `/dev/diag` in questa build (solo il gadget USB f_diag).
L'unica cosa mostrabile è una **stima dichiarata**: peso ∝ larghezza di banda
della portante (eventualmente pesata con la qualità quando disponibile).
Dire "stima" nella UI, non spacciarla per misura.

## Come e' cablato (v166, verificato end-to-end)

1. `/usr/lib/nx679j/modem/nx679j-cell.sh` produce le chiavi `cell.*` e `ca.*`
   (query QMI, sola lettura). **Deve stare nella lista file di `build-v90.py`**,
   altrimenti vive solo come copia a mano e sparisce al flash successivo.
2. Il **fetcher** (`nx679j-ui-fetch.sh`) lo chiama **ogni 10 cicli (~30 s)**
   dentro il blocco che compone `$T` -> `$D=/tmp/ui-data.txt`, e mette il
   risultato in cache in `/tmp/ui-cell.txt` (le chiavi si vedono anche nei cicli
   in cui le query non girano).
3. La pagina modem (`ui-5-pages.c`, `draw_modem`) disegna `Aggreg.` / `PCC` /
   `SCC1` sotto la riga `Salute`.
4. **RSRP per portante**: si incrocia il PCI della SCC con le misure di
   `--nas-get-cell-location-info` (funzione `rsrp_di_pci` nello script).
5. Il nome banda nel fix string e' `B%s` con `%s` = `eutran-3` -> legge
   "Beutran-3": va tolto il prefisso `eutran-` per mostrare "B3".

## Gate del flash: atteso del backup

`nx679j-flash-v128.sh` confronta il md5 della partizione **con un valore
hardcoded** e rifiuta di scrivere se non combacia. Va aggiornato **a ogni flash**
col contenuto reale del device (`BACKUP_MD5` che lo script stesso stampa).
Se il gate non riconosce il contenuto attuale: **si aggiorna il valore atteso,
non si aggira il controllo** — e' l'unica cosa che ha impedito di scrivere
immagini sbagliate.
