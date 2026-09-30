#!/bin/sh
# Resume the prepared IPA/DPM state after a userspace preflight failure.
# No IPA trigger, egress, WDA, mux, WDS, packet TX, clock controls, or ping.
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
stamp() {
  line=$(printf '%s uptime=%s\n' "$1" "$(cat /proc/uptime)")
  printf '%s' "$line"
  printf '%s' "$line" >> /tmp/ipa-ingress-resume.log
}
active_pid() {
  test -r "/proc/$1/status" || return 1
  test "$(awk '/^State:/ {print $2}' "/proc/$1/status")" != Z
}
test "$(cat /proc/sys/kernel/random/boot_id)" = "$(cat /tmp/observed-bootstrap.boot-id)"
test -d /tmp/dms-observed-openwrt.once
grep -q 'TLV 0x01 len=1  u8=0' /tmp/dms-observed-fresh-after.log
test "$(cat "$R")" = running
read -r logger < /tmp/crashlog-dump.pid
active_pid "$logger"
test -d /sys/class/net/rmnet_ipa0
test ! -d /sys/class/net/rmnet_data0
test -x /tmp/rmnet-config-agg8192
grep -q ' /sys/kernel/debug debugfs ' /proc/mounts
test -w /sys/kernel/debug/ipa/enable_low_prio_print
test -r /sys/kernel/debug/ipc_logging/ipa/log
test -r /sys/kernel/debug/ipc_logging/ipa_low/log
test -r /sys/kernel/debug/ipc_logging/gsi/log
grep -qx 'driver=rmnet_ipa0' /tmp/data-endpoints.log
grep -qx 'endpoint=1 (0x1)' /tmp/data-endpoints.log
grep -qx 'pipes consumer=2 producer=23' /tmp/data-endpoints.log
mkdir /tmp/ipa-ingress-resume.once
printf '%s\n' "$$" > /tmp/ipa-ingress-resume.pid
: > /tmp/ipa-ingress-resume.log
printf '1\n' > /sys/kernel/debug/ipa/enable_low_prio_print
/tmp/dspawn /tmp/ipa-ipc-resume-collector.log /bin/sh /tmp/ipa-ipc-collect.sh
deadline=$(( $(awk '{print int($1)}' /proc/uptime) + 5 ))
while [ ! -e /tmp/ipa-ipc-collector.ready ]; do
  test "$(awk '{print int($1)}' /proc/uptime)" -lt "$deadline"
  sleep 1
done
read -r collector < /tmp/ipa-ipc-collector.pid
active_pid "$collector"
stamp DPM_OPEN
/tmp/qmi-qrtr-observed dpm-session 4 1 2 23 3600 > /tmp/dpm-session.log 2>&1 &
dpm=$!
printf '%s\n' "$dpm" > /tmp/dpm-session.pid
deadline=$(( $(awk '{print int($1)}' /proc/uptime) + 15 ))
while ! grep -q '^\[dpm\] OPENED endpoint=4:1 rx=2 tx=23 ' /tmp/dpm-session.log; do
  active_pid "$dpm"
  test "$(awk '{print int($1)}' /proc/uptime)" -lt "$deadline"
  sleep 1
done
stamp BEFORE_INGRESS_ONLY
/bin/sh /tmp/rawdump-checkpoint.sh 16 before_ingress_only /tmp/ipa-ingress-resume.log
/tmp/rmnet-config-agg8192 rmnet_ipa0 ingress > /tmp/ipa-ingress.log 2>&1 &
ingress=$!
printf '%s\n' "$ingress" > /tmp/ipa-ingress.pid
result=0
wait "$ingress" || result=$?
cat /tmp/ipa-ingress.log
printf 'INGRESS_EXIT=%s uptime=%s\n' "$result" "$(cat /proc/uptime)" | tee -a /tmp/ipa-ingress-resume.log
dmesg > /tmp/ipa-ingress-dmesg-after.log
/bin/sh /tmp/rawdump-checkpoint.sh 17 after_ingress_only /tmp/ipa-ingress-resume.log
: > /tmp/ipa-ingress.finished
test "$result" = 0
grep -qx 'INGRESS accepted' /tmp/ipa-ingress.log
: > /tmp/ipa-ingress.done
stamp INGRESS_ONLY_COMPLETE
