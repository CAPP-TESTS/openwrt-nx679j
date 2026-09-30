#!/system/bin/sh
# Read-only enumeration for NX679J modem work.
echo "== DATE: $(date) slot=$(getprop ro.boot.slot_suffix) enforce=$(getenforce)"
echo
echo "== SERVICES (init.svc.*, filtered):"
getprop | grep -E '^\[init\.svc\.' | grep -iE 'qti|qcril|net|qmi|ril|modem|data|qmux'
echo
echo "== PROCESSES:"
ps -A -o PID,PPID,USER,NAME 2>/dev/null | grep -iE 'qti|qcril|netmgrd|qmipri|qmuxd|ril-|radio'
echo
echo "== VENDOR BINARIES:"
ls -l /vendor/bin/qti /vendor/bin/qcrilNrd /vendor/bin/netmgrd /vendor/bin/qmipriod /vendor/bin/qmuxd 2>&1
echo
echo "== /dev/socket listing:"
ls -la /dev/socket/ 2>&1 | head -80
echo "-- qmux_radio:"
ls -la /dev/socket/qmux_radio/ 2>&1
echo
echo "== /proc/net/unix (filtered):"
cat /proc/net/unix | grep -iE 'qmux|qmi|radio|rmnet|qti|mdm|smd|gbmux'
echo
echo "== DEV NODES:"
ls -la /dev/rmnet_ctrl* /dev/smdcntl* /dev/smd* /dev/glink* 2>&1
echo
echo "== INIT RC mentioning services:"
grep -rlE 'qcrilNrd|netmgrd|qmipriod|/vendor/bin/qti' /vendor/etc/init/ 2>/dev/null | head -20
echo
for p in $(pidof qti) $(pidof qcrilNrd) $(pidof qmipriod) $(pidof netmgrd); do
  echo "==== PID $p"
  echo "exe: $(readlink /proc/$p/exe)"
  echo "cmdline: $(tr '\0' ' ' < /proc/$p/cmdline 2>/dev/null)"
  echo "-- fds:"
  ls -l /proc/$p/fd/ 2>&1
  echo "-- maps (qmi/rmnet/glink):"
  grep -iE 'libqmi|qmux|rmnet|glink|qcril|libdiag' /proc/$p/maps 2>/dev/null | awk '{print $NF}' | sort -u
  echo "-- threads:"
  for t in /proc/$p/task/*; do
    echo "  $(basename $t) state=$(awk '{print $3}' $t/stat 2>/dev/null) wchan=$(cat $t/wchan 2>/dev/null)"
  done
done
echo "== DONE"
