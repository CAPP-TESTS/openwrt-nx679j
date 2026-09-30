#!/bin/sh
# Isolate ADSP availability; preserve MSS, IPA, and the three service daemons.
# Stock adspr/adspua.jsn map avs/audio to msm/adsp/audio_pd, instance 74.
# qcom_sysmon.c handles real remoteproc events; do not forge SSCTL events.
set -eu
A=/sys/class/remoteproc/remoteproc0
M=/sys/class/remoteproc/remoteproc3
uptime_s() { awk '{print int($1)}' /proc/uptime; }
snapshot() {
  dmesg > /tmp/adsp-dmesg-after.log
  /tmp/qmi-qrtr list > /tmp/adsp-services-after.log || true
  ip link show > /tmp/adsp-links-after.log
  cp /tmp/rprocstart.status /tmp/adsp-rproc-status.log
}
sample() {
  reply=$(/tmp/qmi-qrtr raw 2 0001002d000000) || reply="query failed"
  mode=$(printf '%s\n' "$reply" | awk '/TLV 0x01 len=1/ {split($NF,a,"="); print a[2]}')
  services=$(/tmp/qmi-qrtr list | grep -c service=) || services=0
  printf 'SAMPLE uptime=%s elapsed=%s mode=%s services=%s modem=%s adsp=%s\n' \
    "$(uptime_s)" "$(( $(uptime_s) - start_at ))" \
    "${mode:-unknown}" "$services" "$(cat "$M/state")" "$(cat "$A/state")"
  printf '%s\n' "$reply"
}

grep -q '^RESULT: no ONLINE transition in 660 seconds after IPA-ready protective restart$' /tmp/ipa-cycle.log
read -r previous_pid < /tmp/ipa-cycle.pid
previous_state=
if [ -r "/proc/$previous_pid/status" ]; then
  previous_state=$(awk '/^State:/ {print $2}' "/proc/$previous_pid/status")
fi
if kill -0 "$previous_pid" 2>/dev/null && [ "$previous_state" != Z ]; then
  printf 'ERROR: previous experiment still alive\n'
  exit 1
fi
test "$(cat "$A/name")" = 3000000.remoteproc-adsp
test "$(cat "$A/state")" = offline
test "$(cat "$A/firmware")" = adsp.mdt
test -r /proc/1/root/lib/firmware/adsp.mdt
test "$(cat "$M/name")" = 4080000.remoteproc-mss
test "$(cat "$M/state")" = running
test -d /sys/class/net/rmnet_ipa0
test -x /tmp/qmi-qrtr-next
pidof rmtfs >/dev/null
pidof tqftpserv >/dev/null
/tmp/qmi-qrtr lookup 14 | grep 'service=14 '
/tmp/qmi-qrtr lookup 64 | grep 'service=64 '
/tmp/qmi-qrtr lookup 49 | grep 'service=49 version=1 instance=1 node=1 '
read -r logger_pid < /tmp/crashlog-dump.pid
kill -0 "$logger_pid"
tr '\000' '\n' < "/proc/$logger_pid/cmdline" | grep -qx '/tmp/crashlog-dump.sh'
mkdir /tmp/adsp-init.once
printf '%s\n' "$$" > /tmp/adsp-init.pid
printf 'BOOT_ID='
cat /proc/sys/kernel/random/boot_id
trap snapshot EXIT
dmesg > /tmp/adsp-dmesg-before.log
/tmp/qmi-qrtr list > /tmp/adsp-services-before.log
ip link show > /tmp/adsp-links-before.log
start_at=$(uptime_s)
sample
test "$mode" = 5

start_at=$(uptime_s)
printf 'START_ADSP uptime=%s\n' "$start_at"
/tmp/rprocstart "$A/state" start
limit=$(( start_at + 30 ))
while [ "$(cat "$A/state")" != running ]; do
  if [ "$(uptime_s)" -ge "$limit" ]; then
    printf 'ERROR: ADSP did not reach running within 30 seconds\n'
    exit 1
  fi
  sleep 1
done
printf 'ADSP_RUNNING uptime=%s\n' "$(uptime_s)"
/tmp/qmi-qrtr list > /tmp/adsp-services-started.log
/tmp/qmi-qrtr-next servreg-state 74 msm/adsp/audio_pd > /tmp/adsp-audio-state.log 2>&1 || true
next=$(uptime_s)
while :; do
  sample
  if [ "$mode" = 0 ]; then
    printf 'ONLINE uptime=%s\n' "$(uptime_s)"
    exit 0
  fi
  if [ "$(cat "$M/state")" != running ] || [ "$(cat "$A/state")" != running ]; then
    printf 'STOP_OBSERVATION: unexpected remoteproc state\n'
    exit 1
  fi
  if [ "$(( $(uptime_s) - start_at ))" -ge 660 ]; then break; fi
  next=$(( next + 15 ))
  delay=$(( next - $(uptime_s) ))
  if [ "$delay" -gt 0 ]; then sleep "$delay"; fi
done
printf 'RESULT: no ONLINE transition in 660 seconds after ADSP start\n'
