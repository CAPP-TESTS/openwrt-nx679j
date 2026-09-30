#!/system/bin/sh
# Trace qcrilNrd + qti and sample DMS mode while restarting the modem.
export PATH=/system/bin:/system/xbin:$PATH
rm -f /data/local/tmp/trace-qcril.log /data/local/tmp/trace-qti.log /data/local/tmp/mode.log
for p in $(pidof qmi-trace); do kill -9 $p 2>/dev/null; done
Q=$(pidof qcrilNrd); QTI=$(pidof qti)
echo "qcril=$Q qti=$QTI"
for p in $Q; do
  setsid /data/local/tmp/qmi-trace $p 300 >> /data/local/tmp/trace-qcril.log 2>&1 < /dev/null &
done
if [ -n "$QTI" ]; then
  setsid /data/local/tmp/qmi-trace $QTI 300 >> /data/local/tmp/trace-qti.log 2>&1 < /dev/null &
fi
setsid sh /data/local/tmp/mode-sampler.sh /data/local/tmp/mode.log 300 < /dev/null > /dev/null 2>&1 &
sleep 3
echo "== restarting modem (rproc4) $(date +%H:%M:%S)"
echo stop > /sys/class/remoteproc/remoteproc4/state && echo "stopped ok"
sleep 6
echo start > /sys/class/remoteproc/remoteproc4/state && echo "started ok"
echo "state=$(cat /sys/class/remoteproc/remoteproc4/state)"
