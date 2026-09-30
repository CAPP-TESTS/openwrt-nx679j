#!/bin/sh
# nx679j WAN share (v61) — condivisione Internet SIM -> LAN Wi-Fi, verifica inclusa.
# FATTI: kernel vendor SENZA nf_tables (niente fw4/nft); iptables (xtables) e'
# built-in; userspace = Alpine musl in /usr/sbin (xtables-legacy-multi).
# v61: MASQUERADE limitata alla LAN wifi (-s), forward esplicito nelle due
# direzioni, e il verdetto (ok/FAIL) e' calcolato QUI (rc coerente).
set -u
IPT=/usr/sbin/iptables-legacy
LAN=192.168.77.0/24
AP=phy0-ap0
WAN=rmnet_data0
FAIL=0

echo 1 > /proc/sys/net/ipv4/ip_forward 2>/dev/null || FAIL=1

# NAT: solo il traffico della LAN wifi esce mascherato sulla SIM
$IPT -t nat -C POSTROUTING -s $LAN -o $WAN -j MASQUERADE 2>/dev/null || \
  $IPT -t nat -A POSTROUTING -s $LAN -o $WAN -j MASQUERADE 2>/dev/null || FAIL=1

# FORWARD esplicito (policy ACCEPT ma niente firewall su questo kernel)
$IPT -C FORWARD -i $AP -o $WAN -j ACCEPT 2>/dev/null || \
  $IPT -A FORWARD -i $AP -o $WAN -j ACCEPT 2>/dev/null || FAIL=1
$IPT -C FORWARD -i $WAN -o $AP -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || \
  $IPT -A FORWARD -i $WAN -o $AP -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || FAIL=1

# verifica effettiva
$IPT -t nat -S POSTROUTING 2>/dev/null | grep -q MASQUERADE || FAIL=1
if [ "$FAIL" = "0" ]; then
  echo "wan-share: OK (fwd=$(cat /proc/sys/net/ipv4/ip_forward))"
  exit 0
else
  echo "wan-share: FAIL"
  exit 1
fi
