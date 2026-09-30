#!/bin/bash
set -e

IMAGE="boot_b_openwrt_fixed_cmdline.img"
MONITOR_LOG="boot-test-monitor-$(date +%Y%m%d-%H%M%S).log"

echo "=== OpenWrt boot test sequence ==="
echo "Image: $IMAGE"
echo "SHA-256: $(sha256sum "$IMAGE" | cut -d' ' -f1)"
echo "Monitor log: $MONITOR_LOG"
echo ""

# Step 1: Reboot to fastboot
echo "[1/5] Rebooting to fastboot..."
adb reboot bootloader
sleep 5

# Wait for fastboot
echo "[2/5] Waiting for fastboot device..."
for i in {1..20}; do
  if fastboot devices 2>/dev/null | grep -q .; then
    echo "✓ Fastboot device ready"
    break
  fi
  if [ $i -eq 20 ]; then
    echo "ERROR: Fastboot device not found after 20 seconds"
    exit 1
  fi
  sleep 1
done

# Step 3: Flash boot_b
echo "[3/5] Flashing boot_b..."
fastboot flash boot_b "$IMAGE"

# Step 4: Set active slot B
echo "[4/5] Setting active slot to B..."
fastboot set_active b

# Step 5: Reboot and monitor
echo "[5/5] Rebooting and starting USB monitor..."
echo ""
echo "Monitor will run for 30 seconds checking USB/ADB/network every 1.5s"
echo "Log: $MONITOR_LOG"
echo ""

fastboot reboot

# Monitor in foreground (no background operator &)
sleep 2
echo "=== Boot test started at $(date +%H:%M:%S) ===" | tee "$MONITOR_LOG"

for i in {1..20}; do
  echo "--- Check $i/20 ($(date +%H:%M:%S.%3N)) ---" | tee -a "$MONITOR_LOG"
  
  # USB devices
  lsusb 2>&1 | grep -iE '(android|qualcomm|zte|nubia|google)' | tee -a "$MONITOR_LOG" || echo "(no USB device)" | tee -a "$MONITOR_LOG"
  
  # ADB
  adb devices 2>&1 | tail -n +2 | grep -v '^$' | tee -a "$MONITOR_LOG" || true
  
  # Network interfaces
  ip link show 2>/dev/null | grep -E '^[0-9]+: (usb|ncm|rndis)' | tee -a "$MONITOR_LOG" || true
  
  # Fastboot check (indicates boot failure)
  if fastboot devices 2>/dev/null | grep -q .; then
    echo "⚠ FASTBOOT DEVICE DETECTED — boot failed, returned to fastboot" | tee -a "$MONITOR_LOG"
  fi
  
  sleep 1.5
done

echo "=== Monitor ended at $(date +%H:%M:%S) ===" | tee -a "$MONITOR_LOG"

echo ""
echo "=== Boot test completed ==="
echo "Full log: $MONITOR_LOG"
echo ""
echo "Interpret results:"
echo "  - USB NCM gadget (usb0/ncm0) = OpenWrt boot SUCCESS"
echo "  - ADB device = OpenWrt or Android booted"
echo "  - Fastboot device after 10s = boot FAILED, ABL reject"
echo "  - No device change = boot hung or slot A fallback without USB"
