# NX679J — HANDOFF OPERATIVO (fine sessione 2, 2026-09-19 sera)

**Obiettivo:** da OpenWrt sul Nubia RedMagic 7 NX679J (SM8450, modem X65) usare la
SIM WindTre fino a Internet reale.

**Criterio di successo (TUTTI e tre, altrimenti NON risolto):**

```text
rmnet_dataN con IP assegnato
route default via gateway su rmnet_dataN
ping -c 3 -W 2 8.8.8.8  ->  3 risposte, 0% packet loss
```

**Stato: NON RISOLTO.** DMS5→0 e bearer WDS riprodotti su OpenWrt.
IPv4 e default route su `rmnet_data0` applicati e verificati in quattro boot.
Sei reboot del SoC; nessun ping cellulare riuscito.
**Confine ora ristretto:** statistiche riuscite in due boot; il reset segue
il checkpoint immediatamente prima del comando ping nei boot07ca, a28d e f206.
Il terzo reboot (c8) è invece avvenuto nello stallo ingress, prima del ping.
**Evidenza nuova (boot a28d):** ultimo record kernel prima della perdita di SSH
`ipahal ipa_hw_opcode_to_opcode:1396 unsupported Status Opcode 0x0`.

**Stato corrente:** slotB/OpenWrt, boot
`ca60b6b2-9f6d-4a73-bed2-fe3b1b15fe7f`, MSS offline; nessuna nuova prova dati.
Il boot f206 aveva ONLINE, ingress, egress/WDA/mux/WDS e IPv4/default/statistiche
riusciti: `10.180.238.160/26`, gateway `10.180.238.161`, MTU1500.
Il ping separato2930.60 ha preceduto il sesto reboot. Sonda host:
primo record WAN a2930.683119, tutti32byte zero, LR `.text+0x5a818`.
Non prova che sia echo RX né identifica ancora il percorso di crash.
Rawdump f206 preservato integralmente prima di nuove scritture.
Nel boot precedente a28d: tool/MCFG ripristinati, cinque addon rimossi
normalmente prima di MSS/IPA/VND, catena dati completa e ping fallito.
Tutti i client, logger, DPM e bearer dei boot precedenti sono persi: non
riusarne i PID. Rawdump e log archiviati prima di qualsiasi scrittura.

**Risultati consolidati della sessione3:** prove passive pd-mapper/IPA/ADSP/
DPM/pipe/WDA/mux concluse e archiviate, senza ONLINE. La cattura Android
completa ha invece individuato il Set DMS0x2e nativo con TLV0x10/5 byte zero:
validato indipendentemente e poi efficace per OpenWrt5→0 in sei boot.
La diagnosi attiva riguarda ora la verifica dati, non più la transizione DMS.
Ricetta, limiti, evidenze e risultati cronologici in §8.

---

## 1. Dove siamo (tutto verificato live su hardware)

- **Il modem su OpenWrt si avvia COMPLETO**: 56→59 servizi QMI annunciati su QRTR,
  inclusi WDS(1), DMS(2), NAS(3), UIM(11), EFS(21), PDC(36), IPA(49).
- **EFS funzionante**: rmtfs (service QRTR 14) serve modemst1/modemst2/fsg/fsc; nei
  log si vedono `open /boot/modem_fs1`, `write 0:5120 @0xd4500000` dal modem.
- **Config MCFG funzionante**: il modem scarica via TFTP (service 4096) da
  `/rfs/readonly/firmware/image/modem_pr/...`: `so/848_0_0.mbn` (476KB), i digest
  `mcfg_hw/mbn_hw.dig` e `mcfg_sw/mbn_sw.dig`; scrive `readwrite/mcfg.tmp` e
  `readwrite/server_check.txt` ("hello").
- **Riavvio protettivo efficace nella precedente osservazione passiva**
  oltre20min. Non garantisce stabilità della parte dati: cinque reboot successivi
  sono documentati in §8.

### Gap storico della sessione2: transito `mode 5` -> `mode 0`

**Superato in sessione3:** il testo seguente descrive l'evidenza disponibile
allora. La transizione OpenWrt5→0 è ora verificata; vedere §8.

`DMS Get Operating Mode (0x2D)` sul modem OpenWrt risponde **u8=5 ("shutting down")**
e non è MAI passato a 0 finora (attese osservate: 45s, 340s, 568s, 225s dopo restart).

La sessione precedente riporta Android ONLINE dopo ~200s dal restart e
WINDTRE LTE IN_SERVICE con mDataConnectionState=2. **Questa baseline va
riconfermata con nuove risposte DMS e rete correlate all'uptime**: i file
`android-mode.log`/`android-mode2.log` disponibili non documentano il flip a0.
Nel trace ptrace di qcrilNrd, negli ultimi secondi prima del flip compare solo
polling UIM e non compare DMS Set Operating Mode. Questo non dimostra l'assenza
di azioni QMI da ogni altro processo Android.

## 2. RICETTA DI AVVIO su OpenWrt (ordine obbligatorio!)

Tutti gli strumenti sono in `/tmp` dopo push — `/tmp` si svuota a ogni reboot:
ripushare da `/home/user/nx679j-stock/experiments/` (vedi §4).

```sh
# 1. shared memory (il driver che l'init NON carica) + device UIO
/tmp/finitmod /proc/1/root/lib/modules/msm_sharedmem.ko
[ -e /dev/uio0 ] || mknod /dev/uio0 c 239 0          # 239:0, name "rmtfs"

# 2. nodi partizioni EFS (da /sys/block/sdf/sdfN/dev; attesi 8:82..8:85)
mkdir -p /tmp/efs
for p in 2:modemst1 3:modemst2 4:fsg 5:fsc; do
  N=${p#*:}; M=$(cat /sys/block/sdf/sdf${p%:*}/dev)
  [ -e /tmp/efs/$N ] || mknod /tmp/efs/$N b ${M%:*} ${M#*:}
done

# 3. rmtfs (patchato: uio diretto, no udev) -> service 14
/tmp/dspawn /tmp/rmtfs.log /tmp/rmtfs -P -o /tmp/efs -v
sleep 2; /tmp/qmi-qrtr lookup 14      # DEVE rispondere, altrimenti non proseguire

# 4. tqftpserv (patchato: 4096 v1..10, root /rfs)
mkdir -p /rfs/readwrite/ota_firewall /var/lib/tqftpserv
/tmp/dspawn /tmp/tqftpserv.log /tmp/tqftpserv -d

# 5. logger persistente (su rawdump, sopravvive ai reboot)
/tmp/dspawn /tmp/crashlog.log /tmp/crashlog-dump.sh

# 6. trasporto QRTR del modem
/tmp/finitmod /proc/1/root/lib/modules/qrtr-smd.ko

# 7. modem up
/tmp/rprocstart /sys/class/remoteproc/remoteproc3/state start

# 8. RIAVVIO CONTROLLATO entro ~90s (previene l'auto-reset che riavvia il SoC!)
sleep 86
/tmp/rprocstart /sys/class/remoteproc/remoteproc3/state stop
sleep 6   # attendere state=offline
/tmp/rprocstart /sys/class/remoteproc/remoteproc3/state start

# 9. sorveglianza mode (txn SEMPRE < 0x100 nel raw!)
/tmp/qmi-qrtr raw 2 0001002d000000 | grep -o 'u8=[0-9]*'    # txn=1; 0=online, 5=shutting down
```

Note:
- I lotti `/rfs` (tree modem_pr da 165 file, 21MB) e i binari sono già pronti in
  `experiments/`. Il tar contiene direttamente `modem_pr/`: se manca il tree,
  creare `/rfs/readonly/firmware/image` ed estrarre lì:
  `cat refs/modem_pr.tar | ssh ... 'tar -C /rfs/readonly/firmware/image -xf -'`.
  Verificare il risultato `/rfs/readonly/firmware/image/modem_pr/{mcfg,so}`.
  Il precedente comando `tar -C /` era errato per questo archivio.
- Un solo rmtfs per volta: se lo si uccide, il service 14 sparisce (del_client) e va
  riavviato.
- mss = **remoteproc3** su OpenWrt, **remoteproc4** su Android.

## 3. FATTI già falsificati — NON ripeterli

| Tentativo | Esito |
|---|---|
| Frame QMUX raw su /dev/smd7/8/11 | write -> EBUSY; /dev/smdcntl8 open -> timeout. Non è il confine. |
| Secondo open di /dev/rmnet_ctrl | EBUSY/EPERM: single-owner (qti lo tiene). |
| Vecchi tentativi `DMS Set Operating Mode` (0x2E) | Errore1 riportato; non ripetere quei pacchetti. **La conclusione “non è il canale” è smentita dalla nuova cattura nativa**: 0x2e con TLV0x10/5 byte zero riesce. I vecchi frame originali non sono stati ritrovati nella conversazione; non attribuire ancora con certezza l'errore alla loro omissione del TLV. |
| `UIM Power On SIM` (0x31), `PDC Get/List` (0x22/0x24) | idem: malformed. Non insistere. |
| Playback dei 137 msg QMI che qcrilNrd manda al modem | 137/137 con risposta: non basta a far partire il flip. Il contatore del tool non verifica ogni result/error. |
| UIM "Change Provisioning Session" (0x38) | error 2/3; card status BYTE-IDENTICA ad Android (stessa SIM, stesso stato): non è la discriminante. |
| `/dev/subsys_modem` + keepfileopen (via pmOS msm-modem-downstream) | Il device NON esiste su questo kernel: non applicabile. |
| Attese passive di 200-570s post-restart | mode resta 5. |

## 4. Strumenti e file (esperimenti/)

`/home/user/nx679j-stock/experiments/`:

- `qrtr/qmi-qrtr` — client QMI-over-QRTR: `list`, `lookup <svc>`, `raw <svc> <hex>`,
  `rawseq <svc> <hex...>` (stesso socket), `playback <file>` (replay "fd svc hex"),
  `wds-status`, `wds-get-settings`, `wds-start <apn> [ipfam]`, `hello`, `watch`.
  Nota: nei `raw` usare txn `< 0x100` (bug noto con txn > 0xFF).
- `qrtr/rprocstart` — start/stop remoteproc senza bloccare (fork).
- `qrtr/finitmod` — finit_module.
- `qrtr/dspawn <logfile> <cmd...>` — daemonizza.
- `qmi-trace` — tracer ptrace aarch64 (TRACECLONE, payload sendto/recvfrom, sockaddr,
  timestamp) — usato con successo su Android su qcrilNrd/qti.
- `crashlog-dump.sh` — logger persistente su rawdump (slot 32KB da offset 1024KB,
  ogni 3s: dmesg filtrato + tail rmtfs/tqftpserv + conteggio servizi).
- `refs/rmtfs-master` + binario `refs/rmtfs` — patchato per /dev/uio0.
- `refs/tqftpserv-master` + binario `refs/tqftpserv` — patchato (root /rfs + v1..10).
- `refs/modem_pr.tar` + `refs/modem_pr/` — tree mcfg completo.
- `refs/android-trace2-qcril.log` + `refs/parse-trace.py` + `refs/qcril-playback.txt`
  + `refs/extract-playback.py` — trace completo init qcril post-restart e replay.
- `refs/mm-broadband-modem-qmi.c` — il flusso ModemManager per "power on"
  (Set Event Report TLV 0x14 opmode=TRUE -> Set Operating Mode; fallback
  "DMS Set FCC Auth" 0x555F/0x5571 se INTERNAL/INVALID_TRANSITION).
- `refs/android-portmap.txt` — mappa porta->servizio dei servizi modem (node 0).

Nomi/costanti utili: `/rfs` root tqftpserv; uio0=239:0; EFS=sdf2..sdf5; rawdump=sda11;
boot_b=sde41 (mknod b 259 25 se manca); reti host: 10.0.0.2/24 su enp103s0f3u1,
telefono 10.0.0.1.

## 5. Sequenza storica delle ipotesi (eseguita; risultati aggiornati in §8)

1. **pd-mapper** (l'unico daemon della pila pmOS che non gira): port/build da
   `linux-msm/pd-mapper` (aarch64 static; si appoggia a libqrtr). Su Android il
   `pd-mapper` è attivo; pubblica i "protection domain"/servreg che potrebbero essere
   il prerequisito del transito a ONLine. Dopo averlo avviato: restart modem (ricetta
   §2 punto 8) e sorvegliare il mode.
2. **Init IPA lato AP**: su Android al boot compare `QMI_IPA_INIT_MODEM_DRIVER_REQ_V01`
   (ipa_fmwk + ipacm). Su OpenWrt ipacm non gira. Capire se ipa_fmwk può fare
   l'init da solo (modprobe parametri? sysfs?) o costruire un mini-ipacm.
3. **Sorveglianza lunga passiva** (15-20 min, solo osservazione, nessun comando):
   escludere che su OpenWrt il flip sia semplicemente più lento che su Android (~200s).
4. **SSCTL (service 43)**: sul fw questo modem annuncia SSCTL v2 con instance 18; ha
   messaggi vendor-specific. Da esplorare (nessun dump pubblico noto).
5. Appena `mode=0`: completare la parte dati:
   `wds-start internet.it` -> `wds-get-settings` (IP/gw/DNS/MTU) -> capire quale
   `rmnet_dataN` corrisponde alla call (su Android era rmnet_data2@rmnet_ipa0;
   con IPA la mappatura call->iface va verificata) -> `ip addr add` + `ip link set up`
   + `ip route add default via ...` -> PING.

## 6. Regole operative (imparate a caro prezzo)

- Un esperimento alla volta; FATTO vs IPOTESI sempre separati.
- MAI dichiarare risolto senza il ping a 3 risposte.
- Il restart del modem a ~90s (punto 8 della ricetta) NON è opzionale: senza, il modem
  si auto-resetta e riavvia tutto il SoC (perdendo lo stato).
- Prima di inventare: cercare online (pmOS wiki "Modem"/"Qualcomm Modem Debugging",
  libqmi, ModemManager, linux-msm/{qrtr,rmtfs,tqftpserv,pd-mapper}) e in `refs/`.
- Gli heredoc `ssh` oltre ~1KB si TRONCANO: scrivere script locali e pusharli
  (`cat script | ssh ... 'cat > /tmp/x; chmod 755 /tmp/x; sh /tmp/x'`).
- Su Android il quoting `su -c` annidato è fragile: pushare script in
  /data/local/tmp ed eseguirli con `su -c 'sh ...'`.
- pstore vuota, rawdump `/dev/rd` = finestra persistente: il crashlog-dump è l'unica
  osservabilità pre-reboot.

## 7. Stato storico del telefono a fine sessione2

- Slot **B (OpenWrt)** attivo; SSH `root@10.0.0.1` (`~/.ssh/nx679j_key`).
- Modem avviato con la ricetta: **mode=5**, 59 servizi, rmtfs+tqftpserv attivi,
  uptime > 20 min, NESSUN crash.
- Cambio slot senza toccare il telefono: push `reboot2` -> `/tmp/reboot2 bootloader`
  -> `fastboot set_active a|b; fastboot reboot`.
- Android (slot A): funzionamento riportato nella sessione precedente,
  da riconfermare prima di usarlo come controllo positivo.

## 8. SESSIONE 3 — pd-mapper (2026-09-19, aggiornamento durante la prova)

### FATTI

- Controllo iniziale: stesso boot OpenWrt stabile, 59 servizi, DMS=5,
  rmtfs/tqftpserv attivi, solo IP e route USB. Nessun reboot del telefono.
- Fonti consultate: wiki pmOS `Modem` e `Qualcomm_Modem_Debugging`;
  `linux-msm/pd-mapper`, commit `5ecd2fe926aca7abfe40724177f63b942cff3947`.
- Sorgenti in `refs/pd-mapper/`; build locale `sh build-pd-mapper.sh`;
  output `refs/pd-mapper-static`. Compilazione statica AArch64 riuscita
  (`-Os -Wall -Wextra`; warning preesistenti upstream conservati nel log).
- Le 7 mappe stock `.jsn` sono già leggibili, senza modifiche, in
  `/proc/1/root/lib/firmware/`. `modemr.jsn` contiene `msm/modem/root_pd`,
  istanza 180. Build con `FIRMWARE_BASE` su quel percorso e `NO_LZMA`
  (mappe non compresse); aggiunto logging di mappe e richieste.
- Con libqrtr corrente non adattata, `qrtr_publish()` restituiva successo
  ma il lookup non trovava service 64. Adattamento in
  `refs/qrtr-master/lib/qrtr.c`: bind esplicito prima di pubblicare e
  valorizzazione node/port in NEW_SERVER, secondo l'ABI legacy nelle reference.
  Dopo l'adattamento: **service 64, version=1, instance=1, node=1**.
- Verifica locator locale: GET_DOMAIN_LIST `tms/servreg` restituisce success
  e 7 domini. Il modem (node 0) interroga effettivamente il daemon per
  `tms/servreg` (7), `avs/audio` (1), `tms/pddump_disabled` (0).
- Il rawdump è `/proc/1/root/dev/rd`, non `/dev/rd` dentro OpenWrt:
  verificato sysfs `sda11`, 8:11, PARTNAME=rawdump. Finestra precedente salvata in
  `refs/session3-20260919/rawdump-before-pd.bin` prima di riattivare il logger.
- Correzione dei nuovi campioni: `0001002d000000` usa txn=1; il precedente
  `0001012d000000` usa in realtà txn=257. Entrambi hanno restituito DMS=5,
  ma i nuovi script rispettano il limite txn<0x100 dell'handoff.

### ESPERIMENTO / RISULTATO pd-mapper

`pd-cycle.sh` è stato pushato ed eseguito tramite dspawn. Mantiene rmtfs e
tqftpserv, riattiva il logger (ora include pd-mapper), riavvia il modem e
applica automaticamente il restart protettivo a 86 secondi. Successivamente
campiona ogni 15 secondi per 660 secondi, terminando prima solo a mode=0.

Primo start uptime=28758; stop protettivo uptime=28844; secondo start=28850.
Ultimo campione uptime=29510: elapsed=660, DMS=5, 60 servizi, running.
Il log termina con `RESULT: no ONLINE transition in 660 seconds after
protective restart`. Boot ID invariato: `2ca42e08-8c9a-4d61-8ae3-2b632149d26a`.
**RISULTATO: pd-mapper funzionante non è sufficiente a portare ONLINE
in questa finestra.** Non è una prova che il daemon sia inutile.

Log telefono: `/tmp/pd-cycle.log`, `/tmp/pd-mapper.log`,
`/tmp/pd-dmesg-before.log`, `/tmp/pd-dmesg-after.log` a fine prova.
Copie host e build log: `refs/session3-20260919/`, incluso `pd-final.tar`.

### ESPERIMENTO IPA, osservazione completata

IPA: moduli presenti, class device `/sys/class/ipa/ipa/dev` = 503:0 in
questo boot, ma `/dev/ipa` assente nei due root. La semplice presenza dei
moduli non prova l'inizializzazione IPA.

**FATTO aggiornato:** a uptime=29354 `ip link show` non presenta né
`rmnet_ipa0` né `rmnet_dataN`. ADSP/CDSP/SLPI/SPSS sono offline; solo MSS
(remoteproc3) è running. Non usare le interfacce Android storiche come
fotografia di questo boot OpenWrt.

**ABI verificata:** sorgenti correlati
`LineageOS/android_kernel_qcom_sm8450-modules`, lineage-20,
commit `c1e096fa00a6a90568f8e16720f8c9749102833b`, in `refs/ipa-lineage20/`.
`ipa.c:8368-8470` accetta la stringa ASCII `1` in `ipa3_write`, verifica
`ipa3_is_ready()` e genera `IPA_FW_LOAD_EVNT_FWFILE_READY`. Carica i firmware
originali già montati, senza modificarli. Verificato anche nel vero
`stock-modules/ipam.ko`: disassemblato in
`refs/session3-20260919/ipa3-write-stock.asm`, confronto `0x31` a `0x2ccd0`.
Il kernel dovrebbe poi pubblicare IPA AP service 49/instance 1 e inviare
`QMI_IPA_INIT_MODEM_DRIVER_REQ_V01`; prima del trigger era visibile solo
l'istanza modem 2.
Il codice QMI può chiamare `BUG()` in caso di handshake fallito: logger
persistente e backup obbligatori prima della prova.

Preparati `ipa-init.sh` (un trigger, poi massimo 1200 secondi di campioni)
e `qrtr/ipa-trigger.c` (major/minor da sysfs, verifica character device,
un solo byte `1`, allarme userspace 5s). Build statica con
`aarch64-linux-gnu-gcc -static -Os -Wall -Wextra -Werror` riuscita.
`stat` e `timeout` non sono disponibili nel rootfs; l'helper evita di
dipenderne. Non sovrascrivere il logger in esecuzione: arrestarlo,
salvare la finestra rawdump, poi avviare la versione aggiornata.

**FATTI misurati dopo il trigger:**

- Finestra rawdump pd salvata in `rawdump-before-ipa.bin` (13107200 byte).
  Logger precedente PID27325 arrestato; nuovo logger PID32026,
  script IPA PID32042. rmtfs/tqftpserv/pd-mapper lasciati attivi.
- Scrittura di un byte `1` riuscita a uptime=29612.
- `IPA FW loaded successfully` @29612.189598; driver ready @29612.195645.
- `rmnet_ipa0` ora esiste (ifindex15); nessuna `rmnet_dataN` ancora.
- Request IPA inviata @29612.196582, **response received @29627.205277**.
- Service49/instance1 AP pubblicato su node1/port17512; istanza2 modem
  ancora node0/port26. A uptime=29631 DMS ancora 5, nessun reboot.
- `ipa-init.sh` osserva ogni15s, termina a mode0 o dopo1200s dal trigger
  (deadline circa30812). Non introdurre altre modifiche hardware durante
  la finestra. Log live `/tmp/ipa-init.log`, dmesg before/after e snapshot
  servizi/link sotto `/tmp/ipa-*`.
- Helper `qrtr/rmnet-inspect` legge solo RTM_GETLINK e decodifica parent/mux
  dai layout UAPI. Build cross `-Werror` e prova host ASan/UBSan riuscite;
  eseguito sul telefono, conferma l'apparizione di `rmnet_ipa0`.

**RISULTATO:** ultimo campione uptime=30812, elapsed=1200, DMS=5,
servizi=61, remoteproc running. Log concluso con
`RESULT: no ONLINE transition in 1200 seconds after IPA trigger`.
Boot ID invariato. Inizializzazione IPA ottenuta, ma non sufficiente nella
finestra completa di20 minuti. Nessun bearer, IP o ping ottenuto.
Archivio host verificato: `refs/session3-20260919/ipa-final-20260920.tar`.

### ESPERIMENTO separato: IPA pronto prima del restart modem

**IPOTESI:** l'ordine di avvio potrebbe contare; il trigger precedente è
avvenuto a modem già avviato. Il driver pubblico gestisce esplicitamente
SSR e ripete l'handshake (`ipa_qmi_service.c`, `rmnet_ipa.c` nelle reference).
Non è ancora dimostrato che questo cambi DMS.

**Preparazione:** logger precedente fermato e finestra salvata in
`rawdump-before-ipa-cycle.bin` (13107200 byte). Nuovo logger PID7180;
`ipa-cycle.sh` PID7213, log `/tmp/ipa-cycle.log`. Il logger include questa
prova. rmtfs/tqftpserv/pd-mapper sono stati mantenuti attivi.

**FATTO misurato prima del restart:** helper in sola lettura
`qrtr/rmnet-query.c`, compilato con l'UAPI del kernel locale
`include/uapi/linux/msm_rmnet.h` e `-idirafter`. Ioctl0x89fd,
struttura24 byte verificata anche con assert statici:

- driver `rmnet_ipa0`, supported_features=7;
- endpoint=1; pipe consumer=2, producer=23;
- MRU=1000; scatter_gather=1.

Non sono stati configurati pipe, mux, link dati o WDS. I getter non
provano che le pipe siano attivate. Build cross `-Werror`, verifica host
ASan/UBSan degli argomenti e regressioni QMI passate.
Log getter telefono: `/tmp/rmnet-query-before-cycle.log`.

**Sequenza iniziata:** precondizione DMS5/61servizi a30912; stop30916,
start30922. Risposta IPA ricevuta30923.896833, circa11ms dopo la request.
Campione30953: DMS5, servizi61. Compare anche
`Got bad response 49 from request id 1 (error 48)` dopo la risposta IPA:
registrare separatamente, non interpretarlo come prova di fallimento del
caricamento firmware o come causa accertata di DMS5.
Lo script applica autonomamente il restart protettivo a86s e poi osserva
per660s. Non introdurre altre modifiche durante la prova.

**Ricerca per la fase successiva, non ancora sperimentata:** a30537
ADSP(remoteproc0), CDSP(1), SLPI(2), SPSS(4) offline; MSS(3) running.
Le mappe stock `adspr.jsn` e `adspua.jsn` assegnano istanza74 ai domini
`msm/adsp/root_pd` e `msm/adsp/audio_pd`. Il servizio `avs/audio`, realmente
richiesto dal modem a pd-mapper, è nel secondo dominio. `adsp.mdt` esiste.
Il kernel locale `drivers/remoteproc/qcom_sysmon.c` documenta SSCTL:
shutdown0x21, get_failure_reason0x22, eventi0x25 con transaction_id.
Gestisce già le notifiche tra remoteproc: nessun messaggio SSCTL inventato
è stato inviato.

### Chiusura del ciclo IPA pronto (2026-09-20)

**FATTO:** stop protettivo uptime31008, esattamente86s dopo start30922;
secondo start31014. Ultimo campione31674: elapsed660, DMS5, 61servizi,
MSS running, boot ID invariato. Log concluso con
`RESULT: no ONLINE transition in 660 seconds after IPA-ready protective restart`.
Il PID7213 è terminato (stato `Z`, parent1): `kill -0` da solo non dimostra
che un esperimento stia ancora girando.

**RISULTATO:** anche quest'ordine di avvio non è sufficiente nella finestra
misurata. Nessun bearer/IP/route/ping cellulare ottenuto.
Archivio host verificato `refs/session3-20260919/ipa-cycle-final-20260920.tar`
(1377792 byte, 13 voci). Logger7180 fermato, rawdump salvato in
`rawdump-before-adsp.bin` (13107200 byte); nuovo logger PID12789.

**Chiarimento da sorgenti:** in `ipa_qmi_service_v01.h` la richiesta49
(`0x31`) è `QMI_IPA_GET_APN_DATA_STATS_REQ_V01`, non init (`0x21`).
In `ipa3_check_qmi_response` il formato stampa prima req_id e poi result,
nonostante il testo ambiguo: `bad response 49 ... id 1` corrisponde a
req_id49/result1. Non attribuire a questa riga un fallimento dell'init.

**Diagnostica SSCTL:** nuovo `qmi-qrtr-next ssctl-reason`, istanza esatta
43/v2/18, richiesta0x22 senza payload da `qcom_sysmon.c`. Risposta
`02***********************e00`: result0/error94, nessun TLV della causa.
Non è emersa una causa utilizzabile; nessun shutdown/evento SSCTL manuale.
Il nuovo client conferma separatamente DMS5 con risposta/txn corretti.

**Prova ADSP preparata:** `adsp-init.sh` richiede il risultato IPA completo,
ADSP offline e firmware stock disponibile; avvia solo remoteproc0 e
osserva per massimo660s, lasciando invariati MSS/IPA/daemon.
`servreg-state 74 msm/adsp/audio_pd` registra temporaneamente un listener,
legge lo stato e lo rimuove sullo stesso socket, secondo `pdr_internal.h`.
Build statica `-Werror`, test host ASan/UBSan e sintassi shell superati.
Client pushato come `/tmp/qmi-qrtr-next`; il polling usa ancora il vecchio
`/tmp/qmi-qrtr` per comparabilità.

**FATTI iniziali della prova ADSP:** processo13127, logger12789.
Start remoteproc0 a uptime31874, running31875. Servizi61→69; MSS sempre
running e boot ID invariato. Servreg66/v1/74 su node5/port3 restituisce
`msm/adsp/audio_pd` = `0x1fffffff` (**UP**), result/error0.
Rimozione del listener sullo stesso socket confermata con result/error0.
Primo campione31875: elapsed1, DMS5. Deadline osservazione circa32534.
Log `/tmp/adsp-init.log`, `/tmp/adsp-audio-state.log`, snapshot
`/tmp/adsp-*`. Non introdurre altre modifiche hardware durante la finestra.

### Chiusura ADSP e nuova prova DPM (2026-09-20)

**RISULTATO ADSP:** campione finale uptime32535, elapsed661, DMS5,
69servizi, MSS/ADSP running, boot invariato. Log concluso con
`RESULT: no ONLINE transition in 660 seconds after ADSP start`.
PID13127 terminato, stato Z. Audio UP non è sufficiente in questa finestra.
Archivio host verificato `refs/session3-20260919/adsp-final-20260920.tar`,
1497600 byte, 14voci. Logger12789 fermato prima del backup
`rawdump-before-dpm.bin` (13107200 byte); nuovo logger17991.

**FONTI DPM:** libqmi commit `b7913df8b49330956f0337dcc6255f63751fdda1`,
`refs/qmi-service-dpm.json` e `qmi-service-wda.json`; ModemManager commit
`e1f8061541c974d046e264b1069716109a607e5e`, `refs/mm-port-qmi.c`.
MM apre DPM prima di WDA; RX modem è TX IPA, TX modem è RX IPA.
`rmnet_ipa.c:2753-2773` restituisce WAN_PROD come consumer e WAN_CONS come
producer nell'ioctl. Con i getter riconfermati si ricava:
EMBEDDED=4, endpoint1, RX modem2, TX modem23. Nessun ID copiato da altri SoC.

**ESPERIMENTO:** `dpm-init.sh`, PID18070, avvia solo DPM Open Port0x20
su service47/v1/0 (node0/port61). Client persistente PID18117,
`/tmp/qmi-qrtr-dpm dpm-session 4 1 2 23 3600`, conserva il socket fino
al limite3600s o SIGTERM; in uscita invia Close Port0x21 documentato.
Nessun restart MSS, WDA Set, ioctl pipe, mux o WDS in questa prova.
Build statica `-Werror`, test host ASan/UBSan dell'encoding e sintassi shell
superati. Script controlla fine ADSP, daemon/logger e tuple endpoint prima
della richiesta; campiona per660s, termina prima solo a ONLINE o errore.

**FATTI iniziali:** Open a uptime32590, risposta result0/error0 verificata.
Primo campione32591: DMS5/69servizi; a32606 elapsed15 ancora5.
WDA Get0x21 sull'endpoint4:1 prima di DPM: result1/error48; dopo DPM:
result0/error0, QoS0, link-layer2, aggregazione UL0/DL0. Questo è un
cambiamento osservato dell'accessibilità dell'endpoint, non prova di ONLINE.
`rmnet_ipa0` è ifindex17, DOWN, senza figli/IP; unica route USB10.0.0.0/24.
Log `/tmp/dpm-init.log`, `/tmp/dpm-session.log`, `/tmp/dpm-wda-{before,after}.log`,
snapshot `/tmp/dpm-*`. Deadline circa33251; nessuna modifica hardware
aggiuntiva prima della conclusione. **Internet NON RISOLTO.**

### DPM concluso; setup pipe IPA in osservazione

**RISULTATO DPM:** finale33251/elapsed660/DMS5/69servizi, boot invariato.
PID18070 zombie terminato; client DPM18117 mantenuto aperto e vivo.
Archivio verificato `dpm-final-20260920.tar` (1604608byte/16voci).
Logger17991 arrestato, `rawdump-before-pipes.bin` salvato (13107200byte),
nuovo logger23362. Nessun cambiamento radio durante la finestra.

**FATTO diagnostico:** NAS Get Serving System0x24 (sola lettura,
`qmi-service-nas.json`) ha result/error0; TLV1=`02 02 02 00 01 00`,
TLV0x21=`00 00 00 00 00`, nessun servizio radio disponibile.
UIO attuale=`0xd4500000`, size=`0x280000`; tutte le allocazioni rmtfs
nel log usano lo stesso indirizzo, result/error0, conteggio errori0.
Il precedente indirizzo `0xd4900000` è storico, non un valore da fissare.

**ESPERIMENTO pipe:** `pipe-init.sh` PID23387 controlla fine DPM, client
ancora attivo, daemon e logger; esegue una sola volta
`/tmp/rmnet-config rmnet_ipa0 pipes`, poi osserva660s.
Helper da UAPI stock `linux/msm_rmnet.h`: ingress selector7/flags0xe
(MAP/DEAGGREGATION/DEMUXING), egress selector6/flags0xa (MAP/MUXING).
Nessuna abilitazione checksum o aggregazione TX. Precontrollo non distruttivo
ETHTOOL_GFEATURES: GRO_HW bit55 dal kernel stock deve essere spento.
Build/test riproducibili con `sh build-data-tools.sh`, log
`refs/session3-20260919/data-tools-build.log`; `-Werror` e ASan/UBSan passati.

**FATTI iniziali:** entrambi gli ioctl accettati a33290.
Feature attive netdev: block0=`0x00004000`, block1=0; GRO_HW spento.
`rmnet_ctl driver probed` @33290.613740; setup low-lat prod @33290.616854.
WDA Get rimane result/error0, raw-IP2, aggregazioneUL0/DL0.
DMS5/69servizi al primo campione33290. Nessun WDA Set, mux, interfaccia dati,
WDS, restart MSS o modifica firmware. Deadline circa33950; non introdurre
altri cambiamenti prima della conclusione.
Log `/tmp/pipe-init.log`, `/tmp/pipe-ioctl.log`, `/tmp/pipe-wda-{before,after}.log`
e snapshot `/tmp/pipe-*`. **Internet NON RISOLTO.**

### Pipe concluse; negoziazione WDA verificata (2026-09-20)

**RISULTATO pipe:** campione finale33950, elapsed660, DMS5,69servizi,
MSS/ADSP running, boot invariato. PID23387 terminato, stato Z.
Archivio `refs/session3-20260919/pipe-final-20260920.tar` verificato:
1720832byte,19voci. Logger23362 arrestato; prima della sostituzione salvato
`rawdump-before-wda.bin` (13107200byte), SHA256
`e91eee6f4c94ff55ab231967c7554ea4a8efa1366b8e4b775d357e6c33437066`.

**FONTI / IPOTESI:** libqmi stesso commit sopra, `qmi-enums-wda.h` e
`qmi-service-wda.json`: RAW_IP2, QMAP5 senza estensioni checksum,
Set0x20/endpoint TLV0x17. ModemManager `mm-port-qmi.c` richiede rmnet
multiplexing per IPA. È un prerequisito dati documentato, non una causa
accertata del blocco DMS5.

**ESPERIMENTO:** `wda-init.sh` PID29240, nuovo logger29213.
Un solo Set, controllo result/error e formato restituito, Get sullo stesso
client e successivo Get su nuovo socket. Client `/tmp/qmi-qrtr-qmap`;
il binario `/tmp/qmi-qrtr-dpm` del DPM18117 non è stato sovrascritto.
DPM mantiene il socket; scadenza prevista circa36190. Lo script rifiuta
una finestra che intersechi tale scadenza, con90s di margine.
Nessun mux/link dati/WDS/restart MSS o modifica firmware nella prova.

**FATTI iniziali:** Set e Get result0/error0; QoS0, raw-IP2,
aggregazioneUL5/DL5 verificati. Altri valori restituiti, senza reinterpretarli:
TLV0x15=1,0x16=63,0x17=1,0x18=64512,0x1a=0,0x1b=0.
Primo campione dopo la configurazione33998: DMS5,69servizi.
Osservazione660s, deadline circa34658. Log `/tmp/wda-init.log`,
`/tmp/wda-set.log`, `/tmp/wda-get-{before,after}.log` e snapshot `/tmp/wda-*`.
Non introdurre altre modifiche finché questa prova non è conclusa.

**Preparazione offline successiva:** `qrtr/rmnet-link.c` crea esclusivamente
un link netlink, controlla ACK/errore e non assegna IP/route. Attributi
verificati in UAPI stock e `refs/ipa-lineage20/rmnet_config.c`.
Build statica `-Werror`, test host ASan/UBSan e sintassi shell passati:
`refs/session3-20260919/data-tools-qmap-build.log`.
Il trace Android contiene un bind WDS reale a endpoint4:1/mux1
(`android-trace2-analysis.txt`, eventi59 e241): mux1 ha quindi evidenza
stock; non attribuire automaticamente quel mux al vecchio bearer Android.
Nessun helper netlink eseguito ancora sul telefono in quella fase.

### WDA conclusa; prova mux isolata (2026-09-20)

**RISULTATO WDA:** finale34658/elapsed660/DMS5/69servizi, MSS e ADSP
running, boot invariato. PID29240 terminato (Z). Archivio host verificato
`refs/session3-20260919/wda-final-20260920.tar`:1839616byte,16voci.
Il formato dati è stato negoziato, ma non ha prodotto ONLINE nella finestra.
QMAP5 qui significa il valore5 dell'enum WDA (QMAP semplice), NON QMAPv5.

Logger29213 arrestato prima del backup `rawdump-before-mux.bin`,
13107200byte, SHA256
`62022bac13f3fd548d1e1e4e52569a97bd791f368302aed395b6e70859d1ded4`.
Nuovo logger3207; DPM18117 sempre vivo, scadenza prevista36190.

**ESPERIMENTO mux:** `mux-init.sh` PID3402. Guardie: WDA conclusa,
boot corretto, DPM vivo con tempo sufficiente, nessun link preesistente,
logger attivo. Una creazione netlink esclusiva, controllo RTM_GETLINK,
una notifica ioctl ADD_MUX_CHANNEL, poi link parent/figlio UP.
ABI da UAPI stock e `refs/ipa-lineage20/rmnet_config.c`/`rmnet_ipa.c`.
Build locale `-Werror`, test ASan/UBSan e shell superati; log
`refs/session3-20260919/data-tools-mux-build.log`.
Non sono stati inviati WDS Start, nuovi WDA Set o restart MSS.

**FATTI iniziali a34706:** `rmnet_data0` ifindex18, parent17 (`rmnet_ipa0`),
kind rmnet, mux_id1; rmnet flags1/mask0xffffffff riconfermati via netlink.
Notifica stock selector5 accettata. Parent e figlio UP/LOWER_UP.
IPv6 link-local generato dal kernel: NON è un indirizzo assegnato dal bearer.
Nessun IPv4 cellulare/default route; rimane la sola route USB10.0.0.0/24.
DMS5/69servizi. Finestra660s, deadline≈35366, prima della scadenza DPM.
Log `/tmp/mux-init.log`, `/tmp/mux-create.log`, `/tmp/mux-notify.log`,
`/tmp/mux-wda-before.log`, snapshot `/tmp/mux-*`.
Nessun altro cambiamento hardware durante l'osservazione. **NON RISOLTO.**

**Ultimo controllo letto:** uptime34930; campione mux34916/elapsed210,
DMS5/69servizi. Il controllo SSH successivo è stato annullato:
non è stato ripetuto e l'esito finale non è ancora acquisito.
Non introdurre un'altra prova prima di leggere la conclusione dello script
e verificarne lo stato. DPM18117 scade≈36190; logger3207 ha400 slot da3s,
quindi va salvato/rinnovato prima di qualsiasi ulteriore prova.

**Preparazione offline per una cattura Android più completa:** scaricata la
release ufficiale `strace/strace` v7.2; archivio SHA256 verificato contro
il digest dell'asset GitHub:
`4bde6246926890dcee824f6e6ac42a06752f47d77e5097d86e3c0d6d4b709fe5`.
`build-strace.sh` usa toolchain GNU, header Linux bundled, mpers disabilitato,
link statico; nessuna dipendenza Bionic né modifica al vecchio build.
Output `refs/strace-static`: ELF64 AArch64 statico, SHA256
`4bb0bf8cad13a0c38d6f45010ab645df7b3a94f33200a16305643d713ad20cbc`.
Log `refs/session3-20260919/strace-build.{log,err}`; warning glibc per
getpwnam/initgroups registrati, non nascosti.
`qrtr/trace-fixture.c` usa solo un thread e un socketpair UNIX locale:
build AArch64 `-Werror` e test host ASan/UBSan superati.
`trace-smoke.sh` prepara la verifica di attach/thread/detach, ma
**né strace né lo smoke test sono stati eseguiti sul telefono**.
Nessun cambio slot, restart o cattura Android aggiuntiva effettuati.

### Chiusura mux e ripresa della diagnosi (2026-09-20)

**RISULTATO mux acquisito:** ultimo campione35366/elapsed660/DMS5,
69servizi. Nessuna transizione ONLINE nell'intera finestra; nessun WDS,
IP/default cellulare o ping. Il controllo successivo a uptime60278 conferma
boot `2ca42e08-8c9a-4d61-8ae3-2b632149d26a` invariato e DMS5.
Observer3402, logger3207 e DPM18117 sono terminati (stato Z).
Il log DPM contiene Close Port0x21 con result0/error0: **DPM non è più aperto**.
Questo risultato sostituisce il precedente stato “conclusione non letta”.

**FATTO, archivi verificati** in `refs/session3-20260919/`:

- `mux-final-20260920.tar`:1987072byte,18voci, SHA256
  `cbc2b0fc377dcaed2af24fd4f412fbafc0f3c1805d5af398198fac85c88bfd7d`.
- `rawdump-after-mux.bin`:13107200byte, SHA256
  `2828e728bf18be0f56ac450bd004e6a8f39e9699f3e3c2b175988c4ecef7504c`.
- `post-mux-live-20260920.log`: snapshot successivo della rete e del modem.

**ESPERIMENTO strumentazione, RISULTATO:** push di strace7.2, fixture e
`trace-smoke.sh` in `/tmp`; eseguito soltanto sul nostro processo innocuo.
PASS: attach durante sleep, clone3/thread nuovo, sendmsg/recvmsg del payload
locale `trace-probe-v1`, detach con TracerPid0, processo sopravvissuto ed
exit0. Risultato host `trace-smoke-openwrt-result.log`.
Nessun processo vendor tracciato e nessuna richiesta modem aggiuntiva.
Dopo il backup e la verifica del vecchio logger Z, avviato logger25710
(`/tmp/crashlog-post-mux.log`); anche questo ha durata limitata400×3s.

**RICERCA / IPOTESI:** il thread pubblico ModemManager
`https://www.mail-archive.com/modemmanager-devel@lists.freedesktop.org/msg07101.html`
descrive SM7225 con IPA/QRTR e DeviceNotReady: confronto pertinente per
architettura, ma errore diverso, non una soluzione dimostrata per DMS5.
Le due pagine wiki pmOS e l'issue ModemManager484 restituiscono la
protezione Anubis; non attribuire loro contenuti non letti. I casi USB
qmi_wwan/DTR/FCC non sono una prova applicabile a questo modem integrato.

**PROSSIMA PROVA:** baseline Android pulita, poi cattura limitata e
comparativa dei client effettivi secondo il piano. Prima del cambio slot
salvare lo stato OpenWrt e arrestare/archiviare il logger. Nessun replay,
setter già falsificato o modifica firmware. **Internet NON RISOLTO.**

### Baseline e cattura Android concluse (2026-09-20)

**FATTO:** logger OpenWrt25710 arrestato e snapshot prima del cambio slot
salvato. `rawdump-before-android-20260920.bin`:13107200byte, SHA256
`924a069b7d2a9f57cefe4359c2aef6445d406327cc1a3e682877060d5a3e5090`.
Cambio B→A via fastboot verificato, senza flash. Nuovo boot Android
`6c0c9043-bd14-45c5-8d84-1c986a1c63d3`; a24.14s DMS0,
WindTre LTE, `rmnet_data2` mux3, IPv4 `10.98.71.57/30`, gateway
`10.98.71.58` nella tabella rmnet_data2. Ping vincolato a tale interfaccia:
2/3 risposte a uptime136s. È controllo positivo Android, NON successo OpenWrt.
Log `android-baseline-initial-20260920.log` e `android-positive-control.log`.

**ESPERIMENTO strumentazione:** test innocuo strace passato anche su Android
Enforcing; verificato separatamente `timeout -s INT -k 2 4` con le opzioni
complete: timeout124, TracerPid0, fixture sopravvissuta. Prima preparazione
radio interrotta prima dell'attach/toggle per gara col primo slot del logger;
archiviata in `android-radio-preflight-aborted.tar`. Corretta con attesa
limitata del primo slot. Nessun tentativo radio ripetuto.

**ESPERIMENTO radio nativo:** `android-radio-capture.sh`, controller7247,
logger7347. Tracciati qcrilNrd1913/1914, netmgrd1983, qmipriod1915, qti2099
e tutti i thread, inclusi quelli nuovi. Attach verificato537.54,
modalità aereo enable537.60, disable542.86; watchdog indipendente di ripristino,
limite strace180s. DMS0→1 a542.70→0 a543.01; MSS sempre running.
Fine cattura664.06, tutti i detach verificati665.79, airplane0, exit0.
Nessun restart MSS, setter QMI manuale, arresto daemon o modifica firmware.
Logger e tracer ora terminati.

**FATTO protocollo nuovo:** endpoint DMS2/v1/0, node0/port89 (`0x59`).
Nel file `android-radio2-syscalls.log` il normale ONLINE è:
`0016002e000c00************************` (txn22, riga2635);
risposta corrispondente `0216002e00******************` (riga2640).
Seconda istanza invia lo stesso payload con txn31, risposta successo.
TLV0x01/u8=0; TLV0x10/len5=`00 00 00 00 00`.
Il sorgente correlato `refs/qcom-dms/qcril_qmi_nas.cpp:18077-18090`,
commit pubblico `36fc163a534963a5b3af52186af5efcc63401ad2`, valorizza
`e911_pending_info_valid=TRUE`, `is_pending=FALSE`, struttura inizialmente zero.
Il vecchio IDL pubblico e libqmi descrivono solo TLV0x01: non spiegano
l'estensione completa. Il significato proposto è coerente col sorgente,
mentre byte, lunghezza e successo sono evidenza diretta della cattura stock.

**ARCHIVI** in `refs/session3-20260919/`:
- `android-radio2-final.tar`:4828160byte/31voci, SHA256
  `bf0322521099efa319caf9349e36232cb1f63d9b344886259531ac8d99f4a00e`.
- `rawdump-after-android-radio2.bin`:13107200byte, SHA256
  `a8ffc4d6152c92bf50764d8a9e88567071c16ce5b84ae9ec109bfbbc494df7ae`.
- Copie estratte `android-radio2-{syscalls,services-before,samples}.log`.
Attenzione: `adb exec-out` può mescolare stderr remoto allo stream binario;
nei backup rawdump usare `dd ... 2>/dev/null`, poi controllare dimensione/hash.
Il primo stream contaminato da84 byte di statistiche dd è conservato come
`rawdump-before-android-radio-with-adb-stderr.stream`, non come rawdump valido.

**IPOTESI / prossima prova:** la differenza nel formato del Set potrebbe
spiegare i precedenti malformed, ma manca il pacchetto originale fallito.
Nuovo comando `dms-online-observed`: un solo Set nel formato nativo,
precondizione DMS0 oppure5, result/error rigorosi e readback limitato.
La validazione idempotente Android0 è ora completata (sotto); segue una sola
prova su OpenWrt5 dopo bootstrap e restart protettivo. Non ripetere il vecchio
setter minimale né il replay. **Internet OpenWrt NON RISOLTO.**

### Validazione indipendente DMS Android e preparazione OpenWrt

**ESPERIMENTO / RISULTATO:** `android-dms-observed-check.sh` a uptime1381.71
invia un solo ONLINE osservato, da socket indipendente, a DMS2/v1/0.
Richiesta19byte/txn2, risposta result0/error0; readback0 sullo stesso socket
e su un altro client dopo la chiusura. Fine1383.94/exit0, logger arrestato.
Log host `refs/session3-20260919/android-dms-observed-check-result.log`.
Questo verifica encoding e accessibilità da un client nostro su Android0,
NON dimostra ancora la transizione da5 né Internet su OpenWrt.

**FATTO:** binario `qrtr/qmi-qrtr-observed`, SHA256
`245e521d7e40eb00bf73b3f2fb9b3c8945521d54820cc2dc341c8d53d181ada0`.
`build-data-tools.sh` produce `qrtr/qmi-qrtr-dpm`, poi copiato col nome
observed; il vecchio `qrtr/qmi-qrtr` NON contiene il comando nuovo.
Build `-Werror`, test byte-per-byte del frame nativo e ASan/UBSan passati.

**ARCHIVI verificati** in `refs/session3-20260919/`:
- `android-dms-observed-final.tar`:294400byte/12voci, SHA256
  `b0b812d508c8dd17d133cf5c0e7821133220293a3295d9d7c721831f6990c5de`.
- `rawdump-after-android-dms-observed.bin`:13107200byte, SHA256
  `016f81b7c6e031c3041ff262ee19632567bf2ca6611fdea5dc34dddbb037792e`.
Preflight `android-observed-preserve-result.log` controlla logger terminato,
TracerPid0 dei daemon, boot e modalità aereo ripristinata.

**PREPARAZIONE, non ancora eseguita:** `openwrt-observed-bootstrap.sh`
ripristina solo EFS/MCFG/PD e il trasporto, con logger rawdump verificato
prima del primo start e stop protettivo automatico a86s. Non invia setter,
non configura dati e non avvia una nuova attesa passiva già falsificata.
`crashlog-dump.sh` ora delimita/padda ogni slot32KiB e include il nuovo log.
NON usare `openwrt-final.sh`: manca il restart protettivo e usa txn>255.
Telefono ancora Android slotA al momento di questo aggiornamento.

### OpenWrt ONLINE e primo bearer; nuovo blocco nel percorso dati

**FATTO:** ritorno A→B senza flash, boot
`b3c9f801-4555-4b2d-a3f8-e836ef7fd091`. Bootstrap104.84,
MSS start109.86, stop protettivo195.95 (+86.09s), start201.95.
Pronto217.99/DMS5. File MCFG476008byte copiato dal tar, hash host/telefono
`fd9b2b565f8a193c44dab06e84f3663059307100b8d3cb083f064c177f2a9294`.
Corretto il percorso di estrazione del tar in §2.

**ESPERIMENTO / RISULTATO DMS:** un solo Set osservato a241.05,
result0/error0. Primo readback5, secondo0 a243.07, anche client fresco0;
fine243.08/exit0. NAS restituisce registrazione WINDTRE/LTE.
Non erano ancora inizializzati IPA o ADSP in questo boot: per la transizione
osservata non sono serviti. Non attribuire retroattivamente ogni vecchio
malformed al TLV senza i frame originali.
Archivio `openwrt-dms-observed-final.tar`:360448byte/19voci, SHA256
`593e8f8ad0e553830f9ada8c4909b6fad48890bb5633f121d10197ef377da368`.
Rawdump13107200byte `rawdump-after-openwrt-dms-observed.bin`, SHA256
`546f13f3509696087453dcad99a37d00c794366ff95c256146feb9df03ca3d45`.

**FATTO dati:** `openwrt-data-observed.sh`, PID11764, logger11726:
trigger IPA527.12, handshake response527.438713; tupla endpoint riconfermata,
DPM client11831 aperto, pipe accettate, WDA raw-IP2/QMAP(enum5) e mux1
verificati. WDS `internet.it` pronto530.17, socket mantenuto aperto3600s.
Risposta integrale `openwrt-wds-first-result.log`: IPv4 `10.181.104.203`,
mask `255.255.255.248` (/29), gateway `10.181.104.204`, MTU1500,
DNS `151.5.216.30` e `151.5.216.130`. Sono valori restituiti dal bearer,
NON prova che l'indirizzo fosse già applicato all'interfaccia.

**RISULTATO negativo della verifica rete:** lanciato
`openwrt-cellular-verify.sh` come13387; poi SSH bloccato e reboot del SoC.
Rawdump conserva51 slot del logger nuovo fino675.97, senza una riga
CONFIGURE/ping o panic. Non localizza ancora quale comando abbia causato
il reset. Archivio13107200byte `rawdump-after-first-cellular-crash.bin`,
SHA256 `1c0f1e080c4e024033be7d9ef4b360cb59cc4f75dc9828b656b0472e3ef54cde`;
ultimi slot estratti in `openwrt-data-crash-last-slots.log`.
Tutti gli artefatti citati sono in `refs/session3-20260919/`.

**STATO ATTUALE:** boot OpenWrt `a609cace-9d94-43e6-8cbf-432d9fff1ca6`,
MSS offline, `/tmp` perso; vecchi PID/sessioni NON più attivi.
Pstore vuota, panic=-1/panic_on_oops=1. Nessun nuovo setter o restart effettuato.
**PROSSIMA PROVA preparata:** ripristino della ricetta ora positiva, poi
`openwrt-cellular-staged.sh`: inspect/MTU/address/route/route-get/ping separati.
`rawdump-checkpoint.sh` usa fsync (opzione verificata sul BusyBox del telefono)
nei soli slot380..399 della finestra autorizzata; logger limitato a380slot.
Non ripetere il vecchio script monolitico senza questa osservabilità.

### Secondo bearer e localizzazione parziale del crash (2026-09-20)

**FATTO:** boot `a609cace-9d94-43e6-8cbf-432d9fff1ca6`, MSS start911.08,
stop protettivo997.16 (+86.08s), secondo start1003.17. DMS Set1045.18,
ONLINE1047.21. Bearer pronto1049.27: IPv4 `10.140.64.82/30`,
gateway `10.140.64.81`, MTU1500. Log `openwrt-data-second-result.log`.

**ESPERIMENTO:** scrittura/lettura checkpoint rawdump sincroni verificata;
inspect, MTU, IPv4, route e route-get eseguiti separatamente e completati.
A1312.13: `rmnet_data0@rmnet_ipa0`, IPv4 `10.140.64.82/30`,
`default via 10.140.64.81 dev rmnet_data0`; route-get8.8.8.8 seleziona
quel gateway, quell'interfaccia e `src 10.140.64.82`.
Log `cellular-stage-{inspect,mtu,address,route}-result.log`;
snapshot `openwrt-before-staged-network.tar`.

**RISULTATO negativo:** checkpoint `before_ping` a1398.95 persistito nello
slot390; nessun `after_ping`, SSH255 e nuovo boot. Lo stage originale legge
quattro contatori sysfs PRIMA di eseguire ping: il confine non distingue
ancora getter statistiche da TX/RX. Ultimo campione periodico1396.33,
nessun panic acquisito. `rawdump-after-staged-ping-crash.bin`:13107200byte,
SHA256 `b8fe9fac00b94bd6f62ce2b298037039b46fd946f4882ca07827e84e53115db9`.
Estratti `cellular-checkpoint-390.log` e `openwrt-staged-ping-last-slot.log`.

**ESPERIMENTO di controllo / RISULTATO:** nuovo boot
`c8bf2d2d-3185-4fe4-be41-3591387b026f`, MSS offline. A111s il comando
telefono→host `ping -c 1 -W 2 10.0.0.2` riceve1/1, exit0, boot invariato.
Checkpoint17/18 conservano il controllo; log `usb-ping-control-after-crash.log`.
Non dimostra traffico cellulare. A347.54 ancora stesso boot/MSS offline.
Panic=-1, panic_on_oops=1, pstore vuota: letti, non modificati.

**ARCHIVI** in `refs/session3-20260919/`, prima di nuove scritture:
- `rawdump-after-usb-control.bin`:13107200byte, SHA256
  `dafe057cbe42974dfc828717c16882ba6d35e45998d50f6d9d3c50473cd0a56b`.
- `openwrt-usb-control-final.tar`:4608byte, SHA256
  `da44f0ef617c79407f9e6fa980bff1e1c42a0c7033e87655ce6b0b57ec1b501c`.
- `openwrt-c8-pre-experiment.log`: boot, moduli, link e hash rmnet installati.

**IPOTESI / prossima prova:** `openwrt-cellular-staged.sh` ora separa
`stats` (checkpoint10/11) e `ping` (12/13, ulteriore14 subito prima del
comando), senza letture statistiche nel ramo ping. Non ancora eseguita.
Nessun addon rimosso: l'analisi stock non dimostra che i warning SHS causino
il crash. L'eventuale isolamento addon richiede una prova distinta, prima
di qualsiasi VND/traffico; niente unload a caldo. **Internet NON RISOLTO.**

### Terzo reboot: stallo ingress prima del mux, nuova diagnostica IPC

**FATTO:** nel boot c8, bootstrap659.25, primo start664.28, stop750.44
(+86.16s), secondo start756.45. Un Set osservato814.32, ONLINE816.34.
Trigger IPA828.20, handshake828.273753, DPM Open829.23 riuscito.
Ingress selector7/flags0xe iniziato829.24 ma mai ritornato; nessun egress,
WDA/mux/WDS né stage statistiche/ping effettuato.
A877.63 helper10650 in D: `ipa3_send_cmd+0x208` →
`__ipa_commit_hdr_v3_0` → `ipa3_add_hdr_usr` → `ipa3_setup_a7_qmap_hdr`.
Disassemblato stock: attesa completion senza timeout, dopo submit riuscito
e voto clock; non un'attesa di QMI/ipacm. L'ioctl trattiene RTNL.
Log `openwrt-c8-pipe-blocked.log`; non lanciare `ip` per diagnosticare
questa attesa, perché si accoderebbe sullo stesso lock.

**RISULTATO:** terzo reboot confermato dal nuovo boot07ca sopra.
Rawdump c8:98slot, ultimo956.27, nessun panic acquisito.
`rawdump-after-c8-pipe-crash.bin`:13107200byte, SHA256
`9586f441be11030380133f8feec802590d54218542624a709f722cc59b090f9a`.
Estratti completi `c8-pipe-last-slots-complete.log`, e analoghi
`first-cellular-...`/`second-cellular-...`. Nei log QMI ci sono NUL interni:
decodificare rimuovendo SOLO il padding finale, non tagliando al primo NUL.
Il warning QMP ret=-19 è presente anche nel primo ingress riuscito:
non è una causa dimostrata. Il reset non richiede in questa prova un ping.

**FATTO diagnostico nel nuovo boot:** hung_task_panic0, timeout120,
panic=-1/panic_on_oops1/panic_on_warn0. Questi valori sono stati solo letti.
Montati pstore e debugfs (prima non montati). Pstore resta vuota:
ramoops mem_size2097152 e pmsg_size2097152, record/console/ftrace_size0.
Nessun buffer panic configurato; non assumere che basti montare pstore.
IPC RAM `ipa` e `gsi` già presenti prima del trigger; debugfs `ipa`
compare dopo init. Nessuna lettura registri o modifica clock effettuata.

**ESPERIMENTO preparato:** `openwrt-ipa-ingress-diagnostic.sh` riproduce
IPA/DPM, abilita solo `enable_low_prio_print=1` (writer stock verificato),
avvia un unico collector dei cursori IPC RAM e invia UN ioctl ingress.
`ipa-ipc-collect.sh` dura al massimo180s; checkpoint16/17 prima/dopo.
Nessun addon scaricato, nessun egress/WDA/mux/WDS/ping automatico.
Nuovi argomenti `rmnet-config ingress|egress` separano gli ioctl esistenti,
senza cambiare i frame. La strumentazione può perturbare i tempi.
Hash moduli rmnet telefono/host confermati identici; i test shell mocked
stats/ping hanno superato casi successo, contatore mancante e ordine errato.
Pacchetto precedente `openwrt-observed-tools-v2.tar`:13219840byte,
SHA256 `6a6c6dcfb5ac971446018aa16cc1bc106b286116905f99e50c630d03af163d6f`.
Tutti gli archivi in `refs/session3-20260919/`. **NON RISOLTO.**

### Prova ingress IPC conclusa nel boot07ca (2026-09-20)

**FATTO:** bootstrap1093.74, primo start1098.77, stop1184.95 (+86.18s),
secondo start1190.95. Un solo DMS osservato1208.56, ONLINE1210.63,
anche client fresco0. Trigger IPA1259.06, logging RAM1260.09, DPM1261.10.
Ingress1262.10, exit0/accepted1262.11, checkpoint16/17 durevoli;
`INGRESS_ONLY_COMPLETE`1262.12. Collector concluso1263.11.
Controllo1315.04: stesso boot, DPM11140/logger8736 attivi, ingress terminato.
Nessun egress/WDA/mux/WDS/statistiche/ping in questa prova.

**RISULTATO:** ingresso riuscito con strumentazione; non spiega ancora
la mancata completion del boot c8 e non è prova di Internet.
IPC conserva invio comandi19/10/19, queue su canale12, callback TX e
`ipa3_transport_irq_cmd_ack ... got ack for cmd=19`.
I timestamp IPC e `/proc/uptime` hanno una piccola differenza osservabile:
non dedurre latenza mescolando le due basi temporali.

**ARCHIVI verificati**, tutti nella directory session3:
- `openwrt-observed-tools-v3.tar`:14110720byte, SHA256
  `3882a98caddcef7014d36181f3fd10da5a0fb4b07107a3d378ad8f1dbb06954a`.
  Include logger IPC, diagnostica e helper staged; lo script cellular-staged
  del pacchetto va aggiornato separatamente alla versione stats/ping separati.
- `openwrt-07ca-ingress-snapshot-01.tar`:1340928byte, SHA256
  `88b7ca7df88fb384c9521f6c05d081544e9d1e33617640c79c5feebcc37f4464`.
  Copia estratta omonima, log IPC integrali e dmesg.
- `rawdump-after-07ca-ingress.bin`:13107200byte, SHA256
  `912b0e0a2659e3766854681f7bad96f1c53ab0c3507c80269ef2c59542bb23e9`.
- Bootstrap/DMS/status: `openwrt-07ca-{bootstrap-result,dms-result,ingress-final-status}.log`.

**FASE DATI ESEGUITA:** `openwrt-data-staged.sh` riprende da ingress.done,
una sola fase per invocazione, senza rifare ingress/DPM.
Checkpoint18/19 riutilizzati SOLO dopo archivio della fase precedente.
Nessuna lettura netlink in un trap di errore: potrebbe attendere RTNL.

### Quarto reboot: il confine è il solo ping, statistiche escluse

**FATTO, tutte le fasi separate e archiviate nel boot07ca:**
egress accettato1641.58 (selector6/flags0xa); WDA1643.79 raw-IP2/QMAP5 UL e DL,
endpoint4:1; mux1646.01 con `rmnet_data0` ifindex16/parent15/mux1;
WDS `internet.it`1649.23 → IPv4 `10.100.199.48`, mask `255.255.255.224` (/27),
gateway `10.100.199.49`, MTU1500, DNS `151.5.216.30`/`151.5.216.130`.
Poi inspect1698.87, MTU1701.10, address1703.32, route1705.55,
route-get1707.78: `default via 10.100.199.49 dev rmnet_data0` e
`8.8.8.8 via 10.100.199.49 ... src 10.100.199.48` verificati.

**RISULTATO decisivo:** lo stage `stats` è ora separato ed è **riuscito**
a1746.79 (`rx_packets=0 tx_packets=10 rx_bytes=0 tx_bytes=616`),
checkpoint10/11 durevoli. Il ping successivo (nessun getter nel suo ramo)
ha checkpoint12 a1808.06 e14 `before_ping_exec` a1808.08, PING_EXEC1808.07,
ma **nessun after_ping**: SSH perso e quarto reboot.
Le letture sysfs non appartengono al confine del quarto reboot;
questo non prova retroattivamente dove siano falliti i boot precedenti.
Non è ancora distinto TX dalla prima RX: prima del ping TX era già10 pacchetti
(616byte) e RX0, quindi la trasmissione da sola non era ancora fatale.

**FATTO:** ultimo slot periodico1805.38; USB disconnect10:33:01Z e
ricomparsa10:33:45Z; nuovo boot `a28dc1c5-8505-4bae-a39a-308ce7faa168`,
MSS offline, nessun panic acquisito (ramoops resta pmsg-only).

**ARCHIVI** in `refs/session3-20260919/`:
- `rawdump-after-07ca-ping-crash.bin`:13107200byte, SHA256
  `103395bf1fe3cdeeaefa7c6c76f07544221b6a61fafc20064db1c10bd052b9aa`.
- `07ca-ping-last-slots-complete.log`, `07ca-ping-checkpoint-{390..394}.log`.
- Snapshot per fase `openwrt-07ca-data-*` e `openwrt-07ca-cellular-*`
  con log IPC, dmesg, `/proc/interrupts` e stack; `openwrt-a28d-post-crash-initial.log`.
- Script pushati: `openwrt-data-staged.sh` SHA256
  `24d21f0ba61433389427b5e63c5d9eeb2baf7291b1f47dcf9653f7f43c832091`;
  `openwrt-cellular-staged.sh` SHA256
  `f2360d2539d190165be97a1baf4cd6b6fffa55133704b2d4a99d3d1b87056099`.

**IPOTESI da verificare, non causa dimostrata:** nel modulo opzionale
`rmnet_shs` il ramo IPv4/ICMP di `rmnet_shs_main.c` (lineage-20,
`datarmnet-ext/shs`) avvia hrtimer e `__pm_stay_awake` sul primo ICMP.
Sono caricati anche `rmnet_perf`, `rmnet_offload`, `rmnet_aps`,
`rmnet_perf_tether`, `rmnet_wlan`, `rmnet_sch`. Un eventuale isolamento va
fatto su boot fresco PRIMA di creare VND/traffico, minimizzando i moduli
toccati; nessun unload a caldo dopo il bearer. I casi pubblici USB
(`gro_cell_poll` su5.4) e il fix CFI module-init non corrispondono a questo
quadro e non vanno applicati. **Internet NON RISOLTO.**

**IPOTESI timer SHS ESCLUSA dal confronto binario:** nello stock
`rmnet_shs.ko`, `DATARMNET9303cec796` (0x9a8–0xe54) non contiene i timer
del riferimento pubblico. Il ramo IPv4 ICMP0xc50–0xc68 azzera hash,
imposta sw_hash e ritorna; non importa `__pm_stay_awake`.
Inoltre `DATARMNET45d8cdb224` non rifiuta la mappa RPS nulla: converge
all'init e imposta init_complete1. Non usare il sorgente di un'altra
revisione per inventare un fix timer o una maschera CPU.
Gli IPC pre-ping provano anche10 submit dati su GSI11 e completion EP2:
il TX generico era già attraversato, ma non ne è identificato il protocollo.
Rimane candidata una prova della pila base senza gli addon opzionali,
previa verifica dei cleanup stock; non identifica ancora un modulo colpevole.

### Isolamento addon eseguito prima del modem (boot a28d)

**FATTO / primo tentativo abortito:** a956.45 lo script si ferma sul primo
`rmmod rmnet_aps`, senza rimozione. Il parent8463 è Z, exit255; APS resta
live/refcnt0, nessun processo rmmod in attesa. Traccia innocua `--help`:
kmodloader cerca `/lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604/`,
assente nel chroot con soli moduli6.12.94, ed esce prima di `delete_module`.
Non è un hang del cleanup APS. Sorgente pubblico conservato come
`refs/session3-20260919/kmodloader-85f10530.c`.

**CORREZIONE USERSPACE:** directory vuota privata
`/tmp/rmnet-kmodloader/modules/$(uname -r)`, passata solo a rmmod tramite
`LD_LIBRARY_PATH`. Il loader accetta l'assenza di `modules.builtin`;
nessun modulo6.12 caricato, nessun file stock modificato, niente force.
Preflight strace senza unload e test offline
`python3 test-rmnet-base-isolation.py` superati (guardie, ordine, checkpoint,
duplicati e stop a ogni possibile errore). Tentativo abortito conservato.

**RISULTATO isolamento:** `rmnet_aps`1362.93 → `rmnet_perf_tether`1362.94 →
`rmnet_perf`1362.95 → `rmnet_offload`1362.96 → `rmnet_shs`1362.97:
tutti exit0; completamento1362.98. Checkpoint0..9 e16/17 verificati.
MSS ancora offline e nessun rmnet; core/ctl/ipam/gsim preservati.
Script SHA256 `1f01ae1c10c7ce2b4b03f32521671a6952051d5e8becf89b9bc001a4428092d1`.

**ARCHIVI** in `refs/session3-20260919/`:
- `openwrt-a28d-base-isolation-complete.tar`, SHA256
  `576738e723e9a360453b80685704c9ea507b504fa6bf64e6b5a0146ff760cb2e`.
- `a28d-after-base-isolation-complete-checkpoints.bin`,655360byte, SHA256
  `03dc8a49c54af75e415c2857d2e3a76c13c6198cf3edd794a0d81dff408a7bc2`.
- `openwrt-a28d-kmodloader-{help,private-help}-trace.log`,
  `openwrt-a28d-isolation-observe-20260920T105126Z.log`.

Montata debugfs, bootstrap8887 avviato1364.19, logger8931,
MSS primo start1369.22. Il restart protettivo resta automatico86s.
L'isolamento è una prova di classe, non attribuisce la colpa a un addon.

### RISULTATO pila ridotta: quinto reboot, confine invariato

**FATTO, catena completa senza i cinque addon (boot a28d):**
stop protettivo1455.40 (+86.18s), secondo start1461.41, pronto1477.44/DMS5.
Un solo Set osservato: ONLINE1519.54, confermato anche da client fresco.
IPA1519.67, low-RAM1520.71, DPM1521.71, ingress1521.73 exit0.
Egress1586.92, WDA1589.12, mux1591.34, WDS1594.55 →
IPv4 `10.101.158.83`, mask `255.255.255.248` (/29), gateway `10.101.158.84`,
MTU1500. Inspect1622.95, MTU1625.23, address1627.50, route1629.78,
route-get1632.07 (`default via 10.101.158.84 dev rmnet_data0`,
`8.8.8.8 via 10.101.158.84 ... src 10.101.158.83`).
Statistiche isolate riuscite1634.34: rx0/tx9,568byte.

**RISULTATO:** ping unico PID14710, `before_ping`1698.05 e
`before_ping_exec`1698.06 durevoli, nessun `after_ping`; SSH perso e quinto
reboot (nuovo boot `f2064c04-e0b0-41fa-b9ed-746808a2b741`, MSS offline).
**La rimozione dei cinque addon NON è sufficiente**: non attribuire più il
reset a quei moduli, ma neppure dichiararli irrilevanti per altri percorsi.

**EVIDENZA NUOVA, la più specifica finora:** lo stream continuo di
`/proc/1/root/dev/kmsg` salvato sull'host termina a1698.131329 con
`ipahal ipa_hw_opcode_to_opcode:1396 unsupported Status Opcode 0x0`,
circa75ms dopo `PING_EXEC`. È il primo messaggio kernel correlato al
confine; non è ancora dimostrato quale buffer sia stato letto come status,
né che questa riga sia la causa del reset.
Il log IPC copiato in parallelo si ferma a1697.18 e prima del ping conteneva
solo `ipa3_a5_svc_disconnect_cb:498 Received QMI client disconnect` ripetuti.

**ARCHIVI** in `refs/session3-20260919/`:
- `a28d-ping-live-kmsg.log` (277505byte,3316righe) e `a28d-ping-live-ipa-low.log`.
- `rawdump-after-a28d-ping-crash.bin`,13107200byte, SHA256
  `5806488528cb2a7c624bae242af139b8d9f3746dc62139bb5c0ea10685d028af`;
  estratti `a28d-ping-last-slots-complete.log` e
  `a28d-ping-checkpoint-{390,391,392,394,396,397}.log`.
- Per fase: `openwrt-a28d-{ingress,data-*,cellular-*}.tar` e directory omonime;
  `rawdump-after-a28d-ingress.bin`, SHA256
  `b6a657746886e4d8aa1ace8d71cc0413659bef091ab77c86df5cd7a867da9a4d`.
- `openwrt-a28d-bootstrap-dms.tar`, SHA256
  `9674940ec046ae955d39c45b8c87406b29ae1a6a26055f3e432cca5332bfdc1a`.

**PROSSIMA PROVA (non ancora eseguita):** analisi del parser status nello
stock `ipam.ko` (chiamanti RX di `ipa_hw_opcode_to_opcode`, effetto del
ritorno, relazione con `cs_offload_en`/`hdr_len`/status per la pipe WAN CONS).
Solo dopo, un esperimento singolo e discriminante. Il collector IPC ora ha
una fase `network` con lock esclusivo sui cursori condivisi, validata offline.
**Internet NON RISOLTO.**

### Audit stock dopo il quinto reboot: niente correzione di formato alla cieca

**FATTO binario:** `ipa_hw_opcode_to_opcode` nello stock `ipam.ko`
(`.text+0xc6e7c`, stampa a0xc6fa4) segnala il byte0 e ritorna l'enum interno0,
senza BUG proprio. Anche l'opcode hardware1 valido è tradotto in interno0.
I due chiamanti del parser completo sono LAN (`.text+0x58e14`) e WAN
(`.text+0x5a814`); il parser LAN thin non emette questa riga.
Il solo warning NON identifica WAN, un echo-reply o il successivo BUG.
Esistono controlli successivi di lunghezza/src/dst: nessuno è ancora provato
dal log acquisito. La dimensione dello status stock è32byte; il layout dei
campi va scelto dalla versione hardware effettiva, non da un SoC diverso.

**FATTO da log a28d già archiviati:** in
`openwrt-a28d-cellular-stats/ipa-ipc-ipa_low.log`, righe34–74, il setup
APPS_WAN_CONS/client35/EP23 configura header4, checksum0, aggregazione
GENERIC e status_en1/location0. Il LAN EP16 decodifica già status validi
src14/dst16/len8 dei TAG (righe490–493). Non è quindi dimostrata una
semplice omissione di enable-status. I nove completamenti TX EP2 prima del
ping non identificano il protocollo degli skb.

**FATTO osservabilità:** `ipa/status_stats` nello stock stampa su printk
le30 strutture già decodificate per pipe e restituisce EOF; non contiene i
byte raw. Il dump `ep_reg` non comprende IPA_ENDP_STATUS. Il kernel live
espone CONFIG_KPROBES=y/CONFIG_KPROBE_EVENTS=y; la loro utilizzabilità va
ancora validata, senza confondere una configurazione presente con una
cattura riuscita. Nessun probe, nuovo setter o ping eseguito in questo boot.

**STATO riconfermato:** bootf206 invariato a618.19, MSS offline, `/tmp` senza
tool ripristinati; panic=-1/panic_on_oops1/panic_on_warn0 solo letti.
**IPOTESI:** opcode0 è compatibile con un offset su MAP/padding o corruzione,
ma senza buffer raw non distingue queste possibilità. Nessun flag o offset
IPA è stato cambiato sulla base di questa ipotesi.

### Sonda raw32: errore dello smoke diagnosticato e corretto (boot f206)

**FATTO:** DT live `qcom,ipa-hw-ver` = BE `00000016` (enum22);
con la tabella stock è IPA5.1/hw_idx19. Status V5 di32byte:
opcode byte0, lunghezza LE16+4, src byte6 intero, dst byte30 intero.
Il wrapper `ipahal_pkt_status_parse` è a `.text+0xc30b8`; LR LAN
`.text+0x58e18`, LR WAN `.text+0x5a818`. Base live
`0xffffffecfd2c1000`. Entry prima di PACIASP, non offset+4.

**RISULTATO smoke1/2:** primo test abortito986.23, senza traffico modem.
La ripetizione diagnostica1241.95 si ferma a `register_probe`:
`String accepts only memory argument` sull'array `+u0(%x1):x64[4]`.
`trace_probe.c` locale esclude ST_UMEM dal ramo array: non è prova
di assenza dei kprobe né di un problema IPA. FUNCTION_TRACER disabilitato;
il gate condizionato DYNAMIC_FTRACE/NOTRACE non si applica.
Gli artefatti falliti sono conservati, non sovrascritti.

**ESPERIMENTO / RISULTATO smoke3:** quattro fetch scalari utente da8byte;
`qrtr/kprobe-write-fixture.c` seleziona il proprio PID e usa `write(2)`
esplicita su un nuovo file, evitando la dipendenza dal printf/writev musl.
Build statica `-Werror` e test host ASan/UBSan passati.
A1373.875836 un solo evento `vfs_write+0` con count32, LR
`0xffffffecfab57050` e parole
`37************** 66************** 37************** 66**************`.
Fixture confrontata byte-per-byte; PASS1373.86, cleanup0 a1373.94.
Boot invariato, MSS offline, cinque kretprobe USB vendor intatti.
Questo valida il meccanismo raw32+LR, NON ancora il punto IPA/TAG.

**ARCHIVI** in `refs/session3-20260919/`:
- `f206-kprobe-smoke1-aborted.tar`, SHA256
  `707d7f7c9166431d695988c6822d6fef73e2f39d9d0efb37e44899e6bc41b15f`.
- `f206-kprobe-smoke2-diagnostic.tar`,227840byte, SHA256
  `d7b0cf9e63e5cc44e5da0d871167df2e173db64b7c66cc8077bca3466254a425`.
- `f206-kprobe-smoke3.tar`,339456byte, SHA256
  `3a3eb2f7cb49d59d4dad7adb66deb687ab1eb2578f2e09cc0ac135a6b83d5f5a`.
- Log `f206-kprobe-smoke{2,3}-{result,stderr}.log`, syscall e format nei tar.

**PROSSIMA PROVA:** sonda entry `ipam:ipahal_pkt_status_parse`,
puntatore x0, LR x30, quattro scalar kernel a offset0/8/16/24.
Prima cattura limitata dei TAG di controllo, con bootstrap86s e archivio
prima di WDS/ping. Slot checkpoint15 libero negli script correnti:
logger0..379, rete0..14, ingress16/17, dati18/19.
La persistenza userspace può perdere l'ultimo record se il kernel si
arresta prima di schedulare il lettore; non dichiararla crash-proof.
Nessun formato/flag/firmware modificato. **Internet NON RISOLTO.**

### Validazione entry IPA/TAG riuscita e persistita (boot f206)

**FATTO strumentazione:** registrazione disabilitata sul simbolo stock a
1786.85, cleanup0; cinque probe USB invariati. Tool
`qrtr/ipa-trace-capture.c`: reader nonblocking, limite180s, log locale/host
e pwrite32KiB+fsync nel solo slot395/checkpoint15. Test ASan/UBSan dei
confini, storico circolare, guardie, stop e deadline superati; fixture
AArch64 su file regolare superata prima dell'uso del block device.
Hash binario `3205c076cfaf61734ed73af9e4ef49f436db1f8bdd44b04961d0fbbf56eae204`.
Decoder/test `ipa-status-decode.py` e `test-ipa-status-decode.py`.

**FATTO bootstrap:** rawdump a28d riconfermato byte-identico prima di nuove
scritture. Tool v4 (`openwrt-observed-tools-v4.tar`,15134720byte, SHA256
`ec3f8072af6705782b06d9bf1d035a25eefe5e2b37688195feb2ac9e308c28d3`).
MSS1874.76→stop1960.94 (+86.18s)→start1966.95; pronto1982.99/DMS5.
Un solo ONLINE osservato, readback0 a2038.52 e su client fresco.
Questa è la sesta riproduzione ONLINE, non una nuova prova di Internet.

**ESPERIMENTO / RISULTATO:** trace attivo2070.65, IPA trigger2094.21,
DPM2096.25, ingress2097.26→2097.28 exit0. Tre eventi a
2094.249657/2097.279805/2097.377577, LR `.text+0x58e18` (LAN),
opcode1, src14/dst16, pkt_len8. Maschere0x310/0x4310/0x4310.
Tre hit, zero miss. I696byte dello stream locale coincidono esattamente
con lo slot395 letto dal rawdump; cleanup0 a2144.28, probe USB intatti.
Nessun opcode0, nessun WAN osservato in questa fase, nessun egress/WDS/ping.

**ARCHIVI** in `refs/session3-20260919/`:
- `openwrt-f206-ingress-trace.tar`,498688byte/34voci, SHA256
  `e03ce7f9d510180236c745d63eb9b9f71e4324b2332365e044bbe3d30dbdc1b5`.
- `rawdump-after-f206-ingress-trace.bin`,13107200byte, SHA256
  `0eb0db347efc1d341b03059926fc0f265960eb07b389a154459c26e924375e0f`.
- `f206-ipa-status-ingress-live.{log,err}`,
  `f206-ingress-trace-{slot395.log,decoded.json}`.

**PROSSIMA FASE:** mantenere formati e moduli invariati, catturare
separatamente il setup dati e archiviarlo prima di un eventuale ping con
nuova cattura limitata. Non riusare la directory/istanza terminata.
Il controller aggiornato ammette una fase `ping` distinta e richiede
il marker host della validazione ingress; aggiornare quel file rispetto
al tar v4. La garanzia di acquisire l'ultimo record prima di panic resta
assente. **Internet NON RISOLTO.**

### Setup dati f206 sotto trace completato; ping ancora separato

**FATTO:** egress2421.69, WDA2421.98 (raw-IP2/QMAP enum5 UL/DL),
mux2422.26 (rmnet_data0/ifindex16/parent15/mux1), bearer2423.54.
WDS restituisce IPv4 `10.180.238.160/26`, gateway `10.180.238.161`, MTU1500.
Inspect2478.78, MTU2479.10, address2479.39, route2479.72,
route-get2480.05: default cellulare e percorso8.8.8.8 con source/interfaccia
corretti; USB ha soltanto la connected10.0.0.0/24.
Statistiche separate2480.34: RX0/TX9,568byte.
Moduli invariati rispetto al boot normale; isolamento addon non ripetuto.

**RISULTATO trace setup:** nove record LAN opcode1/src14/dst16/len8,
zero miss/overrun/drop, nessun record WAN o opcode0.
Tutti2088byte coincidono con lo slot395; sonda e collector IPC terminati.
Ogni fase ha tar/checkpoint dedicati `openwrt-f206-data-*` e
`openwrt-f206-cellular-*` in `refs/session3-20260919/`.
Finale `openwrt-f206-network-trace-final.tar`:2324480byte/31voci, SHA256
`6f735539e2975cc92a5518351a9bbba747bf537cbe5ddca12311026043060e6d`.
Rawdump finale13107200byte, SHA256
`e6a0bfe900bdd3a57e4698cb28a62f4e3f7c6e8b96b54257a26ab3996bad8795`.

**RICERCA Android, non modifica proposta:** netlink a
`android-radio2-syscalls.log:126` e links-after conserva flags0xbc000071 e
UL_AGG_PARAMS=16000/32/1/1000000ns. Low bits includono coalescing e checksum
MAPv5 RX/TX; i bit alti hanno significati stock, non padding di fill_info.
Il TX V5 aggiunge4byte anche a ICMP; ICMP bypassa solo l'aggregazione.
Nel RX WAN il full status IPA è letto prima del callback e di rmnet.
I13 ioctl0x89fd Android nella cattura hanno payload opachi: non ricostruiscono
le maschere ingress/egress. Non copiare quei flag come presunta correzione.

**ESPERIMENTO PREPARATO (esito sotto):** un solo ping con nuova cattura `ping`, stream
kernel host e reader IPC esclusivo, dopo preflight della sonda. Gli slot
setup sono già archiviati. Nessun altro cambiamento di formato o flag.

### Ping f206: primo status WAN nullo catturato, sesto reboot

**FATTO:** trace `ping` registrata2847.65. Preflight2895.88: boot corretto,
logger9272 vivo/slot337, DPM12396 e WDS16989 vivi, trace enabled1,
controller23033/sink23106 e IPC23034 vivi, zero hit/miss prima del ping.
Archivio preflight18voci verificato prima del comando. Ping BEGIN2930.59,
checkpoint14 `before_ping_exec`2930.60; nessun ritorno/after_ping di f206.

**RISULTATO discriminante:** stream host conserva un record a2930.683119:
pointer `0xffffff8799c16600`, caller `0xffffffecfd31b818`,
base `.text=0xffffffecfd2c1000`, quindi LR+0x5a818/WAN.
`raw0=raw1=raw2=raw3=0`: esattamente32byte nulli.
Non è un TAG LAN. Non dimostra che il contenuto sia una risposta ICMP,
né che lo zero sia causato da WDA o dai flag rmnet.
Primo audit stock: opcode0 viene tradotto nell'enum interno0, pkt_len0
fa consumare32byte come chiusura aggregato; non basta a dimostrare il crash.

**RESET confermato:** nuovo bootca60 sopra, uptime52.14/MSSoffline.
Rawdump13107200byte salvato prima di qualsiasi nuovo writer:
`rawdump-after-f206-ping.bin`, SHA256
`1f51985322b2bfc97b560de44c0ce6bff848dcecac2a4d19adc54f9a0f6ba856`.
Ultimo logger349@2929.14; slot394 contiene il checkpoint ping.
Slot393 è residuo di una vecchia prova, NON un after_ping f206.
Slot395 conserva solo header iniziale `phase=ping total=0`: evento arrivato
all'host ma non persistito prima del reset. Non affermare persistenza completa.

**LIMITE kmsg corretto:** `/dev/kmsg` nel chroot è un file regolare54byte,
non il device kernel; lo stream tentato non ha acquisito il panic.
Il nodo corretto verificato in ca60 è `/proc/1/root/dev/kmsg`, char1:11.
Usare in futuro `test -c` e verificare record kernel prima del traffico.
Il rawdump mostra inoltre un `dev_watchdog` a2552.824, prima del ping:
il tail30 non conserva il nome netdev; non attribuirlo al modem senza prova.

Archivio `openwrt-f206-ping-evidence.tar`:245760byte/25voci,
SHA256 `5a7423cf4c4139ed0b868fcd193a1f102b13dd8d05b9266b03ce683949ffa332`.
Contiene preflight, raw32 host/JSON, checkpoint, ultimi logger, bootca60
e tool usati; rawdump intero separato. Controller IPC aggiornato per fase
ping esclusiva: SHA256
`ae4d12a5c37aa46137ad0b7ebb6f3c044ec08080641dae4a5d76d663b52694a0`.

**PROSSIMO:** spiegare ramo WAN e lunghezza/buffer GSI, confrontare setup
nativo Android. Nessun altro ping identico, setter o flag alla cieca.
**Internet NON RISOLTO.**

### Prova ingress AGG_DATA preparata e validata offline (2026-09-20)

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
