#!/bin/sh
# v57: configurazione firewall minimale per NX679J.
# Zone:
#   mgmt = usb0 (SSH/LuCI) + phy0-ap0 (AP) -> input ACCEPT
#   wan  = rmnet_data0 (cellulare)          -> input REJECT (LuCI non esposto)
# Il forward/masq NON viene abilitato (nessun NAT verso i client Wi-Fi).
# Rollback: se non confermo entro 45s, il firewall viene fermato.
set -eu
cat > /etc/config/firewall <<'EOF'
config defaults
	option syn_flood	1
	option input		REJECT
	option output		ACCEPT
	option forward		REJECT

config zone
	option name		mgmt
	list   device		'usb0'
	list   device		'phy0-ap0'
	option input		ACCEPT
	option output		ACCEPT
	option forward		ACCEPT
	option mtu_fix		0

config zone
	option name		wan
	list   device		'rmnet_data0'
	option input		REJECT
	option output		ACCEPT
	option forward		REJECT
	option masq		0
	option mtu_fix		0

config rule
	option name		Allow-DHCP-Client
	option src		wan
	option proto		udp
	option dest_port	68
	option target		ACCEPT
	option family		ipv4
EOF
# modem: visibile come interfaccia LuCI (proto none, auto per mostrarla "up")
uci set network.modem.auto='1'
uci commit network
echo "config scritto"
# avvio con watchdog di rollback
/etc/init.d/firewall start > /tmp/fw-start.log 2>&1 &
sleep 8
echo "=== zone attive ==="
nft list ruleset 2>/dev/null | grep -E "chain (input|forward)" | head -6
echo "=== test input cellulare (deve fallire con firewall on) ==="
curl -s -m 3 -o /dev/null -w "curl a me stesso via rmnet: %{http_code} (000=bloccato)\n" http://10.97.176.4/ || echo "curl: bloccato (rc=$?)"
echo "=== test input mgmt (deve funzionare) ==="
curl -s -m 3 -o /dev/null -w "curl usb0: %{http_code}\n" http://10.0.0.1/
curl -s -m 3 -o /dev/null -w "curl ap: %{http_code}\n" http://192.168.77.1/
echo "WATCHDOG: crea /tmp/fw-keep entro 45s o il firewall viene fermato"
( sleep 45; if [ ! -e /tmp/fw-keep ]; then /etc/init.d/firewall stop > /tmp/fw-rollback.log 2>&1; echo ROLLBACK >> /tmp/fw-rollback.log; fi ) &
echo DONE
