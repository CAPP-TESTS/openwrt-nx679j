#!/bin/sh
# Existing stock-UAPI/trace-derived data chain; no new radio setters or restart.
# Keeps DPM and WDS clients alive for a bounded 3600-second diagnostic session.
find_mss_state() {
  for name in /sys/class/remoteproc/remoteproc*/name; do
    if [ -r "$name" ] && [ "$(cat "$name")" = 4080000.remoteproc-mss ]; then
      printf '%s/state\n' "${name%/name}"
      return 0
    fi
  done
  return 1
}
set -eu
uptime_s() { awk '{print int($1)}' /proc/uptime; }
stamp() { printf '%s uptime=%s\n' "$1" "$(cat /proc/uptime)"; }
active_pid() {
  test -r "/proc/$1/status" || return 1
  test "$(awk '/^State:/ {print $2}' "/proc/$1/status")" != Z
}
wait_log() {
  pattern=$1 logfile=$2 client=$3 budget=$4
  deadline=$(( $(uptime_s) + budget ))
  while ! grep -q "$pattern" "$logfile"; do
    if ! active_pid "$client" || [ "$(uptime_s)" -ge "$deadline" ]; then
      cat "$logfile"
      printf 'ERROR: client=%s did not reach %s\n' "$client" "$pattern"
      return 1
    fi
    sleep 1
  done
}
snapshot() {
  result=$?
  trap - EXIT
  set +e
  dmesg > /tmp/data-dmesg-after.log
  /tmp/rmnet-inspect > /tmp/data-links-after.log
  ip addr show > /tmp/data-addresses-after.log
  ip route show > /tmp/data-routes-after.log
  printf 'DATA_SETUP_EXIT=%s UPTIME=%s\n' "$result" "$(cat /proc/uptime)"
  exit "$result"
}
test "$(cat /proc/sys/kernel/random/boot_id)" = "$(cat /tmp/observed-bootstrap.boot-id)"
R=$(find_mss_state)
test "$(cat "$R")" = running
read -r logger < /tmp/crashlog-dump.pid
active_pid "$logger"
test -s /tmp/crashlog-slot-number
test ! -d /sys/class/net/rmnet_ipa0
test ! -d /sys/class/net/rmnet_data0
test ! -e /tmp/dpm-session.pid
test ! -e /tmp/wds-session.pid
mkdir /tmp/data-observed.once
printf '%s\n' "$$" > /tmp/data-observed.pid
trap snapshot EXIT
/tmp/qmi-qrtr raw 2 0001002d000000 > /tmp/data-mode-before.log
grep -q 'TLV 0x01 len=1  u8=0' /tmp/data-mode-before.log
stamp IPA_TRIGGER
/tmp/ipa-trigger
deadline=$(( $(uptime_s) + 45 ))
while :; do
  dmesg > /tmp/data-ipa-dmesg.log
  if grep -q 'QMI_IPA_INIT_MODEM_DRIVER_REQ_V01 response received' /tmp/data-ipa-dmesg.log; then break; fi
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
stamp DPM_OPEN
/tmp/qmi-qrtr-observed dpm-session 4 1 2 23 3600 > /tmp/dpm-session.log 2>&1 &
dpm=$!
printf '%s\n' "$dpm" > /tmp/dpm-session.pid
wait_log '^\[dpm\] OPENED endpoint=4:1 rx=2 tx=23 ' /tmp/dpm-session.log "$dpm" 15
cat /tmp/dpm-session.log
stamp PIPE_SETUP
/tmp/rmnet-config rmnet_ipa0 pipes > /tmp/pipe-ioctl.log 2>&1
cat /tmp/pipe-ioctl.log
stamp WDA_SETUP
/tmp/qmi-qrtr-observed wda-qmap 4 1 > /tmp/wda-set.log 2>&1
cat /tmp/wda-set.log
stamp MUX_SETUP
/tmp/rmnet-link rmnet_ipa0 rmnet_data0 1 > /tmp/mux-create.log 2>&1
/tmp/rmnet-inspect > /tmp/data-links-created.log
parent=$(cat /sys/class/net/rmnet_ipa0/ifindex)
grep -E "^ifindex=[0-9]+ name=rmnet_data0 parent=$parent flags=0x[0-9a-f]+ kind=rmnet mux_id=1$" /tmp/data-links-created.log
/tmp/rmnet-config rmnet_ipa0 notify-mux 1 rmnet_data0 > /tmp/mux-notify.log 2>&1
cat /tmp/mux-notify.log
ip link set rmnet_ipa0 up
ip link set rmnet_data0 up
active_pid "$dpm"
stamp WDS_START
/tmp/qmi-qrtr-observed wds-session internet.it 4 1 1 3600 > /tmp/wds-session.log 2>&1 &
wds=$!
printf '%s\n' "$wds" > /tmp/wds-session.pid
wait_log '^\[session\] HOLDING ' /tmp/wds-session.log "$wds" 140
cat /tmp/wds-session.log
active_pid "$dpm"
active_pid "$wds"
stamp DATA_BEARER_READY
