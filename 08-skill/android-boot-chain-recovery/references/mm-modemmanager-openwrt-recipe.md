# ModemManager su OpenWrt + kernel vendor Qualcomm (qcom-soc) — ricetta NX679J

Ricetta verificata live (NX679J SM8450, OpenWrt 25.12.5, kernel vendor 5.10.66, MM 1.24-r8 patchato, 2026-09-21).
Risultato: modem X65 visibile e gestito da MM (initialized, registered LTE+5G 68-80%) MENTRE la catena dati custom resta viva.

## TL;DR problemi & soluzioni

| # | Problema | Soluzione |
|---|---|---|
| 1 | "Failed to find a net port" | regola in `/lib/udev/rules.d/80-mm-nx679j.rules` (prefisso 77/78/79/**80**-mm-!) |
| 2 | "port filtered: virtual device" | patch `mm-filter.c` (swap blocchi net/virtual) |
| 3 | WDA/CTL data-format resetta l'endpoint QRTR | patch `mm-port-qmi.c` (skip data-format sui port QRTR) |
| 4 | netdev sbagliato scelto alfabeticamente | `rmnet_data0` con `ID_MM_PORT_IGNORE=1` (solo `rmnet_ipa0` associato) |
| 5 | "No modems" subito dopo il start | ASPETTARE ~60-90s: MM fa 5-8 cicli di retry (i reset qrtr durante il probing sono NORMALI e convergono) |
| 6 | connect: "No error information" da libqmi | errore REALE = **EBUSY mascherato da musl** (`strerror(-16)`) + mux fantasma → patch libqmi **106 probe mux** (vedi RISOLUZIONE FINALE) |
| 7 | connect: `call-already-present` | una sola WDS per porta EMBEDDED: killare la wds-session della catena prima del connect MM |
| 8 | boot: "couldn't find modem" / link port timeout | MM restart FORTE (kill -9+10s+start) dopo l'inject; `netlink-watch` annuncia i netdev runtime (S71 nella lista del wrapper!) |

## ✅ RISOLUZIONE FINALE — `ifup modem` standard funziona end-to-end

**Verificato**: `ifdown modem && ifup modem` → UP in ~3s, ping 1.1.1.1 0% loss, DNS ok; flusso 100% automatico al boot (log: chain DONE → kill WDS → inject → MM restart → `OK: iface modem UP` → `OK: ping`).

**Le tre cause vere del blocco "link port" (in ordine di scoperta):**
1. **Mux "fantasma" EBUSY nel driver vendor.** Dopo un tentativo fallito il driver tiene il mux id occupato anche senza netdev esistente; `get_first_free_mux_id` di libqmi sceglieva per NOME e ricadeva sempre sullo stesso mux. Fix = **patch libqmi 106 (probe mux)**: roundtrip reale create+delete su ogni mux candidato prima di sceglierlo (`Using dynamic mux ID 3` nel log). Trappola: **musl `strerror(-16)` = "No error information"** — l'errore che sembrava "nessun errore" era un EBUSY mascherato; il valore vero si legge rispedendo l'hex del messaggio con un tool raw e leggendo l'ACK signed. (Il messaggio netlink di libqmi era già byte-corretto — dimostrato rispedendolo da un processo esterno: ACK −16.)
2. **Una sola WDS per porta EMBEDDED**: la wds-session della catena fa fallire il connect MM con `call-already-present`. Fix: killare SOLO la wds-session della catena (pid in `/tmp/wds-session.pid`) dopo che il datapath IPA è pronto; il resto della catena resta.
3. **Al boot il primo probe di MM scarta il modem** (rmnet_ipa0 non esiste ancora al suo start) e l'evento iniettato dopo non lo recupera: serve **restart FORTE** = `kill -9` di tutti i ModemManager + pausa 10s + `/etc/init.d/modemmanager start` (il semplice restart è troppo rapido: il port QRTR non si libera), poi poll `mmcli -L` e `ifup`.

**Componenti del flusso automatico (nel rootfs):**
- `/usr/lib/nx679j/modem/nx679j-mm-standard-boot.sh` — poll chain DONE → kill wds → inject evento `rmnet_ipa0` → MM restart forte → poll modem → `ifup modem` → log in `/tmp/mm-standard-boot.log` (+ sync NTP).
- `netlink-watch` v3 (servizio **S71** — deve stare nella lista esplicita di servizi del wrapper, altrimenti non parte): annuncia a MM i netdev creati a runtime. Senza: `Timed out waiting for link port`.
- libqmi patch 100–106, MM patch 0100–0204, proto standard `modemmanager` con `device=any` e `auto=0` (voluto: l'ifup lo fa lo script dopo la catena; in LuCI appare "Not started on boot" = informativo).

## La regola corretta (fix #1 + #4)

Il parser integrato di MM (build `-Dudev=false`) carica **solo file con prefisso `77-mm-` `78-mm-` `79-mm-` `80-mm-`**.
Per i netdev virtuali `DRIVERS==` NON matcha (nessun symlink driver): usare `KERNEL==`. Una azione per riga.

`/lib/udev/rules.d/80-mm-nx679j.rules`:
```
SUBSYSTEM=="net", KERNEL=="rmnet_ipa0", ENV{ID_MM_PHYSDEV_UID}="qcom-soc"
SUBSYSTEM=="net", KERNEL=="rmnet_data0", ENV{ID_MM_PORT_IGNORE}="1"
```
`rmnet_data0` va IGNORATO per due motivi: (a) `mm_base_modem_get_best_data_port` sceglie il primo net port **in ordine alfabetico** (data0 < ipa0) e tenterebbe mux SOPRA un figlio mux; (b) per il bearer MM deve operare sul main iface (`rmnet_ipa0`).

## Patch MM (build SDK 25.12.5 armsr/armv8)

- `0100-nx679j-allow-virtual-net.patch`: `mm-filter.c` — spostare il blocco "net devices always allowed" PRIMA del "virtual device".
- `0101-nx679j-qrtr-skip-dataformat.patch`: `mm-port-qmi.c` case `PORT_OPEN_STEP_SETUP_DATA_FORMAT` — se `self->priv->node` (QRTR) salta il machinery data-format (`ctx->step = PORT_OPEN_STEP_LAST`). Sul X65 la negoziazione WDA/CTL fallisce e resetta l'endpoint.
- `0102-nx679j-rmnet-data-port.patch`: `src/plugins/qcom-soc/mm-broadband-modem-qmi-qcom-soc.c` `peek_port_qmi_for_data()` — driver NULL + nome `rmnet*` → riusa il ramo `peek_port_qmi_for_data_ipa()`.

Build: `make package/modemmanager/clean && make package/modemmanager/compile`; output in `bin/packages/aarch64_generic/packages/`.
Al reinstall installare il set completo SDK insieme (dependency churn) e **riscrivere la regola 80-mm-nx679j.rules** (sparisce col pacchetto).

## 🔬 SESSIONE 2026-09-22 NOTTE — DATAPATH MM: PROGRESSO GIGANTESCO (mistero ristretto)

### Patch libqmi portate e attive (in `qti-patches/` del progetto + `feeds/packages/libs/libqmi/patches/`):
- `100-fix-nlmsg-data-hdr.patch` — BUG UFFICIALE libqmi: `err = NLMSG_DATA(buf)` → `(hdr)` in qmi-net-port-manager-rmnet.c (~riga 410 in 1.36.0). Senza: lettura spuria dell'ACK.
- `101-…-netlink-zero-ifi.patch` — azzera `ifi_type`/`ifi_change` (0xFFFFFFFF rompeva il vendor).
- `102-…-netlink-sendto-kernel.patch` — sendto esplicito con `sockaddr_nl` kernel (g_socket_send usa sendto NULL addr).
- `103-…-netlink-sock-raw.patch` — SOCK_DGRAM → SOCK_RAW|SOCK_CLOEXEC.
- `104-debug-nlqmi-hexdump.patch` — dump hex in /tmp/nlqmi.txt (debug, rimuovere a fine lavoro).

### Patch ModemManager (già in feed net/modemmanager/patches/):
- `0100` allow virtual net; `0101` QRTR skip dataformat SOLO se net_driver≠ipa; `0102` rmnet→ipa nel plugin qcom-soc;
- `0200` set_net_details: NULL→"ipa"; `0201` bearer: NULL→"ipa" (multiplex REQUIRED).

### Catena di blocchi RISOLTI (in ordine):
1. "Unsupported QMI kernel driver" → 0102+0200 ✓
2. "Multiplexing required but not supported" → 0201 + 0101-fix ✓
3. "No more data format combinations" → 0101-fix (max_multiplexed_links) ✓
4. netlink EBUSY/EEXIST vari → cleanup ✓
5. "Netlink message failed: No error information" → patch 100 (bug libqmi) ✓
6. "Timed out waiting for link port 'net/qmapmuxX.0'" → **RISOLTO** (dettagli nella RISOLUZIONE FINALE in testa): il messaggio netlink di MM era già corretto (l'hex esatto rispedito da un processo esterno crea il netdev); le cause vere erano il **mux fantasma EBUSY** (musl maschera `strerror(-16)` come "No error information"), la **WDS della catena** che occupa l'unica sessione EMBEDDED, e l'annuncio del netdev a MM (netlink-watch).

### Tool di test sul device (creati):
- `/tmp/rmnet-link` (raw, sempre funziona), `/tmp/rmnet-link-nonul`, `/tmp/test-gsock{,2,3,4}` (G_SOCKET + varianti NUL/delay).
- Node QRTR modem: `qrtr://0` (qmicli diretto OK); lookup servizio: `/tmp/qmi-qrtr lookup 14`.
- Sessione catena: `/tmp/qmi-qrtr-observed wds-session internet.it 4 1 1 3600` (ep EMBEDDED=4/iface1/mux1) — CONFERMATO.

### Note operative:
- Reboot ripristina il binario MM dal ramdisk → ricopiare SEMPRE da /tmp/MM.new dopo ogni riavvio.
- Processi MM si accumulano zombie (9!): pulire con `for p in $(pidof ModemManager); do kill -9 $p; done` + restart.
- Hotplug inject: `ACTION=add DEVPATH=/devices/virtual/net/rmnet_ipa0 INTERFACE=rmnet_ipa0 sh /etc/hotplug.d/net/25-modemmanager-net`.
- MM vede il modem ~60-90s dopo l'inject; il connect richiede ~30s; leggere sempre `/var/log/mm.log` (DEBUG attivo).
- libqmi device: `libqmi-glib.so.5.11.0` (1.36.0), build SDK: `make package/libqmi/clean && make package/libqmi/compile`.

## ✅ PATCH TROVATE (2026-09-22, ricerca 10 agenti) — PIANO DEFINITIVO

**Fonti reali (scaricate e verificate in `qti-patches/`):**
- **qualcomm-linux/meta-qcom** `.../recipes-connectivity/modemmanager/files/`: 6 patch QTI datapath (raw.githubusercontent, branch master): `0001-port-qmi-add-BAM-DMUX-DPM-support-and-fix-QRTR-WDA.patch` (= MR!1452, contiene "unrecognized net driver → EMBEDDED/0" riga 125 + retry WDA senza endpoint TLV su InvalidArgument + DPM bam-dmux), `0001-qcom-soc-add-QRTR-MHI-based-modem-support.patch` (= MR!1443, template branch driver), `0002-base-modem-allow-QMI-modem-creation-without-net-port.patch`, `0003-bearer-qmi-use-BindMuxDataPort-for-BAM-DMUX-WDS-client.patch`, `0004-plugins-qcom-soc-send-DPM-open-port-during-enabling.patch`, `0005-plugins-qcom-soc-replace-sio_port_per_port_number.patch`.
- **fork meizu-m2172-mainline/ModemManager** (athbe): commit 95882e2 "qcom-soc: fall back to generic QMI data port lookup" (sostituisce l'errore con parent_class->peek_port_qmi_for_data; MA il parent accetta solo qmi_wwan/mhi_net → da soli sposta l'errore, il nostro driver è NULL!); commit 9e407303 tag `ID_MM_QMI_FIXED_MUX_ID` (bind WDS su interfaccia principale, "Since 1.26"); commit 18105f69 tag `ID_MM_QMI_DEFAULT_MULTIPLEX`; branch `sdx55m-mhi-wds-mux-1.24`.

**PARAMETRI VERIFICATI SUL NOSTRO X65 (dalla catena funzionante, `/tmp/wds-session.log` + `qmi-qrtr-observed wds-session`):**
`wds-session internet.it 4 1 1 3600` ⇒ **ep-type=4=EMBEDDED, ep-iface=1, mux-id=1, ipfam=4** — e MM upstream per EMBEDDED usa interface_number=1 di default ⇒ **i default MM sono GIÀ corretti per il nostro modem!** (il commento QTI 0005 che dice EMBEDDED/0 vale per altri SoC: il NOSTRO vuole /1).

**PATCH NECESSARIA PER IL NOSTRO CASO (il минимум che nessuno ha pubblicato — rmnet_NULL driver):**
1. Plugin `mm-broadband-modem-qmi-qcom-soc.c` `peek_port_qmi_for_data()`: aggiungere il branch per net_port_driver NULL o rmnet* → `peek_port_qmi_for_data_ipa()` (che usa EMBEDDED/1) + udev rule `SUBSYSTEM=="net", KERNEL=="rmnet_ipa0", ENV{ID_MM_QCOM_SOC}="1", ENV{ID_MM_PHYSDEV_UID}="qcom-soc", GOTO="mm_qcom_soc_process"` (DRIVERS== non matcha con driver NULL! usare KERNEL== + ATTRS).
2. Dalla 0001-port-qmi QTI: hunk retry WDA senza endpoint TLV su `QMI_PROTOCOL_ERROR_INVALID_ARGUMENT`.
3. Dalla 0002: mm_port_qmi_set_net_driver hooks (per far sapere il driver al QMI port quando il netdev arriva dopo).
4. Se il WDS Start chiede il bind obbligatorio: dalla 0003 il pattern BindMuxDataPort; o portare il tag `ID_MM_QMI_FIXED_MUX_ID` (meizu) con regola udev `ENV{ID_MM_QMI_FIXED_MUX_ID}="1"`.
5. Verifica on-device: `qmicli -d qrtr://0 --dms-get-manufacturer` FUNZIONA (node=0!); `qmi-qrtr lookup 14` per trovare il node; il nostro tool fa BIND(0x00a2)+SetIPFamily(0x004d)+Start(0x0020) in un processo (MM farà lo stesso).

**NOTA qmicli test manuale:** invocazioni qmicli SEPARATE non funzionano senza qmi-proxy (il CID muore con la socket: "Unknown client 1"); su OpenWrt il binario qmi-proxy non è installato (c'è qmicli 1.36.0, qmi-network, qmi-firmware-update). Per test puri via qmicli serve compilare qmi-proxy o usare il nostro qmi-qrtr.

## Eventi hotplug (fix complementare)

**La iface `/etc/config/network` `modem` usa ORA il proto STANDARD `modemmanager`**: `option device 'any'`, `apn 'internet.it'`, `iptype 'ipv4'`, `auto 0`. In LuCI: card "Protocol: ModemManager" + errore strutturato netifd (MM_CONNECT_FAILED) invece del guscio vuoto. Pagina **Status→Cellular Network** (view standard `modemmanager/status` @luci 128a7812): WINDTRE/22288, lte+5gnr, registered/home, IMEI, ICCID/IMSI, cell location — TUTTO via componenti standard.

**Installazione LuCI standard — metodo apk (preferito)**: `apk add luci-proto-modemmanager` — il pacchetto installa `modemmanager_helper.js`, `protocol/modemmanager.js`, `view/modemmanager/status.js` e il menu `admin/status/modemmanager` (Status→Cellular Network). **Prerequisiti apk** (vedi Trappole apk): orologio via NTP e feed su http. **Allineare le versioni**: un pacchetto nuovo su base vecchia (es. 26.263 su 26.180) produce il banner `ReferenceError: View is not defined` su tutte le pagine → `apk upgrade` di TUTTI i `luci-*` alla stessa versione (backup dei file LuCI prima). **Verifica della UI senza chiedere screenshot**: `opencli doctor` (profili connessi) poi `opencli --profile <nome> browser <session> open/state/console`. Fallback storico se apk non è usabile: scaricare i file da raw.githubusercontent **openwrt/luci @COMMIT_ESATTO** (commit della LuCI del device; `git describe`). Senza `modemmanager_helper.js` il require fallisce → "Unsupported protocol type"; con `auto=0` la card mostra "Not started on boot" = **informazione**, non errore (l'ifup lo fa lo script di boot).

**Inject per far vedere il modem a MM dopo il boot** (i retry da soli NON bastano se la catena raw occupa già il QMI): `mmcli --report-kernel-event="action=add,name=rmnet_ipa0,subsystem=net"` → modem creato (2 port: qrtr0+rmnet_ipa0). Formato da `mm_report_event` in `modemmanager.common`. Il connect MM puro ORA FUNZIONA end-to-end (vedi RISOLUZIONE FINALE): `ifup modem` standard → UP in ~3s → ping 0%; la catena custom serve solo al setup del datapath (IPA/ingress), non più ai dati.

## Eventi hotplug (fix complementare)

MM su OpenWrt riceve gli eventi port via `mmcli --report-kernel-event`. Gli script `/etc/hotplug.d/{net,tty,wwan}/25-modemmanager-*` li inoltrano, ma `modemmanager.common` scarta i virtuali salvo eccezioni: aggiungere `"rmnet"*)` (senza return). I netdev creati PRIMA del start di MM richiedono o un restart MM (wrapper+replay) o inject manuale.

## BEARER DATI via MM (ricetta 4 patch — NON ancora applicata, da ricerca 2026-09)

Nessuna soluzione upstream esiste. MM 1.24/main: il datapath è CABLATO al driver del netdev (`ipa`/`bam-dmux`); driver NULL → errori. Patch minima raccomandata (tutte modellate su MR upstream aperte):

1. **plugin** (`mm-broadband-modem-qmi-qcom-soc.c`): ramo vendor in `peek_port_qmi_for_data()` che riusa `peek_port_qmi_for_data_ipa()` e FORZA `out_endpoint->type=QMI_DATA_ENDPOINT_TYPE_EMBEDDED(4)`, `interface_number=1` (valori stock X65). Modello: MR !1452 (fallback EMBEDDED per driver non riconosciuto su QRTR) https://gitlab.freedesktop.org/mobile-broadband/ModemManager/-/merge_requests/1452
2. **mm-port-qmi.c**: trattare il driver come `ipa` in `load_current/supported_kernel_data_modes` (→ MUX_RMNET) e negli step DPM (o SALTARLI: la catena apre già il DPM; `rx/tx_endpoint_id` non leggibili da sysfs su netdev virtuale).
3. **mm-bearer-qmi.c** `load_settings_from_bearer`: multiplex REQUIRED per questo driver (o portare i tag meizu `ID_MM_QMI_DEFAULT_MULTIPLEX`/`ID_MM_QMI_FIXED_MUX_ID`; alternativa: `--test-multiplex-requested`).
4. **regole**: come sopra (solo rmnet_ipa0; data0 ignore).

Sequenza bearer qcom-soc (per debug): best_data_port → peek_port_qmi_for_data (check driver!) → OPEN_QMI_PORT → SETUP_DATA_FORMAT (WDA client, kernel data modes driver-keyed, DPM open SOLO se driver==ipa, GET/SYNC WDA, check combinazione QMAPv5→v4→QMAP→RAW_IP) → SETUP_LINK (libqmi RTM_NEWLINK rmnet, INGRESS_DEAGGREGATION sempre forzato — compatibile kernel vendor) → WDS client → BIND_DATA_PORT (mux) → SET_NETWORK → start → config IP/DNS/MTU sul link.

Rischio residuo: formato dati lato firmware X65 (mai verificato end-to-end da terzi). Piano B (stato attuale): MM per il controllo, la catena custom WDS/DPM/rmnet per i dati.

## Regressione MSS (modem "not present") e recupero catena — misurato

**Sintomo**: dopo molti reset hardware/reboot bruschi il modem sparisce (`mmcli` "couldn't find modem", LuCI/MM "not present"). dmesg: `remoteproc remoteproc0: releasing 4080000.remoteproc-mss` a ~19 s (probe fallito) e il MSS **non compare affatto** in `/sys/class/remoteproc/*` (se `remoteproc3` e' `spss`, non e' lui). `/tmp/chain.log`: `MSS=offline` → `STOP bootstrap` (bootstrap rc=1).

**Fix primario**: un **reboot pulito** ri-registra il MSS (osservato due volte) — prima di diagnosticare oltre, riavviare. Variante leggera: MSS registrato ma `offline` → il bootstrap lo avvia da solo (`MSS=running`); il problema serio e' solo quando il probe l'ha rilasciato del tutto.

**Recupero a boot avviato (catena saltata)** — procedura ripetuta piu' volte, sempre riuscita:
1. Replicare i symlink del servizio: `D=$(ls -d /usr/lib/*/modem); for f in "$D"/*; do b=${f##*/}; [ -e /tmp/$b ] || ln -s "$f" /tmp/$b; done`.
2. `setsid sh "$D/chain.sh" >/tmp/chain.out 2>&1 &` → il bootstrap avvia il MSS.
3. Il **dms-check fallisce per finta** durante il bootstrap (`dms rc=1 finished=0` nello chain.log): aspettare `READY_FOR_OBSERVED_DMS_TEST` in `/tmp/observed-bootstrap.log`, poi rilanciare a mano `sh "$D/openwrt-dms-observed-check.sh"` (crea il suo gate) e rilanciare `chain.sh`: salta i gate `once` già soddisfatti e prosegue prepare→dpm→ingress→data→cellular fino a `DONE` (~250-270 s totali).
4. Il boot script `...-mm-standard-boot.sh`, se ancora vivo, completa da solo: wds stop → inject `rmnet_ipa0` → MM restart forte → `ifup modem`; altrimenti rilanciarlo. **Verifica**: `ubus call network.interface.modem status` → `"up": true` + `ping 1.1.1.1` 0% loss.

**Nota generale**: una catena a stadi con gate `once` è ri-eseguibile senza danni; concludere "il check è rotto" prima che il produttore (bootstrap) abbia finito manda la diagnosi dalla parte sbagliata.

## Build del pacchetto (SDK OpenWrt)
```bash
# SDK 25.12.5 armsr/armv8 (stesso target dei pacchetti aarch64_generic)
make defconfig && make package/modemmanager/compile -j$(nproc)
# le patch in package/feeds/packages/modemmanager/patches/ si applicano da sole
# dopo aver modificato le patch: make package/modemmanager/clean prima del compile
```

## Trappole apk
- `apk add --force-reinstall` che fallisce le dipendenze **rimuove il pacchetto e la sua pila**: lanciare sempre l'intero set insieme.
- Dopo il reinstall va riscritta la regola `/lib/udev/rules.d/80-mm-nx679j.rules`.
- **Orologio prima di tutto**: il device parte al 1970 e ogni TLS/apk muore con "SSL error"/wget error — `ntpd -nq -p pool.ntp.org` (se fallisce con `Alarm clock`/`bad address` — orologio assurdo o DNS giù — impostare l'ora dal host: `date -s '<YYYY-MM-DD HH:MM:SS>'`, più affidabile in quel momento; `@<epoch>` è l'alternativa) PRIMA di qualunque `apk update`; la sync NTP è stata aggiunta allo script di boot.
- **Feed su http**: anche con l'ora giusta il wget busybox di questa build non completa il TLS verso downloads.openwrt.org → `sed -i 's|https://|http://|' /etc/apk/repositories.d/distfeeds.list` (i pacchetti restano firmati e verificati da apk; ~11k pacchetti invece di 236).
- **Dopo un re-flash dell'immagine**: NTP, feed http, `apk add luci-proto-modemmanager` e l'allineamento versioni vanno rifatti — o meglio inclusi nel tar del nuovo boot.img (**ora implementato**: i tar di persistenza etc/luci/tools vengono estratti nella build prima del cpio; vedi `nx679j-project-state.md`). Un pacchetto installato a runtime nella rootfs volatile sparisce al primo reboot: è la causa tipica del `Unsupported protocol type` che "ritorna".

## Flusso operativo al boot (NX679J)
1. MM parte da `/etc/init.d/modemmanager` (wrapper+monitor+replay).
2. La catena modem crea i netdev (S95 o manuale).
3. Gli hotplug notificano i netdev a MM vivo, o la chain fa `modemmanager restart` alla fine.
4. **Verifica**: `mmcli -L` (dopo fino a 90s di retry!) → `mmcli -m N -e` → state: registered.

## Controprova su un sistema di riferimento reale (falla PRIMA di dichiarare "nessuno standard esiste")

Un sistema comparabile professionale e funzionante (Quectel RM551E-GL su Snapdragon SDX75, OpenWrt 23.05, manager web con AT-commands via CGI) raggiungibile in rete ha mostrato: la sua WAN su `rmnet_data0` e' gestita da un **proto custom del firmware** e il suo LuCI mostra sulla card **"Unsupported protocol type"** - lo stesso sintomo del nostro. ⇒ Nemmeno un prodotto 5G commerciale funzionante ha un proto standard per il datapath rmnet vendor. Conseguenze: (a) per i DATI non inseguire il 'tutto standard' - la divisione controllo-standard (MM) / dati-vendor e' il tetto pratico senza patch a monte; (b) la parte che l'utente giudica (operatore, banda, segnale) va sul percorso standard → pagina Status→Cellular Network; (c) `Unsupported protocol type` su una iface custom e' normale anche su firmware commerciali (manca il descrittore JS del proto).

Nota debug: `LOG_LEVEL="DEBUG"` in `/etc/init.d/modemmanager` scrive in `/var/log/mm.log` (NON in logread) - e' li' che si leggono probing, creazione modem e il motivo esatto del fallimento ("Failed to find a net port", "Unsupported QMI kernel driver for 'net/rmnet_ipa0'").

## Prior art DEFINITIVO (ricerca esaustiva 2026-09-21)
**Nessuno ha pubblicato MM+qcom-soc su kernel VENDOR + OpenWrt no-udev + netdev rmnet virtuali: siamo i primi.** Ma i 4 mattoni esistono:

| Cosa esiste | Dove | Applicabile |
|---|---|---|
| **Patch "allow QMI modem creation without net port"** (UNICA deroga pubblicata; `mm_base_modem_get_bam_dmux_pending()`: plugin==qcom-soc && !data → passa). MR!1452 **OPEN** (Harsh Sharma/QTI). Carry-down in meta-qcom (`0002-*`) e radxa/meta-radxa-dragon (0001/0003/0004/0005: QRTR-MHI, BindMuxDataPort, DPM) | gitlab.freedesktop MM MR!1452 | SI' come modello per proporre upstream; noi NON ne abbiamo bisogno (i netdev ci sono già, associati via regola!) |
| **Regole meizu** `79-mm-sdx55m-net.rules`: `ID_MM_PHYSDEV_UID=qcom-soc` sui netdev per raggrupparli col device QRTR (stesso identico pattern nostro!) | meizu-m2172-mainline/modem-userspace | SI' (conferma il nostro approccio) |
| **MR!1443** QRTR+mhi_net per PCIe (handler endpoint PCIE/4, udev ID_BUS=pci) | gitlab.freedesktop MM MR!1443 (OPEN) | SI' come modello twin |
| **OpenWrt master hotplug**: `25-modemmanager-net` ESCE su `/devices/virtual/*`; `mm_report_event` salta i virtuali salvo `qmapmux*/qmimux*/mbimmux*` (commit c51a804a63, PR#23551). **Nessun commit OpenWrt per rmnet_ipa0/rmnet_data0** | openwrt/packages | Il NOSTRO patch di `modemmanager.common` (aggiunto `rmnet*`) = il mattone mancante! |
| **libudev-zero** (OpenWrt, v1.0.5, Daniel Golle): SOLO libudev.pc, NESSUN gudev-1.0 (MM richiede >=232), daemonless; OpenWrt pinna `-Dudev=false`; MM fa il parsing interno delle regole (doc ufficiale: parser "non completo", regole custom "may not always work" se non equivalenti a esistenti) | openwrt/packages libs/libudev-zero | NO: strada morta, non risolve (manca gudev+demone) |
| **wwand** (ucode, no libqmi/glib/MM; multi-PDP QMAP; datapath vendor via add-on senza patch: docs/datapath-interface.md). PR#30185 aperta | github.com/ddimension/wwand | ALTERNATIVA completa a MM per i dati |
| Filtro `mm-filter.c` virtuali: `/sys/devices/virtual/net` → nessun symlink driver → `DRIVERS==` inutile (serve `KERNEL==`); bypass = `ID_MM_DEVICE_PROCESS=1` | codice main | già risolto dalla nostra patch 0100 |
| Issue #476 "modem senza net port" (aperta dal 2021, LOW, nessuna MR); #766 (QRTR+IPA mainline flaky); thread mm-devel 2020-10 origine del design driver-based | gitlab | informativo |
