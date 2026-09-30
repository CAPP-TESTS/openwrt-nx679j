#!/bin/sh
# crashlog-dump - persistent crash logger for the NX679J experiments.
# Writes dmesg + daemon-log snapshots into rawdump slots every 3s, so the last
# state before a whole-system reboot survives in flash.
#
# Slot i lives at byte offset (1024 + i*32) KiB of /dev/rd; slot size 32 KiB.
L=/proc/1/root/dev/rd
test -b "$L" || exit 1
grep -q '^PARTNAME=rawdump$' /sys/block/sda/sda11/uevent || exit 1
find_mss_state() {
  for name in /sys/class/remoteproc/remoteproc*/name; do
    if [ -r "$name" ] && [ "$(cat "$name")" = 4080000.remoteproc-mss ]; then
      printf '%s/state\n' "${name%/name}"
      return 0
    fi
  done
  return 1
}
MSS_STATE=$(find_mss_state)
printf '%s\n' "$$" > /tmp/crashlog-dump.pid
i=0
# Reserve slots 380..399 for synchronous operation-boundary checkpoints.
while [ $i -lt 380 ]; do
  {
    echo "slot=$i uptime=$(awk '{print $1}' /proc/uptime) boot=$(cat /proc/sys/kernel/random/boot_id) mss=$(cat "$MSS_STATE" 2>/dev/null)"
    echo "-- dmesg:"
    dmesg | grep -vE 'journal mirror|_count\)' | tail -30
    echo "-- rmtfs:"
    tail -6 /tmp/rmtfs.log 2>/dev/null
    echo "-- tqftpserv:"
    tail -4 /tmp/tqftpserv.log 2>/dev/null
    echo "-- pd-mapper:"
    tail -6 /tmp/pd-mapper.log 2>/dev/null
    echo "-- IPA experiment:"
    tail -6 /tmp/ipa-init.log 2>/dev/null
    echo "-- IPA-ready restart experiment:"
    tail -6 /tmp/ipa-cycle.log 2>/dev/null
    echo "-- ADSP experiment:"
    tail -6 /tmp/adsp-init.log 2>/dev/null
    echo "-- DPM experiment:"
    tail -6 /tmp/dpm-init.log 2>/dev/null
    tail -6 /tmp/dpm-session.log 2>/dev/null
    echo "-- IPA pipe experiment:"
    tail -6 /tmp/pipe-init.log 2>/dev/null
    tail -6 /tmp/pipe-ioctl.log 2>/dev/null
    echo "-- WDA experiment:"
    tail -6 /tmp/wda-init.log 2>/dev/null
    tail -6 /tmp/wda-set.log 2>/dev/null
    echo "-- rmnet mux experiment:"
    tail -6 /tmp/mux-init.log 2>/dev/null
    echo "-- observed-DMS bootstrap/test:"
    tail -6 /tmp/observed-bootstrap.log 2>/dev/null
    tail -12 /tmp/dms-observed-openwrt.log 2>/dev/null
    echo "-- online data setup/session:"
    tail -12 /tmp/data-observed.log 2>/dev/null
    tail -25 /tmp/wds-session.log 2>/dev/null
    tail -12 /tmp/cellular-verify.log 2>/dev/null
    tail -16 /tmp/cellular-staged.log 2>/dev/null
    echo "-- ingress diagnostic / IPC RAM:"
    tail -10 /tmp/ipa-ingress-diagnostic.log 2>/dev/null
    tail -6 /tmp/ipa-ingress.log 2>/dev/null
    tail -10 /tmp/ipa-ipc-ipa.log 2>/dev/null
    tail -16 /tmp/ipa-ipc-ipa_low.log 2>/dev/null
    tail -6 /tmp/ipa-ipc-gsi.log 2>/dev/null
    echo "-- qrtr svc count:"
    /tmp/qmi-qrtr list 2>/dev/null | grep -c service=
  } > /tmp/crashlog-slot.txt 2>&1
  test "$(wc -c < /tmp/crashlog-slot.txt)" -le 32768 || exit 1
  dd if=/tmp/crashlog-slot.txt of="$L" bs=32768 count=1 seek=$((32 + i)) conv=sync,notrunc,fsync 2>/dev/null || exit 1
  printf '%s\n' "$i" > /tmp/crashlog-slot-number
  i=$((i+1))
  sleep 3
done
