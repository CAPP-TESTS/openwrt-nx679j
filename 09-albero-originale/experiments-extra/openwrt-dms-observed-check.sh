#!/bin/sh
# A single new-format request, after the established protected bootstrap.
set -eu
find_mss_state() {
  for name in /sys/class/remoteproc/remoteproc*/name; do
    if [ -r "$name" ] && [ "$(cat "$name")" = 4080000.remoteproc-mss ]; then
      printf '%s/state\n' "${name%/name}"
      return 0
    fi
  done
  return 1
}
R=$(find_mss_state)
test "$(cat /proc/sys/kernel/random/boot_id)" = "$(cat /tmp/observed-bootstrap.boot-id)"
grep -q '^READY_FOR_OBSERVED_DMS_TEST ' /tmp/observed-bootstrap.log
test "$(cat "$R")" = running
read -r logger < /tmp/crashlog-dump.pid
test -r "/proc/$logger/status"
test "$(awk '/^State:/ {print $2}' "/proc/$logger/status")" != Z
test -s /tmp/crashlog-slot-number
test -x /tmp/qmi-qrtr-observed
mkdir /tmp/dms-observed-openwrt.once
printf 'BOOT=%s START_UPTIME=%s\n' "$(cat /proc/sys/kernel/random/boot_id)" "$(cat /proc/uptime)"
/tmp/qmi-qrtr raw 2 0001002d000000 > /tmp/dms-observed-preflight.log
cat /tmp/dms-observed-preflight.log
grep -q 'TLV 0x01 len=1  u8=5' /tmp/dms-observed-preflight.log
/tmp/qmi-qrtr raw 3 00010024000000 > /tmp/dms-observed-nas-before.log
result=0
/tmp/qmi-qrtr-observed dms-online-observed || result=$?
printf 'OBSERVED_CLIENT_EXIT=%s UPTIME=%s\n' "$result" "$(cat /proc/uptime)"
fresh=0
/tmp/qmi-qrtr raw 2 0001002d000000 > /tmp/dms-observed-fresh-after.log || fresh=$?
cat /tmp/dms-observed-fresh-after.log
printf 'FRESH_CLIENT_EXIT=%s\n' "$fresh"
/tmp/qmi-qrtr raw 3 00010024000000 > /tmp/dms-observed-nas-after.log
/tmp/qmi-qrtr list > /tmp/dms-observed-services-after.log
dmesg > /tmp/dms-observed-dmesg-after.log
printf 'OBSERVED_CHECK_FINISHED UPTIME=%s EXIT=%s\n' "$(cat /proc/uptime)" "$result"
exit "$result"
