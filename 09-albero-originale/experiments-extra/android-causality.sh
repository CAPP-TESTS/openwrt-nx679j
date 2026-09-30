#!/system/bin/sh
echo "== uio0 identity:"
cat /sys/class/uio/uio0/name 2>/dev/null
ls /sys/class/uio/uio0/maps/map0/ 2>/dev/null
cat /sys/class/uio/uio0/maps/map0/addr 2>/dev/null; cat /sys/class/uio/uio0/maps/map0/size 2>/dev/null
echo "== BASELINE svc:"
/data/local/tmp/qmi-qrtr list 2>/dev/null > /data/local/tmp/svc-baseline.txt
grep -c service= /data/local/tmp/svc-baseline.txt
echo "== stop tftp_server + rmt_storage:"
setprop ctl.stop vendor.tftp_server 2>/dev/null
setprop ctl.stop vendor.rmt_storage 2>/dev/null
sleep 3
echo "pids: $(pidof tftp_server rmt_storage)"
echo "== stop modem:"
/data/local/tmp/rprocstart /sys/class/remoteproc/remoteproc4/state stop
sleep 8
echo "state=$(cat /sys/class/remoteproc/remoteproc4/state)"
echo "== start modem (daemons stopped):"
/data/local/tmp/rprocstart /sys/class/remoteproc/remoteproc4/state start
sleep 25
echo "state=$(cat /sys/class/remoteproc/remoteproc4/state)"
/data/local/tmp/qmi-qrtr list 2>/dev/null > /data/local/tmp/svc-nodaemons.txt
echo "svc after restart: $(grep -c service= /data/local/tmp/svc-nodaemons.txt)"
grep -E 'service=(1|2|3) ' /data/local/tmp/svc-nodaemons.txt
echo "== restart daemons:"
setprop ctl.start vendor.tftp_server 2>/dev/null
setprop ctl.start vendor.rmt_storage 2>/dev/null
sleep 4
echo "pids: $(pidof tftp_server rmt_storage)"
echo END
