#!/bin/sh
# nx679j wifi/luci boot services wrapper (v58, 2026-09-20)
# Eseguito DENTRO il chroot /owrt da switch.sh:  chroot /owrt /bin/sh /etc/nx679j-wifi-services.sh
# FATTI misurati che hanno portato qui:
#  - gli init script richiedono PATH completo (dal contesto switch.sh mancava -> "ubus: not found")
#  - i jail procd (ujail) falliscono su questo kernel vendor (clone EINVAL) -> ujail.off
#  - l'AP va creato con wpad gia' su ubus e netifd avviato
#  - uhttpd (v57) ascolta SOLO su 10.0.0.1 e 192.168.77.1: va avviato DOPO che
#    entrambi gli indirizzi esistono (fallback: solo usb0 se l'AP non sale)
#  - il boot NON dipende dal wifi: qualunque fallimento qui non tocca usb0/SSH
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
cd /
# sicurezza: ujail disabilitato (EINVAL su questo kernel)
[ -x /sbin/ujail ] && mv /sbin/ujail /sbin/ujail.off
# dir runtime
mkdir -p /var/etc /tmp/hosts /tmp/resolv.conf.d /var/run
# 1) attesa driver (phy0 + wlan0), poi settle
i=0
while [ $i -lt 90 ]; do
    [ -e /sys/class/ieee80211/phy0 ] && [ -e /sys/class/net/wlan0 ] && break
    sleep 2; i=$((i+1))
done
echo "nx679j-wifi: driver dopo $((i*2))s (phy0=$([ -e /sys/class/ieee80211/phy0 ] && echo si || echo no) wlan0=$([ -e /sys/class/net/wlan0 ] && echo si || echo no))"
sleep 8
# 2) servizi base (uhttpd NON ancora: aspetta gli IP)
for s in log wpad rpcd dnsmasq network; do
    /etc/init.d/$s start >/dev/null 2>&1
done
# 3) attesa hostapd su ubus
i=0
while [ $i -lt 30 ]; do
    ubus list 2>/dev/null | grep -q '^hostapd$' && break
    sleep 2; i=$((i+1))
done
echo "nx679j-wifi: hostapd ubus dopo $((i*2))s"
# 4) attesa netifd
i=0
while [ $i -lt 20 ]; do
    ubus list 2>/dev/null | grep -q '^network.wireless$' && break
    sleep 2; i=$((i+1))
done
echo "nx679j-wifi: network.wireless ubus dopo $((i*2))s"
# 5) wifi up con retry + down/up insurance
r=1
while [ $r -le 5 ]; do
    wifi up >/dev/null 2>&1
    j=0
    while [ $j -lt 12 ]; do
        [ "$(cat /sys/class/net/phy0-ap0/operstate 2>/dev/null)" = "up" ] && break
        sleep 2; j=$((j+1))
    done
    [ "$(cat /sys/class/net/phy0-ap0/operstate 2>/dev/null)" = "up" ] && break
    echo "nx679j-wifi: tentativo $r fallito, wifi down + retry"
    wifi down >/dev/null 2>&1
    sleep 5
    r=$((r+1))
done
# 6) uhttpd: aspetta che gli IP ci siano (max 40s), altrimenti fallback solo usb0
i=0
while [ $i -lt 20 ]; do
    if ip -4 addr show usb0 2>/dev/null | grep -q '10.0.0.1' && \
       ip -4 addr show phy0-ap0 2>/dev/null | grep -q '192.168.77.1'; then
        break
    fi
    sleep 2; i=$((i+1))
done
if ip -4 addr show phy0-ap0 2>/dev/null | grep -q '192.168.77.1'; then
    /etc/init.d/uhttpd start >/dev/null 2>&1
    echo "nx679j-wifi: uhttpd (usb0+ap)"
else
    uci -q delete uhttpd.main.listen_http
    uci add_list uhttpd.main.listen_http='10.0.0.1:80'
    uci -q delete uhttpd.main.listen_https
    uci add_list uhttpd.main.listen_https='10.0.0.1:443'
    uci commit uhttpd
    /etc/init.d/uhttpd start >/dev/null 2>&1
    echo "nx679j-wifi: uhttpd (solo usb0 - AP non su)"
fi
# 7) report
st=$(cat /sys/class/net/phy0-ap0/operstate 2>/dev/null)
ip4=$(ip -4 addr show phy0-ap0 2>/dev/null | sed -n 's/.*inet \([0-9.]*\).*/\1/p')
echo "nx679j-wifi: ap0 state=$st ip=$ip4 (tentativi=$r)"
# v58: WAN share SIM->LAN (iptables-legacy + ip_forward, idempotente)
if [ -x /etc/nx679j-wan-share.sh ]; then
    /bin/sh /etc/nx679j-wan-share.sh
    echo "nx679j-wifi: wan-share applicato (ip_forward=$(cat /proc/sys/net/ipv4/ip_forward))"
fi

echo "nx679j-wifi: proc dnsmasq=$([ -n "$(pidof dnsmasq)" ] && echo ok || echo morto) hostapd=$([ -n "$(pidof hostapd)" ] && echo ok || echo morto) uhttpd=$([ -n "$(pidof uhttpd)" ] && echo ok || echo morto) netifd=$([ -n "$(pidof netifd)" ] && echo ok || echo morto)"
echo "nx679j-wifi: fine"
