#!/bin/sh
# Phase 0b - deep DT/PCIe/WLAN probes - NX679J OpenWrt
echo "=== boot_id ==="; cat /proc/sys/kernel/random/boot_id
echo "=== DT top ==="; ls /proc/device-tree/ 2>&1 | head -40
echo "=== DT cnss/wlan/wifi/wpss nodes ==="
for d in /proc/device-tree/* /proc/device-tree/soc/*; do
  n=$(basename "$d"); case "$n" in *cnss*|*wlan*|*wifi*|*wpss*) echo "$d";; esac
done 2>/dev/null
echo "=== cnss node props ==="
for d in /proc/device-tree/* /proc/device-tree/soc/*; do
  n=$(basename "$d"); case "$n" in *cnss*) C="$d";; esac
done
echo "C=$C"
if [ -n "$C" ]; then
  ls -la "$C"
  for p in "$C"/*; do echo "--- $(basename $p)"; od -An -c -N 64 "$p" 2>/dev/null | head -4; done
fi
echo "=== DT pcie nodes ==="
for d in /proc/device-tree/* /proc/device-tree/soc/*; do
  n=$(basename "$d"); case "$n" in *pcie*) echo "$d";; esac
done 2>/dev/null
echo "=== platform devices wlan-ish ==="
ls /sys/bus/platform/devices/ | grep -iE 'cnss|wlan|wifi|mhi|ipa|pcie|wpss' 2>&1
echo "=== driver binding cnss/wlan ==="
for d in /sys/bus/platform/devices/*cnss* /sys/bus/platform/devices/*wlan*; do [ -e "$d" ] && echo "$d driver=$(readlink $d/driver 2>/dev/null || echo NONE)"; done
d=/sys/bus/platform/devices/soc:mhi_qrtr_cnss; [ -e "$d" ] && echo "mhi_qrtr_cnss driver=$(readlink $d/driver 2>/dev/null || echo NONE)"
echo "=== deferred probe list ==="
cat /sys/kernel/debug/devices_deferred 2>/dev/null | head -40
echo "=== partitions ==="
cat /proc/partitions
echo "=== PID1 root ==="
ls /proc/1/root/ 2>&1 | head -30
echo "=== nx679j dir ==="
ls /proc/1/root/nx679j/ 2>&1 | head -40
echo "=== lib/modules ramdisk root ==="
ls /proc/1/root/lib/modules/ 2>&1
ls /proc/1/root/lib/modules/*/ 2>&1 | head -40
echo "=== where dvb/ko live ==="
ls /proc/1/root/vendor 2>&1 | head
ls /proc/1/root/lib/firmware 2>&1 | head -30
echo "=== rmnet counters ==="
cat /sys/class/net/rmnet_data0/statistics/rx_packets /sys/class/net/rmnet_data0/statistics/tx_packets 2>&1
echo "=== tools apk/opkg ==="
command -v apk; command -v opkg; ls /etc/apk/ 2>&1 | head; cat /etc/apk/repositories 2>/dev/null
echo "=== fresh dmesg (filtered) ==="
dmesg | grep -viE 'journal mirror|irq_count' | tail -120
echo "=== iomem wlan/ipa ==="
grep -iE 'wlan|ipa|cnss|wpss' /proc/iomem 2>/dev/null | head -20
echo "=== INTERNET TEST (modem) ==="
ip route get 8.8.8.8 2>&1
ping -c 2 -W 3 8.8.8.8 2>&1 | tail -4
echo "=== END ==="
