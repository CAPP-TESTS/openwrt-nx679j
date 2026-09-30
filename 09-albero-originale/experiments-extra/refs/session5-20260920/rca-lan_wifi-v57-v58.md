# RCA attach lan_wifi v57 -> v58 (NX679J, OpenWrt 25.12.5) — sola analisi file locali

## Fatto chiave (nuovo)
Il rootfs v57 e v58 sono IDENTICI nella parte wifi: `usr/share/ucode/wifi/*.uc`,
`lib/netifd/wireless/mac80211.sh` (ucode), `lib/netifd/wireless.uc`, `wireless-device.uc`,
`main.uc`, `sbin/netifd` (md5 4922f9f5…), `etc/config/wireless` → md5 identici
(`build-v57/check/owrt` vs `build-v58/check/owrt`, `diff -rq` = 51 differenze, nessuna nella logica wifi).
Quindi la regressione NON e' nel codice wifi: e' nel PERCORSO che in v58 viene ESEGUITO
perche' l'ambiente di procd ora ha PATH corretto (`switch-v58-live.sh:2` `export PATH=…`;
in v57/`live-switch.sh` manca, procd/netifd ereditavano PATH=/ → tutti i comandi "nudi"
`ucode`/`uci`/`ubus` dentro gli script di boot/hotplug fallivano silenziosamente).

## Catena causale (con evidenza)
1. Bind wireless→iface (funziona in entrambe le versioni): il vif viene ATTACCATO all'iface
   netifd via `wireless-device.uc:74-81 handle_link()` → `netifd.interface_handle_link({name:net,
   ifname:dev, link_ext:true, up})` → C `interface.c:1284-1306` (`interface_set_device_config` +
   `interface_add_link(...,link_ext=true)`). Per questo `network.lan_wifi.device='wlan0'`
   (nome inesistente) poteva comunque andare up in v57: l'opzione `device` NON e' cio' che lega il vif.
2. Percorso "wifi config" attivo solo in v58: `/etc/init.d/boot:46-48`
   (`[ -f /etc/board.json ] && /sbin/wifi config`, /etc/board.json esiste: 187 B) e
   `/etc/hotplug.d/ieee80211/10-wifi-detect:3-5` (`/sbin/wifi config` + `ubus call network.wireless retry`)
   invocano `/sbin/wifi` → `wifi_config()` (`sbin/wifi:64-76`) che usa `ucode`/`uci` **nudi** (PATH!).
   In v57 (PATH=/) tutto questo era un no-op silenzioso; in v58 gira.
3. Churn della radio: la riscrittura/commit di uci wireless (`/lib/wifi/mac80211.uc:96-124`, sezioni
   `radioX`/`default_radioX` generate quando `radio_exists()` (righe 17-32,72) non riconosce la
   sezione esistente) e/o `/sbin/reload_config:4-16` (md5 dei config → `config.change` "wireless",
   riga 13) portano a `ubus call network reload` → netifd `netifd_reload()`→`config_init_all()`
   (`ubus.c:47,209`, `main.c:281-283`) → `netifd_ucode_config_load` → `wireless.config_init`
   (`wireless.uc:111`, `:95-99 cur_dev.update(dev)`) → `wireless-device.uc:378-393 update()` →
   `:441-448 check()` → setup/teardown handler = "wifi-scripts: Tearing down phy0" (prefisso
   `/usr/share/ucode/wifi/common.uc:32`, messaggio `mac80211.sh:330`) e ri-"Starting" (`mac80211.sh:169`).
4. Effetto su lan_wifi: durante il churn il netdev `phy0-ap0` sparisce → il main_dev dell'iface riceve
   DEV_EVENT_DOWN → `interface.c:507-512 interface_set_enabled(iface,false)` → log L_NOTICE
   "Interface 'lan_wifi' is disabled" (`interface.c:424-433`) → `interface_check_state()` con
   `IFS_UP && !iface->enabled` → IFS_TEARDOWN ("is now down", `interface.c:396-409`).
   Il re-setup wifi salta il vif se l'iface netifd risulta non abilitato
   (`wireless-device.uc:327-370 wdev_update_disabled_vifs` con `netifd.interface_get_enabled`,
   che espone `iface->autostart`, `netifd/ucode.c:258-263`): senza handle_link il device
   `wlan0` non esiste → `NO_DEVICE`, definitivo.
5. Timing: l'evento hotplug scatta quando compare il netdev dell'AP (subito dopo `wifi up`),
   coerente con up+carrier a :52 e teardown a :54. In v57 la stessa catena era inerte (PATH).

## Conferma del messaggio "Bug: PHY is undefined"
`lib/netifd/wireless/mac80211.sh:164-167` (setup: `find_phy(data.config,true)` → null →
log + `netifd.set_retry(false)`) e `:321-331` (teardown senza `data.data.phy`).
`find_phy` (`usr/share/ucode/wifi/utils.uc:106-112`) cerca per `config.path` → `config.macaddr` →
`config.phy` in `/sys/class/ieee80211`: il messaggio = sezione `wifi-device` con phy/path inesistente
(sezione generata/duplicata da `/lib/wifi/mac80211.uc` durante il churn) → prova indiretta
che il churn di config esiste davvero.

## Fix `device=phy0-ap0`: perche' funziona / side effects
Con `device=phy0-ap0` il nome configurato esiste come netdev: l'iface non dipende piu' solo dal link
esterno wireless; alla (ri)comparsa del netdev il claim per nome riporta `available`/`enabled`
(`interface.c:485-525` DEV_EVENT_ADD/UP → `interface_set_available(true)`/`interface_set_enabled(true)`).
Nessun side effect di codice: il netifd HEAD locale mostra la stessa logica. Rischio residuo solo
di timing al boot (il vif puo' nascere dopo), coperto dall'evento device.

## Test decisivo (una sola immagine, isola la variabile)
v59 = immagine **v57** con l'unica aggiunta di `export PATH=/usr/sbin:/usr/bin:/sbin:/bin` in testa
a switch.sh (config `modem.proto` resta 'none').
- Se lan_wifi va "up" e poi "disabled"/NO_DEVICE come in v58 → causa = PATH (script boot/hotplug ora eseguiti).
- Se resta up come v57 → PATH escluso; restano da testare `modem.proto=nx679j`/`ifname` e netifd r2.
Controprova senza rebuild (su v58, una attivazione): `chmod -x /etc/hotplug.d/ieee80211/10-wifi-detect`
e commentare la riga 48 di /etc/init.d/boot, poi boot + `wifi up`: se con `device=wlan0` l'attach
regge → trigger confermato. Da raccogliere per prova: `logread | grep -E "wifi config|Tearing down|disabled"`,
`uci show wireless` (compare un `radio1`/`default_radio1` generato?), mtime di /etc/config/wireless
e /etc/board.json (la copia nell'immagine NON ha sezione `wlan`).
