# UI NX679J: il bug dell'orologio, lo standby, e come si verifica (v176-v178)

## Il bug che ha spiegato mezza giornata: now_ms() in overflow

`ui-1-base.c` calcolava `now_ms()` come `(int)(tv_sec*1000 + tv_usec/1000)`. Nel 2026
`tv_sec*1000 ~ 1.79e12` -> overflow int32 -> valore **negativo** (misurato: -654600784).
Due gate usavano `now_ms()` con soglia 0, quindi erano **sempre veri/falsi nel verso sbagliato**:

- il gate di rescan dei tasti (`if (tn < t_key_scan_next) return;`) usciva SEMPRE: **i device dei
  tasti non venivano mai aperti** (nessuna riga `tasto:` nel log);
- il gate anti-tocco-fantasma (`if (now_ms() < t_wake_ignore)`) era vero a ogni frame: **ogni tocco
  veniva SCARTATO prima di do_action()** - la UI sembrava "touch morto" anche se gli eventi arrivavano.

Fix: `clock_gettime(CLOCK_MONOTONIC)` + cast a int (i millisecondi monotoni stanno in int32 per ~24 giorni).
**Lezione**: qualunque gate temporale in questa UI si verifica con un harness che chiama la funzione
VERA (il collaudo host non chiamava key_scan(): per questo non vedeva la regressione).

## Standby del pannello (v178, verificato sul campo)

Requisito utente: il tasto laterale spegne il pannello (burn-in AMOLED) con tutto il resto vivo.
Design che funziona:

- **Regola**: il flag `standby` sale PRIMA del tentativo di spegnimento; da quel momento il loop non
  manda PIU' NULLA al DRM (ne commit ne wait_fence). Il bug storico era il commit del frame FUORI
  dalla guardia `dirty && !standby`: continuava a committare a pannello mezzo spento -> incastro.
- **Spegnimento**: `dpms_set(3)` (OBJ_SETPROPERTY: il commit lo costruisce il kernel), ripiego il
  commit a tre oggetti (crtc ACTIVE=0 + conn CRTC_ID=0 + plane CRTC_ID=0).
- **Risveglio**: un solo tentativo `commit_cnp(fb,0,2)` (modeset+ACTIVE+idle_pc_state nello stesso
  commit), ripiego `dpms_set(0)`. NO loop di ritentativi: un click = un tentativo.
- **Tasto**: click singolo = standby (finestra 350 ms), doppio click = attiva, lungo >=1.5 s = indietro.
- **Heartbeat** ogni 10 s mentre off: `standby: loop vivo (frame fermo a N, tasti X/Y aperti)` -
  cosi' "loop fermo" e "loop in standby" non si confondono mai nel log.
- Comando di servizio: `/tmp/ui-standby` (si crea il file per togglare, senza il tasto).

Prova verificata (25/09): off -> webcam NERA + `dpms=Off` + heartbeat + ui-data che cresce (11990->12019);
on -> `standby: schermo ON` + `dpms=On` + frame che riprendono (4200->4800, gap registrato 40747 ms).

## Verifica: le regole che hanno salvato la giornata

1. **Webcam OBBLIGATORIA** per lo stato dello schermo (l'utente lo pretende): 1920x1080, raffica di
   8-10 frame, si tiene il piu' grande (il 4K e' corrotto; singoli frame escono verdi). Ritaglio 1:1
   se serve leggere valori.
2. **Verificare l'artefatto DENTRO l'immagine**, non quello sul disco: estrarre il ramdisk
   (lz4 -> cpio multi-archivio), cercare il nome in binario, calcolare l'md5 del contenuto. Un
   estrattore troncato produce falsi "non trovato": leggere verboso e controllare le dimensioni.
3. **Un comando di riavvio non e' un riavvio**: la prova e' `U2 < U1` sull'uptime.
4. **kill -9 non uccide la UI se e' in stato D** (commit su CRTC spento): l'unico reset e' il riavvio.
5. **L'iniezione uinput non puo' verificare i tasti**: la UI apre per nome e SALTA i duplicati
   (un "pmic_resin" virtuale viene ignorato perche' quello vero c'e' gia'). Il device sintetico del
   TOUCH invece funziona (nome unico) ed e' il modo giusto per validare il percorso input della UI.

## ESITO FINALE (25/09, v180): lo standby FUNZIONA

Il fix NON e' nel wake: e' nel NON spegnere mai il link. Il DPMS-off su questo tree uccide il
DSI command link (`-110 wait_for_idle`/`wr_ptr_irq wait failed`) e non risorge senza riavvio.
Lo stock Android non lo fa mai (`SetDisplayState: state=0, teardown=0`): tiene il display preparato.

**Design v180 (verificato con webcam end-to-end):**
- standby ON: `bl_read()` del valore attuale, commit di UN frame nero (entrambi i kmap a 0), poi
  `bl_set(0)` su /sys/class/backlight/panel0-backlight/brightness. NIENTE DPMS, niente unprepare.
  Il loop CONTINUA a committare frame neri (tiene esercitato il link); heartbeat
  `standby: loop vivo (nero) (frame N, tasti a/b aperti)` ogni 10 s.
- standby OFF: `standby=0` + `dirty_req=1` (ridisegno pieno) + `bl_restore()` del valore salvato.
  La guardia del click (600 ms) resta.
- Su AMOLED i pixel neri sono spenti fisicamente: zero burn-in con il link vivo.
- Prova misurata: off -> webcam NERA, `bl=0`, `dpms=On` (mai toccato), frame che avanzano; on ->
  webcam che LEGGE L'INTERFACCIA (IPv4/salute live), `bl=2608`, fence_timeout=0.
- Il collaudo host asserisce `dpms_calls==0` (nessuna chiamata DPMS) e i contatori bl/sb.

Se in futuro servisse recuperare un link morto: evento DRM_EVENT_SDE_HW_RECOVERY registrato da
userspace + ciclo detach/attach (connector CRTC_ID=0 poi CRTC_ID=crtc+mode+ACTIVE=1 con
ALLOW_MODESET) - ricetta dalla ricerca, mai necessaria in v180.

## Touch: stato al 25/09 (per chi riprende)

- Init del driver identica riga per riga al log stock funzionante; `rom_pid`/`FW-State: 0x102DC`
  sono normali; `can't find valid panel` e' benigno (lo stampa anche lo stock: `active_panel=NULL`
  su entrambi, il phandle `panel` non esiste in nessuno dei due DT).
- L'hardware risponde sul bus ma **non consegna eventi** (IRQ ~4 statici, 0 byte su eventN anche
  mentre si tocca). Il fingerprint condivide il controller; `fp_switch` non si azzera.
- **MAI** toccare i nodi del driver `fwupdate/result`, `get_rawdata`, `esd_info`: fanno crashare il kernel.
- Il volume su NON e' esposto al kernel (il `pmic_resin` dichiara solo il bit 114).
