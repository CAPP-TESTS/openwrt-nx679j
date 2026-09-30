#!/bin/bash
set -e

INPUT="boot_b_openwrt_fixed_cmdline.img"
OUTPUT="boot_b_openwrt_fixed_cmdline_signed.img"

echo "=== Adding dummy 4096-byte signature to match Android boot format ==="

# Verifica input
if [ ! -f "$INPUT" ]; then
  echo "ERROR: Input not found: $INPUT"
  exit 1
fi

# Copia input
cp "$INPUT" "$OUTPUT"

# Aggiungi 4096 byte di padding zero alla fine (dummy signature)
dd if=/dev/zero bs=4096 count=1 >> "$OUTPUT" 2>/dev/null

echo ""
echo "=== Signature added ==="
ls -lh "$INPUT" "$OUTPUT"

# Verifica dimensione
INPUT_SIZE=$(stat -c%s "$INPUT")
OUTPUT_SIZE=$(stat -c%s "$OUTPUT")
DIFF=$((OUTPUT_SIZE - INPUT_SIZE))

echo "Input: $INPUT_SIZE bytes"
echo "Output: $OUTPUT_SIZE bytes"
echo "Diff: +$DIFF bytes (expected: +4096)"

if [ $DIFF -ne 4096 ]; then
  echo "ERROR: Signature size mismatch!"
  exit 1
fi

echo ""
echo "✓ Dummy signature added successfully"
echo "Note: This is NOT a cryptographic signature — it's zero-padding"
echo "      to match Android boot image v4 format signature_size field."
echo ""
echo "Next: Update boot header signature_size field from 0 to 4096"
