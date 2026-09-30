# SESSION 5 REPORT — NX679J: WiFi+LuCI+Modem+WAN-share, persistente e deterministico

**Data:** 2026-09-20 (sessione 5)
**Device:** Nubia RedMagic 7 NX679J (SM8450/waipio), kernel vendor 5.10.66, OpenWrt 25.12.5 chroot su slot B
**Immagine finale di questa sessione:** `boot_b-v60-wifi.img` sha256 `8dae3d0a14ed17546044170673bcc1e2b1e0c89a2bb97a23ea6c2b550dd5735d`, md5 `e9bffd9592db39cf1e9099c86a1ac225`
**Boot validato a freddo:** `445453be-9160-4d3e-ad37-204f26224a3a`

## Risultato utente
- AP WiFi `NX679J-TEST` su `phy0-ap0` con `192.168.77.1`, automatico al boot.
- DHCP dnsmasq funzionante: **PC (user-Mini) 192.168.77.168** e **S26 Ultra di Gianmarco 192.168.77.108** (lease reali).
- LuCI su `http://10.0.0.1/` e `http://192.168.77.1/` — login **root / nx679j** (persistente).
- **WAN share**: NAT (iptables-legacy) + ip_forward attivi al boot; client LAN → Internet via SIM
  verificato end-to-end (ping 8.8.8.8 4/4, DNS, HTTPS 200) quando la catena modem è su.
- Interfaccia `modem` in LuCI con **proto custom `nx679j`**: mostra IP/mask/gateway reali del bearer
  e **auto-ripara** IP+route dal `/tmp/wds-session.log` quando netifd flusha (es. dopo ifdown).

## FATTI CHIAVE (con evidenza)

### 1. "Il flash che svanisce" (v55 e v59): nodo /dev mancante → dd su FILE REGOLARE
- Il devtmpfs di questo kernel **non crea i nodi `/dev/sdeNN`** per le partizioni UFS.
- `dd if=img of=/dev/sde41` con nodo inesistente **crea un file regolare** in /dev (RAM):
  write "riuscita" (`96+0 records out`) e readback corretto, ma la partizione NON viene toccata.
- Protocollo corretto (usato per v59/v60):
  1. `test -b /dev/sde41 || mknod /dev/sde41 b 259 25` (major:minor da `/sys/class/block/sde41/dev`);
  2. pre-lettura d'identità: `dd bs=1M count=1 | md5sum` confrontata con l'immagine attesa;
  3. write; `sync`×3; `drop_caches`; readback ×2 (secondo a cache fredda).
- Evidenze: v59 flash "riuscito" con readback `d9dbdb4e` ma boot = v58 (`/dev/sde41` assente);
  dopo mknod: identità v58 `de535a6d` (1MB) confermata e flash v60 verificato.

### 2. Radice dei proto handler netifd mancanti: PATH=/ ereditato
- netifd (spawn di procd) aveva `PATH=/` (dal nostro switch.sh senza PATH) → gli handler script
  `/lib/netifd/proto/*.sh` chiamano il binario **`jshn` via PATH** → dump fallito → **nessun handler
  registrato** (nemmeno dhcp!) → `get_proto_handlers` elencava solo "static".
- Fix alla radice: `export PATH=/usr/sbin:/usr/bin:/sbin:/bin` in testa a `switch.sh`.
- Evidenza: con PATH corretto `get_proto_handlers` elenca dhcp, dhcpv6, ppp, pppoe, static, nx679j.
- Nota: i proto script moderni si registrano con **`add_protocol`** (non `add_proto`).

### 3. Attach wireless `lan_wifi` (regressione v57→v58)
- Con `network.lan_wifi.device='wlan0'` (nome storico) l'attach dipende dal bind wireless e in v58
  falliva (NO_DEVICE; log: up→2s→down+disabled; dnsmasq senza dhcp-range perché iface down).
- **Fix deterministico:** `device='phy0-ap0'` (netdev diretto). netifd fa l'up quando il netdev
  appare; il wrapper ha belt `ifup lan_wifi` + fallback `ip addr add`.
- Evidenze: boot v59/v60: IP assegnato automaticamente ("ap0 state=up ip=192.168.77.1/24").

### 4. dnsmasq: tracking procd inaffidabile → spawn diretto nel wrapper
- L'istanza procd di dnsmasq risulta `"running": false, "exit_code": 1` ma il daemon può essere
  vivo; in avvio presto (interfacce non pronte) il range DHCP non serve i client.
- Fix nel wrapper v60: dopo AP+IP → `restart`; check **pid vivi (stato ≠ Z)**; se morto **spawn
  diretto** `dnsmasq -C /var/etc/dnsmasq.conf.cfg01411c -x /var/run/dnsmasq/dnsmasq.cfg01411c.pid &`.
- Evidenza: journal v60 `dnsmasq non vivo dopo restart: spawn diretto` → `dnsmasq vivo=si` → lease OK.

### 5. iptables-legacy da Alpine (kernel senza nf_tables)
- Il kernel vendor NON ha nf_tables (`/proc/net/nf_tables` assente): fw4/nft non utilizzabili.
- Iptables (xtables) è **built-in** nel kernel. Userspace: pacchetti Alpine musl (compatibili):
  `xtables-legacy-multi` + `/usr/lib/xtables/*` + `libxtables/libip4tc/libip6tc` + symlink
  `/lib/libc.musl-aarch64.so.1 → ld-musl-aarch64.so.1`.
- Regole wan-share: `ip_forward=1` + `-t nat -A POSTROUTING -o rmnet_data0 -j MASQUERADE` +
  2 regole FORWARD (phy0-ap0↔rmnet_data0), idempotenti (`-C`), applicate dal wrapper al boot.

### 6. Crash durante bring-up IPA (osservato 1 volta)
- Durante [ingress agg8192 → data stages] il telefono si è riavviato da solo (boot nuovo, SSH reset).
- Già visto in sessioni precedenti ("stallo ingress"). Catena poi completabile con retry.
- **Questo è il rischio residuo noto della catena modem manuale.**

## File di questa sessione (in experiments/20260920-wifi-luci/)
- `nx679j-proto.sh` — proto netifd adottivo con auto-riparazione (→ /lib/netifd/proto/nx679j.sh)
- `nx679j-wan-share.sh` — NAT/forwarding idempotente (→ /etc/nx679j-wan-share.sh)
- `v59-wifi-services.sh`, `v60-wifi-services.sh` — wrapper boot (→ /etc/nx679j-wifi-services.sh)
- `switch-v58-live.sh` — switch.sh del ramdisk con export PATH
- `build-v58.py`, `build-v59.py`, `build-v60.py` — build immagini (overlay cpio)
- `chain.sh` — catena modem completa detached con marker
- `owrt-live{5,6}.tar.gz` — rootfs catturati; `boot_b-v5{9,60}-wifi.img`

## Allegato RCA — regressione attach lan_wifi v57→v58 (subagent, verificata sul vivo)
**Meccanismo (evidenza file:riga nei due alberi + sorgente netifd):**
- I file WiFi (ucode, mac80211.sh, netifd) sono **byte-identici** tra v57 e v58 (md5): la regressione non è nel codice WiFi.
- L'unico delta d'ambiente: `export PATH=...` in switch.sh (v58). Con PATH=/ (v57) i percorsi shell
  `/etc/init.d/boot:48` (`/sbin/wifi config`), `/etc/hotplug.d/ieee80211/10-wifi-detect`,
  `/sbin/reload_config` erano **no-op silenziosi** (comandi `ucode`/`uci`/`ubus` nudi → not found).
  Con PATH valido (v58) possono girare e un `config.change` → `ubus call network reload` →
  churn della radio (teardown+re-setup) → il netdev `phy0-ap0` sparisce → lan_wifi riceve
  DEV_EVENT_DOWN → "is disabled" → con `device='wlan0'` (nome stale) resta **NO_DEVICE**.
- Nota: in v57 il successo era in parte accidentale (con PATH=/ anche gli handler proto netifd
  non si registravano — fatto misurato in questa sessione). Il PATH va quindi tenuto come
  requisito, non come workaround.
**Fix `device='phy0-ap0'` = corretto e auditato:** rende il main_dev risolvibile per nome;
  netifd su DEV_EVENT_ADD riattiva l'interfaccia (interface.c: interface_set_available/enabled),
  quindi l'attach si **auto-ripara** anche se un churn rimuove e ricrea il netdev.
**Verificato live sul boot v60 (PATH attivo + fix):** nessun churn pericoloso, uci wireless
  senza sezioni generate, `lan_wifi` up con 192.168.77.1 stabile.
**Caveat di design (accettato, mitigato dal fix):** l'iface è legata al link esterno; qualunque
  churn che rimuova il netdev senza ricrearlo lascia l'iface down fino al prossimo `ifup`/belt.
**Test decisivo suggerito (non eseguito, ora superfluo):** v57 + sola modifica PATH; la v60/v61
  col fix coprono il caso in modo deterministico (verificato a freddo).


## Performance WiFi→SIM: da 8/8 a 30/12 Mbps (fix 2026-09-20 sera)
Numeri S26 Ultra (speedtest, client reale): [8 down/8 up, ping 108, jitter 80] -> [30 down/12 up, ping 35, jitter 3].
Fix in ordine di impatto (tutti misurati):
1. **AP era in 802.11g legacy** (nessun htmode nel config v54!) -> attivato HT20+Short-GI:
   jitter 80->6ms, down 8->24.
2. **La banda 2.4GHz locale e' congesta** -> AP commutato su **5GHz VHT80 canale 36**
   (hw_mode=a, ieee80211ac=1): ping 180->35ms, jitter 6->3ms, down 24->30, up 8->12.
   (DBS 2.4+5 simultanea NON supportata: una banda alla volta sulla phy0.)
3. Il carrier qui fa ~20-30 down / ~8-12 up col segnale attuale: le condizioni RF della
   posizione restano il tetto (misure oneste: PC forzato con `curl --interface wlan0`,
   `ping -I wlan0`; il phone diretto ~17 Mbps).
**Lezione importante**: il PC di test ha l'ethernet come default route (metric 100 vs 600):
i test "via telefono" DEVONO essere forzati sull'interfaccia (--interface wlan0 / -I wlan0),
altrimenti misurano la rete ethernet (errore commesso e corretto in sessione).
**Config finale**: wireless.radio0 = band 5g, channel 36, htmode VHT80 (stesso SSID NX679J-TEST).
Per tornare al 2.4: uci set wireless.radio0.band='2g'; ... channel='6'; htmode='HT20'.

## Comandi utili
- SSH: `./nxssh.sh '...'` (chiave ~/.ssh/nx679j_key; il telefono ha la hostkey rigenerata a ogni boot)
- Flash: push img → mknod nodo se serve → identità → write → sync×3 → drop_caches → readback×2
- LuCI: login root/nx679j; dopo la catena: `ifup modem` per popolare la pagina dell'interfaccia

## Prossimi passi suggeriti
1. **Automazione catena modem al boot** (oggi manuale): script+tool nel rootfs e runner sequenziale
   con retry e watchdog (il crash ingress noto va gestito con un retry dopo reboot).
2. Reboot "pulito" (senza -f) una volta verificato che la UFS scrive ordinatamente.
3. doc: decidere se esporre la pagina modem con un "proto" ufficiale (adottivo) o lasciare 'none'.
