#!/bin/sh
# Reproduce established EFS/MCFG/PD prerequisites on a fresh OpenWrt boot.
# No DMS setter, IPA/data configuration, firmware modification or replay.
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
uptime_s() { awk '{print int($1)}' /proc/uptime; }
stamp() { printf '%s uptime=%s\n' "$1" "$(cat /proc/uptime)"; }
wait_state() {
  wanted=$1
  deadline=$(( $(uptime_s) + 30 ))
  while [ "$(cat "$R")" != "$wanted" ]; do
    if [ "$(uptime_s)" -ge "$deadline" ]; then
      printf 'ERROR: MSS did not reach %s\n' "$wanted"
      return 1
    fi
    sleep 1
  done
}
test "$(cat "$R")" = offline
test -b /proc/1/root/dev/rd
grep -q '^PARTNAME=rawdump$' /sys/block/sda/sda11/uevent
test -r /rfs/readonly/firmware/image/modem_pr/so/848_0_0.mbn
test -r /proc/1/root/lib/firmware/modemr.jsn
for tool in qmi-qrtr rprocstart finitmod dspawn rmtfs tqftpserv pd-mapper; do
  test -x "/tmp/$tool"
done
if pidof rmtfs >/dev/null; then
  printf 'ERROR: an rmtfs instance already exists\n'
  exit 1
fi
mkdir /tmp/observed-bootstrap.once
printf '%s\n' "$$" > /tmp/observed-bootstrap.pid
cat /proc/sys/kernel/random/boot_id > /tmp/observed-bootstrap.boot-id
stamp BOOTSTRAP_BEGIN

if [ ! -r /sys/class/uio/uio0/dev ]; then
  /tmp/finitmod /proc/1/root/lib/modules/msm_sharedmem.ko
fi
test "$(cat /sys/class/uio/uio0/name)" = rmtfs
dev=$(cat /sys/class/uio/uio0/dev)
test -e /dev/uio0 || mknod /dev/uio0 c "${dev%:*}" "${dev#*:}"
test -c /dev/uio0
mkdir -p /tmp/efs
for pair in 2:modemst1 3:modemst2 4:fsg 5:fsc; do
  part=${pair%:*}
  label=${pair#*:}
  grep -qx "PARTNAME=$label" "/sys/block/sdf/sdf$part/uevent"
  dev=$(cat "/sys/block/sdf/sdf$part/dev")
  test -e "/tmp/efs/$label" || mknod "/tmp/efs/$label" b "${dev%:*}" "${dev#*:}"
done
/tmp/dspawn /tmp/rmtfs.log /tmp/rmtfs -P -o /tmp/efs -v
sleep 2
/tmp/qmi-qrtr lookup 14 | grep 'service=14 '
mkdir -p /rfs/readwrite/ota_firewall /var/lib/tqftpserv
/tmp/dspawn /tmp/tqftpserv.log /tmp/tqftpserv -d
/tmp/dspawn /tmp/pd-mapper.log /tmp/pd-mapper
sleep 2
/tmp/qmi-qrtr lookup 4096 | grep 'service=4096 '
/tmp/qmi-qrtr lookup 64 | grep 'service=64 '
/tmp/dspawn /tmp/crashlog-observed.log /tmp/crashlog-dump.sh
for unused in 1 2 3 4 5 6 7 8 9 10; do
  if [ -s /tmp/crashlog-slot-number ]; then break; fi
  sleep 1
done
test -s /tmp/crashlog-slot-number
read -r logger < /tmp/crashlog-dump.pid
test -r "/proc/$logger/status"
test "$(awk '/^State:/ {print $2}' "/proc/$logger/status")" != Z
if [ ! -d /sys/module/qrtr_smd ]; then
  /tmp/finitmod /proc/1/root/lib/modules/qrtr-smd.ko
fi

# Do not put fallible queries between first start and the protective stop.
first=$(uptime_s)
stamp MSS_FIRST_START
/tmp/rprocstart "$R" start
# v99: ripristinato il workaround stop/restart come passo OBBLIGATORIO (la v98, che lo rendeva
# un fallback, non ha potuto essere misurata: boot con race MSS severa). Comportamento noto-buono.
qmi_ready() { /tmp/qmi-qrtr raw 2 0001002d000000 2>/dev/null | grep -q 'msg_id=0x002d'; }
t=0
while [ $t -lt 86 ]; do
  [ $t -ge 10 ] && qmi_ready && break
  sleep 1; t=$((t+1))
done
stamp "MSS_FIRST_WAIT t=${t}s"
stamp MSS_PROTECTIVE_STOP
/tmp/rprocstart "$R" stop
wait_state offline
stamp MSS_SECOND_START
/tmp/rprocstart "$R" start
wait_state running
t=0
while [ $t -lt 15 ]; do
  qmi_ready && break
  sleep 1; t=$((t+1))
done
stamp "POST_START_WAIT t=${t}s"
stamp POST_PROTECTIVE_RESTART
/tmp/qmi-qrtr list > /tmp/observed-bootstrap-services.log
/tmp/qmi-qrtr raw 2 0001002d000000 > /tmp/observed-bootstrap-mode.log
cat /tmp/observed-bootstrap-mode.log
dmesg > /tmp/observed-bootstrap-dmesg.log
stamp READY_FOR_OBSERVED_DMS_TEST
