#!/bin/bash
# Flash OpenWrt on slot B after clearing bootonce-bootloader from misc.
# Requires: phone in 9008 EDL.
set -u

EDL=${EDL:-/home/user/venvs/edk2/bin/edl}
L=${LOADER:-/home/user/nx679j-stock/edl-recovery/prog_firehose_ddr.melf}
ARTIFACTS=/home/user/nx679j-stock/native-openwrt-usb-build/headless-upstream/abl-compat-v12/artifacts
LOG=/tmp/nx679j-slotb-openwrt-$(date -u +%Y%m%d-%H%M%S).log
mkdir -p "$(dirname "$LOG")"
exec > >(tee -a "$LOG") 2>&1

echo "=== $(date -Is) slot B OpenWrt install ==="
lsusb -d 05c6:9008 || { echo "No 9008"; exit 1; }

run() {
  echo "CMD: $*"
  "$@"
  local rc=$?
  echo "RC=$rc"
  return $rc
}

# 1. Snapshot current GPT
run "$EDL" printgpt --loader="$L" --memory=ufs | tee "$LOG.gpt-before.txt"

# 2. Flash zeroed misc to clear bootonce-bootloader
run "$EDL" w misc /dev/zero --loader="$L" --memory=ufs || \
  run "$EDL" w misc "$ARTIFACTS/abl-logs/clean_misc.img" --loader="$L" --memory=ufs || true

# 3. Set active slot to B
run "$EDL" setactiveslot b --loader="$L"
run "$EDL" getactiveslot --loader="$L"

# 4. Flash OpenWrt boot images onto slot B
run "$EDL" w boot_b "$ARTIFACTS/boot-nx679j-headless-openwrt.img" --loader="$L" --memory=ufs || exit 1
run "$EDL" w vendor_boot_b "$ARTIFACTS/vendor_boot-nx679j-headless-openwrt.img" --loader="$L" --memory=ufs || exit 1
run "$EDL" w dtbo_b "$ARTIFACTS/dtbo-nx679j-v12-noop.img" --loader="$L" --memory=ufs || exit 1

# 5. Ensure vbmeta_b is present and valid
# Check if recon dump has a good vbmeta_b, otherwise use vbmeta_a (same signing)
if [ -f "$ARTIFACTS/../recon-20260717-183330/vbmeta_b.bin" ]; then
  run "$EDL" w vbmeta_b "$ARTIFACTS/../recon-20260717-183330/vbmeta_b.bin" --loader="$L" --memory=ufs || true
else
  echo "No vbmeta_b found in recon, using vbmeta_a"
  run "$EDL" w vbmeta_b "$ARTIFACTS/../recon-20260717-183330/vbmeta_a.bin" --loader="$L" --memory=ufs || true
fi

# 6. Verify writes
run "$EDL" r boot_b /tmp/boot_b_verify.img --loader="$L" --memory=ufs
run "$EDL" r vendor_boot_b /tmp/vendor_boot_b_verify.img --loader="$L" --memory=ufs
sha256sum "$ARTIFACTS/boot-nx679j-headless-openwrt.img" /tmp/boot_b_verify.img
sha256sum "$ARTIFACTS/vendor_boot-nx679j-headless-openwrt.img" /tmp/vendor_boot_b_verify.img

# 7. Verify misc is cleared
run "$EDL" r misc /tmp/misc_verify.img --loader="$L" --memory=ufs
strings /tmp/misc_verify.img

# 8. Reset
run "$EDL" reset --loader="$L" --resetmode=reset
echo "=== $(date -Is) reset issued; watching for boot ==="
echo "LOG=$LOG"
