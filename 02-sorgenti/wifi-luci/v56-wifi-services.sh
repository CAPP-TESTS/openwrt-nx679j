#!/bin/sh
# nx679j wifi/luci boot services wrapper (v56, 2026-09-20)
# Eseguito DENTRO il chroot /owrt da switch.sh:  chroot /owrt /bin/sh /etc/nx679j-wifi-services.sh
# Motivi/FATTI misurati che hanno richiesto questo file:
#  - Gli init script OpenWrt richiedono PATH completo: lanciati dal contesto
#    esterno senza PATH falliscono con "readlink/ubus/uci: not found"
#    (osservato nel journal v55 del boot 48d306d5, righe 9167+).
#  - I jail procd (ujail) su questo kernel vendor falliscono con
#    "jail: failed to clone/fork: Invalid argument" (osservato per dnsmasq e
#    wpad): si usa /sbin/ujail.off.
#  - Ordine validato live il 2026-09-20 sul boot 48d306d5:
#    log -> wpad -> (attesa oggetto ubus hostapd) -> rpcd -> uhttpd ->
#    dnsmasq -> network -> wifi up.  Con questo ordine AP+DHCP+LuCI OK.
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
cd /

# 0) sicurezza: ujail disabilitato (EINVAL su questo kernel)
[ -x /sbin/ujail ] && mv /sbin/ujail /sbin/ujail.off
# dir runtime necessarie (osservato: /var/etc mancante -> dnsmasq/uhttpd KO)
mkdir -p /var/etc /tmp/hosts /tmp/resolv.conf.d /var/run 2>/dev/null

# 1) attesa driver completa (phy0 + netdev wlan0), poi settle per il psoc
i=0
while [ $i -lt 90 ]; do
    [ -e /sys/class/ieee80211/phy0 ] && [ -e /sys/class/net/wlan0 ] && break
    sleep 2; i=$((i+1))
done
echo "nx679j-wifi: driver dopo $((i*2))s (phy0=$([ -e /sys/class/ieee80211/phy0 ] && echo si || echo no) wlan0=$([ -e /sys/class/net/wlan0 ] && echo si || echo no))"
sleep 8

# 2) servizi (PATH corretto)
for s in log wpad rpcd uhttpd dnsmasq network; do
    [ -x /etc/init.d/$s ] && /etc/init.d/$s start >/dev/null 2>&1
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

# 5) wifi up con retry e, se serve, down/up di pulizia
r=1
while [ $r -le 4 ]; do
    wifi up >/dev/null 2>&1
    j=0
    while [ $j -lt 12 ]; do
        [ "$(cat /sys/class/net/phy0-ap0/operstate 2>/dev/null)" = "up" ] && break
        sleep 2; j=$((j+1))
    done
    [ "$(cat /sys/class/net/phy0-ap0/operstate 2>/dev/null)" = "up" ] && break
    echo "nx679j-wifi: tentativo $r non riuscito, wifi down + retry"
    wifi down >/dev/null 2>&1
    sleep 5
    r=$((r+1))
done

# 6) report finale
st=$(cat /sys/class/net/phy0-ap0/operstate 2>/dev/null)
ip4=$(ip -4 addr show phy0-ap0 2>/dev/null | sed -n 's/.*inet \([0-9.]*\).*/\1/p')
echo "nx679j-wifi: ap0 state=$st ip=$ip4 (tentativi=$r)"
echo "nx679j-wifi: proc dnsmasq=$([ -n "$(pidof dnsmasq)" ] && echo ok || echo morto) hostapd=$([ -n "$(pidof hostapd)" ] && echo ok || echo morto) uhttpd=$([ -n "$(pidof uhttpd)" ] && echo ok || echo morto) netifd=$([ -n "$(pidof netifd)" ] && echo ok || echo morto)"
echo "nx679j-wifi: fine"
