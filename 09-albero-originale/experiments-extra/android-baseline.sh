#!/system/bin/sh
# Read-only baseline; never changes radio, services, SIM, routing or firmware.
BASE=${1:?absolute tool directory required}
case "$BASE" in /*) ;; *) exit 2;; esac
export PATH=/system/bin:/system/xbin:$PATH
printf 'BEGIN_BASELINE uptime='
cat /proc/uptime
printf 'BOOT_ID='
cat /proc/sys/kernel/random/boot_id
for property in ro.boot.slot_suffix sys.boot_completed gsm.network.type \
  gsm.operator.alpha gsm.sim.state; do
  printf '%s=%s\n' "$property" "$(getprop "$property")"
done
getenforce
id
printf '\n-- remoteproc identity/state --\n'
for r in /sys/class/remoteproc/remoteproc*; do
  printf '%s name=%s state=%s\n' "$r" "$(cat "$r/name")" "$(cat "$r/state")"
done
printf '\n-- DMS Get Operating Mode, transaction 1 --\n'
"$BASE/qmi-qrtr" raw 2 0001002d000000
printf '\n-- QRTR services --\n'
"$BASE/qmi-qrtr" list
printf '\n-- links, addresses, all route tables, policy rules --\n'
"$BASE/rmnet-inspect"
ip addr show
ip -4 route show table all
ip -6 route show table all
ip rule show
cat /proc/net/dev
printf '\n-- radio state --\n'
settings get global airplane_mode_on
settings get global mobile_data
dumpsys -t 5 telephony.registry |
  grep -E 'mServiceState=|mDataConnectionState=|mDataConnectionNetworkType=|mPreciseDataConnectionStates=|mDataConnectionLinkProperties='
printf '\n-- vendor processes and socket inventory --\n'
getprop | grep -E '^\[init\.svc\.' | grep -iE 'qti|qcril|netmgr|qmi|ril|modem|tftp|rmt'
for name in qcrilNrd netmgrd qmipriod qti tftp_server rmt_storage; do
  for p in $(pidof "$name"); do
    printf 'PROCESS name=%s pid=%s exe=%s\n' "$name" "$p" "$(readlink "/proc/$p/exe")"
    grep -E '^(Name|State|Pid|PPid|TracerPid|Threads):' "/proc/$p/status"
    ls -l "/proc/$p/fd"
  done
done
cat /proc/net/unix
printf '\nEND_BASELINE uptime='
cat /proc/uptime
