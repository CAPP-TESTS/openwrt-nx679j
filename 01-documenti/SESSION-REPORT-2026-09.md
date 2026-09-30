# NX679J — Abilitare il modem di un telefono Android sotto OpenWrt
### Report tecnico di una sessione di reverse engineering e driver bring-up

**Dispositivo**: Nubia RedMagic 7 (NX679J), SoC Qualcomm SM8450 "Waipio"
**Kernel**: 5.10.66-android12-9 stock (invariato)
**Userspace**: OpenWrt 25.12.5 `armsr/armv8` (aarch64_generic), avviato da una
ramdisk custom in `boot_b`
**Canale di controllo**: USB-C in modalità NCM — rete `10.0.0.1/24` + SSH
**Obiettivo**: far funzionare il modem (SIM WindTre già presente) fino ad avere
connettività internet dal userspace OpenWrt

---

## 1. Abstract

Questo documento racconta come, partendo da un telefono il cui kernel non
riconosceva **nessuno** dei sottosistemi del modem (nessun remoteproc, nessun
modulo caricato, nessun `/dev` del modem), siamo arrivati a:

- **avviare il processore modem** (`4080000.remoteproc-mss`) sotto il nostro
  controllo, con il suo firmware caricato dal percorso corretto;
- **portare dentro il kernel lo stack dati completo** (`rmnet`, `ipam`, `ipa`)
  partendo dai moduli della partizione `vendor` di Android;
- **capire perché il modem "spariva"**: deferimento dei probe, driver
  `smp2p` mancante, firmware non raggiungibile, e infine il fatto che il modem
  acceso **senza interlocutore QMI fa cadere la piattaforma**;
- **costruire il controllo di boot completo**: dal nostro OpenWrt possiamo
  mandare il telefono in fastboot, cambiare slot e avviare Android o Linux
  **senza alcun intervento fisico**.

Resta aperto l'ultimo miglio — il **framing esatto del dialogo QMI** — per il
quale il documento indica la misura da fare e la strada da seguire.

Il lavoro non è "un driver scritto a mano": è un percorso di **osservazione →
modello → implementazione → verifica** in cui ogni valore nel codice ha una
misura dietro.

---

## 2. Il contesto di partenza

Uno dei due slot A/B contiene un'**immagine `boot_b` costruita da noi** che
avvia un init statico (PID 1) il quale prepara l'ambiente e passa la mano a un
OpenWrt completo in un chroot. Il telefono è stabile, raggiungibile via SSH e
con LuCI funzionante — **ma il modem è invisibile**:

```
/sys/class/remoteproc/     → vuota
rmnet_*                    → nessuna interfaccia
moduli modem (q6v5, mhi)   → 0 su 111 nella ramdisk
/vendor/firmware_mnt       → inesistente
```

La domanda iniziale quindi non era "come scrivo un driver", ma **"che cosa
manca perché il kernel non veda nemmeno il dispositivo?"**.

---

## 3. Metodo

Due regole, applicate per tutta la sessione:

1. **Fatti e ipotesi restano separati.** Ogni affermazione nel documento e nel
   codice è tracciata a una misura (una riga di `dmesg`, un `readlink`, un byte
   a un offset). Le ipotesi sono dichiarate tali e hanno un esperimento che le
   falsifica.
2. **Un'ipotesi alla volta.** Ogni ciclo di test cambia una variabile e produce
   un'immagine nuova (`v10` … `v53`) costruita in locale in ~1 secondo, oppure
   un test a caldo via SSH quando possibile.

Il ciclo di lavoro si è quasi sempre svolto **senza toccare fisicamente il
telefono**: flash via SSH (`cat` sul device) o via fastboot, reboot via sysrq o
via syscall, verifica via SSH.

---

## 4. Cronologia tecnica

### Fase 1 — Verificare la strada prima di procedere

L'utente ha chiesto esplicitamente di **verificare che la strada fosse quella
giusta prima di investire tempo**. La verifica è stata fatta leggendo lo stato
di **Android** (slot A, intatto), dove il modem funziona:

- `dmesg` di Android, 5.5 s dopo il boot:
  ```
  qcom_q6v5_pas 4080000.remoteproc-mss: Direct firmware load for modem.b11 failed with error -2
  qcom_q6v5_pas 4080000.remoteproc-mss: Falling back to sysfs fallback for: modem.b11
  ueventd: firmware: loading 'modem.b11' for '/devices/platform/soc/4080000.remoteproc-mss/firmware/modem.b11'
  ```

Questa singola lettura ha rivelato **tre cose fondamentali**:
1. su Android il modem passa dal **remoteproc del kernel** (stesso driver
   `qcom_q6v5_pas`, stesso nodo `4080000.remoteproc-mss`) → la strada era giusta;
2. il kernel cerca il firmware in **`/lib/firmware`**, e quando non lo trova
   **chiede aiuto allo userspace** (fallback sysfs);
3. **noi non abbiamo `ueventd`**, quindi quella richiesta non trova mai risposta.

### Fase 2 — Il firmware dove il kernel lo cerca

Costruito il bind `modem_a/image` → `/lib/firmware`, con un vincolo scoperto
sperimentalmente: **`vfat` è un modulo**, quindi i mount riescono solo
**dopo** la catena moduli, non nell'init "presto".

Iterazioni: v33-v39, con il journal come strumento di misura (ogni operazione
dell'init scrive il suo esito, incluso l'errno numerico).

### Fase 3 — Il probe che non parte: `smp2p`

Con il firmware al posto giusto il modem continuava a non apparire. Il nodo
`4080000.remoteproc-mss` aveva **nove fornitori** (`supplier:*`) e un file
`waiting_for_supplier`, ma il vero colpevole era un altro:

```
soc:qcom,smp2p-modem:  driver → VUOTO
```

Il driver dei canali di comunicazione (`smp2p`) **non era caricato** — e senza
di lui il driver del modem deferiva per sempre il proprio probe (aspetta il
canale `qcom_smem_state`). Caricato `smp2p.ko` (+ `smp2p_sleepstate.ko`), **i 5
remoteproc si registrano**:

```
remoteproc0: 3000000.remoteproc-adsp
remoteproc1: 32300000.remoteproc-cdsp
remoteproc2: 2400000.remoteproc-slpi
remoteproc3: 4080000.remoteproc-mss   ← IL MODEM
remoteproc4: 188101c.remoteproc-spss
```

### Fase 4 — I probe differiti si riattivano solo con una registrazione

Scoperta chiave, misurata: il kernel **riprova i device differiti a ogni
nuova registrazione di driver**, ma solo dentro una finestra (su questo kernel
**1424 s**). Da userspace non c'è modo di forzare un retry su un device già
deferito: `drivers_probe`, `driver_override` e `bind` vengono rifiutati.

La soluzione: **caricare un modulo mai caricato** (`finit_module`) — una
registrazione nuova, che fa ripartire i probe differiti. È così che il
remoteproc del modem è comparso la prima volta.

### Fase 5 — `kmodloader` mente, `finit_module` funziona

I moduli dello stack dati (`rmnet_core`, `ipam`, …) risultavano "failed/missing"
nella catena. Il colpevole era lo strumento:

```
/sbin/kmodloader /lib/modules/rmnet_core.ko   →  rc=255, NESSUN messaggio del kernel
finit_module(fd, "", 0)                       →  rc=0, modulo caricato
```

Il `kmodloader` di OpenWrt **rifiuta in userspace** (nessun messaggio del
kernel, nessun effetto) moduli che la syscall diretta carica senza problemi.
Da quel momento il caricamento dei moduli vendor passa **sempre** da
`finit_module` scritta nell'init.

### Fase 6 — Lo stack dati, e il crash da modem "solo"

I moduli dati (`rmnet_*`, `ipam`, `ipa_clientsm`, `gsim`) **non esistono nel
vendor ramdisk**: stanno nella **partizione `vendor`** del telefono (gli stessi
che Android carica). Sono stati estratti via `adb pull` (con `su`, i file erano
protetti), e caricati nell'immagine.

Nota: quei moduli hanno **vermagic diverso** (`5.10.66-gki-…` contro il nostro
`5.10.66-android12-9-…`) ma **caricano lo stesso**, perché il controllo effettivo
è sulle versioni dei simboli.

Caricato lo stack dati, è emerso il comportamento più importante di tutta la
sessione:

> **Il modem acceso senza un interlocutore QMI fa riavviare la piattaforma in
> 1-2 minuti.**

Questo ha reso ogni tentativo sul QMI un ciclo di boot, e ha spiegato i "crash
inspiegabili" precedenti: non era instabilità del sistema, era il modem lasciato
solo.

### Fase 7 — `ipam` blocca l'init (e la soluzione è un fork)

Con lo stack dati caricato *nell'init*, il sistema diventava stabile ma **non
raggiungibile**: uno dei `finit_module` (il bring-up IPA di `ipam`) **blocca il
processo**. Soluzione: il caricamento va **in un processo figlio** — se si
impicca, si impicca lui, e PID 1 prosegue fino al chroot e a `usb0`.

### Fase 8 — `start` è sincrono, e va dopo il probe

Avviare i remoteproc scrivendo `start` in `/sys/class/remoteproc/N/state` è
un'operazione **sincrona** che può durare secondi. Due conseguenze misurate:

- **in PID 1 blocca tutto** (v45: gadget su, niente SSH, recupero solo con
  pressione fisica);
- **se avviene durante il probe, il probe fallisce** e i remoteproc vengono
  *rilasciati* (`releasing` nel dmesg, a 19.251 s — esattamente quando lo
  script iniziava a scrivere `start`).

Soluzione: gli `start` **in background e con ritardo**, dopo che i probe sono
conclusi. Da lì i 5 rproc restano su e il modem **carica il suo firmware**:

```
remoteproc3: powering up 4080000.remoteproc-mss
remoteproc3: Booting fw image modem.mdt, size 5884
remoteproc3: remote processor 4080000.remoteproc-mss is now up
```

### Fase 9 — Il controllo di boot completo (idea dell'utente)

Domanda dell'utente: *"non hai modo di riavviare il telefono in fastboot da
solo? se non ce l'abbiamo possiamo costruirla questa condizione?"*

Sì, ed è **la stessa cosa che fa Android**. `adb reboot bootloader` usa la
variante **`LINUX_REBOOT_CMD_RESTART2`** della syscall `reboot`, che consegna
una **stringa** al bootloader; l'ABL, vedendo `bootloader`, entra in fastboot.

Il `reboot` di busybox e `sysrq-trigger` **non passano nessuna stringa**: per
questo ogni tentativo precedente riavviava semplicemente in Linux.

Con `reboot2` (30 righe di C, in `experiments/reboot2.c`) il ciclo è chiuso:

```
OpenWrt → reboot2 bootloader → fastboot set_active a/b → Android o Linux
```

tutto **senza intervento fisico**.

### Fase 10 — Il puzzle QMI (stato attuale)

Il modem parla, ma nessun client gli risponde. Le misure:

- classi kernel presenti: `glinkpkt`, `ipa`, `ipa_adpl`, `ipa_odl_ctl`,
  `remoteproc`, `rpmsg`; con il modem su nascono i char device **501**
  (`ipa_odl_ctl`), **502** (`ipa_adpl`), **503** (`ipa`);
- canali `glinkpkt` con i rispettivi `dev`:
  `smdcntl8`=504:2 · `smd11`=504:5 · `smd7`=504:3 · `smd8`=504:4 · `at_mdm0`=504:0;
- **pista falsa eliminata**: il device MHI `mhi_1103_00.01.00` è il **WiFi**
  (`cnss`, PCIe RC0) — non il modem;
- **`qmi_encap` non esiste** su questa piattaforma: il framing QMI lo fa il RIL
  in userspace;
- **su Android il processo `radio` tiene aperto `/dev/rmnet_ctrl`** (major 487):
  è quello il canale con cui il modem dialoga.

I due client QMI scritti (`qmi-probe.c`, `qmi-dial.c`, framing QMUX
`[0x80|svc][len][ctrl][txn][msgid][TLV]`) non hanno ancora ricevuto risposta su
nessuno dei canali provati.

---

## 5. La catena che funziona (ricetta)

1. Ramdisk con l'init statico + OpenWrt + moduli.
2. Catena moduli (worker) — **`finit_module`**, mai `kmodloader`.
3. **Dopo** la catena: mount `modem_a` (dev `8:70`) e **bind** di `image/` su
   `/lib/firmware`.
4. **Trigger**: `finit_module` di almeno un modulo mai caricato
   (`qcom_glink_spss` + `qcom_spss`) → fa ripartire i probe differiti.
5. **Stack dati in un figlio**: `rmnet_*`, `ipam`, `ipa_clientsm`, `gsim`, …
6. **Start dei remoteproc in background e ritardati** (mai durante il probe).
7. Il modem parte e carica `modem.mdt` da `/lib/firmware`.

---

## 6. Le scoperte chiave (in ordine di valore)

| # | Scoperta | Evidenza |
|---|---|---|
| 1 | Il kernel carica il firmware da `/lib/firmware`, con fallback sysfs servito da `ueventd` (che noi non abbiamo) | dmesg Android, 3 righe a 5.5 s |
| 2 | Il driver `smp2p` mancante teneva deferito per sempre il probe del modem | `soc:qcom,smp2p-modem: driver=VUOTO` |
| 3 | I probe differiti si riattivano **solo** con una nuova registrazione di driver (finestra 1424 s) | `drivers_probe`/`bind` rifiutati; riprova OK dopo `finit_module` |
| 4 | `kmodloader` di OpenWrt rifiuta in userspace (rc=255) moduli che `finit_module` carica | confronto diretto sui due percorsi |
| 5 | Il modem acceso **senza interlocutore QMI** riavvia la piattaforma in 1-2 min | osservazione ripetuta, 3+ volte |
| 6 | `ipam` blocca il processo che lo carica → va in un figlio | v52 (init fermo) → v53 (stabile) |
| 7 | `start` di un remoteproc è sincrono: blocca PID 1 e fa fallire il probe se concorrente | v45 (hang), v46 (`releasing` a 19.251 s) |
| 8 | Il reboot può passare una stringa al bootloader: `RESTART2` + `"bootloader"` → fastboot | `reboot2` → `fastboot devices` OK |
| 9 | `mhi_1103` è il WiFi (`cnss`), non il modem | dmesg: `cnss: PCIe RC0 link initialized` |
| 10 | Su Android il modem è parlato dal processo `radio` via `/dev/rmnet_ctrl` | `/proc/<pid>/fd` |

---

## 7. Piste eliminate (per non ripercorrerle)

- **Timeout dei probe differiti**: non era la causa; tutte le 14 dipendenze
  dichiarate di `q6v5_pas` sono caricate.
- **IOMMU/SMMU**: `arm-smmu` regolarmente legato, 11 TBU.
- **`download_mode`**: il modulo legge `0` nonostante la cmdline dica `1`.
- **Firmware assente**: era presente e leggibile dal device giusto (`8:70`).
- **MHI come canale del modem**: è il WiFi.
- **`qmi_encap` come modulo kernel**: non esiste su questa piattaforma.

---

## 8. Strumenti costruiti

| File | Cosa fa |
|---|---|
| `experiments/build-v10.py` | Builder locale: moduli, `modules.load`, init, script. ~1 s per immagine |
| `experiments/20260917-init-v9/nx679j-init-v9.c` | Init statico PID 1: mount, bind firmware, trigger, stack dati in figlio, avvio rproc |
| `experiments/reboot2.c` | Reboot con stringa al bootloader (`bootloader`/`recovery`) via `RESTART2` |
| `experiments/qmi-probe.c` | Sonda QMI minimalista (CTL `GET_VERSION_INFO`) |
| `experiments/qmi-dial.c` | Dialer QMI: CTL version → WDS `GET_CLIENT_ID` → WDS `START_NETWORK_INTERFACE` |
| `experiments/MODEM-HANDOFF.md` | Handoff operativo: stato, comandi, prossimi passi |
| `experiments/data-modules/dm/` | Moduli dello stack dati estratti da Android |

---

## 9. Errori di metodo (e le regole che ne sono nate)

La parte più utile da tramandare:

1. **`rc=$?` dopo una pipe dà 0 anche se il comando è fallito.** Verificare
   sempre l'effetto (un modulo è caricato se compare in `/sys/module`).
2. **`except` che inghiotte l'eccezione** ha prodotto un `cpio` da 0 byte senza
   un messaggio d'errore.
3. **Buttare via l'errno numerico** ("exec OTHER") ha nascosto per tre
   iterazioni la vera causa (`libgcc_s.so.1` mancante).
4. **Path sbagliato = diagnosi sbagliata**: `/vendor/firmware_mnt` montato ma
   irrilevante finché non si è capito *dove* il kernel cerca i file.
5. **Numeri di device confusi** (`259:25` = `boot_b` invece di `8:70` =
   `modem_a`) → `mount` con `EINVAL`.
6. **`openat` (56) vuole il `dirfd`**, non solo il path: passando gli argomenti
   sbagliati si ottengono errori "muti" (`-9`) che sembrano altro.
7. **Non dare per buono l'output di un tool che tace**: `kmodloader` con
   `rc=255` e zero messaggi del kernel era il segnale, non il rumore.

---

## 10. Stato attuale e prossimo passo

**Funziona** (verificato su hardware): modem avviabile con il suo firmware,
stack dati nel kernel, IPA inizializzato, controllo di boot completo e autonomo.

**Aperto**: il dialogo QMI non riceve risposta. Il passo indicato è leggere il
framing **reale** dal processo `radio` di Android (`strace` o sorgenti AOSP del
`libril`), replicarlo in `qmi-dial.c`, attivare il bearer WDS, portare su
`rmnet_data0` e instradare fino al `ping`.

Tutti i dettagli operativi sono in `experiments/MODEM-HANDOFF.md`.

---

## 11. Nota legale e di merito

Il lavoro rientra nel **reverse engineering per interoperabilità** (art. 6
Dir. UE 2009/24/CE; DMCA §1201(f)): il fine è far funzionare su un sistema
libero un hardware di cui si è proprietari. Nessuna protezione è stata aggirata;
il codice prodotto è **reimplementazione da specifiche ricostruite** (misure
documentate), non traduzione di disassemblato. Il firmware e i moduli estratti
dal dispositivo **non vanno ridistribuiti**: appartengono al produttore.
