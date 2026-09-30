#!/bin/bash
# Cancella il flag bootonce-bootloader dalla partizione misc
# Fonte: experiments/20260916-122926-native-baseline/BASELINE.md punto 3

echo "=== Clearing bootonce-bootloader from misc partition ==="
adb shell su -c 'dd if=/dev/zero of=/dev/block/by-name/misc bs=32 count=1 conv=notrunc' 2>&1

echo ""
echo "=== Verifying misc partition (first 64 bytes) ==="
adb shell su -c 'dd if=/dev/block/by-name/misc bs=64 count=1 2>/dev/null | xxd' 2>&1

echo ""
echo "✓ BCB cleared — misc partition now starts with zeros"
