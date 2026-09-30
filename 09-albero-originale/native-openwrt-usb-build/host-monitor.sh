#!/bin/bash
# Monitor host for NX679J OpenWrt NCM gadget after reboot.
# NetworkManager owns the usb-gadget profile and configures 192.168.2.2/24.
# This script only waits for that address and then attempts SSH.
set -u
DEV_IP=192.168.2.1
HOST_IP=192.168.2.2
LOG=/home/user/nx679j-stock/native-openwrt-usb-build/host-monitor.log
: > "$LOG"
log(){ echo "[$(date +%T)] $*" | tee -a "$LOG"; }

log "monitoring for NX679J NCM gadget (timeout 180s)..."
START=$(date +%s)
FOUND=""
SUCCESS=0
for i in $(seq 1 180); do
    # look for a cdc_ncm interface in ip -d link
    IF=$(ip -o -d link show 2>/dev/null | grep -i "cdc_ncm" | awk -F': ' '{print $2}' | head -1)
    if [ -z "$IF" ]; then
        IF=$(ip -o link show 2>/dev/null | awk -F': ' '{print $2}' | grep -iE "^usb|enx" | head -1)
    fi
    if [ -z "$IF" ]; then
        IF=$(ip -o -d link show 2>/dev/null | awk -F': ' '/parentbus usb/ {print $2; exit}')
    fi
    if [ -n "$IF" ]; then
        if [ "$IF" != "$FOUND" ]; then
            FOUND=$IF
            log "interface appeared: $FOUND"
        fi

        if ip -4 -o addr show dev "$FOUND" | grep -q "$HOST_IP/24"; then
            log "NetworkManager configured $FOUND with $HOST_IP/24"
            log "pinging $DEV_IP..."
            if ping -c 3 -W 2 $DEV_IP 2>>"$LOG"; then
                log "PING OK"
                log "attempting SSH root@$DEV_IP ..."
                if sshpass -p openwrt ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10 root@$DEV_IP 'uname -a; cat /etc/openwrt_release 2>/dev/null; ip -o addr show; cat /tmp/usb-gadget.log 2>/dev/null | tail -20' 2>>"$LOG"; then
                    log "SSH SUCCESS"
                    SUCCESS=1
                    break
                fi
            fi
            log "gadget is not reachable yet; retrying"
        fi
    fi
    # also report USB device changes
    if [ $((i % 15)) -eq 0 ]; then
        log "t=${i}s: no NCM yet. USB: $(lsusb 2>/dev/null | grep -i '19d2' | head -1)"
    fi
    sleep 1
done
ELAPSED=$(( $(date +%s) - START ))
if [ "$SUCCESS" -eq 0 ]; then
    log "TIMEOUT after ${ELAPSED}s: no reachable NCM gadget. USB devices now:"
    lsusb 2>/dev/null | tee -a "$LOG"
    log "dmesg tail:"; dmesg 2>/dev/null | tail -25 | tee -a "$LOG"
    log ">>> No visibility. Likely boot failed or gadget didn't come up. Recovery: enter EDL/fastboot recovery, then restore the matched boot_b.img and vendor_boot_b.img backup pair."
    exit 1
else
    log "done after ${ELAPSED}s (interface $FOUND)"
fi
