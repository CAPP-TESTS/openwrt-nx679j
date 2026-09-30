#!/bin/bash
set -e

IMAGE="boot_b_stockkernel_openwrt_fixed.img"
MONITOR_LOG="boot-test-stockkernel-$(date +%Y%m%d-%H%M%S).log"

echo "=== OpenWrt boot test with STOCK kernel 5.10.66 ==="
echo "Image: $IMAGE"
echo "SHA-256: $(sha256sum "$IMAGE" | cut -d' ' -f1)"
echo "Strategy: same kernel that works on slot A + OpenWrt ramdisk"
echo ""

echo "[1/4] Flashing boot_b..."
fastboot flash boot_b "$IMAGE"

echo "[2/4] Setting active slot to B..."
fastboot set_active b

echo "[3/4] Rebooting and monitoring..."
fastboot reboot

sleep 2
echo "=== Boot test started at $(date +%H:%M:%S) ===" | tee "$MONITOR_LOG"

for i in {1..30}; do
  echo "--- Check $i/30 ($(date +%H:%M:%S.%3N)) ---" | tee -a "$MONITOR_LOG"
  
  # USB devices
  lsusb 2>&1 | grep -iE '(android|qualcomm|zte|nubia|google)' | tee -a "$MONITOR_LOG" || echo "(no device)" | tee -a "$MONITOR_LOG"
  
  # ADB
  adb devices 2>&1 | tail -n +2 | grep -v '^$' | tee -a "$MONITOR_LOG" || true
  
  # Network interfaces (NCM gadget)
  ip link show 2>/dev/null | grep -E '^[0-9]+: (usb|ncm|rndis)' | tee -a "$MONITOR_LOG" || true
  
  # Fastboot check
  if fastboot devices 2>/dev/null | grep -q .; then
    echo "⚠ FASTBOOT — boot failed, returned to bootloader" | tee -a "$MONITOR_LOG"
  fi
  
  sleep 1.5
done

echo "=== Monitor ended ===" | tee -a "$MONITOR_LOG"
echo ""
echo "Full log: $MONITOR_LOG"
echo ""
echo "Expected outcomes:"
echo "  SUCCESS: USB NCM gadget (usb0) appears, or ADB device"
echo "  PARTIAL: Black screen but no USB (kernel boots, no gadget init)"
echo "  FAILURE: Fastboot after 10-15s (ABL reject or kernel panic)"
