# NX679J DISPLAY + TOUCH — ricognizione e stato (2026-09-21)

## ✅ SCOPERIA 2026-09-22: il pannello VERO è VDTR6130 (fix = DTBO default-panel)

**Sintomo:** panel init "ok" ma frame mai mostrati (logo ABL residuo); su Android, stessa cmdline, "dsi vdtr6130 1080 2400 amoled command dphy" funzionava. **[STORICO — risolto col DTBO fix sotto]**

**Root cause (verificata byte-per-byte):** kernel 5.10.66 vendor IDENTICO tra boot_a/boot_b (solo kernel_size in header + ramdisk differenti), vendor_boot_a/b IDENTICI (md5), dtbo_a/b IDENTICI, running_fdt IDENTICI. MA: msm_drm sceglie il nodo pannello da `boot_disp_en` (module_param `msm_drm.dsi_display0`, cmdline); su Android è settato, su OpenWrt resta VUOTO (il bootconfig dell'ABL per lo slot B non ha la chiave) → fallback `qcom,dsi-default-panel` = nel **dtbo fragment 35** puntava a R66451 (sbagliato!).

**FIX (reversibile):** DTBO slot B (sde45), fragment 35 (dtbo_idx=35): `qcom,dsi-default-panel = <&dsi_r66451_amoled_cmd>` → `<&dsi_nubia_r6130_amoled_cmd_dphy>`; dtc; ricostruzione DTBO (44 entry, header preservato, round-trip byte-perfect); `fastboot flash dtbo_b`.

**Risultato confermato:** debugfs dir pannello = `qcom,mdss_dsi_r6130_1080_2400_amoled_cmd` (era r66451); conn 56 = 5 modi 1080x2400x{90,165,144,120,60}cmd; crtc = 152/214/223/232/241 (= Android); pannello mostra contenuti.

**Legacy vs ATOMIC (SUPERATO — l'ATOMIC FUNZIONA!):** il "crash del legacy" col VDTR6130 era l'ESD recovery conseguente, non il setcrtc: con l'ESD killato il SETCRTC legacy passa al primo colpo e i pixel si vedono. **[AGGIORNAMENTO 22/09 tarda notte: la nota "props atomic assenti / atomic = vicolo cieco" era un FALSO NEGATIVO — mancava `SET_CLIENT_CAP(ATOMIC)` (nessun tool l'aveva mai settata: `drm_atomic_uapi.c:1306` `if (!file_priv->atomic) return -EINVAL;`). Con quella + commit completo c-n-p il pannello AGGIORNA (ROSSO→VERDE verificato 3×) e il processo può restare vivo a lungo. Dettagli e lezioni generali nella sezione "COMMIT ATOMIC" in fondo al file.]**

**Metodi riutilizzabili:** module_param `dsi_display0` @ /sys/module/msm_drm/parameters/; selezione nodo: boot_disp_en → altrimenti `qcom,dsi-default-panel` (dtbo); fragment = `androidboot.dtbo_idx` in /proc/bootconfig (35); dtbo: header 32B + entry 32B (size, off, id, rev), blob contigui; round-trip test prima del flash; padding file preservato (partizione 24MB).

## Hardware identificato (CONFERMATO da dmesg live + sorgente vendor ZTE)

### Pannello: il nodo ATTIVO è `qcom,mdss_dsi_r6130_1080_2400_amoled_cmd` (VDTR6130)
- **[STORICO, SMENTITO]** per una sessione la dir debugfs era `...visionox_r66451_fhd_plus_cmd` e il dmesg diceva `r66451 amoled cmd mode dsi visionox panel with DSC` — era il sintomo del `qcom,dsi-default-panel` sbagliato nel dtbo (fix in testa al file): dopo il DTBO fix la dir è `qcom,mdss_dsi_r6130_1080_2400_amoled_cmd` e i pixel arrivano. Dati del nodo (sorgente/XML ABL): DSC 1.1, slice 540x20.
- La cmdline ABL nomina `msm_drm.dsi_display0=qcom,mdss_dsi_r6130_1080_2400_amoled_cmd:`. Regola generale: la **dir debugfs del driver pannello è la prova** di quale nodo è attivo — la cmdline da sola non basta. (Le vecchie note "VTDR6130/Viewtrix vdtr" non sono confermate dal runtime; per il lavoro mainline resta da verificare quale delle due varianti sia il pezzo fisico.)
- 6.78" AMOLED 1080x2340 (modi kernel: 60/90/120cmd), DSI 4 lane, **command mode**, DSC.
- Timing (fdt timing@0): h 20/20/2, v 40/18/2, framerate 90.
- **/dev/fb0 NON esiste**: `CONFIG_DRM_FBDEV_EMULATION is not set` → solo DRM/KMS.
- Backlight: `bl_ctrl_dcs` (comandi DCS, non PWM!) — se schermo nero: sospetto n.1 la luminosità a 0.
- Power sequence (ZTE dsi_panel.c): VDDIO → 2ms → VCI → 2ms → DVDD_1P2 (GPIO 88) high → 5ms; reset GPIO 0 seq <0 2 1 15>.
- **Driver mainline ESISTE**: `drivers/gpu/drm/panel/panel-visionox-vtdr6130.c` (compatible `visionox,vtdr6130`, DSC aggiunto 2026, default su SM8550/8650-QRD).
- Doppia sorgente pannello: secondo fornitore Raydium RM692E0 (nostro esemplare = Viewtrix, non ipotesi).
- Sorgente vendor: github.com/ztemt/NX679S (display-drivers.zip → msm/dsi/dsi_panel.c; i DTSI pannello NON sono pubblicati).

### Touch: Goodix GT9897 (Berlin-A)
- I2C @990000 (QUP SE4, `i2c@990000/goodix-berlin@5d`, addr 0x5D, 1 MHz).
- GPIO: reset TLMM 20, IRQ TLMM 21 (falling|oneshot), iovdd TLMM 89; avdd = PM8350C L3C.
- Driver vendor: `goodix_core.ko` (da `panel_event_notifier.ko`), alias `i2c:gtx8_i2c` + `platform:nubia_goodix_ts`.
- **CARICAMENTO (verificato live!)**: `/tmp/finitmod gpi.ko → i2c-msm-geni.ko → goodix_core.ko`.
  `gpi.ko` è LA chiave: senza, i bus i2c restano in **deferred probe** (`dma-names="tx rx"` → il provider GPI manca e il probe deferisce per sempre; non auto-retry dopo il timeout del kernel).
  Ordine completo di init utili: `msm-geni-se.ko` (spesso già su), `qcom_ipc_logging/minidump/smem`, `gpi.ko`, `i2c-msm-geni.ko`, `goodix_core.ko`.
- Risultato: `input: nubia_goodix_ts as input0`, IRQ 393, "success register irq", "esd on". Nodo: `mknod /dev/input/event0 c 13 64`.
- **Driver mainline**: `goodix_berlin_core.c` + `goodix_berlin_{i2c,spi}.c` (merged 2024). SPI supporta già gt9897 (serie Berlin-A, v6.15); **I2C NO** → patch ~3 righe: definire `gt9897_data` (fw_version_info_addr=0x1000C, ic_info_addr=0x10068) + compatible in `goodix_berlin_i2c.c`. Binding già documentato. Sarebbe il primo device pubblico GT9897-su-I2C.

## Display su kernel vendor — storico + stato aggiornato 23/09

**Stato attuale:** il test statico `nx679j-drmtest` è master live (PID 24653) e la webcam mostra bande orizzontali pulite; framebuffer XR24 1080×2400 con pitch 4352. Il kiosk precedente mostrava linee diagonali arancio/verdi e sfondo glitchato. Il suo renderer usava stride `1080*4=4320`; il pitch DRM effettivo è 4352. Il pitch-fix host passa RED→GREEN e SHA coincide host/device, ma NON è stato ancora eseguito sul pannello. Non dichiarare il kiosk risolto. Il reboot del precedente handoff resta non attribuito. Touch risponde per conferma utente.


**FATTO (segnalato dall'utente): con tutto il DRM "verde" — connector `card0-DSI-1` = `enabled`, `SETCRTC` rc=0, processo col framebuffer vivo — lo schermo mostra SOLO il logo RedMagic lasciato dal bootloader.** `enabled` e un rc=0 sono stato interno del DRM, NON pixel a schermo. Un pannello cmd-mode con self-refresh continua a mostrare l'ULTIMO frame ricevuto: se l'utente vede il logo, nessun nostro frame è mai arrivato al pannello. **Regola: mai dichiarare il display funzionante senza conferma visiva dell'utente.**

**Pipeline di setup (valida, verificata live):**
1. `mknod /dev/dri/card0 c 226 0` + `/dev/input/event0 c 13 64`
2. `finitmod` panel_event_notifier, gpi, i2c-msm-geni, goodix_core
3. Attendere il bind del goodix (`grep -c nubia /proc/bus/input/devices` = 1; il driver fa init asincrona)
4. Gap di sicurezza (~minuti)
5. **Mode-set statico verificato su CRTC vergine:** nella baseline live, `display-late.sh` con ESD mode impostato ha lanciato `nx679j-drmtest`, che ha prodotto `SETCRTC OK` 1080×2400@90cmd su CRTC 152; dmesg ha riportato `dsi_display_set_mode` e la webcam mostra bande R/G/B/bianco. Questo supersede l'affermazione storica che il `drmtest` diretto non producesse il mode-set; non prova il kiosk atomic o LuCI.
6. Il launcher attuale in v87 `nx679j-display-late.sh` attende uptime ≥900s, poi bind/ESD e gap effettivo 250s prima di lanciare `nx679j-drmtest`; il test di stride sostituirà quel binario solo nel rootfs volatile dopo il reboot, prima che parta il launcher. La funzione anti-loop usa `seek=410`; la lettura live di quell'offset conteneva `slot=378` (`32+378=410`) con boot ID corrente: possibile collisione, non fidarsi del marker finché non chiarita. Nessuna modifica persistente/flash per la prova singola.
7. **Il touch resta attivo** (input0); per VERIFICARLO senza rischi: `nx679j-touchmon` (sola lettura di event0, convive con un holder DRM); per DISEGNARE: `nx679j-touchpaint` o `nx679j-touchsim` (self-test uinput, v. sezione vittoria). I reset attribuiti al "loop input" erano conflitti fra DUE client DRM e setcrtc dopo suspend: **un solo client display alla volta**; per ricominciare puliti, reboot.

**Firma diagnostica dmesg — come sapere se il mode-set ha toccato l'hardware:**
- `[drm:dsi_display_set_mode [msm_drm]] ... hactive=1080 ...` presente = il driver ha eseguito il mode-set hardware (il boot stock Android lo logga: è il riferimento).
- Assenza di quel log + `_sde_encoder_phys_cmd_handle_wr_ptr_timeout: wr_ptr_irq wait failed` e/o `wait_for_idle: -110` (ETIMEDOUT) = i frame non raggiungono il pannello (pipeline cmd-mode bloccata dallo stato lasciato dall'ABL). Un "SETCRTC OK" senza `dsi_display_set_mode` = falso successo.
- **Non chiudere alla leggera il processo DRM.** Sorgenti Qualcomm pubblici comparabili mostrano che il close del master può passare da `preclose` e fare cleanup commit per-file; `lastclose` globale è un percorso distinto che scatta solo all'ultimo fd. Le fonti trovate non sono byte-identiche al vendor live 5.10.66 e non spiegano da sole il reboot osservato. Nessun master attivo va terminato durante il test pitch.

## Le tre fonti-oracolo (il codice risponde: leggerlo prima di tentare)
1. **Sorgenti DRM/SDE: provenienza limitata.** `kernel-patch-test/stock-kernel-source/Makefile` dichiara 5.4.242, quindi quei file techpack sono solo comparabili e non prova del kernel live 5.10.66. La tree pubblica ZTE/NX679S trovata è 5.10.101, anch'essa non byte-identica. Usare i sorgenti per ipotesi di flusso, non attribuire loro i reset live senza log.
2. **FDT live decompilato**: `port-work/vboot-dtb-swap/stock_dump/running_fdt.dts` — nodo `qcom,mdss_dsi_visionox_r66451_fhd_plus_cmd`: `qcom,mdss-dsi-on-command` (la sequenza), `panel-status-*` (read reg 0x0A, atteso 0x1c), te-*, DSC (slice 540x20, 8bpc/8bpp). I .dtb si decompilano con `dtc -I dtb -O dts -f`.
3. **XML del pannello dall'ABL**: `full_extracted_v311/uefi.img.dump/…/Panel_r66451_60hz_fhd_plus_dsc_cmd.xml/body.bin` = testo XML dentro un .bin — la config PROVATA (il suo logo si vede a schermo): timing, DSC 1.1 slice 540x20, `DSIEnableAutoRefresh=True` (frame num div 1), e la sequenza init SHORT (righe `39 <reg> <val…>`; `ff <n>` = delay). Le varianti 60/120Hz sono le voci 55/56 dell'immagine UEFI.

**Cosa vedeva l'utente PRIMA del DTBO fix (storico):**
- Dopo il primo disable-first il logo ABL è sparito (stato pannello azzerato): 60Hz = "una linea glitchata in alto" (identica con bande, rosso pieno e verde pieno); 90Hz = nero. A fine sessione, anche dopo init-completa + recommit + dirtyfb full-frame: la linea resta. Nessuna configurazione ha mai mostrato il frame **finché non sono arrivati il DTBO fix (nodo pannello) + ESD kill: da lì bande e X visibili (v. sezione vittoria).**
- **Verifica visiva autonoma (la via preferita):** webcam puntata sul telefono + OBS (websocket :4455) + `obs_shot.py` → PNG, poi `vision_analyze` che legge il pannello ("bande"/"X bianca"/"logo") — è così che bande e X sono state confermate senza disturbare l'utente. Fallback quando la webcam non c'è: chiedere una FOTO all'utente ("linea glitchata" vs "nero" vs "bande" discrimina "frame parziale" da "nessun frame").
- `crtc153/fps = 0.0` con crtc attivo e con `encoder55/status` che mostra il **vsync che cresce**: i TE arrivano ma il SDE non kicka frame — fps=0 da solo non è diagnosi, ma fps=0 + vsync crescente localizza il blocco a valle del timing (kickoff/frame).

## Esito 22/09: pattern di test statico visibile (non equivale a kiosk pulito o LuCI)

**SELF-TEST COMPLETO touch→disegno (nx679j-touchsim):** uinput: `mknod /dev/uinput c 10 223` (major 10, minor dal /proc/misc); event virtuale: `mknod /dev/input/eventN c 13 (64+N)` (devtmpfs NON crea i nodi input!). **ORDINE CRITICO: disegnare nel fb PRIMA del setcrtc** — il primo frame pushato al setcrtc è quello che si vede (disegnare dopo = nessun update senza un nuovo commit/pageflip). Round-trip: 2948 eventi iniettati+riletti, X gigante visibile a schermo (verificato con foto). REGOLE: UN solo client DRM; setcrtc dopo suspend/lastclose = crash watchdog; mai kill -9 del client display; touchpaint instabile, drmtest stabile.

**LuCI "protocol unsupported"** = manca il descrittore JS del proto custom: creare `/www/luci-static/resources/protocol/nx679j.js` (`network.registerProtocol('nx679j', {getI18n, renderFormOptions})`) — rootfs volatile → il launcher v4 lo ricrea al boot.

**Ricetta finale — servono TUTTI e 3 i pezzi:**
1. **DTBO fix** (pannello sbagliato: frag35 `qcom,dsi-default-panel` = r66451 → patch a `<&dsi_nubia_r6130_amoled_cmd_dphy>`; ricompilare con dtc, riassemblare l'immagine con round-trip md5 verificato, `fastboot flash dtbo_b`) → debugfs `qcom,mdss_dsi_r6130_1080_2400_amoled_cmd`, crtc **152**/214/223/232/241.
2. **ESD status simulation** (impostare `esd_sw_sim_success` prima del test: sul build osservato arresta il polling/mismatch ESD; non protegge da ogni fault DSI o clock e non prova che il reset successivo sia ESD).
3. **Sia SETCRTC sia ATOMIC hanno evidenza:** il legacy `SETCRTC` ha mostrato bande pulite nel baseline statico; ATOMIC funziona dopo `SET_CLIENT_CAP(ATOMIC=3)` + primo commit c-n-p completo, poi plane-only. Non dedurre che il kernel non supporti proprietà atomic dai precedenti tentativi senza cap. PAGE_FLIP legacy resta ENOTTY nel test osservato.
**Il vecchio "crash del setcrtc" era in realtà l'ESD recovery conseguente**, non il setcrtc.

## IL LOOP DI RECOVERY ESD (spegnere PRIMA di ogni bring-up)
- `dsi_display_validate_status` gira da worker ogni ~5s: legge il registro di stato del DDIC (dal DT: read del reg **0x0A** = power mode, atteso **0x1c**) e al mismatch esegue `atomic_set(&panel->esd_recovery_pending, 1)` → il driver considera il pannello guasto e lo RESETTA/re-inizializza. Con un mismatch ogni ~5s nessuna bring-up ha mai una finestra stabile >5s: ogni esperimento parte da uno stato che viene azzerato subito dopo.
- Firma dmesg: `dsi_display_validate_status *ERROR* mismatch[C5]: 0x9c, mismatch[0A]: 0x0, status_value: 0x1c, i:0` a raffica ogni ~5.1s. (0x9c&0x1c=0x1c suggerisce una maschera mancante, ma il driver non maschera: il mismatch È il comportamento.)
- **Spegnerlo (effetto verificato):** `echo "esd_sw_sim_success" > /sys/kernel/debug/qcom,mdss_dsi_r6130_1080_2400_amoled_cmd/esd_check_mode` (la dir è il nodo pannello ATTIVO: col fix dtbo è r6130) → il check risulta sempre OK e la raffica di mismatch si FERMA (verifica: confrontare il timestamp dell'ultimo mismatch col valore corrente di uptime). Valori accettati (strcmp esatto; `echo` col newline va bene): `te_signal_check`, `reg_read` (riattiva), `esd_sw_sim_success`, `esd_sw_sim_failure`.
- **NON persiste tra boot**: rimetterlo nella sequenza di bring-up (launcher) PRIMA del mode-set.
- A loop spento il pannello è stabile: è la condizione minima per poter interpretare qualunque esperimento sul pannello.

## Debug di un reset silenzioso (hardware)
- Un reset HW non lascia panic/oops (nessuna pstore): l'unica traccia è il `crashlog-dump.sh` (parte da solo al boot) che scrive il tail di dmesg ogni 3s negli slot del rawdump — slot fisico `32+i`, lettura `dd if=/proc/1/root/dev/rd bs=32768 skip=$((32+i)) count=1`.
- Dopo il reboot il nuovo boot riscrive gli slot bassi: i dati del boot morto sopravvivono **sopra** il punto raggiunto dal boot nuovo. Mappa i checkpoint (`... | head -c 90 | strings | head -1` → `slot=N uptime=X boot=<id>`) e leggi gli ultimi del boot morto: gli ultimi secondi prima del reset sono lì.
- Il tail dmesg dei checkpoint annega nello spam hostapd/wifi: prima di un esperimento `dmesg -c > /dev/null` e leggi `dmesg` SUBITO dopo l'azione — altrimenti il ring (e quindi i checkpoint) non contiene le righe che servono.
- Firma del reset silenzioso da CONFLITTO display (due client col fb / setcrtc dopo suspend): `crtc:152 event(...) value(0) notified` + `drm_release open_count=2` + ESD ancora `success` per ~20s + morte senza panic = hang della pipeline → watchdog HW.
- **Slot rawdump dei tool: leggere l'INIZIO dello slot, non il tail** — i logger dei tool riscrivono a offset fisso dentro lo slot (pwrite), quindi al boot successivo lo slot può contenere un mix vecchio/nuovo e il `tail` mostra righe del run PRECEDENTE. Azzerare lo slot prima di un test, o leggere `head` (prefisso) per il run corrente: `dd if=/proc/1/root/dev/rd bs=32768 skip=$((32+SLOT)) count=1 | strings | head -20`.

**Debugfs vendor (VERIFICATO; `mount -t debugfs none /sys/kernel/debug` se manca):**
- `/sys/kernel/debug/dri/0/DSI-1/`: `tx_cmd`, `rx_cmd`, `esd_status_interval`, `force`, `edid_override`, `vrr_range`.
- `tx_cmd` = canale comandi DSI raw. **Formato: numeri DECIMALI separati da spazi** (NON esadecimale!) — un pacchetto DSI per write, layout `[type] [last=1] [vc=0] [ack=0] [wait_hi] [wait_lo] [len] [payload...]`. Esempi (hex DT → decimale): `05 01 00 00 78 00 01 11` → `5 1 0 0 120 0 1 17` (sleep-out, delay 120ms); `39 01 00 00 00 00 02 b0 00` → `57 1 0 0 0 0 2 176 0`. In esadecimale il parser fallisce con `[sde error]input buffer conversion failed` sulla prima lettera non numerica. La SEQUENZA DI INIT COMPLETA del nodo attivo (34 pacchetti: config registri + sleep-out 0x11 + display-on 0x29) si invia così ed è accettata 34/34 (`INIT-OK=34 FAIL=0`); generatore host: `scripts/dts-on-command-to-txcmd.py` (dal `running_fdt.dts`) → script sh da eseguire sul device. Inviarla da sola NON ha prodotto pixel (provata su pannello stabilizzato, prima e dopo il recommit). `rx_cmd`/`esd_status_interval` (letto: 0) non ancora sfruttati.
- `/sys/kernel/debug/dri/0/crtc153/`: `state` (clock/bus), `status` (mixer split **540+540 = DSC 2-slice**, plane+fb: id, XR24, src/dst 1080x2340, pitches, rotation), `fps`, `misr_data`, `fence_status`. `crtc153` = crtc-0 DRM = il DSI (`intf_mode:1` = cmd mode). `crtc153/status` = verifica che il pipeline legge il buffer giusto (fb id, formato, rect).
- `/sys/kernel/debug/dri/0/encoder55/`: `status` = `intf:1 vsync:N underrun:0 mode: command` — **se il vsync cresce con l'uptime i TE del pannello ARRIVANO al SDE** (underrun 0). `fps=0.0` con vsync che cresce = TE ok ma NESSUN kickoff di frame → il blocco è a valle del timing. Altri: `frame_trigger_mode`, `idle_power_collapse` (Y), `misr_data` (disabled).
- `/sys/kernel/debug/<pannello-attivo>/` = **DIR** (non un file): `esd_check_mode` (rw), `esd_trigger` (rw, mai usato), `dump_info`, `ulps_*`, `parser/` (+ sul nodo r6130: `aod_lowpower_bypass_enable`, `clk_gating_config`, `cmd_sched_params`, `dsi-ctrl-0`, `dsi-phy-0_*`, `misr_data`). È il debugfs del driver pannello: sede delle manopole ESD (sezione sotto). Sul nodo r6130 NON esiste un file `esd_sw_sim_success`: il valore si scrive su `esd_check_mode`.
- Ogni kill/restart del tool DRM fa fare al driver un ciclo REALE di alimentazione del pannello (`nubia_dsi_pwr_enable_vreg: vreg: vddio`/`vci` ON/OFF): è il power-cycle più vicino a un boot pulito; dopo un kill durante `wait_for_idle` può comparire `sde_rsc ... mdss gdsc power down failed` (si ricompone al successivo enable).

**Bussola (STORICA — risolta dal DTBO fix: il pezzo mancante era il nodo pannello sbagliato; i candidati sotto non servono più):** fatto e verificato: disable-first (mode-set hardware), loop ESD spento (`esd_sw_sim_success`), sequenza init completa consegnata al DDIC (34/34), commit ripetuti + dirtyfb full-frame, TE che arrivano (vsync cresce). Non bastano. Candidati rimasti, in ordine:
1. **Kickoff/frame path**: fps=0 con vsync crescente = TE al SDE ma nessun kickoff — leggere `sde_encoder_phys_cmd.c` (percorso/soglie del kickoff) e provare `esd_trigger` (recovery completa in-kernel, MAI usato): è la mossa più vicina a "fai fare al kernel un init pulito da zero".
2. **Autorefresh**: connector prop id 49, default **0** (max 6 frame); l'ABL usa `DSIEnableAutoRefresh=True` (div 1). Non settabile col legacy SETCRTC → serve un commit ATOMIC coi props vendor (`dsc_mode` id 66, `frame_trigger_mode` id 67 — li setta SurfaceFlinger).
3. **Config DSC host-vs-DDIC**: kernel e ABL concordano sulla geometria (slice 540x20, DSC 1.1) ma le **sequenze di registri differiscono** (es. `d8`: ABL `…5b 00 5b…` vs kernel `…3a 00 3a…`; `cf`: ABL `…04 04 04 04 04 05` vs kernel `…02 02 02 02 02 03`). Se il DDIC resta configurato dall'ABL e l'host manda la config del kernel → decode DSC sbagliato. Test: mandare via tx_cmd la sequenza ABL (quella che DECODIFICA, logo docet).
4. Sequenza PRECOCE al boot (~2 min, DDIC "fresco").

**Le chiavi originali** (per il driver DRM vendor): connettore DSI = **type=16** (id 56) NON type 15 (=Virtual, id 33); modi esatti dal kernel: `1080x2340x60/90/120cmd` (ht=1216, vt=2370); struct UAPI `drm_mode_modeinfo` = clock PRIMA, name alla FINE; GETCONNECTOR richiede TUTTI gli array (props_ptr=NULL → EFAULT); SETCRTC ok su crtc 153/224 + fb.
- Quirk kernel vendor: gli array di GETRESOURCES non sono attendibili (ID brute-forzati 0..255); GETCONNECTOR completa solo passando **TUTTI** gli array (modes+props+prop_values+encoders: con `props_ptr=NULL` → **EFAULT** — ecco perché i modes "non si leggevano"). I tool self-contained devono dichiarare le struct UAPI ESATTE: in `drm_mode_get_connector` i 4 pointer sono a 64 bit e i count nell'ordine `count_modes, count_props, count_encoders` (+`pad` finale); `drm_mode_crtc.set_connectors_ptr` è u64. Layout sbagliato = errori ingannevoli (`getconnector: No such file or directory` su un conn che esiste, array GETRESOURCES a zero).
- **COMANDO OK**: SETCRTC su crtc 224 (o qualsiasi GETCRTC-abile: 153/215/224/233/242) con conn **56** + fb (dumb XRGB8888) + modo esatto → `card0-DSI-1/enabled = enabled`, CRTC valid=1 fb=64 — **NON implica pixel visibili (vedi firma diagnostica sopra)**. Tool: `nx679j-drmtest` (+ `nx679j-drm-enum` read-only) in `/usr/lib/nx679j/modem/`; sorgenti in `experiments/20260920-wifi-luci/`.
- crtc 153 può avere il 60cmd "valid" dal bootloader con fb=0 (schermo nero); settare con fb su 224 = via pulita. Backlight: `/sys/class/backlight/panel0-backlight` (max 8191) — alzarlo NON sblocca i pixel (provato a 8191: stesso risultato), quindi non è quello il problema; il driver la RISCRIVE da solo a ogni prepare/unprepare (`dsi_panel_set_backlight ... lvl:` — osservati 0 e 4095), quindi un valore scritto a mano non sopravvive al prossimo enable.
- **GOTCHA mastership (EACCES=13 su TUTTI i crtc = NON sei DRM master)**: un secondo processo DRM mentre il primo è vivo non è master e ogni SETCRTC/disabilita dà EACCES. Regole operative: (a) kill del tool precedente SEMPRE come passo separato, ~1-2s di attesa, POI lanciare il nuovo — kill+start nello stesso script = race e il nuovo resta non-master; (b) kill -9 può lasciare processi ZOMBIE (stato Z; l'init statico PID1 non fa wait) = innocui ma `pidof` li conta: non usare il conteggio come "istanze vive"; (c) `scp` su un binario in esecuzione = "Text file busy" → killare prima di pushare; (d) busybox del device: `sleep` accetta SOLO interi (`sleep 0.15` → `invalid number`) — nei loop di comandi usare `sleep 1`; (e) il journal-mirror del blackbox inonda il ring dmesg (256K, rotazione) e i grep su dmesg perdono righe: per la cronologia usare il file journal persistente (`/proc/1/root/nx679j-journal`) e/o confrontare i timestamp monotoni `[sec.micro]`; fermare il mirror durante i test riduce il rumore.
- **TOUCH+display uniti**: `nx679j-touchpaint` = setta il modo, poi legge `/dev/input/event0` (eventi ABS_MT 53/54, BTN_TOUCH 330) e disegna blob+linee nel dumb buffer + DIRTYFB (ioctl 0xB1) — la parte touch è viva e verificata; i frame del disegno a schermo sono stati confermati col self-test `nx679j-touchsim` (sezione vittoria), non dal touchpaint — che resta instabile: usarlo DA SOLO, senza altri client DRM.
- **Modalità B — display nei boot-window [RISOLTO: era ESD recovery + conflitti di client DRM, non il timing]** (storico, misurato): il mode-set/touchpaint nei primi ~750s di uptime **resettava il device** (crash immediato, nessun log kernel); ad uptime alto (storico: ~2000s+) il device reggeva. Teorie FALSIFICATE con esperimenti diretti (non riprovarle): moduli touch caricati (crash anche senza), storm hostapd/wifi (crash anche con `wifi down` e storm ferma: contatore `start_bss` stabile), servizi rcS (crash col solo touchpaint). Metodo per trovare la soglia: watcher armato su `uptime > N` che logga l'esito sul rawdump (slot 409) — scrivere SEMPRE il marker prima del fire, perché un crash immediato non lascia la riga postuma.
- ⇒ **NON mettere il mode-set nel boot** (v77 con S94 nel default cicla anche staggered); display on-demand; al boot restano solo i moduli touch quando richiesti (S94 fuori dal default, v. nx679j-project-state.md).
- Prossimo (ordine): 1) ribakare l'immagine (v81) col launcher aggiornato (ESD kill + drmtest + descrittore LuCI) e validare il boot autonomo; 2) test del touch FISICO col touchmon e paint live; 3) userspace KMS (kmscon/weston) per console+touch. Test con l'utente: un solo look per messaggio, dire per quanto resta a schermo (hold del tool); colore PIENO diagnostica meglio delle bande.
- Confronto stock: il boot stock logga `[drm:dsi_display_set_mode] ... hactive=1080 vactive=2400 fps=90` — è IL riferimento; nel nostro boot quel log mancava finché non si è fatto il disable-first dei crtc (ecco perché i frame non arrivavano).

## Mainline (per il futuro)
- SM8450 display DSI = mainline da 6.3 (DP 6.4, GPU 6.8) — https://linux-msm.github.io/mainline-status/soc/sm8450
- pmOS OnePlus 10 Pro (SM8450): Screen+Touch WORK su mainline = modello di riferimento.
- Bootloader: Project Mu (sm8450-mainline/Mu-Qcom) già usato dal progetto.

## COMMIT ATOMIC SU DRM VENDOR — la formula che funziona (22/09 tarda notte)

### La formula verificata (NX679J, kernel vendor 5.10.66)
1. **`SET_CLIENT_CAP(DRM_CLIENT_CAP_ATOMIC=3)`** — ioctl `0x4010640d`. **SENZA: OGNI commit atomic → EINVAL** (check in `drm_atomic_uapi.c` ~1306: `if (!file_priv->atomic) return -EINVAL;`). Questa era la causa di TUTTI i "vicoli ciechi atomic" precedenti (compreso il vecchio `nx679j-atomic`).
2. ESD kill PRIMA, come sempre.
3. **GUARD v2 con DROP_MASTER**: un fd di guardia su `/dev/dri/card0` che **cede il master** (`DRM_IOCTL_DROP_MASTER`) — evita `lastclose` senza rubare il master (la v1 che lo teneva → EACCES su tutti gli ioctl del client principale).
4. **Commit completo c-n-p** (ordine): crtc (MODE_ID blob del modo esatto + ACTIVE=1); connector (CRTC_ID + `autorefresh=0`); **plane 103** (CRTC_ID + FB_ID + SRC_{W,H,X,Y} + CRTC_{W,H,X,Y}; SRC in 16.16!). Flags: `ALLOW_MODESET` (0x400); `TEST_ONLY` (0x100) per validare prima.
5. Esito: commit accettato e **pannello AGGIORNA** (ROSSO→VERDE via webcam, 3×). Tool: `nx679j-atom10`/`atom11` (in `experiments/20260920-wifi-luci/`).

### Flip loop plane-only verificato; rendering kiosk non risolto
Il test di commit prova la sequenza e la cadenza, non la correttezza dei pixel del renderer né la stabilità dopo reset.
Il vecchio "limite ~3 commit" NON era un limite del driver: era l'**IDLE POWER COLLAPSE**.
1. **Commit #1**: completo c-n-p (MODE_ID+ACTIVE, conn CRTC_ID, plane FB_ID+rect).
2. **Frame successivi**: commit del **SOLO plane** (FB_ID + CRTC_ID + SRC/CRTC rect) — MAI crtc, MAI connector. Ricetta dell'HAL vendor (SDM `hw_device_drm.cpp` → `SetupAtomic`): `PLANE_SET_FB_ID+SET_CRTC` ogni frame; `CRTC_SET_MODE` solo al first_cycle/mode-switch; ACTIVE/connector solo al primo commit.
3. **Gap <~58ms:** nei test la cadenza plane-only sotto `IDLE_POWERCOLLAPSE_DURATION` ha retto; oltre l'idle sono stati registrati EBUSY/clk_set_rate/kickoff fail in un run. Non è una legge universale: il reboot del tentativo recente non ha log e la causa resta ignota.
- Commit conn+plane senza crtc → EINVAL "active without enabled" (`drm_atomic.c:337`, il conn trascina il crtc). `PAGE_FLIP` legacy (0xB0) = **ENOTTY** (nessun `.page_flip`).
- **Close del master:** non attribuire ogni reset a `lastclose`. Le fonti Qualcomm comparabili mostrano `preclose` del client master e possibile cleanup commit; il global `lastclose` scatta solo quando si chiude l'ultimo fd. Il master live non si termina durante il pitch test; causa del precedente reboot ignota.
- `frame_trigger_mode=posted_start`: irrilevante (non era quello).
- **Kiosk2 (stato effettivo):** loop plane-only e lettura touch sono stati eseguiti; l'utente ha riferito linee diagonali arancio/verdi e fondo glitchato. Il renderer usava stride 4320 contro pitch XR24 4352. Pitch-fix testato host-side RED→GREEN, ma non verificato visivamente sul target. Il `prev` per-buffer è un fix distinto per le scie, non prova la correzione del pitch.
- **Un tool che "non parte/crasha" dopo un reboot su rootfs volatile = prima verifica che il binario ESISTA** (spesso è stato spazzato dal boot): `ls` la path esatta prima del lancio — un "not found" non lascia traccia nel log del processo e maschera il problema come crash.

### ⭐ LEZIONI GENERALI (riutilizzabili su qualunque device DRM)
- **`SET_CLIENT_CAP(ATOMIC)` è OBBLIGATORIA** prima di qualunque `drmModeAtomicCommit` da un client nuovo: se ogni commit atomic dà EINVAL, è il PRIMO sospetto — vale per mainline e vendor, ogni versione.
- **Gli ioctl DRM cambiano tra versioni del kernel**: su 5.10 `GETPROPERTY=0xAA` (NON 0xB9!), flag atomic = `0x100/0x200/0x400` (non 1/2/4), ATOMIC=0xBC, CREATE_BLOB=0xBD, OBJ_GETPROPS=0xB9, GETPLANERES=0xB5, PAGE_FLIP=0xB0, DIRTYFB=0xB1, SETPLANE=0xB7. **Estrarre la tabella dalla UAPI del kernel IN USO** (`include/uapi/drm/drm.h` di QUELLA versione) — mai dalla memoria o da un header di un'altra release.
- **I driver vendor possono non elencare le prop dei plane in `OBJ_GETPROPS`**: le prop esistono comunque → brute-force ID 1..400 leggendo i nomi con GETPROPERTY (NX679J: FB_ID=17, CRTC_ID=20, SRC_W=11, SRC_H=12, SRC_X=9, SRC_Y=10, CRTC_W=15, CRTC_H=16, CRTC_X=13, CRTC_Y=14, MODE_ID=23, ACTIVE=22, autorefresh=49, frame_trigger_mode=66).
- **Non chiudere il master live durante il handoff.** Il close può attivare preclose/cleanup; lastclose globale è distinto. Per provare un renderer nuovo, usare boot pulito e unico client, non kill del master né secondo consumer concorrente.
- **Il processo col master NON deve mai chiudere il fd**: `lastclose` → modeset di ripristino → potenziale reset su cmd-mode.
- **Un commit accettato ≠ un frame mostrato**: verificare SEMPRE con occhio/camera (cmd-mode self-refresh mostra l'ultimo frame; il controllo è visivo, mai il solo rc/state).
- **Su cmd-mode: disegnare/aggiornare il FB PRIMA del commit** — il primo frame pushato è quello che si vede.
- **Un crash periodico "dopo N operazioni" è quasi sempre un idle/timeout del driver, NON un limite di N**: domanda diagnostica = il crash avviene per CONTEGGIO o per TEMPO dall'ultimo commit? Cercare nel resource-control del driver il delayed-work/power-collapse e il suo timeout, e tenere il gap tra commit SOTTO quel timeout (qui: ~58ms).
- **La ricetta per i frame successivi al primo la detta l'HAL vendor** (qui SDM: primo commit completo, poi solo-plane FB_ID+CRTC): copiarla, non dedurla.
- **Prima di ogni test che può resettare: stream `/dev/kmsg` sul host** (BusyBox `dmesg -w` non c'è; `/dev/kmsg` è multi-reader, non distrugge il ring). Il rawdump è volatile e un marker di launcher può collidere con altri slot: verifica l'offset, non fidarti del solo nome dello slot.
- **Tool nuovi = copia 1:1 di un tool verificato**, cambiando solo il necessario: la riscrittura del setup da zero reintroduce bug silenziosi (ID hardcoded sbagliati, prop risolte male) che bruciano run.
- **Privilegi/sandbox**: i commit che cambiano modalità richiedono `ALLOW_MODESET` e (vendor) il master; EACCES diffuso = qualcuno detiene il master, non un problema di permessi.

### Verifica visiva autonoma (il metodo che ha chiuso il caso)
Webcam sul telefono + OBS websocket + `obs_shot.py` → PNG → `vision_analyze` che legge il pannello ("rosso pieno", "verde", "bande"). Ha confermato ROSSO→VERDE e distingue "cambio frame" da "logo statico" senza disturbare l'utente.

## Touch GT9897 — campagna 25/09: misurato ed escluso

### Il discriminatore: diffare l'init con lo stock
L'init del driver è **identica riga per riga** a quella dello stock Android funzionante, inclusi i due `[GTP-ERR]` che sembrano errori e sono **benigni**: `can't find valid panel` (il phandle `panel` manca in ENTRAMBI i DT; `active_panel` non condiziona IRQ/input/eventi) e `failed get panel-max-p, use default` (c'è un default). Quando l'init combacia con l'oracolo, il guasto è nel **runtime**: confrontare i log di runtime, non di init.

### Trappole del driver (misurate — non ripetere)
- **NON leggere** i nodi `fwupdate/result`, `get_rawdata`, `esd_info`, `nbsp_mode` del platform device `nubia_goodix_ts.0` (in `/sys/devices/platform/`, non sotto `/sys/bus/`): un read ha coinciso con un **crash del kernel** (SSH caduta a metà lettura, device ripartito da solo). Write-only, non interrogarli.
- `bsp_mode` è un canale di notifica pannello→driver (0=resume, 1=suspend), **non** un comando di recupero; è gated da `fp_switch`, e una scrittura a valore invariato risponde `Failed: rewrite`. Ciclo schermo o `bsp_mode=0` non risvegliano il touch: fanno chatter IRQ, non eventi.
- **Contatore IRQ ≠ eventi**: sale da solo (ESD/polling) e coi cicli schermo. Provare sul canale vero: `dd if=/dev/input/eventN bs=24 count=400` per una finestra breve mentre l'utente tocca; 0 byte = nulla consegnato. Individuare il device per NOME (`/sys/class/input/eventN/device/name`), mai per numero.

### Firmware del touch: dove e quando
I `goodix_*.bin` vanno in `/lib/firmware` del **root vero** — la vista del kernel: `/proc/1/root/lib/firmware` — NON della chroot, e nell'immagine vanno installati **DOPO i tar di persistenza** (che li cancellano: stessa regola delle altre iniezioni). Verifiche: `ls /proc/1/root/lib/firmware/goodix*` sul device, e l'md5 del binario estratto **dal ramdisk dell'immagine** (non dal file su disco) prima del flash. `rom_pid:BERLIN` è normale: il discriminante è `pid:` (firmware applicativo in esecuzione) presente/assente.

### Stato misurato (cosa resta aperto)
- pannello r6130 bound a ~19 s, ~55 s prima del probe del touch → l'ordine di caricamento NON è la causa;
- pannello acceso da subito + driver caricato → **0 byte su `event2`** mentre l'utente tocca: il controller non consegna tocchi; `get_rawdata` risponde `[FAIL]-0F-software reason`;
- esclusi con evidenza: panel-event-notifier, ordine moduli, fingerprint (driver non caricato, `fp_switch` già 0), variante firmware `goodix_firmware1.bin` (bind-mount su Android, test inconcludente perché il touch era sospeso);
- piste aperte (nessuna è una ricetta verificata): differenze di alimentazione/reset del chip rispetto allo stock; confronto completo dei log di runtime stock-vs-nostro; recupero a livello più basso.

### UI e pannello (port OpenWrt)
- `/sys/class/drm/card0-DSI-1/dpms` è **read-only dalla shell** (`Permission denied`): blank/unblank vanno dal master DRM (la UI).
- Un commit della UI su un CRTC spento lascia la UI in **stato D** (`kill -9` inutile; `pidof` la vede ancora): si recupera **solo col riavvio**. Un processo di test residuo può spegnere il pannello e lasciarlo nero — il sintomo "premo e non succede nulla" può essere quello, non il touch: controllare `enabled`/`dpms`, i frame della telemetria e la webcam prima di accusare il touch.
- La UI scansiona i device input **solo all'avvio** e rimuove quelli spariti: non aggiunge quelli comparsi dopo (device uinput creati a UI attiva non vengono aperti; serve riavviare la UI).

### Webcam: cattura senza OBS (via diretta)
`ffmpeg -f v4l2 -input_format mjpeg -video_size 1920x1080 -i /dev/video0 -frames:v 8 -y /tmp/g-%02d.jpg`, poi tenere il file **più grande** (il primo frame è spesso verde/corrotto; il 4K MJPG è corrotto su questa camera) e leggerlo con `vision_analyze`. Se il telefono è in controluce (sagoma nera), ritagliare e schiarire prima di giudicare. La webcam è la verifica **primaria** di tutto ciò che è visibile: prima di dire cosa mostra lo schermo, guardarlo.
