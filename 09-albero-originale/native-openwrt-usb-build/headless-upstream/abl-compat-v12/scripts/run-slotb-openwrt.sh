#!/bin/bash
# Wrapper: put device in EDL then flash OpenWrt on slot B
set -eu

SCRIPT=$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)/flash-slotb-openwrt.sh
SCRIPTDIR=$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)

echo "=== Step 1: Enter EDL mode ==="
echo "Hold VOL DOWN + plug in USB cable"
echo "LED should turn red (9008 mode)"
echo ""
echo "Waiting for 9008 device... (Ctrl-C to abort)"

# Poll until device appears in 9008 mode
for i in $(seq 1 30); do
  if lsusb -d 05c6:9008 >/dev/null 2>&1; then
    echo "✓ 9008 detected after ${i}s"
    break
  fi
  sleep 1
done

echo ""
echo "=== Step 2: Flash OpenWrt on slot B ==="
cd "$SCRIPTDIR"
bash "$SCRIPT"
