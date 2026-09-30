# NX679J — OpenWrt nativo su un telefono Android, e il suo modem
### Report tecnico completo — dal primo messaggio allo stato verificato (2026-09-20)

**Dispositivo:** Nubia RedMagic 7 NX679J, Qualcomm SM8450/Waipio, modem X65  
**Kernel:** 5.10.66 stock del produttore, invariato  
**SIM/rete:** WindTre, LTE osservato su Android e OpenWrt  
**Obiettivo:** avviare OpenWrt nativamente e usare il modem cellulare come uplink, con prova finale `ping -c 3 -W 2 8.8.8.8` da OpenWrt.

> **Stato finale documentato:** l'obiettivo di boot, controllo USB, inizializzazione modem QRTR, transizione DMS 5→0, negoziazione WDA, creazione `rmnet_data0`, bearer WDS, assegnazione IPv4 e route è stato raggiunto e riprodotto. Il criterio finale Internet **NON è ancora raggiunto**: il primo ping cellulare provoca un reboot secco prima di una risposta acquisita. Non è corretto dichiarare “Internet funzionante”. L'ultimo fatto discriminante è un callback WAN di IPA con status di 32 byte tutti a zero, seguito circa 75 ms dopo da un messaggio `unsupported Status Opcode 0x0`; la causalità non è ancora dimostrata.

---

## 0. Origine e metodo

Il progetto è iniziato il 16 settembre 2026 con la richiesta di portare OpenWrt nativamente sul telefono collegato via USB. Il lavoro è stato condotto come reverse engineering per interoperabilità:

- **FATTO:** osservazioni provenienti da comandi, log, sorgenti, disassemblati e archivi hashati;
- **IPOTESI:** interpretazioni accompagnate da una prova discriminante;
- **NON RISOLTO:** non trasformare un bearer/IP/route in una falsa dichiarazione di Internet;
- un esperimento per volta, con checkpoint persistenti e backup prima di prove che possono riavviare il SoC.

Le sessioni successive sono conservate negli handoff operativi:

- `experiments/20260916-122926-native-baseline/HANDOFF-20260917.md` — baseline boot chain/EDL/OpenWrt iniziale;
- `experiments/MODEM-HANDOFF.md` — handoff iniziale del modem;
- `experiments/MODEM-HANDOFF-20260919.md` — sessioni QMI/QRTR e prove dati;
- `experiments/MODEM-HANDOFF-20260919-2.md` — log operativo esteso delle prove del 19–20 settembre;
- `experiments/refs/session3-20260919/` — log, snapshot, rawdump e archivi delle prove.

Firmware e moduli proprietari estratti dal dispositivo non sono parte di una distribuzione GitHub: il report descrive il protocollo ricostruito, mentre gli artefatti vendor restano locali.

---

## 1. Punto di partenza: device, bootloader e recupero

### 1.1 Identità e stato

Le misure della baseline riportano:

- modello `NX679J`, product `NX679J-UN`;
- SoC `SM8450`;
- Android slot `_a` come baseline funzionante, root ADB/Magisk;
- bootloader sbloccato, device state `unlocked`, boot state `orange`;
- kernel `5.10.66-android12-9-00005-gf6e6376090be-ab806060`, AArch64;
- due slot A/B;
- EDL Qualcomm non utilizzabile su questa unità: il PBL risponde con `END_OF_IMAGE`, status `INVALID_CMD`, e non ha consentito un trasferimento Firehose affidabile.

La strategia di recupero è quindi stata conservativa: mantenere Android slot A intatto, usare slot B per OpenWrt e verificare sempre readback/hash delle scritture.

### 1.2 Catena ABL/XBL e DTB

Sono state analizzate le immagini stock e la catena di boot:

- `abl_a` è un ELF32 contenente un volume UEFI; al suo interno è stata individuata una sezione LZMA che contiene un PE32+ AArch64 LinuxLoader;
- la selezione DTB passa da `msm-id`/`board-id`; per questo hardware il DTB selezionato è `dtb_idx=5` (“Waipio v2”), con overlay `dtbo_idx=35`;
- il `board-id` `0x10008` è stato associato all'overlay, non al DTB base;
- lo stato A/B è nei metadati GPT (priority, active, retry, successful, unbootable), non in un record `misc` usato come unica fonte;
- `vendor_boot_a` e `vendor_boot_b` risultano identici nella baseline e sono state conservate copie locali.

La provenienza dei bootloader stock è stata verificata byte-per-byte contro l'OTA ufficiale V311 locale e, per `abl`, con verifica della firma OEM ECDSA P-384. Le prove e i relativi hash sono nella baseline handoff; il report non redistribuisce immagini proprietarie.

### 1.3 EDL: risultato negativo ma importante

Sono stati provati Sahara/PBL con più trasporti e client. Fatti principali:

- il device non ha emesso un `HELLO_REQ` riproducibile;
- ha restituito ripetutamente `SAHARA_END_OF_IMAGE`, `image_id=13`, `status=1 (INVALID_CMD)`;
- Firehose/qdl non ha prodotto una lettura GPT valida; un log che dichiarava “success” non era probante perché il client accettava risposte XML prive dell'attributo atteso;
- un solo pacchetto `READ_DATA64` degenere con lunghezza zero è stato osservato, ma non ha trasferito byte e non è stato riprodotto.

Conclusione: EDL non è una via di recovery affidabile per questo esemplare. Non sono state eseguite scritture distruttive via EDL.

---

## 2. Fase OpenWrt: ramdisk, PID 1 e USB

### 2.1 Primo `/init`

È stato costruito un init statico AArch64, compilato senza dipendenze dinamiche, che porta OpenWrt dentro la ramdisk e poi nel suo rootfs.

Problemi risolti:

1. **PID 1:** un `execve` diretto di busybox permetteva a un errore userspace di terminare init. La soluzione è tenere vivo PID 1 e spostare il lavoro potenzialmente fallibile in un figlio (`switch.sh`).
2. **Dipendenze ELF:** il busybox OpenWrt richiede `libc.so` e `libgcc_s.so.1`; la seconda dipendenza mancante causava fallimenti `execve` apparentemente misteriosi.
3. **Loader e permessi:** `ld-musl-aarch64.so.1`, eseguibili e symlink devono avere percorso, tipo e mode corretti.
4. **Compressione:** cercare nomi dentro un'immagine LZ4 non sostituisce l'estrazione del cpio non compresso.

Il primo risultato verificato è stato OpenWrt avviato con SSH su `10.0.0.1:22` attraverso gadget USB NCM (`18d1:4ee7`) e LuCI con risposta HTTP 200.

### 2.2 Userspace operativo

Dentro il chroot sono stati risolti:

- mount di `/proc` e `/sys` dal namespace OpenWrt;
- directory runtime sotto `/var` (symlink a `/tmp`): `lock`, `run`, `log`, `state`;
- avvio diretto di `ubusd`, `rpcd`, `uhttpd` e `dropbear`, poiché procd non assume automaticamente il ruolo di PID 1 in questa architettura;
- reboot attraverso `echo b > /proc/sysrq-trigger` quando il reboot userspace non attraversa correttamente il chroot;
- separazione di `usb0` dal controllo di netifd: avviare il servizio network standard sul gadget ha fatto cadere il canale SSH.

Il flashing di `boot_b` è stato eseguito via SSH con readback SHA-256; il ciclo di reboot è stato poi automatizzato.

### 2.3 `reboot2`: controllo completo senza intervento fisico

`experiments/reboot2.c` usa `LINUX_REBOOT_CMD_RESTART2` con la stringa `bootloader`, cioè lo stesso meccanismo logico di `adb reboot bootloader`. Questo ha chiuso il ciclo:

```text
OpenWrt → reboot2 bootloader → fastboot set_active a/b → Android o Linux
```

Da quel punto è stato possibile cambiare slot, leggere Android come controllo positivo e tornare a OpenWrt senza azione fisica.

---

## 3. Modem e firmware: dall'assenza di firmware al remoteproc

### 3.1 Evidenza Android che ha validato la direzione

Su Android il log mostrava:

```text
qcom_q6v5_pas 4080000.remoteproc-mss:
  Direct firmware load for modem.b11 failed with error -2
qcom_q6v5_pas 4080000.remoteproc-mss:
  Falling back to sysfs fallback for: modem.b11
ueventd: firmware: loading 'modem.b11' ...
```

Da ciò sono stati separati tre fatti:

1. il modem è avviato dal `remoteproc` del kernel;
2. il kernel cerca firmware sotto `/lib/firmware`;
3. Android risponde al fallback tramite `ueventd`, componente assente nell'init OpenWrt.

Sono stati quindi estratti localmente firmware e moduli necessari, montati i percorsi corretti e costruita una sequenza di caricamento compatibile con il kernel stock.

### 3.2 Ordine di inizializzazione verificato

L'ordine che ha prodotto un modem avviato su OpenWrt è:

1. caricare `msm_sharedmem.ko` con `finit_module`;
2. creare `/dev/uio0` per `rmtfs` (major/minor osservati dal sysfs);
3. creare i nodi EFS da `/sys/block/sdf/sdfN/dev` per `modemst1`, `modemst2`, `fsg`, `fsc`;
4. avviare `rmtfs`, che pubblica il servizio QRTR RFS service 14;
5. avviare `tqftpserv` con root `/rfs`, contenente il tree `modem_pr` e directory read/write;
6. caricare `qrtr-smd.ko`;
7. avviare `remoteproc3` su OpenWrt.

`rmtfs` e `tqftpserv` sono stati adattati per il rootfs privo di udev e per i percorsi disponibili. Il modem ha quindi annunciato decine di servizi QMI, inclusi WDS, DMS, NAS, UIM, EFS, PDC e IPA.

### 3.3 Restart protettivo

Fatto riprodotto: lasciare il modem in una certa fase post-start senza il ciclo atteso porta dopo circa 190–260 s a un reset del modem e spesso a un reboot secco del SoC. Il comportamento non produce necessariamente un panic utile in dmesg.

La contromisura verificata è:

```text
start remoteproc
attendere circa 86 s
stop remoteproc
attendere circa 6 s
start remoteproc
```

Dopo questo restart protettivo il sistema è rimasto vivo per finestre di osservazione di centinaia di secondi. Il timing corrisponde al comportamento osservato nell'init Android, che riavvia il modem una volta durante il boot.

---

## 4. Prima ipotesi QMI: QMUX su `glink_pkt` (falsificata)

Il primo modello assumeva che il client dovesse inviare QMUX direttamente sui nodi `glink_pkt`:

- `/dev/smdcntl8` osservato come `504:2`;
- `/dev/smd11` come `504:5`;
- altri nodi `smd7`, `smd8`, `at_mdm0`.

È stato scritto `qmi-dial.c` con CTL GET VERSION, WDS client allocation e WDS START NETWORK, usando un framing QMUX inventariato nel codice. I test hanno prodotto:

```text
/dev/smdcntl8: Connection timed out (errno=110)
/dev/smd11: open riuscito, write: Device or resource busy (errno=16)
/dev/rmnet_ctrl: Device or resource busy (errno=16)
```

Il frame tentato su `smd11` è stato osservato come:

```text
01 0b 00 00 00 00 00 01 21 00 00 00
```

Questa prova non ha ricevuto una risposta. L'errore metodologico era trattare il nodo come endpoint QMUX libero senza avere ancora identificato il trasporto effettivo del vendor RIL.

`/dev/rmnet_ctrl` era già aperto dal processo vendor `qti`/radio; un secondo `open` restituiva `EBUSY`. Cambiare permessi, owner o SELinux non ha trasformato quell'endpoint in un'interfaccia condivisibile.

### 4.1 Tracing iniziale

È stato scritto un tracer `ptrace` AArch64 con supporto ai thread clonati e caricati payload syscall. Il tracing di `qti` ha catturato soltanto thread bloccati in `read`, senza una sequenza QMI utile. Un tentativo di compilare strace Android/Bionic è fallito per conflitti tra gli header bundled Linux e gli header NDK (`struct in_addr` ridefinita); successivamente è stata costruita una versione statica glibc per il contesto OpenWrt, validata con uno smoke test innocuo.

---

## 5. Trasporto corretto: QRTR (`AF_QIPCRTR`)

Il confronto Android ha risolto la falsa pista seriale. Il processo radio non parla QMUX su `smd11` come ipotizzato: usa socket `AF_QIPCRTR` e framing QMI/QRTR.

### 5.1 Evidenza syscall Android

Nel trace `experiments/refs/session3-20260919/android-radio2-syscalls.log` compaiono direttamente:

```text
recvfrom(..., {sa_family=AF_QIPCRTR, sq_node=0, sq_port=0x4b}, ...)
sendto(..., ..., {sa_family=AF_QIPCRTR, sq_node=0, sq_port=0x4b}, ...)
```

I pacchetti hanno il formato QMI su QRTR, per esempio:

```text
00 44 00 a2 00 0f 00 10 08 00 04 00 00 00 01 00 00 00 11 01 00 03
```

Il trace mostra anche il passaggio dal QMI/QRTR all'IPC dati di Android:

- socket TIPC verso il gestore dati;
- `ioctl(SIOCSIFFLAGS)` su `rmnet_data2`;
- netlink per l'interfaccia;
- ioctl Qualcomm su socket UDP (`_IOC(..., 0x89, 0xfd, 0)`), usato dal controllo rmnet/IPA.

L'indirizzo stock Android osservato in un controllo positivo era `rmnet_data2`, con IPv4 `10.98.71.57/30`, gateway `10.98.71.58`; un ping vincolato a quella interfaccia ha ottenuto 2/3 risposte. Questo è controllo positivo del modem Android, non prova di OpenWrt.

### 5.2 DMS: il frame che porta OpenWrt online

Il trace stock ha identificato l'endpoint DMS service 2, version 1, instance 0, node 0, port `0x59` (89). Il messaggio osservato per il normale ONLINE è:

```text
00 16 00 2e 00 0c 00 01 01 00 00
10 05 00 00 00 00 00 00
```

Risposta:

```text
02 16 00 2e 00 07 00 02 04 00 00 00 00 00
```

Il formato include TLV `0x01` e TLV `0x10` di lunghezza 5; non è equivalente al tentativo minimale precedente. Il helper `dms-online-observed` è stato verificato byte-per-byte e con test host ASan/UBSan.

Risultato live OpenWrt:

- un Set osservato ha restituito `result=0/error=0`;
- il readback DMS è passato da mode 5 a mode 0;
- un client fresco ha confermato mode 0;
- NAS ha riportato WindTre/LTE.

Questo è stato riprodotto più volte, inclusi boot con IPA e ADSP non ancora inizializzati. Quindi la transizione DMS 5→0 non richiede necessariamente tutta la pila dati.

### 5.3 Implementazione QRTR

Sono stati costruiti helper statici AArch64 in `experiments/qrtr/` per:

- creare socket `AF_QIPCRTR`;
- fare lookup del service registry;
- inviare richieste QMI con transaction valide sotto `0x100`;
- decodificare result/error e TLV;
- osservare e mantenere client QMI persistenti;
- avviare DPM, WDA, mux e WDS in fasi separate.

La regola pratica emersa è non riusare un transaction ID oltre `0xff`: una prova storica con `txn=257` ha dato risultati ambigui; gli script aggiornati usano transaction piccoli e verificano il frame reale.

---

## 6. Data path IPA/rmnet: costruzione progressiva

Il passaggio da DMS online a traffico ha richiesto isolare ogni fase, perché uno script monolitico rendeva impossibile localizzare un reboot.

### 6.1 IPA

Il driver `ipam.ko` espone il trigger firmware attraverso `ipa3_write`: la scrittura ASCII di un byte `1` su `ipa3` è stata verificata nei sorgenti correlati e nel disassemblato stock. Il trigger ha prodotto:

- `IPA FW loaded successfully`;
- `rmnet_ipa0` presente;
- risposta al request IPA;
- servizio AP IPA pubblicato su QRTR.

Una finestra di 1200 s con IPA pronto prima e dopo il restart modem non ha portato da sola DMS 5→0. Non è quindi corretto attribuire la transizione online alla sola IPA.

### 6.2 ADSP, DPM e pipe

Sono stati provati separatamente:

- ADSP: il dominio audio richiesto dal modem è risultato UP, ma DMS è rimasto 5 nella finestra osservata;
- DPM service 47/v1/0: `Open Port 0x20` ha restituito `result=0/error=0`; dopo DPM, WDA Get sull'endpoint `4:1` è passato da `error=48` a successo;
- pipe IPA: ingress selector 7/flags `0xe` egress selector 6/flags `0xa`, entrambi accettati dal driver stock;
- WDA: Set/Get riusciti con raw-IP 2 e aggregazione UL/DL 5;
- mux: creazione netlink di `rmnet_data0`, parent `rmnet_ipa0`, `mux_id=1`, seguita da notifica stock selector 5.

La sequenza è stata prima osservata come fasi indipendenti, poi riprodotta nel bootstrap dati.

### 6.3 WDS e configurazione del link

Il flusso dati verificato su OpenWrt è:

```text
DMS online
→ IPA trigger/handshake
→ DPM Open
→ IPA ingress/egress
→ WDA Set/Get
→ rmnet_data0 mux 1
→ WDS Start Network (APN internet.it)
→ IPv4/gateway/DNS
→ configurazione link e route
```

Un log integrale di esempio (`openwrt-a28d-data-wds-result.log`) riporta:

```text
WDA: raw-IP=2, UL-QMAP=5, DL-QMAP=5, QoS=0
rmnet_data0: parent=rmnet_ipa0, ifindex=16, mux=1
WDS APN: internet.it
WDS result=0 error=0
IPv4: 10.101.158.83
Gateway: 10.101.158.84
Netmask: 255.255.255.248
MTU: 1500
DNS1: 151.5.216.30
DNS2: 151.5.216.130
```

In un'altra riproduzione il bearer era `10.180.238.160/26`, gateway `10.180.238.161`; in una precedente `10.140.64.82/30`. La variazione è dinamica e non deve essere codificata nel driver.

Il route-get live ha restituito, per esempio:

```text
rmnet_data0@rmnet_ipa0: <UP,LOWER_UP> mtu 1500
inet 10.101.158.83/29
 default via 10.101.158.84 dev rmnet_data0
8.8.8.8 via 10.101.158.84 dev rmnet_data0 src 10.101.158.83
```

Il fatto dimostra assegnazione locale e selezione della route. **Non dimostra che il pacchetto abbia attraversato la rete mobile.**

---

## 7. Il problema residuo: primo traffico WAN e reboot

### 7.1 Tentativi controllati

Sono stati eseguiti boot separati con:

- statistiche sysfs separate dal ping;
- checkpoint `before_ping` e `before_ping_exec` persistiti su `rawdump` con `fsync`;
- logger kernel e collector IPC separati;
- rimozione preventiva dei cinque addon opzionali `rmnet_aps`, `rmnet_perf_tether`, `rmnet_perf`, `rmnet_offload`, `rmnet_shs`;
- setup dati invariato, per non confondere un difetto di teardown con uno di traffico.

Risultati:

| Boot/prova | Setup dati | Confine del guasto | Evidenza |
|---|---|---|---|
| `a609...` | IP `10.140.64.82/30`, route corretta | dopo `before_ping`, con vecchie statistiche nello script | `rawdump-after-staged-ping-crash.bin` |
| `07ca...` | IP `10.100.199.48/27`, route corretta, stats riuscite | subito dopo `before_ping_exec` | `rawdump-after-07ca-ping-crash.bin` |
| `a28d...` | addon rimossi, IP `10.101.158.83/29`, stats riuscite | subito dopo `before_ping_exec` | `rawdump-after-a28d-ping-crash.bin` |
| `f206...` | IP `10.180.238.160/26`, route corretta, TX pregresso | durante il primo ping | `rawdump-after-f206-ping.bin` + trace host |

Nel boot `a28d`, la rimozione degli addon non ha cambiato il confine: il ping singolo ha ancora causato reboot. Questo esclude gli addon come spiegazione sufficiente, senza provare che siano irrilevanti in assoluto.

### 7.2 Evidenza IPA WAN

Nel boot `a28d` un logger host del kernel ha catturato, circa 75 ms dopo `PING_EXEC`:

```text
ipahal ipa_hw_opcode_to_opcode:1396 unsupported Status Opcode 0x0
```

Il messaggio è correlato temporalmente, non causalmente provato.

Nel boot `f206` una sonda kprobe sulla entry stock `ipahal_pkt_status_parse` ha identificato il chiamante WAN:

- base live `0xffffffecfd2c1000`;
- LR `base + 0x5a818` = ramo WAN;
- status buffer di 32 byte;
- `raw0=raw1=raw2=raw3=0`;
- evento a uptime `2930.683119`, subito dopo l'avvio del ping;
- nessuna risposta ICMP e nessun `after_ping` persistito.

Per confronto, la stessa sonda sul ramo LAN ha catturato TAG validi con `opcode=1`, `src=14`, `dst=16`, `pkt_len=8`, tre hit e zero miss. Questo dimostra che la sonda e il decoder funzionano e che il record WAN nullo non è semplicemente un TAG LAN interpretato male.

### 7.3 Cosa è noto e cosa no

**Verificato:**

- la route e l'indirizzo locale sono validi come configurazione kernel;
- il driver arriva al parser di status WAN quando parte il ping;
- il buffer osservato in quel callback è nullo;
- il sistema riavvia prima di restituire un ping.

**Non verificato:**

- se il buffer nullo è un evento di chiusura/terminazione di aggregato, un offset errato, una risposta hardware malformata o corruzione;
- se il primo pacchetto ricevuto è davvero una risposta ICMP o un errore remoto;
- quale campo esatto il parser stock usa per lunghezza/src/dst nella revisione IPA 5.1 di questo telefono;
- se il reboot è causato dal parser, da un watchdog/remoteproc, da un errore GSI o da una conseguenza successiva;
- se l'upload del pacchetto è arrivato al modem: i contatori precedenti mostravano TX ma RX zero.

Il ramo WAN `ipahal_pkt_status_parse` traduce opcode hardware 0 nell'enum interno 0 e poi prosegue con controlli di lunghezza/src/dst. Non è stata applicata alcuna correzione alla cieca a formato, offset, flag o firmware.

---

## 8. Artefatti e strumenti principali

| Artefatto | Ruolo |
|---|---|
| `experiments/build-v10.py` | costruzione locale delle immagini boot/OpenWrt |
| `experiments/20260917-init-v9/nx679j-init-v9.c` | init statico: mount, firmware, `finit_module`, stack dati, remoteproc |
| `experiments/reboot2.c` | reboot con comando `bootloader` verso ABL |
| `experiments/qmi-probe.c`, `qmi-dial.c` | primi probe QMUX, oggi documentati come pista fallita |
| `experiments/qrtr/` | client QRTR/QMI, DMS, DPM, WDA, rmnet, WDS e decoder |
| `experiments/data-modules/dm/` | moduli dati estratti localmente da Android |
| `experiments/refs/session3-20260919/` | log e archivi delle prove recenti |
| `kernel-patch-test/stock-kernel-source/` | sorgenti kernel usati per UAPI/driver e confronto |
| `experiments/qmi-trace-v2.c` | tracer ptrace AArch64 iniziale |
| `experiments/refs/session3-20260919/android-radio2-syscalls.log` | cattura Android decisiva del trasporto QRTR |
| `experiments/refs/session3-20260919/f206-ipa-status-ingress-trace.tar` | validazione indipendente della sonda IPA |
| `experiments/refs/session3-20260919/openwrt-f206-network-trace-final.tar` | setup dati completo sotto trace |
| `experiments/refs/session3-20260919/openwrt-f206-ping-evidence.tar` | evidenza del primo status WAN nullo e reboot |

Gli archivi citati contengono hash SHA-256 nei handoff operativi. Prima di pubblicare il repository, escludere firmware, `modem_pr`, partizioni EFS, rawdump completi e moduli vendor redistribuibili.

---

## 9. Tabella delle evidenze principali

| Elemento | Evidenza | Stato |
|---|---|---|
| kernel/DTB stock utilizzabile | baseline Android, boot OpenWrt ripetuti | verificato |
| OpenWrt su slot B | SSH `10.0.0.1`, LuCI HTTP 200 | verificato |
| controllo boot autonomo | `reboot2` + fastboot slot switch | verificato |
| modem remoteproc su OpenWrt | `remoteproc3` up, firmware caricato | verificato |
| RMTFS/TFTP modem | QRTR service 14, richieste file servite | verificato |
| QRTR come trasporto QMI | `AF_QIPCRTR`/`sendto`/`recvfrom` nel trace Android | verificato |
| frame DMS ONLINE | byte catturati Android e riprodotti con result 0 | verificato |
| IPA initialization | firmware ready, `rmnet_ipa0`, AP service | verificato |
| DPM | Open Port result 0/error 0 | verificato |
| WDA | raw-IP 2, QMAP UL/DL 5, Get success | verificato |
| rmnet mux | `rmnet_data0`, parent `rmnet_ipa0`, mux 1 | verificato |
| WDS | APN `internet.it`, result 0/error 0, IPv4/gateway/DNS | verificato |
| route verso 8.8.8.8 | `ip route get` sceglie rmnet/gateway/source | verificato |
| pacchetto mobile ricevuto | risposta ICMP acquisita | **non verificato** |
| ping esterno 3/3 | `ping -c 3 -W 2 8.8.8.8` | **non verificato / fallito** |
| causa del reboot | status WAN nullo correlato | **ipotesi** |

---

## 10. Lavoro residuo prioritario

1. **Separare il problema WAN dal reboot del sistema.** Conservare kmsg dal nodo corretto `/proc/1/root/dev/kmsg` e catturare status IPA WAN con buffer completo e timestamp; il vecchio `/dev/kmsg` del chroot era un file regolare e quindi non era una cattura valida.
2. **Ricostruire il layout esatto dello status IPA 5.1.** Usare il binario stock effettivo e i TAG LAN validati, non layout di un altro SoC. Verificare lunghezza, opcode, source, destination e terminazione aggregati.
3. **Confrontare setup Android/OpenWrt a livello di IPA/GSI.** Il trace Android mostra `rmnet_data2`, ioctl IPA e netlink/TIPC; confrontare endpoint, flags, coalescing e ordine senza copiare valori non osservati.
4. **Verificare TX/RX con un solo pacchetto controllato.** Prima gateway, poi un singolo IP esterno, con checkpoint e cattura; non ripetere ping ciechi che riavviano il SoC.
5. **Testare il watchdog/remoteproc come ipotesi separata.** Correlare reboot USB, remoteproc state, GSI/IPA e kmsg; non chiamarlo “panic” in assenza di pstore/oops.
6. **Solo dopo una risposta ICMP stabile:** rendere il flusso un servizio OpenWrt riproducibile, configurare DNS/firewall e documentare il criterio `3/3, 0% loss`.

### Criterio di completamento

Il progetto potrà essere dichiarato completato solo quando una sessione OpenWrt fresca produrrà tutti questi fatti nello stesso boot:

```text
rmnet_dataN con IPv4 assegnato
route default via gateway su rmnet_dataN
ping -c 3 -W 2 8.8.8.8
3 packets transmitted, 3 packets received, 0% packet loss
```

---

## 11. Errori metodologici da tramandare

1. `rc=$?` dopo una pipe può restituire zero anche se il comando a monte è fallito: verificare l'effetto (`/sys/module`, netlink, log), non solo l'exit code.
2. Un `except` che inghiotte l'eccezione ha prodotto un cpio da zero byte.
3. Eliminare l'errno numerico ha nascosto `libgcc_s.so.1` mancante.
4. Un path firmware sbagliato produce la diagnosi sbagliata: il kernel deve cercare nel percorso effettivo.
5. Confondere `8:70` (`modem_a`) con la partizione boot (`259:*`) porta a `EINVAL` sul mount.
6. `openat` richiede il `dirfd`; argomenti sbagliati producono errori fuorvianti.
7. `kmodloader` può fallire in userspace prima di chiamare il kernel; `finit_module` e la presenza in sysfs sono la verifica corretta.
8. Un processo zombie non è un esperimento vivo: `kill -0` non basta; controllare `/proc/<pid>/stat`, log e checkpoint.
9. Un rawdump ottenuto con stderr `dd` mischiato allo stream non è un backup valido: usare `2>/dev/null`, dimensione e SHA-256.
10. Un `ping` dopo la route non prova il traffico: servono risposte ICMP osservate nello stesso boot.

---

## 12. Nota legale

Analisi svolta ai fini di interoperabilità su hardware legittimamente posseduto, per reimplementare un driver libero a partire da una specifica ricostruita e documentata. Il codice finale non deve essere una traduzione del disassemblato proprietario. Firmware, EFS, `modem_pr`, moduli vendor e dump delle partizioni appartengono al produttore e non vanno redistribuiti su GitHub.
