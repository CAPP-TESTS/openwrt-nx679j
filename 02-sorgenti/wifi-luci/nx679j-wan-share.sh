#!/bin/sh
# nx679j WAN share — condivisione Internet dalla SIM verso la LAN Wi-Fi.
# FATTI (2026-09-20, live-verificati):
#  - il kernel vendor NON ha nf_tables (/proc/net/nf_tables assente, fw4/nft
#    falliscono) MA ha iptables/xtables completi built-in (/proc/net/ip_tables_names
#    = security raw nat mangle filter; simboli nf_nat_masquerade_ipv4/ipt_do_table).
#  - userspace iptables assente nel rootfs OpenWrt: usato il pacchetto Alpine
#    (musl!) iptables-legacy 1.8.11 (file in /usr/sbin/xtables-legacy-multi +
#    /usr/lib/xtables/*.so + libxtables/libip*tc): compatibile con la musl OpenWrt.
#  - verifica end-to-end fatta: client Wi-Fi -> DHCP 192.168.77.x -> DNS via
#    dnsmasq (server=151.5.216.30/130 dal bearer) -> ping 8.8.8.8 4/4 e HTTP 200.
# Idempotente: usa -C prima di -A. Esce 0 anche se i comandi falliscono
# (il boot non deve dipendere da questo).
IPT=/usr/sbin/iptables-legacy
[ -x "$IPT" ] || exit 0

echo 1 > /proc/sys/net/ipv4/ip_forward

$IPT -t nat -C POSTROUTING -o rmnet_data0 -j MASQUERADE 2>/dev/null || \
    $IPT -t nat -A POSTROUTING -o rmnet_data0 -j MASQUERADE
$IPT -C FORWARD -i phy0-ap0 -o rmnet_data0 -j ACCEPT 2>/dev/null || \
    $IPT -A FORWARD -i phy0-ap0 -o rmnet_data0 -j ACCEPT
$IPT -C FORWARD -i rmnet_data0 -o phy0-ap0 -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || \
    $IPT -A FORWARD -i rmnet_data0 -o phy0-ap0 -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT

exit 0
