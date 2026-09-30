#!/system/bin/sh
# Same verified rawdump geometry as OpenWrt: 1 MiB offset, 32 KiB slots.
# Diagnostic budget: 80 samples, every 3 seconds, for the <=180s capture.
set -eu
OUT=${1:?capture directory required}
L=/dev/block/by-name/rawdump
test -b "$L"
test "$(readlink -f "$L")" = /dev/block/sda11
grep -q '^PARTNAME=rawdump$' /sys/block/sda/sda11/uevent
printf '%s\n' "$$" > "$OUT/raw-logger.pid"
i=0
while [ "$i" -lt 80 ]; do
  dmesg > "$OUT/dmesg-current.log"
  {
    printf 'slot=%s uptime=%s boot=%s\n' "$i" "$(cat /proc/uptime)" \
      "$(cat /proc/sys/kernel/random/boot_id)"
    printf 'MSS=%s\n' "$(cat /sys/class/remoteproc/remoteproc4/state)"
    tail -45 "$OUT/dmesg-current.log"
    printf '\n-- observer --\n'
    tail -30 "$OUT/samples.log" 2>/dev/null || true
    printf '\n-- tracer --\n'
    tail -8 "$OUT/strace-control.log" 2>/dev/null || true
  } > "$OUT/raw-slot.txt"
  test "$(wc -c < "$OUT/raw-slot.txt")" -le 32768
  dd if="$OUT/raw-slot.txt" of="$L" bs=32768 count=1 \
    seek=$((32 + i)) conv=sync,notrunc 2>/dev/null
  printf '%s\n' "$i" > "$OUT/raw-slot-number"
  i=$((i + 1))
  sleep 3
done
