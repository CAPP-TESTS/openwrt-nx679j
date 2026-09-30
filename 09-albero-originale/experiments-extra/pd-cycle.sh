#!/bin/sh
# Run only after the handoff boot prerequisites and locator lookup pass.
# Timing follows MODEM-HANDOFF-20260919-2.md: protective restart at 86 s.
set -eu
R=/sys/class/remoteproc/remoteproc3/state
uptime_s() { awk '{print int($1)}' /proc/uptime; }
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
  services=$(/tmp/qmi-qrtr list | grep -c service=)
  printf 'SAMPLE phase=%s uptime=%s elapsed=%s mode=%s services=%s state=%s\n' \
    "$phase" "$(uptime_s)" "$(( $(uptime_s) - start_at ))" \
    "${mode:-unknown}" "$services" "$(cat "$R")"
  printf '%s\n' "$reply"
}

test -b /proc/1/root/dev/rd
grep -q '^PARTNAME=rawdump$' /sys/block/sda/sda11/uevent
pidof rmtfs >/dev/null
pidof tqftpserv >/dev/null
/tmp/qmi-qrtr lookup 14 | grep 'service=14 '
/tmp/qmi-qrtr lookup 64 | grep 'service=64 '
printf 'BOOT_ID='
cat /proc/sys/kernel/random/boot_id
dmesg > /tmp/pd-dmesg-before.log
/tmp/dspawn /tmp/crashlog-pd.log /bin/sh /tmp/crashlog-dump.sh
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
    dmesg > /tmp/pd-dmesg-after.log
    exit 0
  fi
  if [ "$(( $(uptime_s) - start_at ))" -ge 660 ]; then break; fi
  next=$(( next + 15 ))
  delay=$(( next - $(uptime_s) ))
  if [ "$delay" -gt 0 ]; then sleep "$delay"; fi
done
dmesg > /tmp/pd-dmesg-after.log
printf 'RESULT: no ONLINE transition in 660 seconds after protective restart\n'
