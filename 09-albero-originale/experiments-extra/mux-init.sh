#!/bin/sh
# Stock Android WDS Bind Mux trace: endpoint4:1/mux1; rmnet UAPI as documented.
# Only creates/raises the data link and notifies IPA. No WDS, IP or route.
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
  dmesg > /tmp/mux-dmesg-after.log
  /tmp/qmi-qrtr list > /tmp/mux-services-after.log || true
  ip addr show > /tmp/mux-addresses-after.log
  ip route show > /tmp/mux-routes-after.log
  /tmp/rmnet-inspect > /tmp/mux-links-after.log || true
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

grep -q '^RESULT: no ONLINE transition in 660 seconds after WDA QMAP setup$' /tmp/wda-init.log
read -r previous_pid < /tmp/wda-init.pid
if active_pid "$previous_pid"; then
  printf 'ERROR: WDA observation is still active\n'
  exit 1
fi
grep -q '^\[wda\] VERIFIED raw-IP=2 UL-QMAP=5 DL-QMAP=5 QoS=0$' /tmp/wda-set.log
read -r dpm_pid < /tmp/dpm-session.pid
active_pid "$dpm_pid"
grep -q '^\[dpm\] OPENED endpoint=4:1 rx=2 tx=23 ' /tmp/dpm-session.log
if grep -q '^\[dpm\] CLOSING ' /tmp/dpm-session.log; then exit 1; fi
# Same boot and DPM deadline as wda-init.sh; leave a 90s margin.
test "$(cat /proc/sys/kernel/random/boot_id)" = 2ca42e08-8c9a-4d61-8ae3-2b632149d26a
test "$(uptime_s)" -lt "$((32590 + 3600 - 660 - 90))"
test "$(cat "$M/name")" = 4080000.remoteproc-mss
test "$(cat "$M/state")" = running
test "$(cat "$A/state")" = running
test -x /tmp/rmnet-link
test -x /tmp/rmnet-config
test -d /sys/class/net/rmnet_ipa0
test ! -d /sys/class/net/rmnet_data0
pidof rmtfs >/dev/null
pidof tqftpserv >/dev/null
/tmp/qmi-qrtr lookup 64 | grep 'service=64 '
/tmp/qmi-qrtr lookup 49 | grep 'service=49 version=1 instance=1 node=1 '
read -r logger_pid < /tmp/crashlog-dump.pid
active_pid "$logger_pid"
tr '\000' '\n' < "/proc/$logger_pid/cmdline" | grep -qx '/tmp/crashlog-dump.sh'
mkdir /tmp/mux-init.once
printf '%s\n' "$$" > /tmp/mux-init.pid
printf 'BOOT_ID='
cat /proc/sys/kernel/random/boot_id
trap snapshot EXIT
dmesg > /tmp/mux-dmesg-before.log
/tmp/qmi-qrtr list > /tmp/mux-services-before.log
/tmp/rmnet-inspect > /tmp/mux-links-before.log
/tmp/qmi-qrtr-qmap wda-get 4 1 > /tmp/mux-wda-before.log 2>&1
start_at=$(uptime_s)
sample
test "$mode" = 5
printf 'CREATE_MUX uptime=%s\n' "$(uptime_s)"
/tmp/rmnet-link rmnet_ipa0 rmnet_data0 1 > /tmp/mux-create.log 2>&1
cat /tmp/mux-create.log
/tmp/rmnet-inspect > /tmp/mux-links-created.log
parent=$(cat /sys/class/net/rmnet_ipa0/ifindex)
grep -E "^ifindex=[0-9]+ name=rmnet_data0 parent=$parent flags=0x[0-9a-f]+ kind=rmnet mux_id=1$" /tmp/mux-links-created.log
if /tmp/rmnet-config rmnet_ipa0 notify-mux 1 rmnet_data0 > /tmp/mux-notify.log 2>&1; then
  cat /tmp/mux-notify.log
else
  cat /tmp/mux-notify.log
  printf 'RESULT: mux notification failed; link left DOWN; no retry\n'
  exit 1
fi
ip link set rmnet_ipa0 up
ip link set rmnet_data0 up
printf 'MUX_READY uptime=%s\n' "$(uptime_s)"
snapshot
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
printf 'RESULT: no ONLINE transition in 660 seconds after rmnet mux setup\n'
