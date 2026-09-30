#!/bin/sh
# blackbox: salva tail del journal + stato servizi nel rawdump ogni 8s.
while :; do
  {
    printf '=== BLACKBOX uptime=%s boot=%s ===\n' "$(cat /proc/uptime 2>/dev/null)" "$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)"
    grep -a "ubus pronto\|wifi-wrapper\|margine\|rcS:\|Running boot\|Boot done" /proc/1/root/nx679j-journal 2>/dev/null | tail -8
    printf -- '--- journal tail ---\n'
    tail -c 20000 /proc/1/root/nx679j-journal 2>/dev/null
  } > /tmp/blackbox.txt 2>&1
  dd if=/tmp/blackbox.txt of=/proc/1/root/dev/rd bs=32768 count=1 seek=409 conv=sync,notrunc,fsync 2>/dev/null
  sleep 8
done
