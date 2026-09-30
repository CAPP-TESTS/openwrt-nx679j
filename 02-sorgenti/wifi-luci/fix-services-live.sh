#!/bin/sh
# Validazione live del fix v56: wrapper con PATH corretto (dentro il chroot).
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
cd /
echo "=== init.d presenti ==="
ls /etc/init.d/log /etc/init.d/wpad /etc/init.d/uhttpd /etc/init.d/rpcd /etc/init.d/network /etc/init.d/dnsmasq 2>&1
echo "=== log ==="
/etc/init.d/log start 2>&1 | head -2
echo "=== wpad ==="
/etc/init.d/wpad start 2>&1 | head -3
sleep 4
echo "hostapd ubus: $(ubus list | grep -cE '^hostapd$')"
echo "=== rpcd ==="
/etc/init.d/rpcd start 2>&1 | head -2
echo "=== uhttpd ==="
/etc/init.d/uhttpd start 2>&1 | head -2
echo "=== dnsmasq ==="
mkdir -p /var/etc /tmp/hosts /tmp/resolv.conf.d 2>/dev/null
/etc/init.d/dnsmasq start 2>&1 | head -3
echo "=== network (gia' su) ==="
/etc/init.d/network status 2>&1 | head -3
echo "=== wifi up ==="
wifi up 2>&1 | head -4
sleep 12
iw dev | grep -E "Interface|type|ssid"
ip addr show phy0-ap0 2>/dev/null | grep -E "inet |state"
