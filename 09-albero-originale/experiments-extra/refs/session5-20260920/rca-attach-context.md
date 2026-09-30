# RCA: regressione attach lan_wifi v57 -> v58 + "Bug: PHY is undefined" (NX679J)

Contesto per il subagent. Tutti i FATTI sotto sono stati verificati dal vivo il 2026-09-20
(device Nubia RedMagic 7 NX679J, OpenWrt 25.12.5 su kernel vendor 5.10.66, chroot /owrt).

## Sintomo
- Immagine v57 (boot OK): al boot l'interfaccia netifd `lan_wifi` risultava up con
  IP 192.168.77.1 sull'interfaccia wifi `phy0-ap0` (la associacione avveniva via
  wireless: `wireless.default_radio0.network=lan_wifi`, e `network.lan_wifi.device=wlan0`
  (nome "storico" non piu' esistente, risolto dal bind wireless).
- Immagine v58 (stessa struttura): `ubus call network.interface.lan_wifi status` dava
  `"up": false, "available": false, "code": "NO_DEVICE", device: "wlan0"`.
  Il log mostrava: `Interface 'lan_wifi' is now up` + `has link connectivity` alle :52,
  poi alle :54 `Interface 'lan_wifi' is now down` + `is disabled` +
  `wifi-scripts: Tearing down phy0` + ri-`Starting`.
- FIX DETERMINISTICO applicato e VERIFICATO: `uci set network.lan_wifi.device='phy0-ap0'`
  (riferimento diretto al netdev) -> lan_wifi up:true, IP presente, e dnsmasq emette il
  dhcp-range. Con `device=wlan0` dnsmasq NON emetteva il dhcp-range (iface down => skip).

## Differenze accertate tra gli alberi v57 e v58 (diff -rq dei due rootfs estratti)
Cartelle locali: /home/user/nx679j-stock/experiments/20260920-wifi-luci/build-v57/check/owrt
               /home/user/nx679j-stock/experiments/20260920-wifi-luci/build-v58/check/owrt
Differenze funzionali:
1. etc/config/network: in v58 `network.modem.proto` da 'none' a 'nx679j' (+ ifname) - TESTATO:
   rimettere 'none' a runtime NON ha corretto l'attach.
2. etc/config/dhcp: aggiunte DNS (list server 151.5.216.30/130) - irrilevante per l'attach.
3. lib/netifd/proto/dhcp.sh: aggiornato da upgradde del pacchetto netifd r2 (diff minimo,
   opzione sendclientid) - irrilevante.
4. Nuovi file: /lib/netifd/proto/nx679j.sh, /etc/nx679j-wan-share.sh, iptables-legacy
   (Alpine musl: /usr/sbin/xtables-legacy-multi + /usr/lib/xtables/*), libc.musl-aarch64.so.1.
5. nt/etc/nx679j-wifi-services.sh: wrapper v58 = v57 + blocco wan-share (in coda).
6. **switch.sh (nel ramdisk): aggiunta `export PATH=/usr/sbin:/usr/bin:/sbin:/bin` in testa.**
7. Aggiornamento pacchetto netifd r1->r2 (binario IDENTICO, md5 4922f9f5...): escluso.

## Fatto chiave trovato in sessione (gia' risolto, per contesto)
netifd ereditava PATH=/ da procd (avviato dal nostro switch.sh senza PATH) => gli handler
script /lib/netifd/proto/*.sh (che chiamano il binario `jshn` via PATH) NON si registravano
MAI (nemmeno dhcp). Con PATH corretto: `ubus call network get_proto_handlers` elenca
dhcp, dhcpv6, ppp, pppoe, static, nx679j. Questo e' indipendente dall'attach.

## Ipotesi da verificare (obiettivo del subagent)
1. Perche' in v57 il bind wireless (network list -> iface) funzionava e in v58 no?
   Guardare il codice ucode in /usr/share/ucode/wifi/*.uc (copie negli alberi build-v57/check
   e build-v58/check) + la logica "Interface 'lan_wifi' is disabled" (netifd: interface_set
   disabled quando il device sparisce) e il teardown/rebuild osservato.
2. Condizione esatta di "wifi-scripts: Bug: PHY is undefined for device" (apparso durante
   i test col wifi in churn): cercare nei sorgenti ucode wifi la stringa e la condizione.
3. Verificare che il fix "device=phy0-ap0" non abbia side effects noti (es. al boot il
   device non esiste ancora -> netifd dovrebbe fare l'up automatico quando il netdev appare;
   confermare quale percorso di netifd (device event / hotplug) lo fa).

## Fonti locali
- Alberi: build-v57/check/owrt, build-v58/check/owrt (completi).
- Sorgente netifd (HEAD 2026-09, vicino alla 2026.02.26 usata): /tmp/netifd-src
  (proto-shell.c, handler.c, proto.c, ubus.c; examples/proto-ucode.uc).
- Wrapper: v57-wifi-services.sh, v58-wifi-services.sh; block: switch-v58-live.sh.
- Log di sessione: in questa sessione (session5-20260920).

## Output richiesto
Nota tecnica BREVE (max ~60 righe): causa/i piu' probabile/i con EVIDENZA (file:riga), il test
decisivo per confermare, e ogni altra cosa rilevante scoperta. NIENTE modifiche al device.
