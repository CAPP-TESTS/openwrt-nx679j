#!/bin/sh
# nx679j-wifi-services (v59) — avvio servizi+wifi+wan-share con PATH corretto.
# Lanciato da /nx679j/switch.sh (ramdisk) DENTRO il chroot /owrt:
#   chroot /owrt /bin/sh /etc/nx679j-wifi-services.sh
#
# FATTI (misurati, 2026-09-20 sessione 5):
#  - Il PATH va impostato QUI esplicitamente: e' la stessa classe di bug che ha
#    tenuto spenti gli handler proto di netifd (ereditavano PATH=/ da procd).
#  - uhttpd ascolta SU 10.0.0.1 e 192.168.77.1: va avviato DOPO che il wifi ha
#    l'IP, altrimenti muore. Il fallback NON deve committare uci (la v57/v58
#    lo faceva e danneggiava la config persistente).
#  - lan_wifi ora punta DIRETTAMENTE a phy0-ap0 (device nel config): l'IP lo
#    assegna netifd quando il netdev esiste. Belt: ifup lan_wifi + fallback
#    diretto 'ip addr add' se netifd non lo fa entro il timeout.
#  - NON fare 'wifi up' se il radio e' gia' su (autostart di network start):
#    una seconda chiamata causa un reload (teardown+rebuild) e puo' uccidere
#    l'attach (visto dal vivo). Un solo ciclo esplicito solo se l'AP manca.
#  - wan-share: iptables-legacy (Alpine musl) + ip_forward. Il kernel vendor
#    NON ha nf_tables: niente fw4/nft. Il file esiste anche senza bit x.
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
cd /
J=/proc/1/root/nx679j-journal
say() { echo "nx679j-wifi: $*" | tee -a "$J" >/dev/null; }

# 1) attesa driver wlan (max 90s)
i=0
while [ ! -d /sys/class/ieee80211/phy0 ] && [ $i -lt 45 ]; do sleep 2; i=$((i+1)); done
say "driver dopo $((i*2))s (phy0=$([ -d /sys/class/ieee80211/phy0 ] && echo si || echo NO))"

# 2) servizi (network porta su netifd + autostart radio)
for s in log wpad rpcd dnsmasq network; do
  /etc/init.d/$s start >/dev/null 2>&1
done
say "servizi avviati"

# 3) attesa hostapd ubus
i=0
while ! ubus -t 2 list hostapd >/dev/null 2>&1 && [ $i -lt 30 ]; do sleep 2; i=$((i+1)); done
say "hostapd ubus dopo $((i*2))s"

# 4) attesa network.wireless
i=0
while ! ubus -t 2 call network.wireless status >/dev/null 2>&1 && [ $i -lt 15 ]; do sleep 2; i=$((i+1)); done
say "network.wireless ubus dopo $((i*2))s"

# 5) AP + IP: se l'AP non esiste, UN solo ciclo wifi up (evita il flap)
if [ ! -e /sys/class/net/phy0-ap0 ]; then
  say "phy0-ap0 assente: un ciclo wifi up"
  wifi up >/dev/null 2>&1
fi
i=0
while [ ! -e /sys/class/net/phy0-ap0 ] && [ $i -lt 45 ]; do sleep 2; i=$((i+1)); done

# attesa IP assegnato da netifd (lan_wifi -> phy0-ap0)
i=0
while [ $i -lt 45 ]; do
  ip -4 addr show phy0-ap0 2>/dev/null | grep -q '192.168.77.1' && break
  sleep 2; i=$((i+1))
done
if ! ip -4 addr show phy0-ap0 2>/dev/null | grep -q '192.168.77.1'; then
  # belt 1: forza ifup dell'interfaccia lan_wifi
  ifup lan_wifi >/dev/null 2>&1
  i=0
  while [ $i -lt 20 ]; do
    ip -4 addr show phy0-ap0 2>/dev/null | grep -q '192.168.77.1' && break
    sleep 2; i=$((i+1))
  done
fi
if ! ip -4 addr show phy0-ap0 2>/dev/null | grep -q '192.168.77.1'; then
  # belt 2: assegnazione diretta deterministica
  ip addr add 192.168.77.1/24 dev phy0-ap0 2>/dev/null
  say "IP assegnato dal fallback diretto (netifd non lo ha fatto)"
fi
apstate=$(cat /sys/class/net/phy0-ap0/operstate 2>/dev/null || echo n/d)
say "ap0 state=$apstate ip=$(ip -4 addr show phy0-ap0 2>/dev/null | awk '/inet /{print $2}' | head -1)"

# 6) uhttpd: solo ora che gli IP ci sono (config persistente = usb0+AP)
if ip -4 addr show phy0-ap0 2>/dev/null | grep -q '192.168.77.1'; then
  /etc/init.d/uhttpd start >/dev/null 2>&1
  say "uhttpd (usb0+ap)"
else
  # fallback NON persistente: modifica solo la config attiva in /tmp/.uci
  # NB: NON committare: uci set senza commit = solo config attiva (tmp),
  # sparisce al reboot. Il commit in v57 danneggiava la config persistente.
  uci -q set uhttpd.main.listen_http=10.0.0.1:80
  uci -q set uhttpd.main.listen_https=10.0.0.1:443
  /etc/init.d/uhttpd start >/dev/null 2>&1
  say "uhttpd (solo usb0 - AP non su)"
fi

# 7) wan-share: NAT SIM -> LAN wi-fi (iptables legacy + ip_forward)
if [ -f /etc/nx679j-wan-share.sh ]; then
  /bin/sh /etc/nx679j-wan-share.sh >/dev/null 2>&1
  say "wan-share applicato (ip_forward=$(cat /proc/sys/net/ipv4/ip_forward 2>/dev/null))"
fi

# 8) report finale
say "proc dnsmasq=$(pidof dnsmasq >/dev/null && echo ok || echo NO) hostapd=$(pidof hostapd >/dev/null && echo ok || echo NO) uhttpd=$(pidof uhttpd >/dev/null && echo ok || echo NO) netifd=$(pidof netifd >/dev/null && echo ok || echo NO)"
say "fine"
exit 0
