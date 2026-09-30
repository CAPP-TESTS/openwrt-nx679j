#!/bin/sh
# One documented write to the stock IPA character device, no modem restart.
# ABI: refs/ipa-lineage20/ipa.c:8368-8470 and stock ipa3-write disassembly.
# Run after pd-cycle's complete 660-second negative observation and rawdump backup.
set -eu
R=/sys/class/remoteproc/remoteproc3/state
uptime_s() { awk '{print int($1)}' /proc/uptime; }
sample() {
  reply=$(/tmp/qmi-qrtr raw 2 0001002d000000) || reply="query failed"
  mode=$(printf '%s\n' "$reply" | awk '/TLV 0x01 len=1/ {split($NF,a,"="); print a[2]}')
  services=$(/tmp/qmi-qrtr list | grep -c service=) || services=0
  printf 'SAMPLE uptime=%s elapsed=%s mode=%s services=%s state=%s\n' \
    "$(uptime_s)" "$(( $(uptime_s) - start_at ))" \
    "${mode:-unknown}" "$services" "$(cat "$R")"
  printf '%s\n' "$reply"
  /tmp/qmi-qrtr lookup 49 || true
}

grep -q '^RESULT: no ONLINE transition in 660 seconds after protective restart$' /tmp/pd-cycle.log
test "$(cat "$R")" = running
pidof rmtfs >/dev/null
pidof tqftpserv >/dev/null
/tmp/qmi-qrtr lookup 14 | grep 'service=14 '
/tmp/qmi-qrtr lookup 64 | grep 'service=64 '
test -x /tmp/ipa-trigger
read -r logger_pid < /tmp/crashlog-dump.pid
kill -0 "$logger_pid"
tr '\000' '\n' < "/proc/$logger_pid/cmdline" | grep -qx '/tmp/crashlog-dump.sh'

read -r device_number < /sys/class/ipa/ipa/dev
mkdir /tmp/ipa-init.once

printf 'BOOT_ID='
cat /proc/sys/kernel/random/boot_id
printf 'IPA_DEVICE=%s\n' "$device_number"
dmesg > /tmp/ipa-dmesg-before.log
/tmp/qmi-qrtr list > /tmp/ipa-services-before.log
ip link show > /tmp/ipa-links-before.log
start_at=$(uptime_s)
sample
test "$mode" = 5
printf 'TRIGGER uptime=%s payload=ASCII_1 length=1\n' "$(uptime_s)"
# Allow the active rawdump logger to persist the pre-trigger marker.
sleep 4
if /tmp/ipa-trigger; then
  printf 'WRITE_RESULT=success uptime=%s\n' "$(uptime_s)"
else
  rc=$?
  printf 'WRITE_RESULT=failure status=%s uptime=%s\n' "$rc" "$(uptime_s)"
  dmesg > /tmp/ipa-dmesg-after.log
  exit "$rc"
fi
start_at=$(uptime_s)
next=$start_at
while :; do
  sample
  if [ "$mode" = 0 ]; then
    printf 'ONLINE uptime=%s\n' "$(uptime_s)"
    break
  fi
  if [ "$(cat "$R")" != running ]; then
    printf 'STOP_OBSERVATION: unexpected remoteproc state\n'
    break
  fi
  if [ "$(( $(uptime_s) - start_at ))" -ge 1200 ]; then
    printf 'RESULT: no ONLINE transition in 1200 seconds after IPA trigger\n'
    break
  fi
  next=$(( next + 15 ))
  delay=$(( next - $(uptime_s) ))
  if [ "$delay" -gt 0 ]; then sleep "$delay"; fi
done
dmesg > /tmp/ipa-dmesg-after.log
/tmp/qmi-qrtr list > /tmp/ipa-services-after.log
ip link show > /tmp/ipa-links-after.log
