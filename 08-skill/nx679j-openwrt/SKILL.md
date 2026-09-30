---
name: nx679j-openwrt
description: Use when working on the NX679J OpenWrt port (RedMagic 7).
---

# NX679J — OpenWrt nativo su Nubia RedMagic 7

Progetto multi-sessione (dal 16/09/2026): portare OpenWrt nativo su un NX679J (SM8450/Waipio), usare il modem X65 come uplink e aggiungere display/LuCI. La storia completa dal primo messaggio è in `references/storia-completa.md`. **Leggi questa skill prima di toccare il device.**

> ⚠️ Il percorso del progetto e i nomi sul device possono apparire REDATTI nei transcript («redacted»). Nei comandi reali funzionano: `/home/user/nx679j-stock` (host) e le glob `/usr/lib/*/modem` (device). Verifica prima di presupporre.

## Documentazione completa (25/09/2026)

`experiments/20260920-wifi-luci/DOCUMENTAZIONE.md` (~245 KB, 21 sezioni) e' la storia completa del progetto: Fase 0 (l'inizio di luglio, secure boot/AVB, EDL/QDL buggato, XBL/ABL/UEFI), Fasi 1-5 (boot chain, immagine, modem X65, UI, kernel/display), crediti (strumenti + fonti con URL), cronologia, scheda hardware, manuale operativo, archivio. Ogni sezione e' passata da un audit indipendente contro i sorgenti locali (~37 correzioni applicate). Leggerla PRIMA di qualunque lavoro sul device.

### Trappole aggiornate (25/09)

- **Mai spegnere il link DSI**: standby = frame nero + backlight 0 (v180), mai DPMS. **Mai commit su CRTC spento** (stato D: solo riavvio fisico).
- **I nodi tasti si cercano per NOME** (`strstr(nm,"pmic_pwrkey")`), non per eventN: l'ordine cambia a ogni boot.
- **`rst.sh` e' device-side**: eseguirlo SUL telefono (`nxssh.sh 'sh -s' < rst.sh`), non sull'host.
- **Volume up**: esiste una linea `spmi-gpio 5 Edge volume_up` (IRQ 332, `interrupts.txt:176`) mai esplorata — porta aperta.
- **Fan-out: max 3 agenti per ondata** e ognuno salva l'output su disco (i fan-out grossi fanno crashare la sessione e uccidono i figli).
- **Probe di liveness: SEMPRE `8.8.8.8`, MAI `1.1.1.1`** (cieco all'ICMP da questa rete: un probe su target filtrato = falso guasto → renew loop → firmware del modem incastrato — v181, 27/09). E "il riconnetti non fa nulla" = modem incastrato: controllare WDS/DMS + `qrtr_tx_wait` prima di tutto.

## Stato attuale (23/09 — v87; display ancora in diagnosi)

- **v87 flashata e VERIFICATA**: `LUCI=1 TOOL=2 UCI=2 MODEM=1` al boot. La persistenza risiede nel ramdisk `boot_b` via `persist-tars/*.tar` → build/flash; modem e riconnessione automatica sono già verificati (vedi references).
- **Display — stato verificato dopo il singolo commit autorizzato (23/09):** boot ID `746a3fd8-47e2-40cd-979a-6af24b49cb41`; PID24653 resta master su fd3 `/dev/dri/card0`; CRTC152 attivo 1080×2400×90cmd; plane78 ora punta a FB57 XR24 pitch4352 (FB64→57 plane-only, flags=0). DRM atomic ha restituito 0.
- **Limite visivo importante:** la webcam vede ancora le stesse barre RGBW uniformi; dato che vecchio e nuovo FB contengono lo stesso pattern, non è prova che i pixel del nuovo buffer siano arrivati al pannello, e non è kiosk/LuCI.
- **Errore post-commit, catena verificata:** a monotonic 11757.225 `disp_cc_mdss_byte0_clk_src` fallisce l’handshake RCG `CMD_UPDATE` (upstream stable v5.10.66: poll 500×1µs, poi `-EBUSY`); `CMD_RCGR=1`, `CFG_RCGR=0x201`, parent richiesto DSI0 PHY PLL byteclk. Il log continua con DSI parent `-16`, pixel/link `-22` e kickoff SDE fallito. Il trace `KICKOFF`/`rc_state=IDLE` nel peer Waipio 5.10.101 è compatibile con uscita da idle, non prova la causa hardware. Il commit DRM torna 0 ma FB software nuovo e RGBW identiche non provano scanout. Nessun fix SM8750 applicato per analogia; sorgente esatta live 5.10.66 non trovata.
- **FASE 2 (23/09, solo letture): il link DSI è MORTO, il pannello mostra un frame STALE.** `dsi0_phy_pll_out_byteclk`/`_dsiclk` `enable_cnt=0 prepare_cnt=0`; `disp_cc_mdss_byte0_clk_src`/`pclk0_clk_src` a 19.2 MHz (XO) con i branch `disp_cc_mdss_byte0_clk`/`pclk0_clk` ancora `enable_cnt=1`; IRQ `sde 4 dsi_ctrl` fermo a 3 e invariato in 3 s; `dri/0/crtc152/fps=0.0`; release fence `commit_count=2 done_count=1`; `dsi-ctrl-0/state_info` dice `CTRL_ENGINE=ON` con BYTE_CLK 82049180 (software convinto, hardware no). Il pannello command-mode tiene l'ultimo frame nella sua GRAM: le RGBW identiche prima/dopo NON erano un limite della webcam, era il pannello congelato. ⇒ Per provare un nuovo scanout serve un pattern DIVERSO o in MOVIMENTO (+ diff pixel fra due catture), mai un pattern uguale.
- **Ricerca e review:** fan-out online 10 assi completata; un consulto GPT-6-Astra via `oneprovider` è completato. Entrambi confermano il timeout di update RCG come primo guasto osservato, ma la causa fisica resta aperta; IDLE-exit plausibile, non provato. Non inviare altri commit/rollback, ioctl display o patch: il singolo commit autorizzato è consumato. Un eventuale snapshot CMD/CFG + `clk_summary` va considerato solo se il guasto riappare spontaneamente e dopo autorizzazione per l’accesso raw.
- **Kiosk/pitch fix:** renderer vecchio usava stride 4320 invece del pitch 4352; fix compilato e fixture host RED→GREEN, SHA host/device uguale, ma il fix kiosk non è stato provato sul pannello.
- **Reboot precedente:** causa ignota. I sorgenti Nubia pubblici comparabili non sono prova del kernel live 5.10.66.

## Percorsi chiave

| Cosa | Dove |
|---|---|
| Progetto host | `/home/user/nx679j-stock/experiments/20260920-wifi-luci/` |
| Fonte unica anti-loop | `STATO-ATTUALE.md` (LEGGERLA prima di ogni passo) |
| Report completo | `/home/user/nx679j-stock/experiments/NX679J-OPENWRT-REPORT.md` |
| Build corrente | `build-v87.py` → `boot_b-v87-final.img` |
| Persist tars | `persist-tars/{etc,luci,tools}.tar` |
| SSH device | `ssh -i ~/.ssh/nx679j_key root@10.0.0.1` (USB gadget, rtt 3ms) o 192.168.77.1 (WiFi) |
| Toolchain host | `aarch64-linux-gnu-gcc -static` (binari glibc statici girano sul rootfs musl: verificato). L'SDK OpenWrt in `/tmp/sdk/...` citato nelle sessioni vecchie **NON esiste più**: ricrearlo se serve, altrimenti usare il cross di sistema |
| Sorgenti kernel Nubia | `/home/user/re-nubia/NX679S` (git) + `re-nubia-disp/src/` |

## Procedure rapide

### 1. Verifica post-boot (sempre, un comando)
```sh
ssh -i ~/.ssh/nx679j_key root@10.0.0.1 'L=$(ls /www/luci-static/resources/protocol/modemmanager.js >/dev/null 2>&1 && echo 1 || echo 0); T=$(ls /usr/lib/*/modem/ | grep -cE "atom11|guard"); U=$(uci show network.modem | grep -cE "qcom-soc|force"); M=$(ubus call network.interface.modem status | grep -c "\"up\": true"); echo "LUCI=$L TOOL=$T UCI=$U MODEM=$M"'
# atteso: LUCI=1 TOOL=2 UCI=2 MODEM=1 (modem sale da solo a ~360s)
```

### 2. Aggiornare la persistenza (3 passi, ~1 min, tutto da SSH)
```sh
# a) sul device: modifica → tar → scp
D=$(ls -d /usr/lib/*/modem); cd /; tar cf /tmp/pw/etc.tar etc   # o i percorsi che servono
scp -O -i ~/.ssh/nx679j_key root@10.0.0.1:/tmp/pw/etc.tar persist-tars/etc.tar
# b) build
python3 build-v87.py   # (o vNN successiva; ~10s, con assert)
# c) flash (protocollo forte)
scp -O -i ~/.ssh/nx679j_key boot_b-vNN.img root@10.0.0.1:/tmp/ && ssh ... 'rm -f /dev/sde41; mknod /dev/sde41 b 259 25; dd if=/tmp/boot_b-vNN.img of=/dev/sde41 bs=4M oflag=direct conv=fsync; sync'
```

### 3. Flash — PROTOCOLLO FORTE (il sync da solo NON basta)
```sh
N=/dev/sde41; [ -b $N ] || { rm -f $N; mknod $N b 259 25; }
dd if=<img> of=$N bs=4M oflag=direct conv=fsync
sync -f $N 2>/dev/null; sync
dd if=$N bs=1M count=96 2>/dev/null | md5sum   # LETTURA FREDDA 1 = md5 atteso
sleep 3; dd if=$N bs=1M count=96 2>/dev/null | md5sum   # LETTURA FREDDA 2
# SOLO se entrambe coincidono: reboot -f
```

### 4. Recovery modem (se il boot auto non fa salire il modem)
```sh
# 0) diagnosi: cat /tmp/chain.log — se "STOP bootstrap"/"MSS=offline":
# 1) symlink tool → /tmp, poi catena a mano:
D=$(ls -d /usr/lib/*/modem); for f in "$D"/*; do b="${f##*/}"; [ -e "/tmp/$b" ] || ln -s "$f" "/tmp/$b"; done
setsid sh "$D/chain.sh" > /tmp/chain.out 2>&1 &
# 2) attendere "DONE" in /tmp/chain.log (poll dinamico). Il boot script prosegue da solo
#    (wds stop → inject → MM restart forte → ifup). Se serve: dopo READY rilanciare
#    'sh "$D/openwrt-dms-observed-check.sh"' e di nuovo chain.sh.
# 3) verificare: ubus call network.interface.modem status | grep up
```
Se il MSS non è registrato affatto (dmesg: `releasing 4080000.remoteproc-mss` a ~19s): **reboot pulito → il MSS torna**. NON fare rebind sysfs del MSS (crasha il device).

### 5. Reinstallare pacchetti (se mancano dopo un boot)
```sh
# 1) NTP: il device parte dal 1970! Impostare l'ora dall'host:
ssh root@10.0.0.1 "date -s '$(date '+%Y-%m-%d %H:%M:%S')"
# 2) feed HTTP (busybox wget non completa TLS):
sed -i 's|https://|http://|g' /etc/apk/repositories.d/distfeeds.list
# 3) apk update && apk add luci-proto-modemmanager; upgrade mirato dei luci-* alla stessa versione
```

### 6. Diagnosi display READ-ONLY: link vivo o pannello congelato?
```sh
ssh -i ~/.ssh/nx679j_key root@10.0.0.1 'grep -E "dsi0_phy_pll_out|disp_cc_mdss_byte0_clk|disp_cc_mdss_pclk0_clk|disp_cc_mdss_mdp_clk_src" /sys/kernel/debug/clk/clk_summary; grep -E "dsi_ctrl|dp_display_isr|disp_rsc" /proc/interrupts; head -c 80 /sys/kernel/debug/dri/0/crtc152/fps; cat /sys/kernel/debug/dri/0/crtc152/fence_status; head -c 300 /sys/kernel/debug/qcom,mdss_dsi_r6130_1080_2400_amoled_cmd/dsi-ctrl-0/state_info'
```
- **VIVO:** PLL `dsi0_phy_pll_out_byteclk/dsiclk` con `enable_cnt>0`, RCG byte/pixel a ~59,78/101 MHz, contatore IRQ `dsi_ctrl` che **cresce** fra due letture a 3 s, `fps > 0`, release fence `done_count == commit_count`.
- **MORTO/congelato:** PLL `enable_cnt=0`, RCG a 19,2 MHz (XO), contatore IRQ fisso, `fps=0.0`, `done_count < commit_count`. In quel caso la foto del pannello NON è evidenza di ciò che il driver ha scritto.
- Nodi debug read-only utili: `dri/0/debug/{rm_status,core_irq,recovery_reg,dsi0_ctrl_reg,dsi0_phy_reg,sde_reg,pm_suspend_clk_dump}`, `dri/0/{crtc152,plane78,connector56}/`, `dri/0/state`, `dri/0/framebuffer`. **NON scrivere** su `debug/dump`, `debug/enable`, `debug/panic`, `esd_trigger`.
- Proprietà DRM verificate sul device (23/09): CRTC152 `22 ACTIVE`, `23 MODE_ID`, `19 OUT_FENCE_PTR`, **`166 idle_pc_state` range[0,1,2]** (`2` = disabilita idle power collapse: `sde_crtc.c:4053` → `sde_encoder_control_idle_pc(false)`), `165 vm_request_state`, `154 output_fence`; connector56 `20 CRTC_ID`, `2 DPMS`, `49 autorefresh`, `66 frame_trigger_mode`; plane78 (con `type`=1, quindi **primario**) `17 FB_ID`, `20 CRTC_ID`, `9-12 SRC_*`, `13-16 CRTC_*`.
- Sonde host in `cache/scratch/`: `nx679j-drmprops.c` (enumera le proprietà) e `nx679j-plane78-scanout-once.c` (commit). Compilano con **`aarch64-linux-gnu-gcc -static`** e girano sul rootfs musl del device; usano `pidfd_getfd` sull'fd del master (PID 24653, fd 3) ⇒ **nessun kill, nessun takeover, master intatto**.
- `timeout` non esiste su busybox: usare il `timeout` dell'host attorno a `ssh`.

### 7. Flag atomic DRM: valori esatti e trappole (verificati sul device)
- `DRM_MODE_ATOMIC_TEST_ONLY = 0x100`, `NONBLOCK = 0x200`, `ALLOW_MODESET = 0x400`, `PAGE_FLIP_EVENT = 0x01`. **`0x02` e' `PAGE_FLIP_ASYNC` e il core atomico lo respinge con EINVAL**: se un TEST_ONLY torna EINVAL con 0x02, e' il flag sbagliato, non il driver.
- **Mai commit bloccanti** su questo pannello (flags senza NONBLOCK). Osservato: commit bloccante su pannello command-mode → ioctl appeso >120 s (`wr_ptr_irq wait failed, switch_te:0` + `kickoff timed out ctl 0 koff_cnt 2` + `reg-dump logging` + `xin halt:0x3fff0000`) → **il telefono si riavvia da solo** pochi secondi dopo. Usare `NONBLOCK` e verificare con fence/log.
- `/sys/fs/pstore` resta **vuoto** dopo quel reboot: li' non c'e' la causa. Il launcher display parte a **uptime >= 900 s** (`/tmp/display-late.log`), non a 3000.
- Il commit riesce solo a display **fresco e attivo**: dopo idle lungo (>58 ms, `IDLE_POWERCOLLAPSE_DURATION`) o fallisce con l'RCG update o si appende. Ricetta storicamente buona: init al boot (uptime ~900) + flip con gap < 58 ms.

- **Caduta display = device irraggiungibile:** una cascata di fault SMMU (`smmu_sde_unsec_cb`) lascia il kernel in uno stato in cui USB enumera ancora ma **non esegue comandi** (niente SSH utilizzabile, ping 100% perso, adb assente perche' il gadget espone solo funzioni di rete CDC, nessuna seriale). In quel caso **l'unico recupero e' il tasto power** (~15-20 s): non perdere tempo a riprovare `ssh reboot -f`. `ssh … reboot -f` resta il metodo normale quando il device e' vivo.

### 9. Velocità di boot e trappole di misura (23/09)
**► Per riprendere il lavoro sul boot leggi prima `/home/user/nx679j-stock/experiments/20260920-wifi-luci/RIPRESA.md`** (punto di ingresso unico: numeri, prossimo passo, procedura di flash, recovery, trappole).
- **Display: `DISPLAY_LATE_GAP` era 250 s** (riga ~64 di `nx679j-display-late.sh`): il launcher dormiva 250 s DOPO l'ESD check, prima di avviare kiosk3 → il client arrivava a ~400 s mentre la gate X sembrava dire 120. Ora 10 s → kiosk3 ~150 s. Verifica sempre con PID vivo + `fence done==commit`.
- **Modem: race del rproc MSS.** `4080000.remoteproc-mss` può mancare per centinaia di secondi; se la catena parte prima vede `MSS=` vuoto → `STOP bootstrap` → niente modem per tutto il boot. Il waiter (`nx679j-mm-watchdog.sh`) aspetta l'MSS, replica lo **staging symlink** `/usr/lib/nx679j/modem/* → /tmp/*` (senza, la catena anticipata non ha gli helper) e poi lancia la catena; la catena stessa ora aspetta l'MSS invece di arrendersi.
- **Il `log` non definito in mm-standard-boot è cosmetico:** lo script NON muore su command-not-found.
- **Condizione di prontezza = risposta QMI REALE** (`qmi-qrtr raw 2 0001002d000000 | grep msg_id=0x002d`). La presenza di `service=4096` in QRTR è un **falso positivo** (vera anche a modem non pronto).
- **Mai giudicare da un boot solo:** la fase ModemManager varia di ~90 s tra boot; servono 4-5 campioni prima di attribuire un effetto a una modifica.
- Timings raggiunti: catena DONE 65 s, modem+dati ~135 s, kiosk3 ~150 s (da 265/315/900).

### 8. REGOLE DISPLAY VINCOLANTI (imparate a caro prezzo)
- **MAI lanciare un client DRM (`kmscube`, `weston`, `cog`, `drmtest`, kiosk) quando nessun master e' attivo**: il launcher display parte a **uptime 900**, quindi prima di allora `open("/dev/dri/card0")` diventa *implicitamente* master e il client fa un modeset reale. Osservato: `sde_rm_topology_get_topology_def invalid topology`, `sde_fence_signal extra signal attempt`, **154 fault SMMU** su `smmu_sde_unsec_cb` e device **bloccato** (SSH muto su USB e WiFi) → serve power-cycle. Prima di ogni prova: `/sys/kernel/debug/dri/0/clients` + PID master; se il display non e' inizializzato, non provare nulla.
- **Non esiste fbdev su questo kernel**: niente `/dev/fb*`, il display e' solo DRM/KMS. Percio' **la shell/console non puo' apparire** sul pannello: serve un client userspace che disegni in un framebuffer DRM e committi. Il **testo non richiede GL** (weston pixman + weston-terminal, o font bitmap + PTY): e' il traguardo intermedio piu' economico. LuCI richiede `cog -P drm` (WPE) con EGL/GBM software.
- **Il client display deve committare di continuo** (mai >58 ms di idle) e la prima commit deve avvenire mentre lo splash e' attivo: e' l'unico modo di non esercitare il bug pre-fix del reparent RCG. **Eccezione verificata (v139+):** con la proprieta' CRTC `idle_pc_state=2` (`idle_pc_disable`, che il launcher attiva al boot via `/tmp/ui-idle-pc-disable`) il pannello **sopravvive alle pause** — ucciso il client e lasciato il pannello fermo 25 s, la nuova istanza ha ripreso a 61 fps con 0 fence in ritardo; senza la proprieta' lo stesso test lascia il pannello morto (200 timeout). Con essa si puo' **riavviare il client a caldo** (iterazione UI in ~15 s). Dettagli ed esperimento: `references/display-ui-touch.md`.

### 10. Pagina LuCI "Cellular Network" (luci-proto-modemmanager) — provenienza e inventario
- Inventario completo (file, ogni invocazione mmcli, ogni campo JSON reso, menu/ACL, righe esatte): **`/home/user/mm-openwrt-25.12-source/luci-cellular-page-inventory.md`** (sorgenti upstream del commit LuCI `0834d099439e4ba788b4c33c54616d8ee9df5c6e` accanto, prefisso `luci-`).
- I 3 file JS sul device sono **minificati**: per citare righe usare il sorgente upstream, non il file sul device. Verificare l'identita' con confronto a token (script `luci-mm-tokcmp.py`), non a hash.
- **Il pacchetto NON e' registrato in apk** (`/lib/apk/db/installed` non contiene `luci-proto-modemmanager`, solo `luci-proto-ipv6/ppp`): i file sono side-loaded. Conseguenze: `apk add/upgrade` non li gestisce (non li sovrascrive ma non li aggiorna), `apk del` non li rimuove, e il check `LUCI=1` della procedura 1 verifica solo la **presenza del file**, non l'installazione.
- La pagina legge via `fs.exec_direct` -> ubus `file` (pacchetto `rpcd-mod-file`), **non** via `/usr/libexec/rpcd/modemmanager` (che su questo device non esiste). ACL: `/usr/share/rpcd/acl.d/luci-proto-modemmanager.json` concede `file.exec` per esattamente 4 argv mmcli read-only (`-L -J`, `-m N -J`, `-i N -J`, `-m N --location-get -J`). Qualsiasi vista sostitutiva che lanci altri comandi deve estendere quell'ACL.
- L'indice del modem/SIM si ricava dalla coda del D-Bus path (`Modem/7` → `-m 7`), mai dalla posizione nell'array: `-m 0` fallisce.

## Regole dell'utente (vincolanti)

1. **PRIMA la ricerca online, POI il codice** (ripetuta infinite volte). Mai duplicare l'esistente. Se manca la conoscenza: fan-out di agenti (skill `block-research-fanout`), anche 10-20 simultanei.
2. **Subagenti illimitati** per ricerca/verifica; **verifica TU i numeri** che riportano.
3. **Mai aspettare passivamente** — check attivi (webcam per il display), polling dinamico 2-5s, MAI sleep lunghi.
4. **USB gadget preferito** (10.0.0.1, rtt 3ms); il WiFi risponde su **192.168.77.1**.
5. **Persistenza totale**: tutto ciò che funziona va reso persistente ("questo telefono ha tantissimo spazio").
6. **Standard OpenWrt**: usare i componenti stock (proto modemmanager, MM, libqmi); niente protocolli custom se ne esiste uno standard.
7. **Android slot A = oracolo** (riavviabile per vedere come il driver/HWC pilotano l'hw).
8. **Mai interrompere il lavoro**; max 1 azione utente per test; i reboot li faccio io.
9. **Leggere STATO-ATTUALE.md prima di ogni passo**; un tentativo si ripete SOLO con un dato nuovo.

Dettagli, verbatim e storia: `references/regole-utente.md`.

## Trappole note (imparate duramente)

- Il **rawdump viene PULITO al boot** (>15MB) — non è persistente.
- `/«redacted»/`, `/rfs`, rootfs OpenWrt, `/tmp`: tutti volatili; sopravvive solo il ramdisk.
- I path con caratteri speciali NON passano via SSH doppio: usare **glob** (`ls -d /usr/lib/*/modem`).
- `rc=$?` dopo una pipe mente: verificare l'effetto, non l'exit code.
- Il journal ruota (256KB): per verificare i fatti del boot, controllare il RISULTATO funzionale, non il journal.
- `stat -c`, `od`, `timeout`, `pkill`, `top`: NON esistono su busybox — usare `wc -c`, `strings`, `head -c`.
- scp: SEMPRE con `-O` (no sftp-server).
- Su SSH instabile: riprovare (`cd` con glob, comandi idempotenti).

## File di riferimento

- `references/storia-completa.md` — dal primo messaggio (16/09) a oggi: tutte le fasi con evidenze.
- `references/persistenza-v87.md` — il sistema di persistenza completo.
- `references/modem.md` — catena, MM standard, libqmi patch, recovery, riconnessione.
- `references/qrtr-qmicli-repeat-concurrency.md` — affidabilità di `qmicli -d qrtr://0` su uso **ripetuto/concorrente**: bug non corretti in libqmi 1.36/libqrtr-glib 1.2.2, timeout 1 s della lookup, cap kernel, perché `-p` non è la risposta. Leggerlo prima di parallelizzare polling QMI.
- `references/display-touch.md` — formula atomic, guard, limiti, touch, prossimi passi.
- `references/boot-flash-device.md` — boot chain, partizioni, protocollo flash, SSH/gadget.
- `references/regole-utente.md` — le richieste e regole verbatim dell'utente.
- `references/ubus-control-surface.md` — tutti gli oggetti/metodi ubus controllabili da una UI (39 oggetti/234 metodi, ACL web vs libubus nativo, comandi esatti per interfacce/wifi/modem/reboot/log).
- `references/display-ui-touch.md` — **UI sul display** (v135-v137): componenti, coordinate dei controlli, la trappola del tocco (risolvere il click a SYN_REPORT), il nodo /dev statico, come verificare con uinput-touch senza dita.
