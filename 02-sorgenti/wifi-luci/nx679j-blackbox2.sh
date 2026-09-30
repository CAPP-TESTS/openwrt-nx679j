#!/bin/sh
# blackbox v2: journal + dmesg tail nel rawdump ogni 5s.
while :; do
  {
    printf '=== BB2 uptime=%s ===\n' "$(cut -d' ' -f1 /proc/uptime 2>/dev/null)"
    dmesg 2>/dev/null | grep -av "journal mirror" | tail -25
    printf -- '--- dmesg end ---\n'
  } > /tmp/bb2.txt 2>&1
  dd if=/tmp/bb2.txt of=/proc/1/root/dev/rd bs=32768 count=1 seek=409 conv=sync,notrunc,fsync 2>/dev/null
  sleep 5
done
