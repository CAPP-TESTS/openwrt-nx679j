#!/system/bin/sh
# Read-only checks before archiving the completed independent-client test.
set -eu
BASE=/data/local/tmp/nx679j-20260920-compare
OUT=$BASE/dms-observed-check
test "$(id -u)" = 0
test "$(getprop ro.boot.slot_suffix)" = _a
test "$(cat /proc/sys/kernel/random/boot_id)" = 6c0c9043-bd14-45c5-8d84-1c986a1c63d3
printf 'BOOT=%s UPTIME=%s\n' "$(cat /proc/sys/kernel/random/boot_id)" "$(cat /proc/uptime)"
test "$(settings get global airplane_mode_on)" = 0
read -r logger < "$OUT/raw-logger.pid"
if [ -r "/proc/$logger/status" ]; then
  state=$(awk '/^State:/ {print $2}' "/proc/$logger/status")
  printf 'LOGGER pid=%s state=%s\n' "$logger" "$state"
  test "$state" = Z
else
  printf 'LOGGER pid=%s absent\n' "$logger"
fi
for name in qcrilNrd netmgrd qmipriod qti; do
  for pid in $(pidof "$name"); do
    tracer=$(awk '/^TracerPid:/ {print $2}' "/proc/$pid/status")
    printf '%s pid=%s tracer=%s\n' "$name" "$pid" "$tracer"
    test "$tracer" = 0
  done
done
grep -q 'ONLINE_VERIFIED' "$OUT/samples.log"
ls -l "$OUT"
"$BASE/qmi-qrtr" raw 2 0001002d000000
printf 'ARCHIVE_PREFLIGHT_PASS\n'
