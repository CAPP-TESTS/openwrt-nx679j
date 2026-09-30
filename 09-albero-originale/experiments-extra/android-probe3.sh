#!/system/bin/sh
echo "=== /proc/modules (qrtr/glink/ipa/rmnet):"
grep -E 'qrtr|glink|qcom_smd|ipc|ipa|rmnet' /proc/modules
echo "=== /sys/bus:"
ls /sys/bus/ | tr '\n' ' '
echo
echo "=== glink/class:"
ls /sys/class/ | grep -iE 'glink|smd|qrtr'
ls /sys/bus/glink/devices/ 2>/dev/null
echo "=== dmesg qrtr/glink:"
dmesg 2>/dev/null | grep -iE 'qrtr|glink|ipcrtr|qcom_smd' | head -30
echo "=== END"
