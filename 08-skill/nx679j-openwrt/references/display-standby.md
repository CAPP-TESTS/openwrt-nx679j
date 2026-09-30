# Standby del display (pannello Raydium R6130, DSI command-mode)

Ricerca completata (fonti: sorgenti vendor NX679J in `~/nx679j-stock/kernel-patch-test/stock-kernel-source/`,
siêu DRM v5.10, wiki postmarketOS, SDE/DSI techpack).

## Identita' del pannello (accertata)

Raydium **R6130** (driver `vtdr6130`), 1080x2400 AMOLED, **command mode**,
4 lane DPHY + DSC, TE su tlmm 82, reset su tlmm 24.
DT: `arch/arm64/boot/dts/vendor/qcom/display/dsi-panel-r6130-nubia-dsc-fhd-cmd-dphy.dtsi`.
**Nessun backlight hardware**: luminosita' via DCS `0x51` (`bl_ctrl_dcs`),
esposta in `/sys/class/backlight`.

- off-command: `DCS 0x28` (display off) poi `0x10` (sleep-in) con **100 ms** di attesa
- on-command: sequenza lunga con `0x11` (sleep-out, 120 ms) e `0x29` (display on);
  reset `<1 10><0 10><1 10>`; latenza di risveglio tipica **150-250 ms**

## La decisione: DPMS off, NON frame nero

Tre modi confrontati. Solo il primo spegne qualcosa:

- **CRTC disable / DPMS off** → catena `sde_encoder_phys_cmd_disable` ->
  `dsi_display_disable` -> `dsi_panel_disable` (0x28) -> `dsi_display_unprepare`
  -> `dsi_panel_unprepare` (0x10) -> rail off. **L'unico vero standby.**
- **frame nero a 45 fps**: pannello, link DSI, PHY/PLL e clock DPU tutti accesi ✗
- **luminosita' 0**: idem, e il pannello va comunque tenuto vivo dai commit ✗

Il collasso a 58 ms (`IDLE_POWERCOLLAPSE_DURATION`) **non si applica** a pannello
spento: il timer viene cancellato in PRE_STOP/STOP. Idle-collapse e DPMS off
convergono sullo stesso stato hardware finale.

## Regole operative (dalle fonti, non dedotte)

1. **Fermare il loop di commit PRIMA dello spegnimento** e attendere l'ultimo
   fence. Su CRTC inattivo: un pageflip legacy da' `-EINVAL`, e nel vendor tree
   un commit atomico e' un **no-op silenzioso** -> il commit non completa mai ->
   `kickoff timed out` / `wait_for_idle -110` / watchdog.
2. **`drmModeSetCrtc(fd, crtc, 0, 0, 0, NULL, 0)`** e' la via piu' pulita per
   spegnere (equivalente a DPMS off, ma non tocca lo stato `dpms`).
3. **Al risveglio serve un MODESET completo** (mode + connector + fb), mai un
   pageflip.
4. **Rimettere `idle_pc_state=idle_pc_disable` subito dopo il wake**: durante il
   disable il driver forza `sde_encoder_control_idle_pc(encoder, true)`
   (sde_crtc.c), quindi senza questo il pannello **collassa dopo 58 ms**.
5. **Scartare i tocchi al risveglio**: dopo il wake, drenare la coda evdev
   (~200 ms) prima di considerare i tocchi validi.
6. Il touch **non richiede wakeup del kernel**: qui il SoC resta acceso
   (spento e' solo il display), quindi e' la UI a riaccendere al primo evento.

## Verifica del risparmio (obbligatoria, altrimenti e' fede)

In standby: `regulator_summary` deve mostrare i rail DSI/pannello off,
`clk_summary` (disp_cc) senza byte/pixel/link clock, e al wake **nessuna**
occorrenza di `kickoff timed out` / `-110` in dmesg.

## Rischio e recupero

Il percorso critico e' il **wake** (reparent RCG/PLL): e' la stessa famiglia di
problemi di clock DSI gia' vista sulla tree vendor. Se il pannello non torna,
il device **non** e' perso: SSH su 10.0.0.1 (o 192.168.77.1) funziona
indipendentemente dal display, e `reboot -f` ripristina tutto. **Dirlo
all'utente prima di flashare.**
