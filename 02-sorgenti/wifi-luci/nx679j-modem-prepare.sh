#!/bin/sh
# nx679j modem prepare (pre-ingress) v2: attiva IPA + endpoint + sessione DPM.
# ORDINE CORRETTO (dal kernel log 2026-09-20): NIENTE pipes/egress qui!
# L'ingress agg8192 (0x2e) deve essere la PRIMA config ingress: se una pipe
# viene già allocata (es. rmnet-config pipes -> 0xe), l'agg fallisce con
# "EP 23 already allocated" -> EINVAL. (In ca60: agg prima, egress dopo.)
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
uptime_s() { awk '{print int($1)}' /proc/uptime; }
R=$(find_mss_state)
test "$(cat "$R")" = running
test "$(cat /proc/sys/kernel/random/boot_id)" = "$(cat /tmp/observed-bootstrap.boot-id)"
read -r logger < /tmp/crashlog-dump.pid
test -r "/proc/$logger/status"
test -s /tmp/crashlog-slot-number
test ! -d /sys/class/net/rmnet_ipa0
mkdir /tmp/modem-prepare.once
printf 'PREPARE_BEGIN uptime=%s\n' "$(cat /proc/uptime)"
/tmp/qmi-qrtr raw 2 0001002d000000 > /tmp/prep-mode.log
cat /tmp/prep-mode.log
grep -q 'TLV 0x01 len=1  u8=0' /tmp/prep-mode.log
printf 'IPA_TRIGGER uptime=%s\n' "$(cat /proc/uptime)"
/tmp/ipa-trigger
deadline=$(( $(uptime_s) + 45 ))
while :; do
  dmesg > /tmp/prep-ipa-dmesg.log
  if grep -q 'QMI_IPA_INIT_MODEM_DRIVER_REQ_V01 response received' /tmp/prep-ipa-dmesg.log; then break; fi
  test "$(uptime_s)" -lt "$deadline"
  sleep 1
done
test -d /sys/class/net/rmnet_ipa0
/tmp/qmi-qrtr lookup 49 | grep 'service=49 version=1 instance=1 node=1 '
/tmp/rmnet-query rmnet_ipa0 > /tmp/data-endpoints.log
cat /tmp/data-endpoints.log
grep -qx 'driver=rmnet_ipa0' /tmp/data-endpoints.log
grep -qx 'endpoint=1 (0x1)' /tmp/data-endpoints.log
grep -qx 'pipes consumer=2 producer=23' /tmp/data-endpoints.log
printf 'DPM_OPEN uptime=%s\n' "$(cat /proc/uptime)"
/tmp/qmi-qrtr-observed dpm-session 4 1 2 23 3600 > /tmp/dpm-session.log 2>&1 &
dpm=$!
printf '%s\n' "$dpm" > /tmp/dpm-session.pid
deadline=$(( $(uptime_s) + 15 ))
while ! grep -q '^\[dpm\] OPENED endpoint=4:1 rx=2 tx=23 ' /tmp/dpm-session.log; do
  test -r "/proc/$dpm/status"
  test "$(uptime_s)" -lt "$deadline"
  sleep 1
done
cat /tmp/dpm-session.log
printf 'PREPARE_CLOSE_DPM uptime=%s\n' "$(cat /proc/uptime)"
kill "$dpm" 2>/dev/null || true
rm -f /tmp/dpm-session.pid
sleep 1
printf 'PREPARE_COMPLETE uptime=%s\n' "$(cat /proc/uptime)"
