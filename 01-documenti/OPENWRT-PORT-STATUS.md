# v10→v16 OpenWrt nativo sul NX679J — stato e prossimi passi (agg. 2026-09-17)

## Dove siamo (tutto verificato su hardware)
- Il **bring-up nostro funziona**: journal, gadget NCM (`18d1:4ee7`, UDC `a600000.dwc3`,
  super-speed-plus), `usb0 = 10.0.0.1/24`, ping 3/3 dal PC.
- Il **rootfs OpenWrt 25.12.5 aarch64/musl** (19 MB) è dentro la ramdisk sotto `/owrt`,
  + `/nx679j/switch.sh`, `/lib/ld-musl-aarch64.so.1` -> `libc.so` (0755!).
- Il **passaggio di consegne ora si completa**: i mount, il chroot e l'esecuzione di
  `/sbin/init` avvengono; poi il telefono **cicla ogni ~40 s**.
  ATTENZIONE: la spiegazione "PID 1 muore -> panic" era un'INFERENZA NON MISURATA ed e'
  stata **smentita** da v16 (che rende impossibile la morte di PID 1 e non ha fermato
  il ciclo).  Ipotesi concorrente, coerente coi numeri: e' il **nostro heartbeat di
  riavvio** (`period = journal_ok ? 8000 : 30000` nel percorso di fallimento di /init:
  8 s + ~30 s di boot ABL ≈ i ~40 s osservati).  Si distingue con UNA parola nella
  journal: se c'e' `heartbeat`, il riavvio e' NOSTRO; se la journal si interrompe
  senza, allora e' un panic.
- Evidenza dal lato PC (log degli `nc` dell'utente): `Cannot find device enp103s0f3u1`
  quasi sempre -> **il gadget e' su solo in brevi finestre** e il relè parte DOPO il
  gadget, quindi durante il ciclo `nc` trova porte chiuse (0 byte).  **Non e' un canale
  affidabile mentre il telefono cicla: la journal va letta da Android.**
- Limite: la journal (ramfs) arriva fino a "handing over"; la copia su `rawdump` viene
  fatta dal worker PRIMA del passaggio, quindi **sopravvive ai riavvii**.

## MISURATO nell'ultimo dump (rawdump, telefono ciclante)
- `heartbeat`: **0** -> il percorso di fallimento di /init NON viene preso: **non e' il nostro
  riavvio**; la coda della journal e' piena di `maint: tick` con gadget `configured`.
- `execve FAILED`: **0** -> l'execve di busybox NON fallisce piu' (il fix 0755 su libc.so ha
  funzionato).
- `switch.sh:`: **0** -> **lo script del passaggio NON produce nessuna riga**: non e' mai
  partito, oppure busybox muore prima di eseguirne la prima istruzione.
- `handing over`: 2 -> il passaggio viene tentato regolarmente.
- **Catena dedotta**: execve OK -> busybox muore subito -> PID 1 muore -> panic -> `panic=10`
  -> riavvio: ~30 s di avvio + 10 s = i ~40 s osservati.
- CONSEGUENZA DI PROGETTO: proteggere PID 1 *dentro* lo script non basta; serve proteggerlo
  dal caso "lo script non parte affatto".  Un `exec` che riesce ma il cui programma muore
  subito e', per il kernel, identico a un successo.

## v17 — non exec, ma spawn_bg (patch piccola, zero nuovo codice C)
1. In `/init`: sostituire il blocco `execve` con `spawn_bg("/nx679j/switch.sh")` e **registrare
   in journal l'exit status del figlio** (il init sa gia' fare wait4 sui figli, come per il
   relè).  Cosi': lo script non puo' uccidere PID 1, e se muore **leggiamo il suo rc**.
2. `switch.sh` deve avere shebang `#!/owrt/bin/busybox sh` (spawn_bg execa il programma senza
   argomenti: la shebang fa arrivare lo script a busybox).
3. Resta valido tutto il resto (mount loggati, mdev, procd come figlio, output in journal).

## Prossimo passo A — leggere la journal da Android (nessuna ipotesi)
Slot A e' la baseline Android. Telefono in fastboot (Volume Giu' durante il ciclo):
```
fastboot set_active a && fastboot reboot
# attesa Android ~32 s, poi:
adb -s 0123456789ABCDEF exec-out "su -c 'dd if=/dev/block/by-name/rawdump bs=4096 count=2048 2>/dev/null'" > /tmp/raw-v15.bin
grep -a 'nx679j\|switch.sh' /tmp/raw-v15.bin | tail -30
```
Cerca in particolare: `switch.sh: mount ... OK|FAILED`, `mdev rc=`, `/sbin/init executable:`,
`handing over with exec`.  Se compare "handing over with exec" e nulla dopo, il passaggio
e' pulito e il colpevole e' dentro procd.

## Prossimo passo B — v16 (patch a switch.sh, zero codice C)
1. **Non usare `exec`**: `$BB chroot /owrt /sbin/init >> /nx679j-journal 2>&1` seguito da
   `say "chroot returned rc=$?"` e dal ciclo di sicurezza `while :; do $BB sleep 3600; done`.
   Cosi' **PID 1 e' la shell di busybox e non puo' morire** -> niente panic, niente cicli,
   e il telefono resta leggibile dal relè.
2. L'output di procd finisce **nella journal** -> i suoi errori diventano leggibili.
3. Con la shell come PID 1, procd gira come figlio: OpenWrt funziona lo stesso, e dropbear
   parte comunque. Quando sara' stabile si tornera' a `exec` per avere procd come PID 1.

## v17 misurato (dump rawdump + relè, telefono STABILE)
- **PID 1 sopravvive**: il ciclo di riavvii e' FINITO.  Il telefono e' stabile (gadget su
  ininterrottamente per 110+ s).  Questo e' acquisito.
- La journal contiene `v17: switch.sh as a CHILD (PID1 cannot die)` -> `spawn_bg` e' stato
  chiamato.
- **`switch.sh` non scrive NEMMENO `start (pid=…)`** -> il figlio non esegue la prima
  istruzione: sospetti in ordine: shebang `#!/owrt/bin/busybox sh` non risolta, loader
  non trovato **per il figlio** (percorso assoluto risolto nella radice corrente), o
  esecuzione immediata dell'exit.
- **Mancano anche le due righe subito dopo la spawn** (`switch child started` /
  `spawn_bg FAILED`): il padre non le scrive, pur restando vivo e in `maint: tick`.
  Da chiarire: o `spawn_bg` non ritorna come crediamo, o quei `run_op(OP_DUMP, …)` non
  arrivano al worker.  **Non dedurre: misurare** (es. far scrivere al padre `spawn_bg
  rc=N` PRIMA di qualunque altra cosa, con un valore numerico).
- SSH: `Connection refused` -> niente in ascolto sulla 22: coerente con switch.sh/procd
  mai avviati.

## v18-b: contraddizione aperta (misurare, non dedurre)
- `grep -a` sull'immagine v18: `owrt/bin/busybox` presente (1), ma **`nx679j/switch.sh` NON
  trovabile**, mentre il passo di verifica del build (che controlla i nomi nel cpio) passava.
- Ipotesi in ordine: (a) il nome e' archiviato con prefisso diverso (`./`); (b) il file non
  finisce nella ramdisk **compressa** e la verifica guarda un albero/cpio diverso da quello
  impacchettato; (c) entrambe vere.
- NON risolta.  Da sciogliere con una misura locale sul **cpio prima della compressione**.

## DECISIONE DI PROGETTO (v19) - non dipendere piu' dal path exec-abile
Il difetto strutturale non e' "quel file": e' che il **passaggio di consegne dipende da un
`execve` di uno script dentro PID 1**, e un exec che fallisce in un modo qualsiasi uccide o
blocca.  Nuovo progetto, che elimina la classe di problema invece dell'istanza:

**nuova OP `switch` nel WORKER** (non nell'init):
- il worker e' gia' un processo figlio di PID 1 con accesso a file ed exec (e' lui che carica
  i moduli con `finit_module`): e' il posto giusto per fare il lavoro sporco;
- la OP fa `execve("/owrt/bin/busybox", {"/owrt/bin/busybox","sh","/nx679j/switch.sh"}, env)`
  con **argv esplicito** -> nessuna dipendenza dal bit exec dello script ne' dalla shebang;
- se l'execve fallisce, il worker **riporta l'errno numerico in journal** (come per i moduli);
- PID 1 resta vivo comunque: se il worker-procd muore, il telefono NON si riavvia.

Questo e' anche il disegno corretto in generale: **PID 1 deve solo sorvegliare, non eseguire.**

## v34: ESCLUSIONI FATTE (tutte misurate, nessuna dedotta)
- **download_mode NON e' la causa**: la cmdline dice `qcom-dload-mode.download_mode=1`, ma il
  modulo legge `download_mode = 0` (/sys/module/qcom_dload_mode/parameters/download_mode).
- **l'IOMMU non e' la causa**: `arm-smmu` e' BOUND a `15000000.apps-smmu`, i TBU sono agganciati
  (qsmmuv500-tbu: 11 device) e `msmdrm_smmu` e' bound.
- **la cmdline NON sta nel boot image**: in `boot_b` (mia e stock/Magisk) `cmdline_size = 0`.
  Arriva dal **bootconfig del vendor_boot** (offset ~13.934.592, verificato in passato).
  => per passare `deferred_probe_timeout=0` va scritto **vendor_boot_b** (partizione che non ho
  mai toccato: PRIMA backup, poi write+readback, e' recuperabile).
- **i rifiuti di bind/probe sono SILENZIOSI**: `bind` e `drivers_probe` tornano rc=1 senza una
  riga in dmesg neanche a printk=8.  Coerente con: device saltato perche' in stato deferred.

## FIX INDIVIDUATO (v34): RIALZARE IL TIMEOUT DEI PROBE DIFFERITI
Il device del modem non e' mai arrivato al *probe*: al primo tentativo una dipendenza non
era pronta, e il kernel ha **spento i retry** al timeout (misurato: "deferred probe timeout,
ignoring dependency" a ~19 s).  Conseguenza: dopo quel momento NON esiste modo di agganciarlo
(verificato: drivers_probe rifiutato, driver_override inefficace, rmmod+insmod inefficace).
Il firmware NON e' la causa: montato in /vendor/firmware_mnt e verificato leggibile, il device
resta deferred.

**Rimedio: portare la cmdline del kernel a `deferred_probe_timeout=0`** (= non scade MAI, i
retry restano attivi per tutto il boot).  Cosi' quando la catena moduli dell'init registra
`qcom_q6v5_pas` (a ~10 s), il kernel riprova il probe del device e questa volta le dipendenze
ci sono.  Il campo cmdline sta nella header del boot image: offset 268 (cmdline_size=2048),
verificato in passato con il test "probe-cmdline".  Il builder puo' riscriverlo.

Da misurare dopo il flash: `/sys/class/remoteproc/` popolata, `state=running`, poi `rmnet_*`.

## MODEM: STATO AL 2026-09-17 (v32) — fatti misurati, non ipotesi
FATTI ACQUISITI:
- l'init carica all'avvio tutto lo stack del modem (mdt_loader, qcom_pil_info, qcom_sysmon,
  qcom_q6v5, qcom_q6v5_pas, qmi_helpers, pdr_interface, glink_pkt, smem, mhi, mhi_cntrl_qcom,
  ipa_fmwk, usb_f_gsi) — verificato: compaiono in /sys/module a ogni boot
- il driver q6v5_pas CONOSCE il modem di questa board: alias of:N*T*Cqcom,waipio-modem-pas
  (58 compatible nella tabella, "waipio_mpss_resource" presente)  [strings del .ko]
- il device esiste col modalias giusto: of:Nremoteproc-mssT(null)Cqcom,waipio-modem-pas
- TUTTI i 7 fornitori (devlink) sono BOUND e waiting_for_supplier=0
- il firmware e' MONTATO prima della catena moduli (v32) e leggibile:
  /vendor/firmware_mnt/image/modem.mdt  (mknodat+mount nell'init, dev **8:70**)
- LEZIONI DURE: modem_a = /dev/sde6 = blk **8:70** (259:25 e' boot_b: un dev sbagliato
  e' costato 2 giri); vfat e' INTEGRATO (CONFIG_VFAT_FS=y, NLS_*=y) — i built-in NON
  appaiono in /sys/module, e io avevo concluso "e' un modulo" da quel check sbagliato,
  spostando il mount e causando io stesso il -EPROBE_DEFER.

APERTO: il device resta in **deferred** ("deferred probe timeout, ignoring dependency" a 19 s)
e non si aggancia: ne' drivers_probe, ne' driver_override, ne' rmmod+insmod (dopo il timeout
il kernel non riprova piu').  Differiti: 4080000.remoteproc-mss, 3000000.remoteproc-adsp,
2400000.remoteproc-slpi, 3da0000.kgsl-smmu, soc:gpio_keys.  Il cdsp invece si aggancia.

PROSSIMA IPOTESI (da misurare, non dedurre): la **dipendenza comune** del gruppo
mss/adsp/slpi (+kgsl-smmu).  Candidati in ordine:
 1. il path del firmware: il driver potrebbe cercare in **/lib/firmware** (path standard del
    firmware_class) e non in /vendor/firmware_mnt -> provare a montare/bindare anche li';
 2. l'IOMMU di questi rproc (devlink non visibile come supplier, ma presente via iommus=<>);
 3. il driver del core (qcom_q6v5) che richiede qcom_smem_state/smp2p pronti.
Misurare con: mount -t debugfs none /sys/kernel/debug; cat /sys/kernel/debug/devices_deferred
(e il motivo), dmesg senza il rumore "journal mirror".

## DIAGNOSI MODEM CHIUSA (2026-09-17) — la strada e' una sola
FATTO: tutti i fornitori (devlink) del device `4080000.remoteproc-mss` sono agganciati:
  rpmh-regulator-msslvl -> qcom,rpmh-regulator | qcom,rpmhclk -> clk-rpmh
  ed18000.qcom,ipcc     -> qcom_ipcc           | soc:qcom,smp2p-modem -> smp2p
  1700000.interconnect  -> qnoc-waipio
FATTO: il driver `qcom_q6v5_pas` e' registrato, il device DT esiste con
  compatible "qcom,waipio-modem-pas", ma il device resta in **deferred** e in dmesg
  NON esiste alcuna riga di probe per esso.
=> Il kernel ha esaurito i tentativi di probe differito ("deferred probe timeout,
   ignoring dependency").  Percio': **caricare i moduli DOPO il boot non puo' funzionare**,
   in nessun ordine.  I driver devono essere caricati DALL'INIT, prima che il budget scada.

## v26 — SPECIFICA (l'unica strada che puo' accendere il modem)
1. L'init carica, in questo ordine ESATTO (le dipendenze sono misurate):
     mdt_loader, qcom_pil_info, qcom_sysmon, qcom_q6v5, qcom_q6v5_pas,
     qmi_helpers, pdr_interface, glink_pkt, smem, mhi, mhi_cntrl_qcom,
     ipa_fmwk, usb_f_gsi
   (l'ordine conta: q6v5_pas da' "Unknown symbol qcom_q6v5_init" se q6v5/sysmon/pil_info
    non sono gia' dentro)
2. **PRIMA** di caricarli: montare `modem_a` (partizione /dev/sde6, dev 8:70, 300 MB)
   su `/vendor/firmware_mnt` -> il driver cerca li' il firmware.
3. Poi avviare i servizi permanenti: ubusd, rpcd, uhttpd (oggi a mano: LuCI non
   sopravvive al riavvio).
4. Verifica del successo: `/sys/class/remoteproc/` popolata e `remoteproc-mss` in
   `state=running`; poi comparsa delle `rmnet_*`.
ATTENZIONE ai falsi positivi misurati oggi: `rc=$?` dopo una pipe da' 0 su un
caricamento fallito; `${r:-OK}` stampa OK con output vuoto. Usare sempre
`out=$(cmd 2>&1); rc=$?`.

## MATERIALE MODEM TROVATO (2026-09-17, estratt0 dal PC)
La ramdisk vendor del telefono e' un blob **LZ4 legacy**:
  unpacked-current/vendor_boot_a/vendor_ramdisk00
Si estrae con la **CLI** (che c'era gia'): `lz4 -d -l -f <blob> /tmp/vr.cpio` -> 35205632 byte,
poi `cpio -idm < /tmp/vr.cpio` -> **337 file, 327 .ko**.
(2 tentativi persi: modulo python `lz4` assente, e un `except` MUTO che ha nascosto l'errore.
 Lezione: usare gli strumenti CLI e MAI inghiottire le eccezioni.)

.ko rilevanti presenti nello stack modem/acceleratori:
  ipa_fmwk.ko, mhi.ko, mhi_cntrl_qcom.ko, mhi_dev_*.ko, glink_pkt.ko, glink_probe.ko,
  qcom_glink.ko, pmic_glink.ko, cnss2.ko, cnss_nl.ko, cnss_plat_ipc_qmi_svc.ko,
  cnss_prealloc.ko, cnss_utils.ko, icnss2.ko, altmode-glink.ko, charger-ulog-glink.ko
NON compaiono q6v5/rmnet/ipc_router fra i .ko: sono built-in o interni al vendor
(da verificare: /sys/module sul telefono e la lista completa dei 327).

## PROSSIMO PASSO (v26) — portare su il modem
1. caricare i .ko dello stack modem/ipa/mhi/glink (dependancies incluse, da modules.dep);
2. montare la partizione **modem_a** dove il kernel cerca il firmware
   (/vendor/firmware_mnt non esiste oggi): identificarla nella GPT di /dev/sde;
3. avviare i servizi in modo permanente dall'immagine: ubusd, rpcd, uhttpd
   (oggi li avvio a mano via SSH: LuCI non sopravvive a un riavvio);
4. misurare: `/sys/class/remoteproc/*/state` = running, comparsa di `rmnet_*`;
5. poi QMI: installare `uqmi` dal PC (il telefono non ha ancora internet) e fare la call dati.

## v23/v24 MISURATO via SSH (il passo avanti grande)
- **I mount VANNO FATTI DALL'INTERNO del chroot**: dopo v23, dentro /owrt `ps w` = 187 processi,
  `ls /proc` = 245 voci (prima: ps vuoto, "mount: no /proc/mounts", /proc/<pid> inesistenti).
- **/var assente** (v24 lo crea): senza, ogni init script moriva con
  "can't create /var/lock/procd_*.lock: nonexistent directory".  Dopo v24: /var/lock, /var/run,
  /var/log esistono.
- **ubusd NON e' rotto**: avviato a mano (`/sbin/ubusd &`) `ubus list` mostra l'albero standard
  (container, hotplug.*, ...).  Quindi **procd non lancia i servizi**, non e' ubus a essere rotto.
- **Flash autonomo via SSH (funziona)**: `boot_b` = **/dev/sde41** (dev 259:25), identificato
  dalla GPT di /dev/sde (nome + start_512=2996088 combacianti).  Sequenza validata:
  backup (dd if=sde41) -> `cat img | ssh 'dd of=/dev/sde41 bs=1M'` -> readback sha256 identico.
  Il `reboot` di busybox nel chroot **NON** riavvia: usare
  `echo 1 > /proc/sys/kernel/sysrq; echo b > /proc/sysrq-trigger`.
- Backup di stato buono: /tmp/boot_b-backup-v23.img (100663296 B, sha d593d1361a341459).

## ERRORE APERTO (importante)
`/etc/init.d/network start` **ha buttato giu' il percorso di rete**: netifd prende in mano usb0
e il telefono non risponde piu' su 10.0.0.1 (gadget presente, ping/ssh muti).
=> La config di rete in OpenWrt **non deve toccare usb0**: usb0 e' di competenza del NOSTRO init.
Recupero: power cycle -> fastboot -> set_active a (Android) oppure ripristino di
/tmp/boot_b-backup-v23.img.

## v18 MISURATO (verdetto definitivo)
- Journal: `v18: spawn ok` + **`v18: child exit 127 EXEC FAILED`**.
- Il padre sopravvive e riporta il codice d'uscita: **il kernel rifiuta l'exec** del figlio
  (`/nx679j/switch.sh`).  Non e' la shebang mal interpretata, non e' procd: **execve fallisce**.
- Corollario: la conclusione v14 "execve ora PASSA" era **un'inferenza da una riga ASSENTE**,
  non una misura.  Va cancellata: non sappiamo che execve passi.
- 127 = lo shell-fallback di `spawn_bg` (il figlio non e' riuscito a eseguire).
  Da distinguere con un `stat`/`access` **dal worker** (che ha accesso ai file) su:
  `/nx679j/switch.sh`, `/owrt/bin/busybox`, `/lib/ld-musl-aarch64.so.1` + i loro modi.
  Questo e' il prossimo passo: **misurare l'errno esatto**, non dedurlo.

## Prossimo passo (in ordine, dal piu' economico)
1. **Test minimo della shebang**: un `/nx679j/switch.sh` che scrive UNA riga e dorme,
   lanciato con `spawn_bg`: se scrive, il problema e' dentro lo script; se non scrive,
   e' il modo in cui il figlio viene eseguito.
2. Sostituire la shebang con `/owrt/bin/busybox` come **launch diretto** dal padre
   (argv con argomenti) invece di affidarsi alla shebang.
3. Se serve, far riportare al padre `wait4` sul figlio con il suo **exit status numerico**
   (il padre sa gia' farlo per il relè).

## Lezioni durevoli imparate (per non ripeterle)
- **newc**: i 13 campi del cpio sono **ASCII esadecimale** di 8 caratteri, non interi binari;
  il nome e' paddato perche' *header+nome* sia multiplo di 4; i dati sono allineati a **4**.
- **L'interprete dinamico deve essere eseguibile** (0755): un `/lib/libc.so` 0644 copiato a
  mano fa fallire l'`execve` con **EACCES**, non ENOENT.
- Un `tar` estratto da utente non privilegiato **puo'** perdere i bit `x`; i modi autorevoli
  stanno nel tar e vanno ripristinati da li'.
- **Dopo ogni flash il lato PC va riconfigurato**: `sudo ip addr replace 10.0.0.2/24 dev enp103s0f3u1`
  (l'interfaccia USB viene ricreata da zero).
- Le immagini si costruiscono **in famiglia con script** (`experiments/build-v10.py`, ~0,5 s),
  NON con subagent: il collo di bottiglia era l'orchestrazione, non il PC.
- Un PID 1 che puo' morire = panic = ciclo di riavvii: **il fallback deve impedire la morte
  di PID 1**, non solo registrarla.
