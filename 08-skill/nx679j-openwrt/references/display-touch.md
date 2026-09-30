# Display e touch NX679J

## Il pannello
Visionox **VTDR6130** (`qcom,mdss_dsi_r6130_1080_2400_amoled_cmd`), 1080×2400, **TE-based command-mode**. Connettore `card0-DSI-1`. Driver mainline analogo: `panel-visionox-vtdr6130.c`. DDIC reg 0x0A: 0=deep sleep atteso 0x1c.

## LA FORMULA VERIFICATA (commit atomic che aggiorna il pannello)

1. **ESD kill OBBLIGATORIO** (si perde ad ogni reboot):
```sh
mount -t debugfs none /sys/kernel/debug
echo esd_sw_sim_success > /sys/kernel/debug/qcom,mdss_dsi_r6130_1080_2400_amoled_cmd/esd_check_mode
```
**Osservazione di questo build nei test precedenti:** un mismatch ESD 0x0A è stato seguito da recovery/reset; non è una legge per ogni commit. `esd_sw_sim_success` salta il controllo di stato ESD, ma non disabilita altri errori DSI/clock o ogni possibile reset.

**Readback ambiguo (23/09, confronto sorgenti pubblici):** `nx679j-display-late.sh` usa `echo esd_sw_sim_success` con newline; il log dello stesso boot riporta `ESD KILL ok`. Questo prova il ritorno positivo del write, non da solo il cambio di stato. Nel peer pubblico NX679S `0280bdce...` (Makefile 5.10.101, pur includendo config NX679J) e nel Qualcomm 5.10 comparatore il file ha `.read` e `.write`: lo store riconosce esattamente `esd_sw_sim_success\\n` ma può comunque restituire `len` per token ignoti; la read torna EOF se `*ppos != 0`, altrimenti fa `snprintf(buf, len, ...)`, copia `len` byte NUL-padded e ritorna `len`. Una lettura breve può apparire vuota: spiegazione plausibile, non verificata sul BusyBox/build live. Il sorgente esatto NX679J 5.10.66 non è disponibile; considera il readback **stato ignoto**, non bypass OFF. Non riscrivere il controllo solo per un readback vuoto. Questi callback controllano il risultato software del polling ESD; non provano la salute del pannello e non spiegano il nuovo errore clock RCG/DSI.

2. **Guard v2 CON DROP_MASTER** (fd di guardia su `/dev/dri/card0` per non far scattare lastclose; v1 teneva il master → EACCES nei commit!).

3. **`SET_CLIENT_CAP(DRM_CLIENT_CAP_ATOMIC=3)`** — ioctl `0x4010640d` — **OBBLIGATORIA** (senza: `drm_atomic_uapi.c:1306` → EINVAL su ogni commit).

4. **Commit atomic COMPLETO, ordine c-n-p:**
   - **crtc 152**: MODE_ID (blob del mode `1080x2400x90cmd`) + ACTIVE=1
   - **connector 56**: CRTC_ID=152 + `autorefresh=0`
   - **plane 103** (NON 78!): CRTC_ID=152 + FB_ID + SRC_W/H/X/Y + CRTC_W/H/X/Y (SRC in 16.16)
   - flags: ALLOW_MODESET (0x400) | TEST_ONLY (0x100) per validare, poi 0x400

5. **Non terminare il master live per un handoff improvvisato.** Il close del file DRM master può eseguire `preclose`/cleanup per-file; una guardia aperta impedisce soltanto il `lastclose` globale, non quel percorso. Il reboot visto dopo close+relaunch non è attribuito: causa da provare.

## Ioctl corretti per questo kernel 5.10
GETPROPERTY=0xAA (NON 0xB9!), OBJ_GETPROPS=0xB9, GETPLANERES=0xB5, PAGE_FLIP=0xB0, DIRTYFB=0xB1, SETPLANE=0xB7, ATOMIC=0xBC, CREATE_BLOB=0xBD. Flag atomic: TEST_ONLY=0x100, NONBLOCK=0x200, ALLOW_MODESET=0x400.

**Prop del plane NON escono da OBJ_GETPROPS** (driver msm vendor) → ID globali via brute-force: FB_ID=17, CRTC_ID=20, SRC_W=11, SRC_H=12, SRC_X=9, SRC_Y=10, CRTC_W=15, CRTC_H=16, CRTC_X=13, CRTC_Y=14, MODE_ID=23, ACTIVE=22, autorefresh=49, frame_trigger_mode=66.

## Limiti noti (fisica)
- **Risultato storico, non comportamento universale:** dopo idle e modeset ripetuti un run ha registrato `dsi_clk_update_parent` EBUSY(-16) → `clk_set_rate`(-22) → kickoff failed → RESET HW. Il power-collapse DSI è plausibile (soglia sorgente 58 ms); l'ultimo reboot non ha log e non è attribuito a questo percorso.
- Commit "np" (senza crtc): EINVAL "active without enabled" (`drm_atomic.c:337`).
- `frame_trigger_mode=posted_start` NON ha migliorato.

## Tool (host, experiments/20260920-wifi-luci/)
`nx679j-atom8/9/10/11.c` (atomic, log su rawdump 431–433), `nx679j-guard.c` (v2 DROP_MASTER), `nx679j-dirty.c`, `nx679j-drmtest.c` (setcrtc), `nx679j-paneltest.c`, `nx679j-touchsim.c`, ecc. Compilazione: `aarch64-openwrt-linux-musl-gcc -O2 -static -o ... *.c -ldrm`. Binari in `/tmp/nxbin/`.

## Touch
- Touchscreen fisico: `nubia_goodix_ts` su `/dev/input/event0`.
- uinput per input sintetico: `mknod /dev/uinput c 10 223`; event node `eventN c 13 (64+N)`.
- `/dev/dri/card0 c 226 0` (creato da display-late.sh).

## Log di test (rawdump)
```
dd if=/proc/1/root/dev/rd bs=32768 skip=$((32+433)) count=1   # atom11
```
⚠️ Il rawdump viene PULITO al boot (>15MB) — leggere nello stesso boot.

## Sorgenti
`/home/user/re-nubia/NX679S` (git blob:none; `git show HEAD:<path>`) → `display-drivers.zip` → estratto in `re-nubia-disp/src/` (`sde/sde_crtc.c`, `sde_encoder_phys_cmd.c:1884`, `sde_connector.c`, `msm_atomic.c`, `dsi/*`). Il driver usa il percorso ATOMIC completo; `.dirty = drm_atomic_helper_dirtyfb`.

## FLIP LOOP VERIFICATO; KIOSK PITCH FIX ANCORA NON PROVATO SUL PANNELLO (23/09)

### IDLE POWER COLLAPSE (~58ms): MECCANISMO OSSERVATO, CAUSALITÀ DA VERIFICARE
- Fatto da sorgente peer: `IDLE_POWERCOLLAPSE_DURATION = 66 - 16/2 = 58ms` (`sde_encoder.h:52`); il path command-mode può spegnere/riattivare risorse DSI.
- Fatto live: a monotonic 11757.225 `disp_cc_mdss_byte0_clk_src` ha `CMD_RCGR=1`, `CFG_RCGR=0x201`, parent richiesto `dsi0_phy_pll_out_byteclk`; il `CMD_UPDATE` non è stato ackato. Linux stable v5.10.66 `clk-rcg2.c:update_config()` polla 500×1µs e ritorna `-EBUSY` se il bit resta set. Seguono parent DSI `-16`, pixel/link `-22`, SDE kickoff `-22`.
- Fatto peer (Nubia `0280bdce`, 5.10.101, non il binario live): event1=`KICKOFF`, rc_state4=`IDLE`; il path è compatibile con idle-exit/reparent. Il peer ignora il risultato del PLL-toggle/reparent in un punto e continua al set-rate; il pixel `-22` può essere cascata o rate incompatibile, non deciso.
- Il ritorno 0 dell’ioctl non prova il kickoff: il custom MSM vendor commit può fare flush del worker ma non propagare gli errori interni del callback SDE. FB57 nello stato software + bande RGBW identiche non provano il latch fisico.
- Causa fisica dell’RCG ack failure ignota; il peer 5.10.101 e il mainline non sostituiscono il sorgente NX679J live 5.10.66. La patch analoga SM8750 su `CLK_OPS_PARENT_ENABLE` non è applicabile senza prova. Snapshot clock attuale più tardi: DSI0 byte PLL unprepared/unenabled e byte/pixel RCG a 19.2MHz; non rappresenta lo stato al kickoff.
- In passato commit rapidi <58ms hanno evitato l'idle path, ma ciò non dimostra che la sequenza sia una soluzione generale né autorizza loop sul pannello.
- Log verificati salvati sull'host: `nx679j-plane78-after-dmesg-20260923.txt` e `nx679j-plane78-scanout-logread-20260923.log`; rawdump/debugfs device sono volatili.

### Ricetta commit (da SDM hw_device_drm.cpp SetupAtomic)
1. `SET_CLIENT_CAP(ATOMIC)` + commit #1 **cnp completo** (crtc MODE_ID+ACTIVE, conn CRTC_ID, plane) → primo frame.
2. Commit successivi: **SOLO PLANE** (PLANE_SET_FB_ID + PLANE_SET_CRTC, niente crtc/conn) → flip puri, ripetibili.
   - NB: commit "senza crtc esplicito" ma CON conn → EINVAL "active without enabled" (il core importa il crtc e azzera enable).
   - PAGE_FLIP legacy NON supportato (ENOTTY). DirtyFB non provato.
3. Gap tra commit <58ms obbligatorio (usleep(16000) = 60fps OK).

### Setup obbligatorio per ogni run (dopo OGNI reboot)
```sh
mount -t debugfs none /sys/kernel/debug
echo esd_sw_sim_success > /sys/kernel/debug/qcom,mdss_dsi_r6130_1080_2400_amoled_cmd/esd_check_mode
mkdir -p /dev/dri /dev/input; mknod /dev/dri/card0 c 226 0; mknod /dev/input/event0 c 13 64
D=$(ls -d /usr/lib/*/modem); setsid "$D/nx679j-guard" &   # guard v2 DROP_MASTER, UNO solo
setsid "$D/nx679j-kiosk2" &                                # UNO solo!
```
**ISTANZA UNICA**: non avviare duplicati. **Non killare un DRM master live** per ripulire: il driver Qualcomm può fare un commit di cleanup nel `preclose`; il global `lastclose` è separato e dipende dall'ultimo fd. Il reset osservato dopo close+relaunch non prova quale percorso l'abbia causato. Verificare `ps` e `/sys/kernel/debug/dri/0/clients`; se esiste un master, preservarlo. Per testare il pitch fix usare un boot pulito e un solo client, con `/dev/kmsg` già streamato all'host.

### Kiosk (nx679j-kiosk2.c)
- `prev` per-buffer riduce le scie da redraw incrementale, ma NON corregge il glitch diagonale attualmente osservato.
- Touch: `/dev/input/event0` (ABS_MT_POSITION_X/Y); l'utente conferma che risponde. Non modificare il percorso touch mentre si corregge il rendering.
- **Causa glitch probabile, non ancora confermata dalla prova kiosk:** framebuffer `XR24 1080x2400 pitch=4352`, mentre il vecchio renderer usava `W*4=4320` byte/riga (padding 32 B = 8 pixel). Il mismatch produce row drift/shear compatibile con linee oblique.
- **Pitch fix host-side:** il source conserva `pitches[0..1]`; clear e `fill_rect()` usano pitch effettivo. `test-kiosk-stride.c`: RED prima, GREEN dopo. Binario statico OpenWrt AArch64/musl; SHA verificato host/device.
- **NON dichiarare kiosk funzionante:** la chiusura del vecchio processo/avvio del nuovo è seguita da SSH timeout, reboot e splash; non è provato se il nuovo binario sia arrivato a eseguire il commit. Rawdump perso; causa reboot e risultato visivo restano ignoti.
- Webcam: OBSBOT Tiny 2 Lite; se OBS WebSocket (4455) è giù, catturare via V4L2 diretto solo dopo aver verificato che nessun processo tiene `/dev/video0`. Controllare visivamente prima e dopo ogni prova.

## FASE 2 (23/09) — link DSI morto, frame congelato

- **FATTI (read-only, boot `746a3fd8...`, uptime ~15650s):** `dsi0_phy_pll_out_byteclk`/`_dsiclk` `enable_cnt=0`; `disp_cc_mdss_byte0_clk_src`/`pclk0_clk_src` 19.2 MHz (XO) con i branch ancora `enable_cnt=1`; `sde 4 dsi_ctrl` = 3 IRQ e invariato in 3 s; `dri/0/crtc152/fps=0.0`; release fence `commit_count=2 done_count=1`; `dsi-ctrl-0/state_info` `CTRL_ENGINE=ON COMMAND_ENGINE=ON BYTE_CLK=82049180 PIXEL_CLK=109398906`; `debug/rm_status` con `intf`/`ctl`/`pingpong` ancora allocati; `esd_check_mode: esd_sw_sim_success` (bypass ESD ATTIVO, il readback vuoto di prima era un artefatto di lettura).
- **Conseguenza:** FB57 non è mai arrivato al pannello; il pannello command-mode tiene il frame in GRAM ⇒ una foto identica non prova nulla. La verifica di un nuovo scanout richiede un pattern DIVERSO o in MOVIMENTO.
- **Mappa proprietà verificata (read-only, `nx679j-drmprops`):** vedi la sezione 6 di SKILL.md.
- **Leva nuova:** `idle_pc_state=2` sul CRTC disabilita l'idle power collapse = il percorso XO↔PLL che è fallito. Non ancora applicata.
- **Toolchain:** `aarch64-linux-gnu-gcc -static` (host) → gira sul device musl. L'SDK OpenWrt in /tmp non esiste più.

## Prossimi passi (aperti — nessun altro commit autorizzato)
1. Fan-out online (10 assi) e consulto GPT-6-Astra via `oneprovider` completati. Primo guasto confermato: RCG update timeout; causa fisica non stabilita. Un timestamped capture di log + `clk_summary` e CMD/CFG è solo proposta per un’eventuale ricorrenza spontanea; non riprodurre con un altro commit, e autorizzare/verificare l’accesso raw prima di leggerli. Nessuna nuova ioctl, rollback o patch autorizzati.
2. **Fatto:** il solo commit utente-autorizzato è stato eseguito una volta: plane78 FB64→FB57, XR24 pitch4352, flags=0; DRM atomic ha restituito 0. **Errore:** a monotonic 11757.225 `disp_cc_mdss_byte0_clk_src` non ha aggiornato RCG; DSI clock parent `-16`, link/pixel clock e SDE kickoff `-22`.
3. **Stato live read-only (uptime 14964.50s, stesso boot):** PID24653 resta master; CRTC152 active; plane78 FB57 pitch4352. `clk_summary`: byte PLL DSI0 `0/0` a 59.784MHz; RCG byte/pixel a 19.2MHz con conteggi riportati in `STATO-ATTUALE.md`. Snapshot successiva, non ricostruisce il kickoff. Webcam RGBW identiche prima/dopo; nuovo scanout, pitchfix, kiosk e LuCI non verificati.
4. Una nuova prova deve essere discriminante (frame visibilmente diverso o pitch-fix kiosk), richiede source review prima e nuova autorizzazione esplicita per ogni commit reale. Nessun flashing/modifica modem; non chiudere il DRM master.
5. Il helper pitch-fix resta presente su host/device e passa la fixture host; **non ancora eseguito sul pannello**. Dopo aver dimostrato scanout/kiosk pulito, verificare stabilità, touch fisico e LuCI.
6. Possibile collisione rawdump marker (seek 410 vs logger slot 378): indagarla prima di rendere persistente il lancio kiosk.

> **Lezioni generali DRM** (valide per QUALUNQUE device, non solo questo): in `android-boot-chain-recovery` → `references/nx679j-display-touch.md` → sezione "COMMIT ATOMIC SU DRM VENDOR" → "⭐ LEZIONI GENERALI" (SET_CLIENT_CAP(ATOMIC) obbligatoria, tabella ioctl per versione kernel, brute-force prop vendor, DROP_MASTER guard, lastclose, cmd-mode rules).
