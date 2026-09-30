#!/bin/bash
# Bounded USB/boot monitor: records what the phone presents after a boot
# attempt. Read-only; it never writes to the device.
# usage: monitor-boot.sh <outdir> <seconds>
set -u
OUT="${1:-/tmp/monitor}"; SECS="${2:-180}"
mkdir -p "$OUT"
LOG="$OUT/usb-monitor.log"
: > "$LOG"
prev=""
end=$((SECONDS + SECS))
while [ $SECONDS -lt $end ]; do
    t=$SECONDS
    usb=$(lsusb 2>/dev/null | grep -iE '05c6:|18d1:|nubia|qualcomm' | sed 's/^Bus [0-9]* Device [0-9]*: //' | tr '\n' ';')
    links=$(ip -o link show 2>/dev/null | awk -F': ' '{print $2}' | grep -viE '^(lo|enp|wlan|docker|veth|br-|virbr|nordlynx|tun|tap)' | tr '\n' ',')
    if [ "$usb|$links" != "$prev" ]; then
        printf '[%4ds] USB=%s IFACES=%s\n' "$t" "${usb:-none}" "${links:-none}" >> "$LOG"
        prev="$usb|$links"
    fi
    sleep 1
done
printf '[%4ds] monitor finished\n' "$SECS" >> "$LOG"
cat "$LOG"
