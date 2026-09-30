# NX679J modem → OpenWrt Internet — handoff

**Data:** 2026-09-19 15:00 CEST  
**Obiettivo:** da OpenWrt avviato sul Nubia RedMagic 7 NX679J, usare la SIM WindTre per ottenere `ping 8.8.8.8`.  
**Stato:** NON RISOLTO. Non dichiarare il ping funzionante.

**Nota di precedenza:** le sezioni iniziali sono storiche. Usare la ricetta
corrente di `MODEM-HANDOFF-20260919-2.md`, inclusi gli aggiornamenti della
sessione 3 in fondo a entrambi i file.

## Executive summary

Aggiornamento2026-09-20: DMS ONLINE e bearer WindTre ottenuti riproducibilmente
su OpenWrt tramite QMI/QRTR. IPv4 e default route su `rmnet_data0` applicati
e verificati; manca ancora il ping cellulare. Sei reboot: il terzo durante
l'ingress IPA, il quarto, quinto e sesto con il solo comando ping dopo statistiche
riuscite. L'isolamento APS/PERF_TETHER/PERF/OFFLOAD/SHS nel boota28d è
concluso: rimozioni normali riuscite prima di MSS/IPA/VND, ma stesso fallimento
al ping. DMS ONLINE è riprodotto in sei boot; IPv4/default verificati in quattro.
SlotB, nuovo boot `ca60b6b2-9f6d-4a73-bed2-fe3b1b15fe7f`, MSS offline.
Nel precedente f206: bootstrap protetto86.18s, ONLINE2038.52,
ingress2097.28 riuscito.
Sonda raw32+LR verificata prima su fixture, poi su tre TAG LAN reali:
696byte interamente persistiti e archiviati, cleanup0. Setup dati successivo
completato: IP `10.180.238.160/26`, default via `10.180.238.161`,
route-get corretto e stats RX0/TX9. Altri nove TAG LAN persistiti, nessun WAN
o opcode0 nel setup. Ping separato2930.60 seguito dal sesto reboot:
sonda host cattura il primo status WAN a2930.683119, tutti32byte zero.
Il record da solo non identifica ancora il percorso di crash.
Ultimo kmsg a28d: `ipahal ipa_hw_opcode_to_opcode:1396 unsupported Status
Opcode 0x0`. Lo stock segnala il byte0 e continua con l'enum interno0:
la riga non prova né il ramo LAN/WAN né la causa del reset. EP23 aveva già
status_en1/header4/checksum0; niente modifica di formato alla cieca.
Audit completo e riferimenti nell'ultima sezione dell'handoff operativo.
Le sezioni storiche sotto non sostituiscono i risultati aggiornati.

## Hardware / boot verificato

- Target: Nubia RedMagic 7 NX679J, NX679J, SM8450/taro.
- Android stock: slot `_a`, ADB disponibile.
- OpenWrt: slot B / immagine custom con kernel stock e moduli vendor.
- Controllo boot: `experiments/reboot2.c` usa `reboot(MAGIC1=0xfee1dead, MAGIC2=672274793, CMD_RESTART2=0xa1b2c3d4, "bootloader")`; quindi è possibile cambiare slot e tornare ad Android senza azione fisica.
- Ultima immagine OpenWrt nota buona: `experiments/20260917-init-v9/boot_b-init-v53.img`; il builder è `experiments/build-v10.py`.
- Rete host per ADB/SSH quando OpenWrt è attivo: host `10.0.0.2/24` su `enp103s0f3u1`, telefono `10.0.0.1/24`, SSH con `~/.ssh/nx679j_key`.

## Evidenze live più recenti

### Android, stato attuale dopo il test

Comandi osservati il 2026-09-19 14:59:

```text
slot: _a
SELinux: Enforcing
qti PID: 10290
qcrilNrd: 10312, 10316
qmipriod: 10306
netmgrd: 10308
```

Device nodes:

```text
/dev/rmnet_ctrl  major:minor 488:0  context vendor_rmnet_device
/dev/smdcntl8    496:2       context vendor_smd_device
/dev/smd11       496:5       context vendor_smd_device
/dev/smd7        496:3       context vendor_smd7_device
/dev/smd8        496:4       context vendor_smd_device
```

Nota: i numeri major/minor cambiano tra immagini/boot. Il vecchio handoff riportava 504:*; l’ultima sessione live ha osservato 496:* per `glink_pkt` e 488:0 per `gsi_usb`. Non hardcodare 504 o 487 nel nuovo codice.

Android espone:

```text
/dev/socket/qmux_radio/ril_ipc    AF_UNIX SOCK_STREAM listener + clients
/dev/socket/qti_dpm_uds_file
```

`/proc/net/unix` ha mostrato un listener e client su `.../qmux_radio/ril_ipc`. La directory è protetta da SELinux; come root Magisk si può leggere il listing.

Rete Android al momento del handoff:

```text
solo usb0 = 10.0.0.1/24
rmnet_data0..5 senza IP e senza traffico
nessuna default route cellulare
```

Questo è coerente con i riavvii manuali dei servizi vendor fatti per il tracing; NON usarlo come prova che la SIM o l’APN siano guasti.

### Radio / SIM

Dal log Android durante un tentativo dati:

```text
NetworkRegistrationInfo: HOME, LTE, WINDTRE, MCC/MNC 22288
APN: internet.it
SETUP_DATA_CALL: cause=4100, cid=-1, ifname=""
OEM_DCFAILCAUSE_4
```

Il tentativo era stato perturbato fermando/riavviando `vendor.dataqti`, `qmipriod`, `netmgrd`, `qcrild`; quindi va ripetuto dopo un reboot Android pulito, lasciando partire tutti i servizi stock.

### Modem e stack OpenWrt

Verificati in precedenza su OpenWrt:

- `4080000.remoteproc-mss` e gli altri remoteproc caricavano il firmware modem.
- Moduli dati vendor caricati: `rmnet_core`, `rmnet_ctl`, `ipam`, `ipanetm`, `gsim` e dipendenze.
- `rmnet_ipa0` esiste, e `rmnet_data0..5` vengono creati dal kernel; il fatto che esistano NON significa che un bearer sia attivo.
- `rmnet_ctl.ko` non crea il char device Android `/dev/rmnet_ctrl`: quel device viene da `gsi_usb.ko`, class `gsi_usb`, driver `f_gsi.c`, major dinamico.

## Tentativi eseguiti e risultati

### 1. QMUX diretto su glink_pkt — FALLITO / pista non ancora utilizzabile

Sorgente: `experiments/qmi-raw-v4.c`, SHA-256:

```text
25e52e0c83547eaf5846b11d32a77f725ce238ccecb749fc229363cc9d302681
```

Frame provato, derivato dal layout pubblico libqmi:

```text
01 0b 00 00 00 00 00 01 21 00 00 00
```

Interpretazione prevista:

```text
01                         QMUX marker
0b 00                      QMUX length
00                         QMUX flags
00                         service CTL
00                         client 0
00 01                      QMI CTL flags=0, transaction=1
21 00                      message id GET_VERSION_INFO
00 00                      TLV length 0
```

Risultati Android, eseguiti in contesto SELinux vendor appropriato:

- `/dev/smd11`, `/dev/smd7`, `/dev/smd8`: `open ok`; `write` ritorna `-1 errno=16 (Device or resource busy)`.
- `/dev/smdcntl8`: `open` blocca fino al timeout del driver e ritorna `errno=110 (Connection timed out)`.
- con servizi fermati o riavviati il comportamento resta incompatibile con un canale QMUX libero.

**Conclusione:** non continuare cambiando casualmente marker/header sui nodi glink. `glink_pkt` usa un solo channel/rpdev e i nodi sono occupati/riservati a client vendor; inoltre il confine che il RIL usa appare essere QMUXD/QtiBus, non il nodo raw.

### 2. `/dev/rmnet_ctrl` — FALLITO come secondo open

Fatto:

```text
qti PID 10290 fd 6 -> /dev/rmnet_ctrl
```

Un nostro probe statico che apre `/dev/rmnet_ctrl` ritorna `EBUSY` (`Device or resource busy`) quando `qti` lo possiede. In altri boot il rifiuto è stato `EPERM` per SELinux. Fermare servizi e cambiare SELinux non ha prodotto un canale libero affidabile.

**Conclusione:** `/dev/rmnet_ctrl` è probabilmente un device single-owner con ABI GSI/USB/QTI; non va sondato con `write(QMUX)`. Prima bisogna estrarre gli ioctl e il protocollo dal binario/vendor source, oppure usare un processo che ne erediti il fd in un ambiente di test controllato.

### 3. ptrace minimale sul processo `qti` — informazione insufficiente

Sorgenti:

```text
experiments/qmi-trace.c
experiments/qmi-trace-v2.c
```

`qmi-trace-v2` compila staticamente AArch64 e traccia `openat/close/read/write/readv/writev/ioctl/socket/connect/sendto/recvfrom/sendmsg/recvmsg`, dumpando argomenti e buffer. SHA-256 sorgente:

```text
e77fa5bb28dd52d729a320a4e70703113f6e6e15d173909fff5e25482db01548
```

Esecuzione prima/durante un toggle dati:

```text
attached qti e 3 thread
T... enter read(0x3, ..., 0x186a0, ...)
detached
```

Non sono apparsi `openat/connect/write/ioctl` perché l’attach avveniva dopo l’inizializzazione di qti, e il thread osservato era in una read lunga. Questo NON prova che il trasporto sia inattivo.

Non usare lo strace AArch64 incompleto costruito in `tools/strace-aarch64`: la build è fallita per conflitti tra header Bionic e Linux (`struct in_addr` ridefinita). Il probe ptrace custom è più riproducibile, ma va attaccato prima dell’azione da osservare o bisogna tracciare il processo che esegue davvero `SETUP_DATA_CALL`.

## Evidenza da sorgenti pubbliche da usare

Skill reference locale: `kernel-re/driver-protocol-recovery/references/qualcomm-ril-ipc.md`.

Pin/source mirror già identificato:

```text
OrphyWang/Qcom-CAMX-CHI
commit 36fc163a534963a5b3af52186af5efcc63401ad2
```

Punti riportati dalla reference:

- `qcril-nr/qcril-common/qtibus/src/QtiBusSocketTransport.h`: `IPC_SOCKET_NAME` = `/dev/socket/qmux_radio/ril_ipc`; comandi `NEW_CLIENT`, `NEW_MESSAGE`, `CLIENT_DEAD`.
- `Messenger.cpp`: registrazione/delivery QtiBus usa messaggi length-prefixed keyed by message-name; non è un QMI service/client address.
- `QtiBusSocketTransportServer.cpp`: envelope server con `CommandId`, `pid_t`, payload length/data; stream Unix e rappresentazioni native del processo.
- `qmi/platform/qmi_platform_qmux_if.h`: endpoint QMUXD documentato = `/dev/socket/qmux_radio/qmux_connect_socket`; platform header `{ int total_msg_size; int qmux_client_id; }`.
- `linux_qmi_qmux_if_client.c`: connessione QMUXD, lettura del client ID, inserzione dell’header platform e invio.
- `qmi/src/qmi_i.h`: QMUX client ID `int32_t` e header con transaction/connection/service/client IDs.
- `qmi_qmux_if_send_raw_qmi_cntl_msg()` è un percorso QMI distinto con alternative direct vs `QMI_MSGLIB_MULTI_PD`; il simbolo non dimostra quale transport è attivo sul telefono.

**Attenzione:** il mirror pubblico è evidenza del protocollo Qualcomm correlato, non prova che la build Nubia usi la stessa revisione o che `ril_ipc` porti QMUX raw. Va catturato il percorso live.

## Stato del modello

| Affermazione | Stato | Evidenza | Confidenza |
|---|---|---|---|
| Il modem/remoteproc è avviabile | osservato/verificato in boot precedenti | dmesg e `/sys/class/remoteproc` nei log di esperimenti | alta |
| IPA/rmnet kernel sono presenti | osservato | moduli e interfacce `rmnet_ipa0`, `rmnet_data*` | alta |
| Il bearer dati OpenWrt è attivo | falso | nessun IP/route, contatori rmnet dati zero, nessun ping | certa: non funzionante |
| `glink_pkt` accetta il frame QMUX standard usato dal probe | falso nei test eseguiti | EBUSY/open timeout | alta |
| `/dev/rmnet_ctrl` è il confine userspace corretto | ipotesi forte | qti fd 6 e major gsi_usb | media-alta |
| `/dev/socket/qmux_radio/ril_ipc` è QtiBus client-facing | osservato + source correlato | listener live e `QtiBusSocketTransport` source | media-alta |
| `ril_ipc` porta QMUX raw | non dimostrato | nessun dump live | bassa |
| `qmux_connect_socket` esiste su questa build | non osservato | directory live contiene solo `ril_ipc` | bassa; potrebbe essere alternativo non creato |
| L’APN WindTre è sbagliato | non dimostrato | Android log usa `internet.it`, ma prova è stata disturbata | bassa |

## Cosa NON fare nella prossima sessione

1. Non inviare altri frame a caso a `/dev/smd*`, `/dev/rmnet_ctrl` o `ril_ipc`.
2. Non usare `qmi_qmux_if_send_raw_qmi_cntl_msg` come prova che un raw QMUX write sia valido sul nodo scelto.
3. Non fermare tutti i servizi vendor e poi interpretare `cause=4100` come guasto SIM/APN.
4. Non hardcodare major 487/504: sono dinamici e nell’ultimo boot sono 488/496.
5. Non flashare un’immagine nuova prima di avere una cattura/ABI riproducibile.
6. Non usare OpenWrt per il primo test del protocollo: prima cattura in Android stock e poi porta la specifica.

## Piano obbligatorio per la prossima sessione

### Fase A — ristabilire baseline Android pulita

1. Se serve, reboot Android slot A; non usare `setenforce 0` salvo necessità stretta.
2. Verificare che `vendor.dataqti`, `vendor.qmipriod`, `vendor.netmgrd`, `vendor.qcrild`, `vendor.qcrild2` siano running.
3. Verificare registrazione LTE e APN senza spegnere servizi.
4. Salvare baseline:

```sh
adb shell 'getprop ro.boot.slot_suffix; getenforce; ps -e -o PID,USER,NAME,ARGS'
adb shell 'ip -4 addr; ip route; cat /proc/net/dev | grep -E "rmnet|usb0"'
adb shell 'logcat -b radio -d -t 500'
```

### Fase B — identificare processo e socket, senza inviare dati

1. Prima di toccare i servizi, enumerare i fd dei processi con contesto root/vendor, specialmente:
   - `qti`
   - `qcrilNrd` (entrambe le istanze)
   - `qmipriod`
   - `netmgrd`
   - eventuale `/vendor/bin/qmuxd` se presente
2. Confermare quale processo ha fd/socket su `ril_ipc`, `rmnet_ctrl`, `smdcntl8`.
3. Usare il ptrace tracer sui processi che fanno la richiesta, non solo su qti dopo l’avvio.
4. Attach prima del trigger, poi usare l’azione Android normale `svc data disable; svc data enable` oppure il toggle dati. Non mandare QMI manuale.
5. Tracciare anche `sendmsg/recvmsg`, `writev/readv`, `ioctl`; dumpare buffer solo per i fd socket/device interessati.

Il tracer custom esistente non traccia automaticamente nuovi thread creati dopo l’attach e il loop di wait non gestisce bene tutti gli eventi ptrace. Se si modifica, il prossimo fix deve essere: `PTRACE_O_TRACECLONE`, gestione `PTRACE_EVENT_CLONE`, e filtro per fd/paths; poi ricompilare/testare in host prima del telefono.

### Fase C — selezionare il trasporto

- Se si osserva `connect("/dev/socket/qmux_radio/ril_ipc")` + `sendmsg/recvmsg`: ricostruire envelope QtiBus dal dump e implementare prima un client Unix stream in OpenWrt; solo dopo verificare se dentro l’envelope c’è QMI.
- Se si osserva `open("/dev/rmnet_ctrl")` + `ioctl`: estrarre ioctl `_IOC` e strutture da `f_gsi.c`, `usb_ctrl_qti.h`, `gsi_ctrl_dev_ioctl()` e disassemblato vendor; implementare un client di control device con ownership corretta.
- Se si osserva `connect("qmux_connect_socket")`: usare il platform header `{int total_msg_size; int qmux_client_id;}` e catturare il primo client-ID; non confonderlo con `ril_ipc`.
- Se il processo client parla solo con un fd già ereditato/aperto prima dell’attach: tracciare dal boot Android iniziale o usare LD_PRELOAD non è affidabile su vendor; preferire ptrace `PTRACE_SEIZE` del processo appena eseguito, oppure strace Android compilato correttamente.

### Fase D — solo dopo il protocollo

1. Reimplementare il framing come tool userspace statico in `experiments/`, non dentro kernel.
2. Compilare con `aarch64-linux-gnu-gcc -static -Os -Wall -Wextra -Werror`.
3. Provare CTL e allocation client, verificare response transaction/result.
4. Implementare WDS client allocation/start con APN `internet.it` (il valore è osservato dal log Android, ma va lasciato configurabile).
5. Verificare comparsa di `rmnet_dataN`, IP/gateway/DNS e contatori.
6. Configurare link/route in OpenWrt e testare prima gateway, poi `8.8.8.8`.
7. Solo con ping verificato aggiornare questo file e `MODEM-HANDOFF.md`.

## Criterio di completamento

Il lavoro è completato solo con una cattura o ABI riprodotta e questi fatti live da OpenWrt:

```text
rmnet_dataN con IP assegnato
route default via gateway su rmnet_dataN
ping -c 3 -W 2 8.8.8.8: 3 risposte, 0% packet loss
```

In assenza di tutti e tre, lo stato resta NON RISOLTO.

## Nota Astra / provider

È stata tentata una consultazione mirata con il modello `gpt-6-astra` usando il provider Hermes `oneprovider`, non OpenRouter. La chiamata non ha prodotto output entro il timeout; non trattare il risultato come evidenza. OpenRouter aveva invece risposto con errore di crediti (`HTTP 402`) e non va usato per Astra in questa attività.

## Comandi rapidi nuova sessione

```sh
cd /home/user/nx679j-stock
sed -n '1,260p' experiments/MODEM-HANDOFF-20260919.md
adb devices -l
adb shell 'getprop ro.boot.slot_suffix; getenforce; pidof qti; pidof qcrilNrd; pidof qmipriod; pidof netmgrd'
```

## Contesto legale

Analisi svolta ai fini di interoperabilità su hardware legittimamente posseduto, per reimplementare un driver libero a partire da una specifica ricostruita; nessun codice proprietario va copiato nel driver finale.

---

# SESSIONE 2 — 2026-09-19 sera (continuazione)

Stato: **NON RISOLTO** (nessun ping ancora), ma grandi progressi: il modem ora viene
portato su COMPLETO da OpenWrt con il suo stack QMI (56 servizi, WDS incluso) e si
conoscono i prerequisiti esatti. Resta un ultimo gap: il transito del modem da
`mode 5` ("shutting down") a `mode 0` (ONLINE) che su Android avviene da solo ~200s
dopo un restart, su OpenWrt (finora) NO.

## FATTI nuovi verificati (con evidenza)

### Catena di avvio del modem su OpenWrt (ricetta funzionante!)

Servono TUTTI questi pezzi, in quest'ordine, prima di avviare il modem:

1. `finitmod /proc/1/root/lib/modules/msm_sharedmem.ko`
   - Crea il device UIO `rmtfs` (239:0). `ls /sys/class/uio/uio0` -> name=rmtfs,
     addr=0xd4900000, size=0x280000.
   - Poi `mknod /dev/uio0 c 239 0` (il /dev di OpenWrt non è devtmpfs).
2. Nodi partizioni EFS: `/sys/block/sdf/sdf{2,3,4,5}/dev` -> mknod
   (modemst1=8:82, modemst2=8:83, fsg=8:84, fsc=8:85) in `/tmp/efs/`.
3. `dspawn /tmp/rmtfs.log /tmp/rmtfs -P -o /tmp/efs -v`
   - rmtfs PATCHATO (refs/rmtfs-master, build statica): usa /dev/uio0 invece di
     /dev/qcom_rmtfs_mem; niente libudev.
   - **PUBBLICA service 14 (RFS) su QRTR — il modem senza questo non scrive l'EFS.**
   - Verifica: `qmi-qrtr lookup 14` deve rispondere.
   - ATTENZIONE: un solo rmtfs per volta; dopo un kill il service 14 sparisce
     (del_client) e va riavviato (successo: publish nuovo).
4. `dspawn /tmp/tqftpserv.log /tmp/tqftpserv -d`
   - tqftpserv PATCHATO (refs/tqftpserv-master): pubblica 4096 con versioni 1..10
     (10 socket, come Android) e risolve i path sotto **/rfs** (root-first), con o
     senza slash iniziale ("readonly/..." e "/readonly/...").
   - Tree da servire: `/rfs/readonly/firmware/image/modem_pr/...` (165 file, dal
     dump modem_pr di Android: refs/modem_pr/ — include so/848_0_0.mbn e
     mcfg/configs/mcfg_sw|mcfg_hw/...).
   - `/rfs/readwrite/` + `/rfs/readwrite/ota_firewall/` devono esistere (il modem
     scrive mcfg.tmp, server_check.txt="hello", ota_firewall/ruleset).
   - Il modem CHIEDE (visto nei log): mbn_hw.dig, mbn_sw.dig (36B), 848_0_0.mbn
     (476008B, a chunk con seek), mcfg.tmp (write+read), server_check.txt.
5. `finitmod /proc/1/root/lib/modules/qrtr-smd.ko` (trasporto QRTR sul SMD del modem).
6. `rprocstart /sys/class/remoteproc/remoteproc3/state start` (mss su OpenWrt = rproc3;
   su Android = rproc4!!).

Con tutto questo: il modem annuncia **56 servizi QMI** (WDS=1, DMS=2, NAS=3, UIM=11,
EFS=21, PDC=36, IPA=49...), ed EFS+mcfg funzionano (verificato nei log).

### Crash del sistema: capito e PREVENUTO

- Se il modem NON viene riavviato entro ~190-260s dal suo (ri)avvio, il modem fa un
  auto-reset che porta giù TUTTO il SoC (reboot secco, niente panic in dmesg —
  crashlog-dump su rawdump: nessun messaggio).
- **Fix provato: riavvio controllato del modem (rproc stop+start) ~86s dopo lo start.**
  Da allora nessun crash (verificato oltre 700s di uptime; prima crashava sempre).
- L'init di Android fa ESATTAMENTE questo: nel dmesg del boot Android si vede
  "Incrementing tid for modem to 1" + stop@92.8s + power@98.9s — Android RIAVVIA il
  modem una volta a ogni boot (a t≈91s).

### Mode 5 ("shutting down") = stato post-avvio del modem; transizione

- Su Android, dopo un restart manuale (rproc4 sysfs), il modem sta in mode 5 per
  ~200s e poi passa a mode 0 (ONLINE) da solo, con dati funzionanti
  (mDataConnectionState=2, WINDTRE LTE). **Nessun comando QMI lo causa** (nel trace
  di qcrilNrd negli ultimi secondi prima del flip: solo polling UIM; il DMS set
  operating mode non viene mai usato da qcril; anzi: "DMS Set Operating Mode"
  (0x2E) risponde **error 1 (malformed) su questo fw QUALSIASI client, anche su
  Android a modem sano**; idem UIM Power On SIM 0x31, PDC List/Get 0x22-0x24).
- Su OpenWrt il modem (finora) NON ha mai fatto il passaggio a 0: attese osservate:
  45s, 200s+340s, 568s, 225s (dopo 2° restart) — sempre 5.
- Il restart doppio (come Android) NON ha ancora prodotto il flip.

### Cosa NON è la causa (falsificato oggi)

- rmtfs/tqftpserv assenti -> RISOLTO e non basta: il flip non avviene comunque.
- Playback dei 137 messaggi QMI che qcrilNrd manda al modem (estratti dal trace):
  tutti accettati dal modem (137/137), nessun flip.
- UIM provisioning session: la card status del modem e' BYTE-IDENTICA su Android e
  OpenWrt (stessa SIM, stesso stato); l'attivazione (0x38) dà error 2/3 anche con
  TLV corretti; non è la discriminante.
- /dev/subsys_modem: NON esiste su questo kernel (il keepfileopen di pmOS non si applica).

### Strumenti nuovi (in experiments/)

- `qrtr/qmi-qrtr`: + `rawseq` (più messaggi su un socket), + `playback <file>`
  (replay di "fd svc hex" — usato per l'init di qcril).
- `qmi-trace` (aarch64 static, in experiments/): tracer ptrace con TRACECLONE,
  payload sendto/recvfrom/sockaddr, timestamp. Usato su Android su qcrilNrd/qti.
- `crashlog-dump.sh`: logger persistente su rawdump (slot da 1024KB, 32KB l'uno,
  ogni 3s: dmesg filtrato + tail rmtfs/tqftpserv + conteggio servizi). FONDAMENTALE.
- `refs/android-trace2-qcril.log` + `refs/qcril-playback.txt` + `parse-trace.py` +
  `extract-playback.py`: il trace completo dell'init qcril post-restart e il suo replay.
- `refs/mm-broadband-modem-qmi.c`: il flusso ModemManager per il "power on"
  (Set Event Report 0x14 opmode=TRUE -> Set Operating Mode; se INTERNAL/INVALID_TRANSITION
  -> "DMS Set FCC Auth" — msgid 0x555F/0x5571 sul nostro fw! — e retry).
- `refs/modem_pr/` (21MB): il tree mcfg completo da servire.

### Trappole di trasporto note

- Gli heredoc ssh lunghi (>~1KB) si troncano: usare SEMPRE file script pushati (cat|ssh).
- /tmp si svuota a ogni reboot: ripushare tutto.
- Su Android: `su -c` con quoting annidato: a volte perde il contesto root -> usare
  script pushati in /data/local/tmp ed eseguirli con `su -c 'sh ...'`.
- Nel tool: txn QMI deve stare in 0x0000-0xFFFF (bug txn>0xFF visto).

## PROSSIMI PASSI (ipotesi ordinate per il flip mode 5 -> 0)

1. **pd-mapper** (l'ultimo daemon pmOS mancante): servizio "service registry locator"
   (64) + le tabelle PD. Su Android gira (`pd-mapper` attivo); su OpenWrt no.
   - Candidato forte: il flip potrebbe richiedere il completamento dei "protection
     domains" (PDR/servreg) che Android ha.
2. **Ipa/rmnet**: su Android al boot compare "QMI_IPA_INIT_MODEM_DRIVER_REQ_V01"
   (ipa_fmwk + ipacm). Su OpenWrt ipacm non gira. Provare a innescare l'init IPA lato
   kernel/ipa_fmwk (o a capire se il flip lo attende).
3. **Attesa più lunga + campionamento**: su Android il flip è a ~200s; su OpenWrt
   potrebbe essere più lento (o dipendere da un evento specifico). Uno script di
   sorveglianza di 15-20 min con mode+servizi, senza toccare nulla.
4. **SSCTL (service 43)**: è annunciato; sul fw ci sono anche messaggi SSCTL non
   standard. Da esplorare con calma.
5. Chiedere in #postmarketOS / libqmi (thread "Modem stuck in shutting down after
   q6v5 restart"): il comportamento "mode 5 per 3-4 minuti poi online" su Android vs
   "mai" su OpenWrt è specifico e qualcuno con SM8450 potrebbe averlo visto.

## Comandi rapidi (OpenWrt)

```sh
# ambiente completo (vedi sopra, in ordine):
/tmp/finitmod /proc/1/root/lib/modules/msm_sharedmem.ko
[ -e /dev/uio0 ] || mknod /dev/uio0 c 239 0
mkdir -p /tmp/efs; # mknod modemst1/2 fsg fsc da /sys/block/sdf/sdfN/dev
/tmp/dspawn /tmp/rmtfs.log /tmp/rmtfs -P -o /tmp/efs -v
/tmp/dspawn /tmp/tqftpserv.log /tmp/tqftpserv -d
/tmp/finitmod /proc/1/root/lib/modules/qrtr-smd.ko
/tmp/rprocstart /sys/class/remoteproc/remoteproc3/state start
sleep 86; # poi RESTART controllato (previene il crash del SoC):
/tmp/rprocstart /sys/class/remoteproc/remoteproc3/state stop; sleep 6
/tmp/rprocstart /sys/class/remoteproc/remoteproc3/state start
# sorveglianza mode (txn < 0x100!):
/tmp/qmi-qrtr raw 2 0001012d000000   # -> u8: 0=online
/tmp/qmi-qrtr list | grep -c service=
```

## Stato corrente del telefono (fine sessione)

- Slot B (OpenWrt) attivo; modem avviato con la ricetta, mode=5, NESSUN crash
  (uptime > 1265s), tutti i servizi su; /tmp popolato (final.sh ecc.).
- Android provato estensivamente (restart modem, trace) e si è ripreso da solo
  (LTE WINDTRE ok) prima di tornare su OpenWrt.

---

# SESSIONE 3 — pd-mapper/IPA inizializzati, ONLINE ancora da ottenere

**Stato: NON RISOLTO.** Ripreso lo stesso boot OpenWrt senza riavviare il SoC:
59 servizi e DMS=5 confermati. Consultate le due guide pmOS richieste e
pd-mapper upstream (`5ecd2fe926aca7abfe40724177f63b942cff3947`).

**FATTO:** pd-mapper compilato statico localmente con `build-pd-mapper.sh`,
mappe stock lette da `/proc/1/root/lib/firmware/` (7 file, nessuna modifica).
La pubblicazione libqrtr ha richiesto bind esplicito e node/port nel
NEW_SERVER. Service 64 è ora visibile (`version=1 instance=1 node=1`);
risposta locator locale verificata e richieste reali del modem osservate
per `tms/servreg`, `avs/audio`, `tms/pddump_disabled`.

**ESPERIMENTO:** `pd-cycle.sh`: start uptime=28758, stop protettivo a
uptime=28844 (86 secondi), poi finestra di 660 secondi con campioni ogni
15 secondi. Ultimo campione uptime=29510: 660s dopo il secondo start,
DMS=5, 60 servizi. Nessun reboot. **RISULTATO: pd-mapper funziona ma non
è sufficiente entro questa finestra.** Log completo `pd-final.tar` in
`refs/session3-20260919/`.
Logger rawdump riattivato dopo aver salvato la finestra precedente;
path corretto nel namespace OpenWrt: `/proc/1/root/dev/rd`.

**Artefatti:** `refs/pd-mapper/`, `refs/pd-mapper-static`,
`refs/session3-20260919/`, `/tmp/pd-cycle.log`, `/tmp/pd-mapper.log`.
Dettagli operativi correnti in `MODEM-HANDOFF-20260919-2.md` §8.

**IPOTESI successiva:** IPA; moduli caricati e class device presente,
ma nodo `/dev/ipa` assente. A uptime=29354 verificato anche che in questo
boot non esistono `rmnet_ipa0`/`rmnet_dataN` (i riferimenti Android sopra
non descrivono lo stato attuale).

**FATTO:** ABI del trigger `ipa3_write("1", 1)` ora verificata sia nei
sorgenti correlati LineageOS (commit
`c1e096fa00a6a90568f8e16720f8c9749102833b`) sia nel modulo stock `ipam.ko`.
Non modifica firmware: segnala al driver la disponibilità dei file stock.
**ESPERIMENTO IPA:** salvato rawdump precedente, riavviato solo il logger,
eseguito `ipa-init.sh` con helper statico `qrtr/ipa-trigger`.
Scrittura accettata a uptime=29612; firmware stock caricato, IPA ready,
`rmnet_ipa0` comparsa (ifindex15), servizio AP49/instance1 pubblicato.
Il kernel riceve la risposta a `QMI_IPA_INIT_MODEM_DRIVER_REQ_V01`
a uptime=29627.205277. DMS ancora5 a29631; nessun reboot.
Osservazione automatica per massimo1200s dal trigger, deadline≈30812.
Nessuna ulteriore modifica hardware durante la prova.
Dettagli e prove in §8 dell'handoff operativo. **Internet NON RISOLTO.**

**RISULTATO IPA (2026-09-20):** osservazione completa fino a uptime30812,
elapsed1200: sempre DMS5, 61servizi, stesso boot. Il log contiene
`RESULT: no ONLINE transition in 1200 seconds after IPA trigger`.
Archivio `refs/session3-20260919/ipa-final-20260920.tar` verificato;
rawdump salvato prima della nuova prova in `rawdump-before-ipa-cycle.bin`.

**NUOVA PROVA, distinta:** `ipa-cycle.sh` verifica l'ordine di avvio con
IPA già pronto prima del restart modem, mantenendo tutti e tre i daemon.
Logger PID7180, prova PID7213. Stop30916, start30922; risposta IPA a30923.896833,
circa11ms dopo la richiesta. DMS5/61servizi a30953; restart protettivo86s
automatico, poi osservazione660s. Nessuna modifica parallela all'hardware.

**FATTI per la futura parte dati:** `qrtr/rmnet-query` usa esclusivamente
getter dell'UAPI stock. Misurati driver=rmnet_ipa0, features=7, endpoint=1,
consumer_pipe=2, producer_pipe=23, MRU=1000, SG=1. Non significa pipe
attivate: nessun setup pipe, mux, bearer o route effettuato.

**Pista successiva non provata:** ADSP offline, ma il modem richiede
`avs/audio`; la mappa stock lo colloca in `msm/adsp/audio_pd`, istanza74.
Sorgente kernel `qcom_sysmon.c` letto: SSCTL gestisce shutdown e notifiche
tra remoteproc; nessuna scrittura vendor arbitraria eseguita.

**RISULTATO ciclo IPA pronto (2026-09-20):** stop protettivo31008,
esattamente86s dopo start30922; secondo start31014. Campione finale31674,
elapsed660: DMS5, 61servizi, running, stesso boot. Risultato negativo
completo, nessun ping. PID7213 è zombie terminato, non un processo attivo.
Archivio verificato `ipa-cycle-final-20260920.tar` e finestra rawdump
`rawdump-before-adsp.bin` (13107200 byte) in `refs/session3-20260919/`.
Logger7180 sostituito, dopo il backup, dal PID12789.

**Chiarimento IPA:** richiesta49/0x31 = statistiche APN nell'UAPI stock;
init è0x21. Il testo del log scambia verbalmente req_id/result, ma i
parametri sono49 e1: non è prova di init fallito.

**SSCTL diagnostico:** lettura Get Failure Reason0x22 su43/v2/18:
result0/error94 senza TLV della causa; nessuna causa utilizzabile.
Preparato e validato `adsp-init.sh`: avvio isolato ADSP, osservazione660s,
MSS non riavviato. Nuovo client `/tmp/qmi-qrtr-next` comprende SSCTL
diagnostico e stato servreg con selezione esatta dell'istanza.
**FATTO ADSP:** processo13127, start31874, running31875, servizi61→69.
Il dominio `msm/adsp/audio_pd` risponde **UP** (`0x1fffffff`) su
servreg66/v1/74, node5/port3; listener poi rimosso con successo.
MSS non riavviato, stesso boot, DMS5 al primo campione. Osservazione fino
a circa32534, log `/tmp/adsp-init.log` e `/tmp/adsp-audio-state.log`.
**Internet resta NON RISOLTO.**


**RISULTATO ADSP conclusivo (2026-09-20):** uptime32535, elapsed661:
DMS5/69servizi, MSS e ADSP running, audio UP già verificato, stesso boot.
Processo13127 zombie terminato. Archivio `adsp-final-20260920.tar` verificato
(1497600byte/14voci); finestra `rawdump-before-dpm.bin` (13107200byte).
L'avvio ADSP da solo non basta nella finestra misurata.

**ESPERIMENTO successivo isolato: DPM.** Fonti libqmi
`b7913df8b49330956f0337dcc6255f63751fdda1`, ModemManager
`e1f8061541c974d046e264b1069716109a607e5e` e `rmnet_ipa.c` correlato.
Modem RX/TX corrispondono rispettivamente a IPA TX/RX: dai getter stock
EMBEDDED4/endpoint1/RX2/TX23. Client statico e encoding testati offline.
Logger17991, script18070; DPM client18117 rimane aperto, limite3600s.
Open Port a32590 accettato con result0/error0; osservazione660s da32591
(deadline circa33251), nessun restart MSS/configurazione pipe/mux/bearer.
WDA Get sullo stesso endpoint prima di Open falliva result1/error48;
dopo Open riesce result0/error0, QoS0/link-layer2/UL0/DL0.
Fatto nuovo, ma DMS ancora5 a32606. Dettagli/log in §8 handoff operativo.

**Precisazioni sulle evidenze storiche:** il trace qcrilNrd non copre
ogni processo Android; l'assenza di Set Operating Mode nel trace non
dimostra assenza di qualsiasi azione QMI sull'intero sistema. Il replay
contava137 risposte, non137 result/error verificati; non ripetere il replay
già negativo. **Nessun IP/default/ping cellulare: NON RISOLTO.**

**RISULTATO DPM conclusivo:** uptime33251/elapsed660: DMS5,69servizi,
stesso boot. Script18070 terminato; client18117 mantenuto aperto.
Archivio verificato `dpm-final-20260920.tar` (1604608byte/16voci),
backup `rawdump-before-pipes.bin` (13107200byte), nuovo logger23362.
NAS Get Serving System senza servizio radio disponibile.
UIO di questo boot=0xd4500000/size0x280000, coerente con allocazioni e
I/O rmtfs: zero errori nel log. Non fissare il vecchio indirizzo storico.

**ESPERIMENTO pipe distinto:** script23387, start/setup a33290,
ioctl stock ingress7/flags0xe ed egress6/flags0xa entrambi accettati.
GRO hardware controllato spento prima delle scritture; checksum/aggregazione
TX non abilitati. Kernel: `rmnet_ctl driver probed` @33290.613740.
WDA rimane raw-IP2, aggregazioneUL0/DL0; DMS5 iniziale.
`qrtr/rmnet-config.c` e test offline, build riproducibile
`sh build-data-tools.sh`, warning-as-error e sanitizers passati.
Osservazione fino a circa33950 (660s), nessun WDA Set/mux/WDS/restart.
Log `/tmp/pipe-init.log` e `/tmp/pipe-*`, dettagli in handoff operativo.
**NON RISOLTO: nessun IP/route/ping cellulare verificato.**

**RISULTATO pipe conclusivo:** finale33950/elapsed660, sempreDMS5,
69servizi, boot invariato, processo23387 terminato (Z).
Archivio `pipe-final-20260920.tar` verificato (1720832byte/19voci).
Logger23362 arrestato prima di conservare `rawdump-before-wda.bin`
(13107200byte); nuovo logger29213.

**ESPERIMENTO WDA distinto:** `wda-init.sh` PID29240; client nuovo
`/tmp/qmi-qrtr-qmap`, DPM18117 lasciato intatto e aperto.
Un Set Data Format0x20 con QoS0/raw-IP2/QMAP5 UL e DL, endpoint4:1
(TLV0x17), secondo libqmi; risposta e Get successivo verificati
result0/error0 e formato richiesto. Prerequisito dati ottenuto, non ONLINE.
Campione33998: DMS5/69servizi; osservazione660s fino a circa34658.
Nessun mux/WDS/restart. Non sovrapporre modifiche hardware.

**Strumenti offline:** `qrtr/rmnet-link.c` e test di encoding netlink,
build `-Werror`/ASan/UBSan superata con `build-data-tools.sh`.
Mux1 osservato nel bind WDS Android a endpoint4:1; link non ancora creato.
Dettagli e valori WDA nei log e in §8 dell'handoff operativo.
**Internet resta NON RISOLTO.**

**RISULTATO WDA conclusivo:** finale34658/elapsed660/DMS5/69servizi,
stesso boot, PID29240 terminato (Z). Archivio `wda-final-20260920.tar`
verificato:1839616byte/16voci. QMAP(enum5) negoziato, ma nessun ONLINE.
Logger29213 fermato prima del backup `rawdump-before-mux.bin`
(13107200byte); nuovo logger3207. DPM18117 mantenuto aperto.

**ESPERIMENTO mux distinto:** `mux-init.sh` PID3402; a34706 creazione
netlink verificata: `rmnet_data0`, ifindex18, parent17=`rmnet_ipa0`, mux1.
Notifica IPA ADD_MUX_CHANNEL (selector5) accettata; parent e figlio UP.
Solo IPv6 link-local automatico, nessun IP cellulare/default route.
DMS5/69servizi. Osservazione660s fino a circa35366, DPM scade≈36190.
Nessun WDS Start, restart MSS, nuovo WDA Set o modifica firmware.
Build/test offline passati (`data-tools-mux-build.log`).
Log `/tmp/mux-init.log` e `/tmp/mux-*`; dettagli in §8 handoff operativo.
**NON RISOLTO: IP, default route e ping cellulare non ottenuti.**

**Ultimo campione effettivamente letto:**34916/elapsed210/DMS5/69servizi,
controllo a uptime34930. Il controllo SSH successivo è stato annullato;
esito finale mux non ancora acquisito. Nessun ulteriore esperimento hardware.
Prima di proseguire leggere il risultato finale e archiviare; DPM scade≈36190.

**Strumentazione offline aggiunta:** `build-strace.sh` compila strace7.2
upstream statico GNU; release SHA256 verificato tramite asset GitHub.
Binario `refs/strace-static`, log `strace-build.{log,err}` in session3.
`qrtr/trace-fixture.c` (solo thread/socketpair locale) supera build AArch64
`-Werror` e test host ASan/UBSan. `trace-smoke.sh` pronto, ma nessun test
strace sul telefono o nuova cattura Android ancora eseguiti.
Dettagli, hash e limiti in §8 dell'handoff operativo. **NON RISOLTO.**

## Conclusione mux acquisita e strace validato (2026-09-20)

**RISULTATO:** mux35366/elapsed660/DMS5/69servizi; nessun ONLINE,
IP/default cellulare o ping. A uptime60278 stesso boot
`2ca42e08-8c9a-4d61-8ae3-2b632149d26a`, DMS5.
Observer3402, logger3207 e DPM18117 sono zombie terminati.
DPM è scaduto e ha inviato Close Port0x21 con result0/error0:
**non trattarlo più come aperto**. Superato lo stato storico “esito non letto”.

**FATTO:** archivi in `refs/session3-20260919/` verificati:
`mux-final-20260920.tar` (1987072byte/18voci), SHA256
`cbc2b0fc377dcaed2af24fd4f412fbafc0f3c1805d5af398198fac85c88bfd7d`;
`rawdump-after-mux.bin` (13107200byte), SHA256
`2828e728bf18be0f56ac450bd004e6a8f39e9699f3e3c2b175988c4ecef7504c`.
Snapshot `post-mux-live-20260920.log`.

**ESPERIMENTO strumentazione:** strace7.2 statico e fixture pushati in
OpenWrt. `trace-smoke.sh /tmp` passa: attach, nuovo thread, socketpair,
sendmsg/recvmsg, detach/TracerPid0 e sopravvivenza del target verificati.
Risultato `trace-smoke-openwrt-result.log`. Nessun daemon vendor toccato.
Logger rawdump rinnovato come25710 dopo backup e verifica del precedente Z.

**IPOTESI / prossima prova:** confronto Android, non altri setter speculativi.
Il vecchio racconto del flip Android a0 non è documentato dai due mode log
disponibili: riconfermare DMS/radio/rete live prima di usarlo come baseline.
Caso pubblico pertinente per architettura: ModemManager/SM7225,
`https://www.mail-archive.com/modemmanager-devel@lists.freedesktop.org/msg07101.html`,
ma DeviceNotReady non equivale a questo DMS5 e non fornisce ancora un fix.
Wiki pmOS/issue484 bloccate da Anubis; casi USB non direttamente applicabili.
Prima del cambio slot: archivio completo OpenWrt/logger; poi cattura limitata,
thread nuovi, client multipli e detach verificato. Dettagli nell'handoff operativo.
**Internet resta NON RISOLTO.**

## Nuova evidenza DMS dal confronto Android (2026-09-20)

**FATTO:** archiviato OpenWrt, spento il logger25710 e cambiato slot B→A
senza flash. Android boot `6c0c9043-bd14-45c5-8d84-1c986a1c63d3`:
DMS0 già a24s, WindTre LTE, rmnet_data2/mux3 con IPv410.98.71.57/30,
gateway10.98.71.58; ping cellulare Android2/3. Nessun successo OpenWrt.
Strace validato in Enforcing su fixture, incluso il timeout e il detach.

**ESPERIMENTO:** cattura contemporanea delle due qcrilNrd, netmgrd,
qmipriod e qti durante il comando Android nativo modalità aereo.
Attach537.54, enable537.60, disable542.86. **RISULTATO:** DMS0→1→0,
MSS sempre running; fine664.06, detach e ripristino verificati665.79/exit0.
Un primo preflight era terminato prima dell'attach/toggle; log conservati.

**Correzione fondamentale alle conclusioni storiche:** qcril usa realmente
DMS Set Operating Mode0x2e con successo. Il formato ONLINE catturato è
`0016002e000c00************************`, risposta
`0216002e00******************` (txn22, result/error0).
Include TLV0x10/5 byte tutti zero oltre a TLV0x01/ONLINE.
Sorgente Qualcomm correlato: `qcril_qmi_nas.cpp` imposta informazioni E911
valide ma non pendenti nella struttura zero-inizializzata; il vecchio IDL
pubblico/libqmi non espone tale estensione. Le vecchie affermazioni “mai
usato/non è il canale” sono smentite, non usarle per escludere questo formato.
I vecchi frame malformed non sono stati ritrovati nella conversazione:
non attribuire ancora con certezza la causa al TLV mancante.

Archivio `refs/session3-20260919/android-radio2-final.tar`,
4828160byte/31voci, SHA256
`bf0322521099efa319caf9349e36232cb1f63d9b344886259531ac8d99f4a00e`;
rawdump dopo cattura13107200byte, SHA256
`a8ffc4d6152c92bf50764d8a9e88567071c16ce5b84ae9ec109bfbbc494df7ae`.

**VALIDAZIONE INDIPENDENTE COMPLETATA:** il nuovo helper tipizzato
`dms-online-observed` invia un solo frame nel formato osservato da un socket
nostro su Android già ONLINE. A1381.71 risposta result/error0, readback0 anche
su client fresco; fine1383.94/exit0, logger terminato. Encoding verificato
byte-per-byte, build `-Werror` e ASan/UBSan passati. Non prova ancora5→0.
`qrtr/qmi-qrtr-observed` SHA256
`245e521d7e40eb00bf73b3f2fb9b3c8945521d54820cc2dc341c8d53d181ada0`.
Il vecchio binario `qrtr/qmi-qrtr` non comprende il comando nuovo.

Archivi della validazione, nella stessa directory session3:
- `android-dms-observed-final.tar`:294400byte/12voci, SHA256
  `b0b812d508c8dd17d133cf5c0e7821133220293a3295d9d7c721831f6990c5de`.
- `rawdump-after-android-dms-observed.bin`:13107200byte, SHA256
  `016f81b7c6e031c3041ff262ee19632567bf2ca6611fdea5dc34dddbb037792e`.

Prossima prova: una sola richiesta nuova su OpenWrt5, dopo bootstrap
`openwrt-observed-bootstrap.sh` e restart protettivo86s. Non eseguire il
vecchio `openwrt-final.sh`, privo della protezione e con txn non validi.
Dettagli, log, sorgenti e limiti nell'handoff operativo §8.
**Internet OpenWrt resta NON RISOLTO.**

## OpenWrt ONLINE e bearer ottenuti; verifica rete interrotta da reboot

**FATTO / RISULTATO:** nel boot OpenWrt
`b3c9f801-4555-4b2d-a3f8-e836ef7fd091`, start MSS109.86,
stop protettivo195.95/start201.95. Un solo Set nel formato osservato a241.05
ottiene result/error0 e DMS5→0 entro243.07, confermato da client fresco.
NAS WINDTRE/LTE. IPA e ADSP non erano ancora avviati: la transizione è
riuscita senza quei prerequisiti dati. Archivi DMS e hash nell'operativo §8.

**FATTO dati:** IPA inizializzato527.12, handshake527.438713, DPM/pipe/WDA
e mux1 verificati. WDS `internet.it` pronto530.17: restituisce
10.181.104.203/29, gateway10.181.104.204, MTU1500 e DNS WindTre.
Log integrale `refs/session3-20260919/openwrt-wds-first-result.log`.
Il primo script di applicazione IP/route/ping è seguito da reboot:
nessun ping riuscito acquisito e fase precisa del crash ancora ignota.
Rawdump finale675.97, nessun panic o CONFIGURE. Archivio
`rawdump-after-first-cellular-crash.bin`,13107200byte, SHA256
`1c0f1e080c4e024033be7d9ef4b360cb59cc4f75dc9828b656b0472e3ef54cde`.

**STATO ATTUALE:** slotB/OpenWrt, boot
`a609cace-9d94-43e6-8cbf-432d9fff1ca6`, MSS offline; vecchi client/PID
terminati col reboot. Preparati checkpoint rawdump sincroni (fsync,
slot380..399) e operazioni di rete separate, non un retry monolitico.
Ricetta, correzione estrazione MCFG, nuovi script e limiti nell'operativo §8.
**Internet NON RISOLTO: il criterio ping3/3 non è stato raggiunto.**

## Secondo bearer: IP e route verificati, crash nel confine statistiche/ping

**FATTO:** boot `a609cace-9d94-43e6-8cbf-432d9fff1ca6`, MSS start911.08,
stop protettivo997.16, secondo start1003.17. Set1045.18, ONLINE1047.21,
bearer1049.27. WDS restituisce `10.140.64.82/30`, gateway `10.140.64.81`,
MTU1500. Inspect/MTU/address/route/route-get completati con checkpoint.
A1312.13 IPv4 assegnato a `rmnet_data0@rmnet_ipa0`, default via quel
gateway e route8.8.8.8 sulla stessa interfaccia/sorgente effettivamente letti.

**RISULTATO:** `before_ping`1398.95 persistito, nessun `after_ping`,
SSH255 e reboot. Lo stage originale legge quattro statistiche sysfs prima
del ping: non è ancora dimostrato se il crash sia nel getter o nel traffico.
Rawdump `rawdump-after-staged-ping-crash.bin`:13107200byte, SHA256
`b8fe9fac00b94bd6f62ce2b298037039b46fd946f4882ca07827e84e53115db9`.
Ultimo slot periodico1396.33; nessun panic acquisito.

**CONTROLLO:** boot nuovo `c8bf2d2d-3185-4fe4-be41-3591387b026f`,
MSS offline. Ping telefono→host USB10.0.0.2 a111s:1/1, exit0,
boot invariato. Non è successo cellulare. Stesso stato confermato347.54.
Rawdump preservato come `rawdump-after-usb-control.bin`,13107200byte,
SHA256 `dafe057cbe42974dfc828717c16882ba6d35e45998d50f6d9d3c50473cd0a56b`.
Log, tar del controllo e fotografia moduli in `refs/session3-20260919/`;
dettagli nell'operativo §8. I vecchi client/PID/logger non esistono più.

**IPOTESI / prossima prova:** separati `stats` e `ping` nello script staged,
con un checkpoint immediatamente prima del comando ping e nessun getter
nel ramo ping. Nessun addon rimosso e nessuna causa kernel dichiarata.
Ripristinare bootstrap con protezione86s; una sola prova per volta.
**Internet resta NON RISOLTO finché il ping3/3 non è verificato.**

## Terzo reboot prima del mux: ioctl ingress IPA bloccato

**FATTO:** boot c8, protezione86.16s rispettata (664.28→750.44),
secondo start756.45, ONLINE816.34. IPA pronto828.26, handshake828.273753,
DPM Open829.23 successo. Ingress829.24 non ritorna: helper10650 in D,
stack `ipa3_send_cmd+0x208` nel commit dell'header QMAP. Il binario stock
aspetta una completion hardware senza timeout, dopo submit e voto clock.
Nessun egress/WDA/mux/bearer/ping eseguito in questo boot.

**RISULTATO:** terzo reboot, nuovo slotB/OpenWrt boot
`07ca5714-6343-4c47-a412-625741059c13`, MSS offline a114.80 e273.95.
Rawdump c8 fino956.27,98slot, nessun panic:
`rawdump-after-c8-pipe-crash.bin`,13107200byte, SHA256
`9586f441be11030380133f8feec802590d54218542624a709f722cc59b090f9a`.
Stack `openwrt-c8-pipe-blocked.log`; estratti `*-last-slots-complete.log`
con NUL QMI interni preservati (rimuovere solo il padding finale).

**DIAGNOSTICA:** hung_task_panic0; pstore/debugfs prima non montati.
Montati, ma ramoops è pmsg-only (2MiB, record_size0): nessun panic disponibile.
Preparata prova ingress-only con logging IPC RAM basso livello esplicitamente
abilitato, un solo collector e checkpoint16/17; nessuna modifica PM/addon
o firmware. Dettagli nell'operativo §8. **Internet NON RISOLTO.**

## Prova IPC ingress riuscita e archiviata (2026-09-20)

**FATTO:** boot07ca, MSS1098.77→stop1184.95 (+86.18s)→start1190.95;
ONLINE1210.63 dopo un unico Set osservato. IPA1259.06, low RAM1260.09,
DPM1261.10, ingress1262.10→1262.11 exit0, checkpoint16/17 e conclusione1262.12.
Collector terminato1263.11; DPM11140/logger8736 ancora attivi a1315.04.
Nessun egress/WDA/mux/WDS/ping. Il log positivo mostra submit su canale12,
callback TX e ACK del comando19; causa del precedente stallo non risolta.

**ARCHIVI** in `refs/session3-20260919/`:
`openwrt-07ca-ingress-snapshot-01.tar`,1340928byte, SHA256
`88b7ca7df88fb384c9521f6c05d081544e9d1e33617640c79c5feebcc37f4464`;
`rawdump-after-07ca-ingress.bin`,13107200byte, SHA256
`912b0e0a2659e3766854681f7bad96f1c53ab0c3507c80269ef2c59542bb23e9`.
Tool v3, log e ricetta aggiornata nell'operativo §8.
Preparato `openwrt-data-staged.sh` per riprendere senza ripetere ingress:
egress/WDA/mux/WDS separati e checkpoint18/19, archivio tra le invocazioni.

## Quarto reboot: statistiche riuscite, confine prima del ping (2026-09-20)

**FATTO:** nel boot07ca la catena dati è stata completata a fasi separate:
egress1641.58, WDA1643.79, mux1646.01, WDS1649.23 con IPv4
`10.100.199.48/27`, gateway `10.100.199.49`, MTU1500. IP, route e route-get
verificati fino1707.78 su `rmnet_data0`.

**RISULTATO:** lo stage `stats`, ora isolato, è riuscito a1746.79
(rx0/tx10, 616byte): le letture sysfs sono escluse dal confine di questa prova,
non retroattivamente da ogni guasto precedente.
Il ping, senza alcun getter nel suo ramo, ha `before_ping_exec` a1808.08
ma nessun `after_ping`; quarto reboot e nuovo boot
`a28dc1c5-8505-4bae-a39a-308ce7faa168`. Nessun panic acquisito.
Prima del ping il TX era già avvenuto (10pacchetti) con RX0: la sola
trasmissione non era fatale, la prima risposta non è ancora distinta.

**ARCHIVI:** `rawdump-after-07ca-ping-crash.bin`,13107200byte, SHA256
`103395bf1fe3cdeeaefa7c6c76f07544221b6a61fafc20064db1c10bd052b9aa`;
snapshot per fase `openwrt-07ca-data-*`/`openwrt-07ca-cellular-*`;
checkpoint `07ca-ping-checkpoint-{390..394}.log`. Dettagli e prossima
ipotesi (hook opzionali rmnet, da isolare su boot fresco pre-VND)
in §8 dell'handoff operativo. **Internet NON RISOLTO.**

**Analisi stock:** la pista timer/wakelock ICMP del sorgente SHS lineage-20
è esclusa: non presente nel binario effettivo. Anche map_mask0 non ferma
l'init SHS stock. Nessun fix pubblico trasferito, nessuna maschera RPS inventata.
La prova candidata resta l'isolamento degli addon prima di MSS/IPA/VND,
dopo verifica statica dei cleanup. Dettagli nell'operativo §8.

## Isolamento addon completato nel boot a28d

**FATTO:** primo tentativo956.45 abortito in userspace: kmodloader esce255
per directory `/lib/modules/$(uname -r)` assente (rootfs6.12/kernel stock5.10).
Parent Z e APS live/refcnt0; strace `--help` conferma nessun `delete_module`.
Non è uno stallo APS. Risolto il prerequisito con directory privata vuota
`/tmp/rmnet-kmodloader/modules/$(uname -r)` e `LD_LIBRARY_PATH` solo per rmmod.
Nessun force, caricamento di moduli incompatibili o modifica firmware.

**RISULTATO:** tutti gli unload normali riusciti1362.93–1362.97,
ordine APS→PERF_TETHER→PERF→OFFLOAD→SHS. Fine1362.98, core/ctl/IPA/GSI
preservati, MSS offline, nessuna interfaccia rmnet. Checkpoint per modulo
verificati e test offline riproducibili in `test-rmnet-base-isolation.py`.
Archivi in `refs/session3-20260919/`:
`openwrt-a28d-base-isolation-complete.tar`, SHA256
`576738e723e9a360453b80685704c9ea507b504fa6bf64e6b5a0146ff760cb2e`;
`a28d-after-base-isolation-complete-checkpoints.bin`, SHA256
`03dc8a49c54af75e415c2857d2e3a76c13c6198cf3edd794a0d81dff408a7bc2`.

**ESPERIMENTO:** montata debugfs, tool/MCFG ripristinati, bootstrap8887
da1364.19, logger8931, MSS first1369.22, protezione86.18s rispettata.

**RISULTATO della pila ridotta:** ONLINE1519.54, ingress1521.73,
egress/WDA/mux/WDS fino1594.55 con IPv4 `10.101.158.83/29`,
gateway `10.101.158.84`, MTU1500; route-get1632.07 e statistiche1634.34
(rx0/tx9) riusciti. Il ping unico si ferma dopo `before_ping_exec`1698.06:
quinto reboot, nuovo boot `f2064c04-e0b0-41fa-b9ed-746808a2b741`.
**Rimuovere i cinque addon non risolve**; l'ipotesi addon come causa del
reset è ora falsificata per questo percorso.

**EVIDENZA NUOVA:** lo stream `/dev/kmsg` salvato sull'host termina a
1698.131329 con `ipahal ipa_hw_opcode_to_opcode:1396 unsupported Status
Opcode 0x0`, ~75ms dopo il comando. Primo indizio kernel specifico del
confine, non ancora una causa dimostrata. Archivi e dettagli in §8
dell'handoff operativo. **Internet NON RISOLTO.**

## Smoke kprobe raw32+LR verificato (boot f206, 2026-09-20)

**FATTO:** hardware live IPA5.1 (DT enum22). Lo smoke iniziale falliva
per l'array di memoria utente `+u0(...):x64[4]`, respinto dal parser
trace5.10; non per indisponibilità dei kprobe. Diagnosi con fasi e strace.
Quattro letture scalari e fixture con write esplicita hanno prodotto
un solo evento con i32byte esatti e LR, PASS1373.86/cleanup0.
MSS sempre offline, stesso boot, probe USB vendor preservati.

**ARCHIVIO:** `refs/session3-20260919/f206-kprobe-smoke3.tar`,
339456byte, SHA256
`3a3eb2f7cb49d59d4dad7adb66deb687ab1eb2578f2e09cc0ac135a6b83d5f5a`.
Tentativi abortiti conservati; dettagli e fonti nell'handoff operativo.
Segue validazione entry IPA con TAG di controllo, non un altro ping cieco.
La cattura userspace/rawdump non garantisce l'ultimo record in caso
di panic immediato. **Internet NON RISOLTO.**

## Entry IPA validata sui TAG reali (f206)

**RISULTATO:** bootstrap1874.76/stop1960.94/start1966.95; ONLINE2038.52.
Ingress2097.28 riuscito sotto sonda stock entry; tre hit LAN, zero miss,
opcode1/src14/dst16/len8. Tutti i696byte coincidono con lo slot395 rawdump.
Sonda rimossa2144.28; nessun egress/WDA/mux/WDS/ping ancora in f206.
Archivio `openwrt-f206-ingress-trace.tar`, SHA256
`e03ce7f9d510180236c745d63eb9b9f71e4324b2332365e044bbe3d30dbdc1b5`;
rawdump `0eb0db347efc1d341b03059926fc0f265960eb07b389a154459c26e924375e0f`.
Dettagli, tool v4 e limiti nell'handoff operativo. **Internet NON RISOLTO.**

**AGGIORNAMENTO f206 dati:** bearer2423.54, IP/default/route-get2480.05,
stats2480.34 riusciti (10.180.238.160/26, gateway10.180.238.161, RX0/TX9).
Nove ulteriori TAG LAN, nessun WAN/opcode0;2088byte tutti persistiti.
`openwrt-f206-network-trace-final.tar` SHA256
`6f735539e2975cc92a5518351a9bbba747bf537cbe5ddca12311026043060e6d`;
rawdump `e6a0bfe900bdd3a57e4698cb28a62f4e3f7c6e8b96b54257a26ab3996bad8795`.
Il successivo ping strumentato separato, senza modifiche formato/addon,
ha preceduto il sesto reboot.

## Ping f206: status WAN nullo e limiti della cattura

**FATTO:** preflight trace/logger/DPM/WDS valido2895.88; ping2930.60.
Un record host2930.683119: LR `.text+0x5a818`/WAN, raw32 interamente zero.
Nessun after_ping o risposta verificata. Nuovo bootca60/MSSoffline.
Rawdump `rawdump-after-f206-ping.bin` SHA256
`1f51985322b2bfc97b560de44c0ce6bff848dcecac2a4d19adc54f9a0f6ba856`.
Checkpoint394 persistito, ultimo logger349@2929.14, slot395 ancora total0:
il record WAN è salvo soltanto nell'host, non nel rawdump.
`/dev/kmsg` era un file regolare del chroot; usare il char1:11
`/proc/1/root/dev/kmsg` con preflight `test -c` per future catture.
Il primo audit stock mostra che pkt_len0 viene saltato consumando32byte:
non attribuire il crash a quel record senza lunghezza residua/seguito.
Archivio `openwrt-f206-ping-evidence.tar` SHA256
`5a7423cf4c4139ed0b868fcd193a1f102b13dd8d05b9266b03ce683949ffa332`.
Approfondire buffer GSI/setup Android; non ripetere il ping identico.
**Internet NON RISOLTO.**

## Prova ingress AGG_DATA preparata e validata offline (2026-09-20)

**FATTO / sorgente:** l'audit del ramo WAN stock collega
`RMNET_IOCTL_INGRESS_FORMAT_AGG_DATA` a
`ipa3_disable_apps_wan_cons_deaggr()` e al flag
`ipa_client_apps_wan_cons_agg_gro`; il percorso page-backed resta selezionato
da `ipa_wan_skb_page && in->napi_obj`. Le costanti stock in
`kernel-patch-test/stock-kernel-source/techpack/dataipa/drivers/platform/msm/ipa/ipa_v3/ipa_dp.c`
sono `IPA_GENERIC_RX_BUFF_BASE_SZ=8192` e
`IPA_GENERIC_AGGR_PKT_LIMIT=0`. I valori Android netlink
`16000/32` non sono stati copiati: appartengono all'aggregazione UL.

**ESPERIMENTO offline:** `qrtr/rmnet-config.c` ora mantiene invariati
MAP/DEAGGREGATION/DEMUXING e aggiunge soltanto
`RMNET_IOCTL_INGRESS_FORMAT_AGG_DATA`; il payload è
`flags=0x2e`, `agg_size=8192`, `agg_count=0`. Egress resta `0xa`.
La struct legacy usata dalla build del progetto è verificata a 24 byte,
con union a offset4 e campi ingress a offset8/12, tramite
`/home/user/nx679j-kernel/kernel_platform/msm-kernel/include/uapi/linux/msm_rmnet.h`.
Il test mock non apre socket né invoca ioctl reali e rifiuta ogni payload
diverso; il controllo GRO_HW resta obbligatorio.

**RISULTATO offline:** compilazione ARM64 statica
`qrtr/rmnet-config-agg8192` con `aarch64-linux-gnu-gcc -static -Os
-Wall -Wextra -Werror` riuscita. Mock host ASan/UBSan riuscito per
combined/ingress-only/egress-only:
`INGRESS selector=7 flags=0x2e`, `EGRESS selector=6 flags=0xa`,
`PASS: stock rmnet layouts, ingress AGG_DATA 8192/0`.
Hash sorgenti: `rmnet-config.c`
`94860573cac63ba962539613d3576bf9209c57ec3961a1b6d0508df709fbba52`,
test `517dcd4551f97260f6a400e95c2e601316d37eba96bc5760c7afaa322fc9eb5e`.
Hash binari: `edb5e4a5134462131a1fe156da2aeaabdd75a331dbef479815df6a842cefe0ec`
e `bf0880da0abb0ef578036010f1937ae717557029beacf84ba6c6c1cc530336b5`.

**IPOTESI non ancora provata su hardware:** il bit aggiunto dovrebbe far
saltare il parser status WAN nel callback quando lo skb RX è interamente
page-backed/non-lineare; non dimostra ancora sopravvivenza del modem né
ricezione IP. La prossima prova hardware deve essere una sola ingress
configurazione con logger `/proc/1/root/dev/kmsg`, rawdump protetto e nessun
ping finché il primo status WAN non è osservato. **Internet NON RISOLTO.**

## Prova ca60 ingress AGG_DATA 8192/0 (2026-09-20)

**FATTO:** boot `ca60b6b2-9f6d-4a73-bed2-fe3b1b15fe7f`; il MSS è stato
selezionato per nome (`4080000.remoteproc-mss`, `remoteproc2`) e l'ADSP è
rimasto offline. Il DMS observed check ha verificato 5→0 con readback fresco.
IPA è stata inizializzata una sola volta e l'endpoint è `1`, con pipe
consumer2/producer23. I primi tre stop sono stati userspace-only: debugfs
non montato, log checkpoint non creato e lock collector orfano; nessuno di
questi ha emesso l'ioctl ingress. Il precheck è stato archiviato in
`refs/session3-20260919/openwrt-ca60-ingress-precheck.tar`, SHA256
`758cad3143fab11262879a4e20a53d9ba8ec13899c90d37dd5bc7c8a268b9def`.

**ESPERIMENTO:** dopo il mount debugfs e il riuso controllato di IPA/DPM,
unico resume hardware: checkpoint16 durevole, `rmnet-config-agg8192
rmnet_ipa0 ingress`, `flags=0x2e`, `agg_size=8192`, `agg_count=0`,
`INGRESS accepted`, exit0, checkpoint17 durevole. IPC stock registra
`get AGG size 8192 count 0`, `set aggr_limit 6`, page-repl capacity client35
450, setup WAN riuscito e `ipa3_cfg_ep_status` pipe23 con `status_en=0`.

**RISULTATO:** la configurazione AGG_DATA è stata accettata e il percorso
page-backed/deaggregazione WAN risulta applicato; non è ancora un test RX:
non sono stati generati bearer o pacchetti e non compaiono
`handle_page_completion`/eventi WAN parser nel log. Rawdump completo:
`rawdump-after-ca60-ingress-agg8192.bin`, SHA256
`192d38b4292c95e88850ec6edf9df2b3bbfc1e18a2657fa959d73cf89f6d5aa2`.
Log: `openwrt-ca60-ingress-agg8192.tar`, SHA256
`9cf8b9c236fdc2dd8b0fecc847c30b22aaa973f894312fabe4731609e4328483`.
La causa del precedente status WAN nullo non è ancora dimostrata e
**Internet NON RISOLTO.**

## Goal finale raggiunto: ca60 AGG_DATA + WindTre IPv4 (2026-09-20)

**FATTO:** nello stesso boot `ca60b6b2-9f6d-4a73-bed2-fe3b1b15fe7f` sono
rimasti MSS `remoteproc2` running e ADSP offline. La catena completa è
riuscita in ordine: ingress `0x2e`/8192/0, egress `0xa`, WDA
raw-IP/QMAP5, mux `rmnet_data0` mux1, WDS APN `internet.it`, IPv4
`10.176.226.211/29`, gateway `10.176.226.212`, MTU1500.

**VERIFICA FUNZIONALE:** `rmnet_data0` ha ricevuto l'indirizzo e la default
route; `ip route get 8.8.8.8` ha restituito
`via 10.176.226.212 dev rmnet_data0 src 10.176.226.211`. Il test singolo
`ping -c 3 -W 2 8.8.8.8` ha prodotto 3 risposte, `0% packet loss`,
RTT min/avg/max `26.341/38.886/50.806 ms`. Contatori dopo il ping:
RX3/RX bytes252, TX14/TX bytes916.

**EVIDENZA PARSER:** la sonda ha catturato quattro record parser tutti sul
caller LAN offset `0x58e18`; non è comparso alcun record WAN offset
`0x5a818` durante il ping. Questo è coerente con il bypass del parser WAN
attivato da AGG_DATA/status_en=0, ma il risultato principale è il ping
funzionale verificato.

Rawdump finale:
`rawdump-after-ca60-ping-agg8192.bin`, SHA256
`312325ede36635c2a7bec81a7231fdf3636984db784946247da6170ad0121de2`.
Archivio completo:
`openwrt-ca60-ping-agg8192.tar`, SHA256
`9f251031b900a8b2a8143b621981b0f26490616a396f78dcd93176cf4f7071ac`.
**RISULTATO: IP, default route e ping 3/3 sono verificati nello stesso
setup. Internet RISOLTO.**
