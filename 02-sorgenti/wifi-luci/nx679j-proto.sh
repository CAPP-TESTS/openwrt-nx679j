#!/bin/sh
# proto netifd "nx679j" — vista LuCI della catena dati modem X65 (adopt/read-only).
#
# CONTESTO (fatti misurati sul NX679J, 2026-09-20):
# - rmnet_data0 con IPv4/gateway NON e' configurata da netifd: la imposta la
#   catena modem esterna (rmnet-config-agg8192 ingress -> egress 0xa -> WDA
#   raw-IP/QMAP5 -> mux rmnet_data0 -> WDS APN, vedi nx679j-modem-prepare.sh).
# - netifd NON deve toccare quel link: proto "none" lo lasciava "unmanaged" e
#   senza IP visibile in LuCI.
# Questo proto ADOTTA lo stato reale: legge l'indirizzo IPv4 e il default
# gateway da rmnet_data0 e li riporta a netifd/LuCI. Non modifica nulla.
# In piu': se il device non esiste e l'opzione "bringup" e' definita, lancia
# lo script indicato in background (hook opzionale, best-effort).
#
# Config esempio in /etc/config/network:
#   config interface 'modem'
#       option proto 'nx679j'
#       option device 'rmnet_data0'
#       option auto '1'

. /lib/functions.sh
. ../netifd-proto.sh
init_proto "$@"

prefix2mask() {
    local p="$1"
    case "$p" in
        *.*) echo "$p"; return;;
        ''|*[!0-9]*) echo ""; return;;
    esac
    awk -v p="$p" 'BEGIN {
        n = 0
        for (i = 0; i < p; i++) n = n * 2 + 1
        n = n * 2 ^ (32 - p)
        printf "%d.%d.%d.%d", int(n/16777216), int(n/65536)%256, int(n/256)%256, n%256
    }'
}

proto_nx679j_init_config() {
    proto_config_add_string "device"
    proto_config_add_string "ifname"
    proto_config_add_string "bringup"
    no_device=1
    available=1
}

dotted2prefix() {
    awk -v m="$1" 'BEGIN {
        n = split(m, a, ".");
        if (n != 4) { print ""; exit }
        bits = 0;
        for (i = 1; i <= 4; i++) {
            v = a[i] + 0;
            for (j = 7; j >= 0; j--) {
                if (int(v / (2 ^ j)) % 2 == 1) bits++;

            }
        }
        print bits;
    }'
}

proto_nx679j_setup() {
    local cfg="$1"
    local device ifname bringup cidr addr mask gw

    config_get device "$cfg" device
    config_get ifname "$cfg" ifname
    config_get bringup "$cfg" bringup
    [ -n "$ifname" ] || ifname="$device"
    [ -n "$ifname" ] || ifname="rmnet_data0"

    if [ ! -e "/sys/class/net/$ifname" ]; then
        if [ -n "$bringup" ] && [ -x "$bringup" ]; then
            logger -t nx679j "device $ifname assente: lancio $bringup in background"
            ( "$bringup" >/dev/null 2>&1 & )
        fi
        proto_init_update "$ifname" 0
        proto_send_update "$cfg"
        return
    fi

    cidr=$(ip -4 addr show dev "$ifname" 2>/dev/null | awk "/inet / {print \$2; exit}")
    gw=$(ip -4 route show default dev "$ifname" 2>/dev/null | awk '{print $3}' | head -1)

    # Auto-riparazione (v59): se il device non ha IP (churn netifd/flush) e la
    # sessione WDS corrente e' attiva, ricrea indirizzo e default route dai
    # valori della sessione. Deterministico e idempotente.
    if [ -z "$cidr" ] && [ -r /tmp/wds-session.log ]; then
        waddr=$(awk '/^  IPv4 addr: / {print $3; exit}' /tmp/wds-session.log)
        wgw=$(awk '/^  IPv4 gateway: / {print $3; exit}' /tmp/wds-session.log)
        wmask=$(awk '/^  IPv4 netmask: / {print $3; exit}' /tmp/wds-session.log)
        wpfx=$(dotted2prefix "$wmask")
        case "$waddr" in ''|*[!0-9.]*) waddr="";; esac
        if [ -n "$waddr" ] && [ -n "$wgw" ] && [ -n "$wpfx" ]; then
            ip addr replace "$waddr/$wpfx" dev "$ifname" 2>/dev/null || true
            ip route replace default via "$wgw" dev "$ifname" 2>/dev/null || true
            cidr=$(ip -4 addr show dev "$ifname" 2>/dev/null | awk "/inet / {print \$2; exit}")
            gw="$wgw"
        fi
    fi

    if [ -n "$cidr" ]; then
        addr="${cidr%/*}"
        mask=$(prefix2mask "${cidr#*/}")
        proto_init_update "$ifname" 1
        [ -n "$mask" ] && proto_add_ipv4_address "$addr" "$mask"
        [ -n "$gw" ] && proto_add_ipv4_route "0.0.0.0" "0" "$gw"
        proto_send_update "$cfg"
    else
        # device presente ma senza IP (catena non ancora su)
        proto_init_update "$ifname" 1
        proto_send_update "$cfg"
    fi
}

proto_nx679j_teardown() {
    local cfg="$1"
    local ifname
    config_get ifname "$cfg" ifname
    [ -n "$ifname" ] || ifname="rmnet_data0"
    proto_init_update "$ifname" 0
    proto_send_update "$cfg"
    # Deliberatamente NESSUN flush di indirizzi: la catena modem e' esterna.
}

add_protocol nx679j
