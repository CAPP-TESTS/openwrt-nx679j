#!/system/bin/sh
# Sample DMS operating mode + NAS registration (raw) every 3s.
LOG=$1
SECS=${2:-420}
i=0
while [ $i -lt $SECS ]; do
  i=$((i+3))
  T=$(date +%H:%M:%S)
  M=$(/data/local/tmp/qmi-qrtr raw 2 0001002d000000 2>/dev/null | grep -o 'u8=[0-9]*' | head -1)
  R=$(/data/local/tmp/qmi-qrtr raw 3 00020024000000 2>/dev/null | sed -n '2,4p' | tr -s ' ' | tr '\n' '|' | cut -c1-70)
  echo "$T $M reg:$R" >> $LOG
  sleep 3
done
echo "sampler done" >> $LOG
