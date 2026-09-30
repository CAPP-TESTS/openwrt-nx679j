#!/system/bin/sh
# One new-format ONLINE request on already-ONLINE stock Android, never low power.
set -eu
umask 077
BASE=${1:?absolute tool directory required}
case "$BASE" in /*) ;; *) exit 2;; esac
OUT="$BASE/dms-observed-check"
test "$(getprop ro.boot.slot_suffix)" = _a
test "$(settings get global airplane_mode_on)" = 0
test "$(cat /sys/class/remoteproc/remoteproc4/state)" = running
test -x "$BASE/qmi-qrtr-observed"
mkdir "$OUT"
logger=
cleanup() {
  code=$?
  trap - EXIT INT TERM
  set +e
  if [ -n "$logger" ]; then
    kill -TERM "$logger"
    wait "$logger"
  fi
  printf 'CHECK_FINISHED uptime=%s exit=%s\n' "$(cat /proc/uptime)" "$code"
  exit "$code"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
timeout -k 1 5 "$BASE/qmi-qrtr" raw 2 0001002d000000 > "$OUT/preflight.log"
grep -q 'TLV 0x01 len=1  u8=0' "$OUT/preflight.log"
cp "$OUT/preflight.log" "$OUT/samples.log"
sh "$BASE/android-crashlog.sh" "$OUT" > "$OUT/raw-logger.log" 2>&1 &
logger=$!
for unused in 1 2 3 4 5 6 7 8 9 10; do
  if [ -s "$OUT/raw-slot-number" ]; then break; fi
  sleep 1
done
test -s "$OUT/raw-slot-number"
test "$(awk '/^State:/ {print $2}' "/proc/$logger/status")" != Z
printf 'CHECK_STARTED uptime=%s\n' "$(cat /proc/uptime)" | tee -a "$OUT/samples.log"
result=0
timeout -k 2 65 "$BASE/qmi-qrtr-observed" dms-online-observed \
  >> "$OUT/samples.log" 2>&1 || result=$?
printf 'CLIENT_EXIT=%s\n' "$result" | tee -a "$OUT/samples.log"
timeout -k 1 5 "$BASE/qmi-qrtr" raw 2 0001002d000000 > "$OUT/fresh-client-after.log"
cat "$OUT/samples.log"
cat "$OUT/fresh-client-after.log"
exit "$result"
