#!/bin/sh
# nx679j netifd proto: read-only view of externally-owned rmnet_data0 L3.
# The modem chain owns the address and route; netifd only reports them.
. /lib/functions.sh
. ../netifd-proto.sh
init_proto "$@"

proto_nx679j_init_config() {
    proto_config_add_string "device"
    proto_config_add_string "ifname"
    no_device=1
    available=1
}

proto_nx679j_setup() {
    local cfg="$1"
    local device ifname cidr addr prefix mask

    config_get device "$cfg" device
    config_get ifname "$cfg" ifname
    [ -n "$ifname" ] || ifname="$device"
    [ -n "$ifname" ] || ifname="rmnet_data0"

    if [ ! -e "/sys/class/net/$ifname" ]; then
        proto_init_update "$ifname" 0 1
        proto_send_update "$cfg"
        return
    fi

    cidr=$(ip -4 addr show dev "$ifname" 2>/dev/null |
        awk '/inet / {print $2; exit}')
    proto_init_update "$ifname" 1 1
    if [ -n "$cidr" ]; then
        addr=${cidr%/*}
        prefix=${cidr#*/}
        case "$prefix" in
            ''|*[!0-9]*) prefix="" ;;
        esac
        if [ -n "$prefix" ]; then
            mask=$(awk -v p="$prefix" 'BEGIN {
                n = 0
                for (i = 0; i < p; i++) n = n * 2 + 1
                n = n * 2 ^ (32 - p)
                printf "%d.%d.%d.%d", int(n/16777216), int(n/65536)%256,
                    int(n/256)%256, n%256
            }')
            proto_add_ipv4_address "$addr" "$mask"
        fi
    fi
    # v129: nessuna route riportata.
    proto_send_update "$cfg"
}

proto_nx679j_teardown() {
    local cfg="$1"
    local device
    config_get device "$cfg" device
    [ -n "$device" ] || device="rmnet_data0"
    proto_init_update "$device" 0 1
    proto_send_update "$cfg"
}

add_protocol nx679j
