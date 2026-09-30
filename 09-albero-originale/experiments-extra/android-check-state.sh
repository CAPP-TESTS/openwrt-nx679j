#!/system/bin/sh
echo "state4=$(cat /sys/class/remoteproc/remoteproc4/state 2>&1)"
echo "rprocstart.status: $(cat /tmp/rprocstart.status 2>/dev/null)"
echo "svc=$(/data/local/tmp/qmi-qrtr list 2>/dev/null | grep -c service=)"
/data/local/tmp/qmi-qrtr list 2>/dev/null | grep -E 'service=(1|2|3) |service=4096 |service=14 ' | head -8
echo "pids: $(pidof tftp_server rmt_storage)"
echo "== dmesg modem/mss:"
dmesg | grep -iE 'remoteproc|mss|modem|q6v5' | tail -12
echo END
