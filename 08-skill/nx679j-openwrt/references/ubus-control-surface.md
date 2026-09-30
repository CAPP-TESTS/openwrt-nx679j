# Superficie di controllo ubus (NX679J, OpenWrt 25.12.5) — inventario verificato

Artefatti completi (host):
- `/home/user/nx679j-ubus-control-surface.md` — report per aree (interfacce, wifi, modem, system/reboot, log, uci, status) con chiamate esatte.
- `/home/user/nx679j-ubus-control-surface.json` — 39 oggetti / 234 metodi dal dump live + 164 voci curate (args, provider, ACL, note).
- Dump grezzo: `~/.hermes/profiles/kernel-re/cache/scratch/nx679j-ubus-verbose-list.txt`.

## Fatti chiave (tutti verificati sul device)

- 39 oggetti ubus. Provider: netifd (`network`, `network.device`, `network.interface[.<iface>]`, `network.wireless`),
  rpcd core (`session`, `uci`, `rc`), rpcd-mod-{file,iwinfo,rpcsys,rrdns,luci,ucode}, procd (`service`, `system`,
  `container`, `hotplug.*`), logd (`log`), dnsmasq, hostapd/wpa_supplicant, plugin nostro `luci.<board>-modem`.
- **UI nativa (C/libubus) su `/var/run/ubus/ubus.sock`: nessuna ACL** per processi root → tutti i 234 metodi.
  **UI web** (uhttpd `-u /ubus` + `uhttpd_ubus.so`): ACL rpcd. `list read '*'` / `list write '*'` in `/etc/config/rpcd`
  NON significa "tutti gli oggetti": `fnmatch` avviene sui **nomi degli access-group** (rpcd `src/session.c`), quindi
  vale l'**unione dei 34 ruoli** di `/usr/share/rpcd/acl.d/*.json`.
- Conseguenza: via HTTP **non** sono concessi `network.interface up/down/renew/status`, `network.wireless up/down/reconf`,
  `system.watchdog|signal|sysupgrade`, `service.set_data`, `log.write`, `uci.rollback|reload_config`, `container.*`,
  `wpa_supplicant.*`. LuCI li aggira con `file exec` (`/sbin/ifup`, `/sbin/ifdown`, `/sbin/wifi`, `/sbin/reboot`,
  `/bin/kill`, `/usr/libexec/syslog-wrapper`, `/bin/dmesg -r`, `/usr/libexec/package-manager-call ...`), oppure si
  aggiunge un file ACL con i metodi voluti (`network.interface: ["up","down"]`, ecc.).
- Up/down interfaccia: `ubus call network.interface up '{"interface":"lan_wifi"}'` oppure oggetto per-iface
  `network.interface.lan_wifi up` (netifd `netifd_handle_iface` ridispatcha per nome). `/sbin/ifup -a` = `network reload`.
- WiFi: `network.wireless up|down|reconf|retry|status {"device":"radio0"}`; `/sbin/wifi reconf` = `network reload` +
  `network.wireless reconf`. Kick client via ubus diretto: `hostapd.phy0-ap0 del_client`.
- Log: `log.read` restituisce un **fd di pipe** (`ubus_request_set_fd` in ubox `log/logd.c`) → `ubus call log read`
  stampa nulla (rc=0): serve il fd, oppure `file exec /usr/libexec/syslog-wrapper` (path canonico LuCI, verificato).
- Config: `uci apply '{"rollback":true,"timeout":30}'` + `uci confirm` = apply sicuro con auto-rollback (`src/uci.c`).
- Modem: MM è **disabilitato a mano** (`/usr/sbin/{ModemManager,ModemManager-wrapper,ModemManager-monitor,mmcli}.disabled`),
  quindi l'ACL `luci-proto-modemmanager` (`/usr/bin/mmcli ...`) è morta e `file exec mmcli` → Not found. Il controllo reale
  è il plugin `luci.<board>-modem` (`getStatus|getImsi|doReconnect|doPin`, qmicli su `qrtr://0`) + init `rc init`.
  `doReconnect` non agisce direttamente: scrive `/tmp/<board>-link-renew.request` (pattern utile per nuovi comandi).
