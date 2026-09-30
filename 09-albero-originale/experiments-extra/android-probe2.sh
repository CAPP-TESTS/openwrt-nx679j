#!/system/bin/sh
echo "== uname: $(uname -a)"
echo "== rmnet_ctrl sysfs (488:0):"
ls -l /sys/dev/char/488:0/ 2>&1
cat /sys/dev/char/488:0/uevent 2>&1
echo "== device path:"
readlink -f /sys/dev/char/488:0/device 2>&1
ls -l /sys/dev/char/488:0/device/driver 2>&1
echo "== classes:"
ls /sys/class/ | grep -iE 'rmnet|gsi|ipa|qrtr|smd|glink'
echo "== modules:"
ls /sys/module/ | grep -iE 'qrtr|glink|rmnet|gsi|ipa|smd|ipc'
echo "== bus:"
ls /sys/bus/ | grep -iE 'qrtr|glink|rpmsg|smd'
echo "== ps qrtr:"
ps -A | grep -iE 'qrtr|rmtfs|tqftpserv|pd-mapper|dbus'
echo "== props:"
getprop | grep -iE 'qrtr|ipcrtr|rmtfs'
echo "== init rc mentioning qrtr:"
grep -rl qrtr /vendor/etc/init/ 2>/dev/null
echo "== /proc/net/:"
ls /proc/net/
echo "== dmesg qrtr/glink:"
dmesg 2>/dev/null | grep -iE 'qrtr|ipcrtr|glink' | head -50
echo "== dmesg head:"
dmesg 2>/dev/null | head -3
echo "== qrtr-lookup -h:"
/vendor/bin/qrtr-lookup -h 2>&1
echo "== qrtr-lookup run:"
/vendor/bin/qrtr-lookup 2>&1 | head -120
echo "== /proc/net/unix FULL:"
cat /proc/net/unix
echo "== END"
