#!/bin/sh
# Phase 0 Wi-Fi/LuCI baseline inventory - NX679J OpenWrt
echo "=== SECTION.root ==="
ls -la /
echo "=== SECTION.cmdline ==="
cat /proc/cmdline
echo "=== SECTION.uname ==="
uname -a
cat /proc/version
echo "=== SECTION.bootid ==="
cat /proc/sys/kernel/random/boot_id
uptime
echo "=== SECTION.net ==="
ip -br link 2>&1 || ifconfig -a 2>&1
ip addr 2>&1
ip route 2>&1
echo "=== SECTION.ieee80211 ==="
ls -la /sys/class/ieee80211/ 2>&1
echo "=== SECTION.pci ==="
ls /sys/bus/pci/devices/ 2>&1
for d in /sys/bus/pci/devices/*; do
  [ -e "$d" ] || continue
  echo "-- $d vendor=$(cat $d/vendor 2>/dev/null) device=$(cat $d/device 2>/dev/null) class=$(cat $d/class 2>/dev/null) driver=$(readlink $d/driver 2>/dev/null)"
done
echo "=== SECTION.platform-matches ==="
ls /sys/bus/platform/devices/ 2>&1 | grep -iE 'wlan|wifi|cnss|icnss|wcn|pcie|wpss|bdf|qcom,cnss'
echo "=== SECTION.modules ==="
cat /proc/modules
echo "=== SECTION.module_count ==="
wc -l < /proc/modules
echo "=== SECTION.dmesg_wifi ==="
dmesg 2>&1 | grep -Ei 'wifi|wlan|ath[0-9]|cnss|icnss|qmi|mhi|firmware|calibration|board file|wcn|wpss|pcie|17cb|1103|1101'
echo "=== SECTION.dmesg_tail ==="
dmesg 2>&1 | tail -40
echo "=== SECTION.rfkill ==="
rfkill list 2>&1
ls /sys/class/rfkill/ 2>&1
for r in /sys/class/rfkill/rfkill*; do [ -e "$r" ] && echo "$r name=$(cat $r/name 2>/dev/null) type=$(cat $r/type 2>/dev/null) soft=$(cat $r/soft 2>/dev/null) hard=$(cat $r/hard 2>/dev/null)"; done
echo "=== SECTION.ubus ==="
ubus list 2>&1
echo "=== SECTION.ps ==="
ps w 2>&1
echo "=== SECTION.listen ==="
ss -lntup 2>&1 || netstat -lntup 2>&1
echo "=== SECTION.tools ==="
for t in iw iwconfig wpa_supplicant wpa_cli wpad hostapd ubus ubusd uhttpd rpcd dropbear netifd iwpriv iperf3 udhcpc; do
  p=$(command -v $t 2>/dev/null); echo "$t: ${p:-MISSING}"
done
echo "=== SECTION.firmware_libfw ==="
ls -la /lib/firmware/ 2>&1 | head -50
echo "=== SECTION.firmware_vendor_mnt ==="
ls /vendor/firmware_mnt/ 2>&1 | head -30
ls /vendor/firmware_mnt/image/ 2>&1 | head -80
echo "=== SECTION.mounts ==="
cat /proc/mounts 2>&1
echo "=== SECTION.owrt_config ==="
ls -la /etc/config/ 2>&1
for f in network wireless firewall dhcp system; do echo "--- /etc/config/$f:"; cat /etc/config/$f 2>&1; done
echo "=== SECTION.initd ==="
ls /etc/init.d/ 2>&1
echo "=== SECTION.rcd ==="
ls -la /etc/rc.d/ 2>&1
echo "=== SECTION.procd ==="
ps w 2>&1 | grep -E 'procd|ubusd|rpcd|uhttpd|dropbear|netifd|dnsmasq' 
echo "=== SECTION.journal_tail ==="
tail -c 4000 /nx679j-journal 2>/dev/null || echo "no journal"
echo "=== SECTION.END ==="
