#!/bin/sh
# Isolate stock ingress/egress pipe creation after the completed DPM window.
# No WDA Set, interface/mux creation, WDS call, or MSS restart.
set -eu
M=/sys/class/remoteproc/remoteproc3
A=/sys/class/remoteproc/remoteproc0
uptime_s() { awk '{print int($1)}' /proc/uptime; }
active_pid() {
  test -r "/proc/$1/status" || return 1
  test "$(awk '/^State:/ {print $2}' "/proc/$1/status")" != Z || return 1
  kill -0 "$1"
}
snapshot() {
  dmesg > /tmp/pipe-dmesg-after.log
  /tmp/qmi-qrtr list > /tmp/pipe-services-after.log || true
  ip link show > /tmp/pipe-links-after.log
}
sample() {
  reply=$(/tmp/qmi-qrtr raw 2 0001002d000000) || reply="query failed"
  mode=$(printf '%s\n' "$reply" | awk '/TLV 0x01 len=1/ {split($NF,a,"="); print a[2]}')
  services=$(/tmp/qmi-qrtr list | grep -c service=) || services=0
  printf 'SAMPLE uptime=%s elapsed=%s mode=%s services=%s modem=%s adsp=%s\n' \
    "$(uptime_s)" "$(( $(uptime_s) - start_at ))" "${mode:-unknown}" \
    "$services" "$(cat "$M/state")" "$(cat "$A/state")"
  printf '%s\n' "$reply"
}

grep -q '^RESULT: no ONLINE transition in 660 seconds after DPM open$' /tmp/dpm-init.log
read -r previous_pid < /tmp/dpm-init.pid
if active_pid "$previous_pid"; then
  printf 'ERROR: DPM observation is still active\n'
  exit 1
fi
read -r dpm_pid < /tmp/dpm-session.pid
active_pid "$dpm_pid"
grep -q '^\[dpm\] OPENED endpoint=4:1 rx=2 tx=23 ' /tmp/dpm-session.log
if grep -q '^\[dpm\] CLOSING ' /tmp/dpm-session.log; then exit 1; fi
test "$(cat "$M/name")" = 4080000.remoteproc-mss
test "$(cat "$M/state")" = running
test "$(cat "$A/state")" = running
test -x /tmp/rmnet-config
test -d /sys/class/net/rmnet_ipa0
pidof rmtfs >/dev/null
pidof tqftpserv >/dev/null
/tmp/qmi-qrtr lookup 64 | grep 'service=64 '
/tmp/qmi-qrtr lookup 49 | grep 'service=49 version=1 instance=1 node=1 '
read -r logger_pid < /tmp/crashlog-dump.pid
active_pid "$logger_pid"
tr '\000' '\n' < "/proc/$logger_pid/cmdline" | grep -qx '/tmp/crashlog-dump.sh'
mkdir /tmp/pipe-init.once
printf '%s\n' "$$" > /tmp/pipe-init.pid
printf 'BOOT_ID='
cat /proc/sys/kernel/random/boot_id
trap snapshot EXIT
dmesg > /tmp/pipe-dmesg-before.log
/tmp/qmi-qrtr list > /tmp/pipe-services-before.log
ip link show > /tmp/pipe-links-before.log
/tmp/rmnet-query rmnet_ipa0 > /tmp/pipe-endpoints.log
cat /tmp/pipe-endpoints.log
/tmp/qmi-qrtr-dpm wda-get 4 1 > /tmp/pipe-wda-before.log 2>&1
start_at=$(uptime_s)
sample
test "$mode" = 5
printf 'CONFIGURE_PIPES uptime=%s\n' "$(uptime_s)"
if /tmp/rmnet-config rmnet_ipa0 pipes > /tmp/pipe-ioctl.log 2>&1; then
  cat /tmp/pipe-ioctl.log
else
  cat /tmp/pipe-ioctl.log
  printf 'RESULT: pipe setup incomplete; no retry or further configuration\n'
  exit 1
fi
printf 'PIPES_ACCEPTED uptime=%s\n' "$(uptime_s)"
/tmp/qmi-qrtr-dpm wda-get 4 1 > /tmp/pipe-wda-after.log 2>&1 || true
start_at=$(uptime_s)
next=$start_at
while :; do
  sample
  if [ "$mode" = 0 ]; then
    printf 'ONLINE uptime=%s\n' "$(uptime_s)"
    exit 0
  fi
  if ! active_pid "$dpm_pid" || [ "$(cat "$M/state")" != running ]; then
    printf 'STOP_OBSERVATION: DPM client or modem no longer active\n'
    exit 1
  fi
  if [ "$(( $(uptime_s) - start_at ))" -ge 660 ]; then break; fi
  next=$(( next + 15 ))
  delay=$(( next - $(uptime_s) ))
  if [ "$delay" -gt 0 ]; then sleep "$delay"; fi
done
printf 'RESULT: no ONLINE transition in 660 seconds after IPA pipe setup\n'
