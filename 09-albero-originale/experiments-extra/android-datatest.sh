#!/system/bin/sh
echo "== BEFORE mobile_data=$(settings get global mobile_data)"
echo "== BEFORE datastate: $(dumpsys telephony.registry | grep -m1 mDataConnectionState)"
echo "== BEFORE addrs:"
ip -4 addr show | grep -E 'rmnet|inet ' | head -20
ip route
echo "== clearing radio log"
logcat -b radio -c
echo "== enabling data"
svc data enable
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18; do
  sleep 5
  st=$(dumpsys telephony.registry | grep -m1 mDataConnectionState | tr -d ' ')
  link=$(ip -o -4 addr show 2>/dev/null | grep rmnet | head -2 | tr '\n' ';')
  echo "t=$((i*5))s $st link=[$link]"
  if [ -n "$link" ]; then break; fi
done
echo "== AFTER addrs:"
ip -4 addr show | grep -E 'rmnet|inet ' | head -30
echo "== AFTER routes:"
ip route
echo "== AFTER /proc/net/dev:"
cat /proc/net/dev | grep -E 'rmnet'
echo "== saving radio log"
logcat -b radio -d -t 3000 > /data/local/tmp/radio-data.log
wc -l /data/local/tmp/radio-data.log
echo "== END"
