#!/bin/sh
# Test ordering only: IPA is ready before MSS boot; keep EFS/MCFG/PD daemons.
# The protective restart at 86s follows the measured handoff recipe.
# Run only after ipa-init's complete negative 1200-second observation.
set -eu
R=/sys/class/remoteproc/remoteproc3/state
uptime_s() { awk '{print int($1)}' /proc/uptime; }
snapshot() {
  dmesg > /tmp/ipa-cycle-dmesg-after.log
  /tmp/qmi-qrtr list > /tmp/ipa-cycle-services-after.log || true
  ip link show > /tmp/ipa-cycle-links-after.log
}
wait_state() {
  wanted=$1
  limit=$(( $(uptime_s) + 30 ))
  while [ "$(cat "$R")" != "$wanted" ]; do
    if [ "$(uptime_s)" -ge "$limit" ]; then
      printf 'ERROR: remoteproc did not reach %s\n' "$wanted"
      exit 1
    fi
    sleep 1
  done
}
restart_modem() {
  printf 'STOP uptime=%s\n' "$(uptime_s)"
  /tmp/rprocstart "$R" stop
  sleep 6
  wait_state offline
  start_at=$(uptime_s)
  printf 'START uptime=%s\n' "$start_at"
  /tmp/rprocstart "$R" start
  wait_state running
}
sample() {
  reply=$(/tmp/qmi-qrtr raw 2 0001002d000000) || reply="query failed"
  mode=$(printf '%s\n' "$reply" | awk '/TLV 0x01 len=1/ {split($NF,a,"="); print a[2]}')
  services=$(/tmp/qmi-qrtr list | grep -c service=) || services=0
  printf 'SAMPLE phase=%s uptime=%s elapsed=%s mode=%s services=%s state=%s\n' \
    "$phase" "$(uptime_s)" "$(( $(uptime_s) - start_at ))" \
    "${mode:-unknown}" "$services" "$(cat "$R")"
  printf '%s\n' "$reply"
}

grep -q '^RESULT: no ONLINE transition in 1200 seconds after IPA trigger$' /tmp/ipa-init.log
test "$(cat "$R")" = running
test -d /sys/class/net/rmnet_ipa0
pidof rmtfs >/dev/null
pidof tqftpserv >/dev/null
/tmp/qmi-qrtr lookup 14 | grep 'service=14 '
/tmp/qmi-qrtr lookup 64 | grep 'service=64 '
/tmp/qmi-qrtr lookup 49 | grep 'service=49 version=1 instance=1 node=1 '
read -r logger_pid < /tmp/crashlog-dump.pid
kill -0 "$logger_pid"
tr '\000' '\n' < "/proc/$logger_pid/cmdline" | grep -qx '/tmp/crashlog-dump.sh'
mkdir /tmp/ipa-cycle.once
printf '%s\n' "$$" > /tmp/ipa-cycle.pid
printf 'BOOT_ID='
cat /proc/sys/kernel/random/boot_id
trap snapshot EXIT
dmesg > /tmp/ipa-cycle-dmesg-before.log
/tmp/qmi-qrtr list > /tmp/ipa-cycle-services-before.log
ip link show > /tmp/ipa-cycle-links-before.log
phase=precondition
start_at=$(uptime_s)
sample
test "$mode" = 5
sleep 4

restart_modem
phase=before-protective-restart
while [ "$(( $(uptime_s) - start_at ))" -lt 86 ]; do
  sample
  remaining=$(( start_at + 86 - $(uptime_s) ))
  if [ "$remaining" -gt 15 ]; then remaining=15; fi
  if [ "$remaining" -gt 0 ]; then sleep "$remaining"; fi
done
restart_modem
phase=observation
next=$start_at
while :; do
  sample
  if [ "$mode" = 0 ]; then
    printf 'ONLINE uptime=%s\n' "$(uptime_s)"
    exit 0
  fi
  if [ "$(cat "$R")" != running ]; then
    printf 'STOP_OBSERVATION: unexpected remoteproc state\n'
    exit 1
  fi
  if [ "$(( $(uptime_s) - start_at ))" -ge 660 ]; then break; fi
  next=$(( next + 15 ))
  delay=$(( next - $(uptime_s) ))
  if [ "$delay" -gt 0 ]; then sleep "$delay"; fi
done
printf 'RESULT: no ONLINE transition in 660 seconds after IPA-ready protective restart\n'
