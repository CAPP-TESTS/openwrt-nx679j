#!/bin/bash
set -e

IMAGE="boot_b_openwrt_fixed_cmdline_signed.img"
MONITOR_LOG="boot-test-signed-$(date +%Y%m%d-%H%M%S).log"

echo "=== OpenWrt boot test with signature dummy ==="
echo "Image: $IMAGE"
echo "SHA-256: $(sha256sum "$IMAGE" | cut -d' ' -f1)"
echo ""

# Fastboot flash
echo "[1/4] Flashing boot_b..."
fastboot flash boot_b "$IMAGE"

echo "[2/4] Setting active slot to B..."
fastboot set_active b

echo "[3/4] Rebooting and monitoring..."
fastboot reboot

sleep 2
echo "=== Boot test started at $(date +%H:%M:%S) ===" | tee "$MONITOR_LOG"

for i in {1..25}; do
  echo "--- Check $i/25 ($(date +%H:%M:%S.%3N)) ---" | tee -a "$MONITOR_LOG"
  
  lsusb 2>&1 | grep -iE '(android|qualcomm|zte|nubia|google)' | tee -a "$MONITOR_LOG" || echo "(no device)" | tee -a "$MONITOR_LOG"
  adb devices 2>&1 | tail -n +2 | grep -v '^$' | tee -a "$MONITOR_LOG" || true
  ip link show 2>/dev/null | grep -E '^[0-9]+: (usb|ncm|rndis)' | tee -a "$MONITOR_LOG" || true
  
  if fastboot devices 2>/dev/null | grep -q .; then
    echo "⚠ FASTBOOT — boot failed" | tee -a "$MONITOR_LOG"
  fi
  
  sleep 1.5
done

echo "=== Monitor ended ===" | tee -a "$MONITOR_LOG"
echo ""
echo "Results in: $MONITOR_LOG"
