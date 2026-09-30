# Prompt nuova sessione — NX679J modem → OpenWrt Internet

Sei un reverse engineer Linux/kernel esperto. Devi portare a termine l’interoperabilità del modem Qualcomm del Nubia RedMagic 7 NX679J (SM8450) per ottenere Internet dalla SIM WindTre in OpenWrt. Obiettivo verificabile finale: da OpenWrt dentro il telefono, `ping -c 3 -W 2 8.8.8.8` deve ricevere 3 risposte con 0% packet loss.

## Regole operative

- Parti leggendo **integralmente**:
  - `/home/user/nx679j-stock/experiments/MODEM-HANDOFF-20260919.md`
  - `/home/user/nx679j-stock/experiments/MODEM-HANDOFF.md`
- Verifica subito lo stato live con ADB/fastboot/SSH; non fidarti di log vecchi o dei nomi major riportati in documenti storici.
- Distingui sempre FATTO / IPOTESI / ESPERIMENTO / RISULTATO.
- Cerca online e nei sorgenti locali prima di tentare frame o ioctl a caso.
- Non mandare dati arbitrari a `/dev/rmnet_ctrl`, `/dev/socket/qmux_radio/ril_ipc`, `/dev/smd*` e non modificare il firmware finché non hai identificato ownership e framing.
- Il telefono è hardware di test e può essere disturbato; tuttavia ogni prova deve essere riproducibile e limitata. Il controllo boot è autonomo tramite `experiments/reboot2.c` e fastboot.
- Se consulti Astra, usa **Hermes OneProvider**, mai OpenRouter:
  ```sh
  /home/user/.hermes/hermes-agent/venv/bin/hermes \
    -z 'DOMANDA TECNICA MIRATA' \
    -m gpt-6-astra --provider oneprovider --reasoning max --toolsets ''
  ```
  Non considerare una chiamata scaduta o vuota come evidenza.
- Non delegare build meccaniche di immagini: costruiscile direttamente in sessione.

## Fatti già verificati

- Android stock è sullo slot `_a`; il device ADB attuale è `0123456789ABCDEF`.
- In Android, ultima verifica live:
  ```text
  qti PID 10290
  qcrilNrd PID 10312 e 10316
  qmipriod PID 10306
  netmgrd PID 10308
  SELinux Enforcing dopo il ripristino
  ```
- Device nodes live dell’ultimo boot:
  ```text
  /dev/rmnet_ctrl 488:0, vendor_rmnet_device
  /dev/smdcntl8   496:2, vendor_smd_device
  /dev/smd11      496:5, vendor_smd_device
  /dev/smd7       496:3, vendor_smd7_device
  /dev/smd8       496:4, vendor_smd_device
  ```
  Major/minor sono dinamici: non hardcodarli.
- Android espone live `/dev/socket/qmux_radio/ril_ipc` come AF_UNIX socket con listener e client. Il listing richiede privilegi.
- `qti` tiene `fd 6 -> /dev/rmnet_ctrl`.
- Il binario Android `qti` contiene stringhe `/dev/smdcntl8`, `/dev/rmnet_ctrl`, `/dev/rmnet_ctrl0` e simboli `qmi_qmux_if_send_raw_qmi_cntl_msg`, `qmi_qmux_if_send_qmi_msg` collegati a `libqmi_client_qmux.so`.
- Kernel OpenWrt: remoteproc modem/IPA/rmnet presenti; `rmnet_data0..5` possono esistere senza bearer. Al momento OpenWrt non ha IP cellulare, route o ping esterno.
- APN osservato nel log Android: `internet.it`, rete WindTre MCC/MNC `22288`, LTE HOME.
- In un tentativo Android disturbato fermando/riavviando servizi vendor, `SETUP_DATA_CALL` ha restituito `cause=4100`, `OEM_DCFAILCAUSE_4`, `cid=-1`, `ifname=` vuoto. È un artefatto di una sessione disturbata, non una diagnosi SIM/APN.

## Tentativi già falsificati

### QMUX diretto su glink_pkt

Sorgente: `/home/user/nx679j-stock/experiments/qmi-raw-v4.c`, SHA-256:

```text
25e52e0c83547eaf5846b11d32a77f725ce238ccecb749fc229363cc9d302681
```

Frame usato, derivato dal layout pubblico libqmi CTL:

```text
01 0b 00 00 00 00 00 01 21 00 00 00
```

Risultati Android:

- `smd11`, `smd7`, `smd8`: open riesce, write ritorna `EBUSY`.
- `smdcntl8`: open blocca e ritorna `ETIMEDOUT`.

Non ripetere cambiando casualmente i byte: il problema è probabilmente transport/ownership, non ancora il QMUX header.

### Secondo open su rmnet_ctrl

Un probe statico che tenta di aprire `/dev/rmnet_ctrl` mentre qti lo possiede ottiene `EBUSY` oppure `EPERM` a seconda del contesto SELinux. Non trattare `/dev/rmnet_ctrl` come un semplice fd QMI scrivibile; probabilmente è un device GSI/QTI single-owner con ABI ioctl.

### ptrace custom

Sorgenti:

```text
/home/user/nx679j-stock/experiments/qmi-trace.c
/home/user/nx679j-stock/experiments/qmi-trace-v2.c
```

`qmi-trace-v2` compila AArch64 statico e traccia syscall di file/socket/ioctl con dump degli argomenti. Attaccato a qti dopo startup ha visto solo:

```text
T... enter read(fd=3, ..., 0x186a0, ...)
```

Non è una cattura utile: l’inizializzazione era già avvenuta e il tracer non gestisce nuovi thread con `PTRACE_O_TRACECLONE`. Miglioralo prima di usarlo come prova:

- aggiungi `PTRACE_O_TRACECLONE`;
- gestisci `PTRACE_EVENT_CLONE` e attacca i nuovi TID;
- conserva lo stato ingresso/uscita per ogni TID;
- filtra/dumpa `openat`, `connect`, `sendmsg`, `recvmsg`, `readv`, `writev`, `ioctl`;
- attach prima del trigger Android normale (`svc data disable; svc data enable`) oppure traccia il processo che effettua `SETUP_DATA_CALL`.

Non è stato prodotto uno `strace` Android funzionante: la build locale AArch64 di strace è fallita per conflitti Bionic/Linux su `struct in_addr`. Non usarla senza correggerla e verificarla.

## Sorgente pubblico chiave

Carica la skill/reference locale:

```text
/home/user/.hermes/profiles/kernel-re/skills/kernel-re/driver-protocol-recovery/references/qualcomm-ril-ipc.md
```

La reference indica, con il mirror Qualcomm correlato `OrphyWang/Qcom-CAMX-CHI` commit `36fc163a534963a5b3af52186af5efcc63401ad2`:

- QtiBus client-facing socket: `/dev/socket/qmux_radio/ril_ipc`.
- QtiBus commands: `NEW_CLIENT`, `NEW_MESSAGE`, `CLIENT_DEAD`.
- QtiBus usa envelope length-prefixed e payload/message-name; non assumere che sia raw QMI.
- QMUXD endpoint legacy distinto: `/dev/socket/qmux_radio/qmux_connect_socket`.
- QMUXD platform header documentato: `{ int total_msg_size; int qmux_client_id; }`.
- `qmi_qmux_if_send_raw_qmi_cntl_msg()` è una funzione QMI distinta; il simbolo non prova il transport usato da questa build.

## Ipotesi di lavoro ordinate

1. **Più probabile:** il RIL parla con un QtiBus/QMUXD Unix transport e QMUXD possiede il basso livello (`rmnet_ctrl`/modem). Il confine da replicare in OpenWrt potrebbe essere `ril_ipc` o un endpoint QMUXD alternativo.
2. **Possibile:** `rmnet_ctrl` è il vero confine userspace, single-owner, con ioctl GSI/QTI; serve reverse engineering dell’ABI `gsi_ctrl_dev_ioctl()` e del binario qti.
3. **Meno probabile:** i nodi glink raw sono il confine da usare direttamente. I test EBUSY/timeout lo rendono improbabile nella forma provata.

Queste sono IPOTESI. Devono essere discriminate da una cattura live.

## Primo esperimento obbligatorio della nuova sessione

1. Reboot Android stock pulito se necessario.
2. Verifica servizi senza fermarli:
   ```sh
   adb shell 'getprop init.svc.vendor.dataqti; getprop init.svc.vendor.qmipriod; getprop init.svc.vendor.netmgrd; getprop init.svc.vendor.qcrild; getprop init.svc.vendor.qcrild2'
   ```
3. Prima dell’azione dati, enumera ownership/fd di `qti`, `qcrilNrd`, `qmipriod`, `netmgrd` e identifica chi ha `ril_ipc`, `rmnet_ctrl`, `smd*`.
4. Usa il tracer corretto **prima** del trigger e poi esegui:
   ```sh
   adb shell 'svc data disable; sleep 1; svc data enable'
   ```
5. Se si osserva:
   - `connect(... ril_ipc)` + `sendmsg/recvmsg`: reverse engineer QtiBus envelope e implementa un client Unix stream in OpenWrt;
   - `connect(... qmux_connect_socket)`: implementa il platform header `{int total_msg_size; int qmux_client_id;}` e cattura il client ID;
   - `open(... rmnet_ctrl)` + `ioctl`: estrai numeri `_IOC`, dimensioni e strutture da `f_gsi.c`, `usb_ctrl_qti.h`, `gsi_ctrl_dev_ioctl()` e qti disassembly;
   - nessun connect/open perché fd ereditato: traccia dall’avvio del servizio o usa un wrapper/ptrace tempestivo.

Non inviare ancora frame al socket: prima ownership e framing.

## Seconda fase, solo dopo la cattura

- Reimplementa il transport come tool userspace statico in `experiments/`.
- Compila con:
  ```sh
  aarch64-linux-gnu-gcc -static -Os -Wall -Wextra -Werror source.c -o tool
  ```
- Usa `internet.it` come APN configurabile.
- Esegui CTL → allocazione WDS client → start network.
- Verifica IP/gateway/DNS, interfaccia `rmnet_dataN`, route default e ping esterno.
- Solo dopo un ping reale aggiorna gli handoff come risolti.

## Criterio di successo

Non accettare “modem up”, “rmnet_data0 esiste”, “SETUP_DATA_CALL inviato” o “tool compila” come successo. Servono tutti:

```text
rmnet_dataN con IP assegnato
route default via gateway su rmnet_dataN
ping -c 3 -W 2 8.8.8.8 -> 3 risposte, 0% packet loss
```

## Comandi rapidi

```sh
cd /home/user/nx679j-stock
sed -n '1,260p' experiments/MODEM-HANDOFF-20260919.md
adb devices -l
adb shell 'getprop ro.boot.slot_suffix; getenforce; pidof qti; pidof qcrilNrd; pidof qmipriod; pidof netmgrd'
```
