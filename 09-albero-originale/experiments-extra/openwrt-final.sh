#!/bin/sh
# Full OpenWrt modem bring-up + patient wait for ONLINE.
echo "== 1. sharedmem+uio:"
/tmp/finitmod /proc/1/root/lib/modules/msm_sharedmem.ko
sleep 1
U=$(cat /sys/class/uio/uio0/dev 2>/dev/null)
echo "uio0 dev=$U"
[ -e /dev/uio0 ] || mknod /dev/uio0 c ${U%:*} ${U#*:}
echo "== 2. efs:"
mkdir -p /tmp/efs
for p in 2:modemst1 3:modemst2 4:fsg 5:fsc; do
  N=${p#*:}
  M=$(cat /sys/block/sdf/sdf${p%:*}/dev 2>/dev/null)
  [ -n "$M" ] && [ ! -e /tmp/efs/$N ] && mknod /tmp/efs/$N b ${M%:*} ${M#*:}
done
ls /tmp/efs/ | tr '\n' ' '; echo
echo "== 3. rmtfs:"
/tmp/dspawn /tmp/rmtfs.log /tmp/rmtfs -P -o /tmp/efs -v
sleep 2
/tmp/qmi-qrtr lookup 14 2>&1 | tail -2
echo "== 4. tqftpserv:"
/tmp/dspawn /tmp/tqftpserv.log /tmp/tqftpserv -d
sleep 1
echo "== 5. crashlog:"
/tmp/dspawn /tmp/crashlog.log /tmp/crashlog-dump.sh
echo "== 6. qrtr-smd:"
/tmp/finitmod /proc/1/root/lib/modules/qrtr-smd.ko
echo "== 7. modem start:"
/tmp/rprocstart /sys/class/remoteproc/remoteproc3/state start
sleep 5
echo "modem started at uptime $(awk '{print $1}' /proc/uptime)"
echo "== 8. PATIENT MODE WATCH (sample every 20s):"
i=0
while [ $i -lt 26 ]; do
  i=$((i+1))
  sleep 20
  M=$(/tmp/qmi-qrtr raw 2 00$(printf %02x $((i+16)))012d000000 2>/dev/null | grep -o 'u8=[0-9]*' | head -1)
  echo "$(date +%H:%M:%S) t=$((i*20))s $M"
  case "$M" in *u8=0*) echo "ONLINE-REACHED"; break;; esac
done
echo "== tftp activity:"
grep -vE 'del_client' /tmp/tqftpserv.log | tail -8
echo END
