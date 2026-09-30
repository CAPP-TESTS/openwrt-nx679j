#!/bin/sh
# nx679j-wifi-services (v61) — servizi+wifi+wan-share deterministici.
#
# FIX v61 (dalla code review + misure live):
#  - ujail: il layer cpio base RI-AGGIUNGE /sbin/ujail a ogni boot (il nostro
#    overlay e' add-only e non puo' cancellarlo). Senza questo rename i jail
#    procd si riattivano e dnsmasq/wpad muoiono (clone EINVAL). => mv forzato.
#  - guardia wifi up: solo se radio GIU' E ap0 assente (il vecchio test su ap0
#    da solo e' in race -> 'wifi up' fa down+reload globale).
#  - uhttpd: servono ENTRAMBI gli IP (usb0+AP); il fallback ridotto viene
#    revertato subito dopo lo start (niente delta persistente in /tmp/.uci).
#  - grep ancorati ('inet 192\.168\.77\.1/') per non matchare .10-.199.
#  - belt lan_wifi via ubus (ifup fa un reload globale di netifd).
#  - wan-share: verifica MASQUERADE/FORWARD e log ok/FAIL.
#  - dnsmasq: restart dopo l'AP + check pid VIVI (non zombie) + spawn diretto.
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
cd /
J=/proc/1/root/nx679j-journal
say() { echo "nx679j-wifi: $*" | tee -a "$J" >/dev/null; }

# 0) ujail OFF (il layer base lo riporta a ogni boot)
if [ -e /sbin/ujail ]; then
  mv -f /sbin/ujail /sbin/ujail.off 2>/dev/null
  say "ujail disattivato (rename runtime)"
fi

# 1) attesa driver wlan (max 90s)
i=0
while [ ! -d /sys/class/ieee80211/phy0 ] && [ $i -lt 45 ]; do sleep 2; i=$((i+1)); done
say "driver dopo $((i*2))s (phy0=$([ -d /sys/class/ieee80211/phy0 ] && echo si || echo NO))"

# 2) servizi (network prima di dnsmasq: il range DHCP richiede l'interfaccia)
for s in log wpad rpcd network dnsmasq; do
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

# 5) AP + IP: 'wifi up' SOLO se radio e AP sono entrambi assenti (evita flap)
radio_up="$(ubus call network.wireless status 2>/dev/null | jsonfilter -e '@.radio0.up' 2>/dev/null)"
if [ "$radio_up" != "true" ] && [ ! -e /sys/class/net/phy0-ap0 ]; then
  say "radio giu': un ciclo wifi up"
  wifi up >/dev/null 2>&1
fi
i=0
while [ ! -e /sys/class/net/phy0-ap0 ] && [ $i -lt 45 ]; do sleep 2; i=$((i+1)); done

ap_ip_ok() { ip -4 addr show phy0-ap0 2>/dev/null | grep -qE 'inet 192\.168\.77\.1/'; }
i=0
while [ $i -lt 45 ]; do ap_ip_ok && break; sleep 2; i=$((i+1)); done
if ! ap_ip_ok; then
  ubus call network.interface.lan_wifi up >/dev/null 2>&1
  i=0
  while [ $i -lt 20 ]; do ap_ip_ok && break; sleep 2; i=$((i+1)); done
fi
if ! ap_ip_ok; then
  if ip addr add 192.168.77.1/24 dev phy0-ap0 2>/dev/null; then
    say "IP assegnato dal fallback diretto (netifd non lo ha fatto)"
  else
    say "FALLBACK IP FALLITO"
  fi
fi
apstate=$(cat /sys/class/net/phy0-ap0/operstate 2>/dev/null || echo n/d)
say "ap0 state=$apstate ip=$(ip -4 addr show phy0-ap0 2>/dev/null | awk '/inet /{print $2}' | head -1)"

# 5b) dnsmasq ROBUSTO (dopo l'AP con IP)
mkdir -p /tmp/resolv.conf.d
[ -e /tmp/resolv.conf.d/resolv.conf.auto ] || touch /tmp/resolv.conf.d/resolv.conf.auto
/etc/init.d/dnsmasq restart >/dev/null 2>&1
sleep 3
dnsmasq_alive() {
  for p in $(pidof dnsmasq); do
    st=$(awk '{print $3}' /proc/$p/stat 2>/dev/null)
    [ -n "$st" ] && [ "$st" != "Z" ] && return 0
  done
  return 1
}
if ! dnsmasq_alive; then
  say "dnsmasq non vivo dopo restart: spawn diretto"
  dnsmasq -C /var/etc/dnsmasq.conf.cfg01411c -x /var/run/dnsmasq/dnsmasq.cfg01411c.pid >/dev/null 2>&1 &
  sleep 3
fi
if dnsmasq_alive; then say "dnsmasq vivo=si"; else say "dnsmasq vivo=NO"; fi

# 6) uhttpd: servono ENTRAMBI gli IP (usb0 + AP)
usb_ip_ok() { ip -4 addr show usb0 2>/dev/null | grep -qE 'inet 10\.0\.0\.1/'; }
if usb_ip_ok && ap_ip_ok; then
  /etc/init.d/uhttpd start >/dev/null 2>&1
  say "uhttpd (usb0+ap)"
else
  # fallback ridotto, NON persistente: revert subito dopo lo start
  uci -q set uhttpd.main.listen_http=10.0.0.1:80
  uci -q set uhttpd.main.listen_https=10.0.0.1:443
  /etc/init.d/uhttpd start >/dev/null 2>&1
  uci -q revert uhttpd
  say "uhttpd (solo usb0 - AP o usb0 non pronti)"
fi

# 7) wan-share: NAT SIM -> LAN wi-fi + verifica
if [ -f /etc/nx679j-wan-share.sh ]; then
  /bin/sh /etc/nx679j-wan-share.sh >/dev/null 2>&1
  if iptables-legacy -t nat -S POSTROUTING 2>/dev/null | grep -q MASQUERADE; then
    say "wan-share ok (ip_forward=$(cat /proc/sys/net/ipv4/ip_forward 2>/dev/null))"
  else
    say "wan-share FAIL (nessuna MASQUERADE)"
  fi
fi

# 8) report finale
say "proc dnsmasq=$(dnsmasq_alive && echo ok || echo NO) hostapd=$(pidof hostapd >/dev/null && echo ok || echo NO) uhttpd=$(pidof uhttpd >/dev/null && echo ok || echo NO) netifd=$(pidof netifd >/dev/null && echo ok || echo NO)"
say "fine"
exit 0
