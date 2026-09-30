#!/bin/sh
# Read-only state capture before the authorized slot switch.
set -eu
test "$(cat /proc/sys/kernel/random/boot_id)" = 2ca42e08-8c9a-4d61-8ae3-2b632149d26a
printf 'UPTIME='
cat /proc/uptime
printf 'BOOT_ID='
cat /proc/sys/kernel/random/boot_id
/tmp/qmi-qrtr raw 2 0001002d000000
/tmp/qmi-qrtr list
/tmp/rmnet-inspect
ip addr show
ip route show
ip rule show
cat /proc/modules
for r in /sys/class/remoteproc/remoteproc*; do
  printf '%s name=%s state=%s\n' "$r" "$(cat "$r/name")" "$(cat "$r/state")"
done
printf 'LOGGER_PID='
cat /tmp/crashlog-dump.pid
cat /proc/25710/status
dmesg
