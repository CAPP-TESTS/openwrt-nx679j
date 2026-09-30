#!/bin/sh
# Stock cleanup audit: only before MSS/IPA/VND, never force or hot-unload.
# PERF depends on SHS; these five optional holders were measured in /proc/modules.
set -eu
expected=${1:?expected boot ID required}
test "$(cat /proc/sys/kernel/random/boot_id)" = "$expected"
test "$(cat /sys/class/remoteproc/remoteproc3/state)" = offline
test ! -e /tmp/observed-bootstrap.once
for net in /sys/class/net/rmnet*; do test ! -e "$net"; done
for module in rmnet_aps rmnet_perf_tether rmnet_perf rmnet_offload; do
  test "$(cat "/sys/module/$module/refcnt")" = 0
done
test "$(cat /sys/module/rmnet_shs/refcnt)" = 1
test -L /sys/module/rmnet_shs/holders/rmnet_perf
for module in rmnet_core rmnet_ctl ipam gsim; do
  test -d "/sys/module/$module"
done
mkdir /tmp/rmnet-base-isolation.once
printf 'BASE_ISOLATION_BEGIN boot=%s uptime=%s\n' "$expected" "$(cat /proc/uptime)"
# kmodloader scans builtins even for rmmod; the chroot has only 6.12 modules.
# Its documented LD_LIBRARY_PATH search accepts this empty, private directory.
# No module is loaded and no stock /lib/modules entry is changed.
module_root=/tmp/rmnet-kmodloader
mkdir -p "$module_root/modules/$(uname -r)"
cat /proc/modules > /tmp/rmnet-base-modules-before.log
/bin/sh /tmp/rawdump-checkpoint.sh 16 before_base_isolation /tmp/rmnet-base-isolation.log
index=0
for module in rmnet_aps rmnet_perf_tether rmnet_perf rmnet_offload rmnet_shs; do
  test "$(cat "/sys/module/$module/refcnt")" = 0
  printf 'REMOVE module=%s uptime=%s\n' "$module" "$(cat /proc/uptime)"
  /bin/sh /tmp/rawdump-checkpoint.sh "$index" "before_remove_$module" /tmp/rmnet-base-isolation.log
  result=0
  LD_LIBRARY_PATH="$module_root" /sbin/rmmod "$module" || result=$?
  printf 'REMOVE_EXIT module=%s result=%s uptime=%s\n' "$module" "$result" "$(cat /proc/uptime)"
  if [ "$result" != 0 ]; then
    /bin/sh /tmp/rawdump-checkpoint.sh "$((index + 1))" "failed_remove_$module" /tmp/rmnet-base-isolation.log
    exit "$result"
  fi
  test ! -d "/sys/module/$module"
  printf 'REMOVED module=%s\n' "$module"
  /bin/sh /tmp/rawdump-checkpoint.sh "$((index + 1))" "after_remove_$module" /tmp/rmnet-base-isolation.log
  index=$((index + 2))
done
for module in rmnet_core rmnet_ctl ipam gsim; do
  test -d "/sys/module/$module"
done
test "$(cat /sys/class/remoteproc/remoteproc3/state)" = offline
for net in /sys/class/net/rmnet*; do test ! -e "$net"; done
cat /proc/modules > /tmp/rmnet-base-modules-after.log
dmesg > /tmp/rmnet-base-dmesg-after.log
/bin/sh /tmp/rawdump-checkpoint.sh 17 after_base_isolation /tmp/rmnet-base-isolation.log
: > /tmp/rmnet-base-isolation.done
printf 'BASE_ISOLATION_COMPLETE uptime=%s\n' "$(cat /proc/uptime)"
