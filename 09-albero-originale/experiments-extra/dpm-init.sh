#!/bin/sh
# Isolate DPM Open Port, after the completed ADSP observation.
# No MSS restart, WDA Set, pipe ioctl, mux creation, or WDS activation.
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
  dmesg > /tmp/dpm-dmesg-after.log
  /tmp/qmi-qrtr list > /tmp/dpm-services-after.log || true
  ip link show > /tmp/dpm-links-after.log
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

grep -q '^RESULT: no ONLINE transition in 660 seconds after ADSP start$' /tmp/adsp-init.log
read -r previous_pid < /tmp/adsp-init.pid
if active_pid "$previous_pid"; then
  printf 'ERROR: ADSP experiment is still active\n'
  exit 1
fi
test "$(cat "$M/name")" = 4080000.remoteproc-mss
test "$(cat "$M/state")" = running
test "$(cat "$A/state")" = running
test -x /tmp/qmi-qrtr-dpm
test -d /sys/class/net/rmnet_ipa0
pidof rmtfs >/dev/null
pidof tqftpserv >/dev/null
/tmp/qmi-qrtr lookup 64 | grep 'service=64 '
/tmp/qmi-qrtr lookup 49 | grep 'service=49 version=1 instance=1 node=1 '
/tmp/qmi-qrtr lookup 47 | grep 'service=47 version=1 instance=0 node=0 '
read -r logger_pid < /tmp/crashlog-dump.pid
active_pid "$logger_pid"
tr '\000' '\n' < "/proc/$logger_pid/cmdline" | grep -qx '/tmp/crashlog-dump.sh'
mkdir /tmp/dpm-init.once
printf '%s\n' "$$" > /tmp/dpm-init.pid
printf 'BOOT_ID='
cat /proc/sys/kernel/random/boot_id
trap snapshot EXIT
dmesg > /tmp/dpm-dmesg-before.log
/tmp/qmi-qrtr list > /tmp/dpm-services-before.log
ip link show > /tmp/dpm-links-before.log
/tmp/rmnet-query rmnet_ipa0 > /tmp/dpm-endpoints.log
cat /tmp/dpm-endpoints.log
# Require the measured stock endpoint tuple; never borrow another SoC's IDs.
grep -qx 'driver=rmnet_ipa0' /tmp/dpm-endpoints.log
grep -qx 'endpoint=1 (0x1)' /tmp/dpm-endpoints.log
grep -qx 'pipes consumer=2 producer=23' /tmp/dpm-endpoints.log
/tmp/qmi-qrtr-dpm wda-get 4 1 > /tmp/dpm-wda-before.log 2>&1 || true
start_at=$(uptime_s)
sample
test "$mode" = 5
printf 'OPEN_DPM uptime=%s endpoint=4:1 rx=2 tx=23 hold_s=3600\n' "$(uptime_s)"
spawn=$(/tmp/dspawn /tmp/dpm-session.log /tmp/qmi-qrtr-dpm dpm-session 4 1 2 23 3600)
printf '%s\n' "$spawn"
dpm_pid=$(printf '%s\n' "$spawn" | sed -n 's/.*pid=\([0-9][0-9]*\).*/\1/p')
test -n "$dpm_pid"
printf '%s\n' "$dpm_pid" > /tmp/dpm-session.pid
limit=$(( $(uptime_s) + 15 ))
while ! grep -q '^\[dpm\] OPENED ' /tmp/dpm-session.log; do
  if ! active_pid "$dpm_pid" || [ "$(uptime_s)" -ge "$limit" ]; then
    printf 'RESULT: DPM open not confirmed; no further configuration attempted\n'
    cat /tmp/dpm-session.log
    exit 1
  fi
  sleep 1
done
cat /tmp/dpm-session.log
/tmp/qmi-qrtr-dpm wda-get 4 1 > /tmp/dpm-wda-after.log 2>&1 || true
start_at=$(uptime_s)
next=$start_at
while :; do
  sample
  if [ "$mode" = 0 ]; then
    printf 'ONLINE uptime=%s DPM client remains open\n' "$(uptime_s)"
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
printf 'RESULT: no ONLINE transition in 660 seconds after DPM open\n'
printf 'DPM client retained as baseline until its 3600-second bound or SIGTERM\n'
