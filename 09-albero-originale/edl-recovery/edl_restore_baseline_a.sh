#!/bin/bash
# Restore known-good baseline on NX679J via Firehose.
# Requires: phone already in 9008 EDL.
set -u
EDL=${EDL:-/home/user/venvs/edk2/bin/edl}
L=${LOADER:-/home/user/nx679j-stock/edl-recovery/prog_firehose_ddr.melf}
R=/home/user/nx679j-stock/edl-recovery/restore-stock-a
RECON=/home/user/nx679j-stock/edl-recovery/recon-20260717-183330
PATCH=/home/user/nx679j-stock/native-openwrt-usb-build/headless-upstream/abl-compat-v12/artifacts/partition-analysis/gpt-patch
OUT=/home/user/nx679j-stock/edl-recovery/restore-stock-a
LOG="$OUT/edl-baseline-restore-$(date -u +%Y%m%d-%H%M%S).log"
mkdir -p "$OUT" /tmp/nx679j-edl-research/logs
exec > >(tee -a "$LOG") 2>&1

echo "=== $(date -Is) baseline restore start ==="
lsusb -d 05c6:9008 || { echo "No 9008 device present"; exit 1; }

run() {
  echo "CMD: $*"
  "$@"
  local rc=$?
  echo "RC=$rc"
  return $rc
}

# Snapshot GPT before restore
run "$EDL" printgpt --loader="$L" --memory=ufs | tee "$OUT/printgpt-before-baseline.txt"

# Prefer pre-experiment GPT restore images for LUNs that were patched.
# SM8450 Nubia mapping from prior analysis:
#   LUN1/sdb = xbl_a, LUN2/sdc = xbl_b, LUN4/sde = boot/abl chain
# We still try write by common LUN indices and also by name partitions.

for lun_pri in \
  "1:$PATCH/sdb_restore_pri.bin" \
  "2:$PATCH/sdc_restore_pri.bin" \
  "4:$PATCH/sde_restore_pri.bin"
do
  lun=${lun_pri%%:*}
  pri=${lun_pri#*:}
  if [ -f "$pri" ]; then
    echo "Restoring GPT primary on LUN $lun from $pri"
    run "$EDL" w gpt "$pri" --loader="$L" --memory=ufs --lun="$lun" || \
      run "$EDL" ws 0 "$pri" --loader="$L" --memory=ufs --lun="$lun" || true
  fi
done

# Critical early boot chain on both slots from known dumps
xbl=${R}/xbl_a_from_b.bin; [ -f "$xbl" ] || xbl=${RECON}/xbl_a.bin
xc=${R}/xbl_config_a_from_b.bin; [ -f "$xc" ] || xc=${RECON}/xbl_config_a.bin
abl=${R}/abl_a_from_b.bin; [ -f "$abl" ] || abl=${RECON}/abl_a.bin

for slot in a b; do
  run "$EDL" w "xbl_${slot}" "$xbl" --loader="$L" --memory=ufs || exit 1
  run "$EDL" w "xbl_config_${slot}" "$xc" --loader="$L" --memory=ufs || exit 1
  run "$EDL" w "abl_${slot}" "$abl" --loader="$L" --memory=ufs || exit 1
done

# Slot A Android boot path from recon dump (known stock at dump time)
run "$EDL" w boot_a "$RECON/boot_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w vendor_boot_a "$RECON/vendor_boot_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w dtbo_a "$RECON/dtbo_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w vbmeta_a "$RECON/vbmeta_a.bin" --loader="$L" --memory=ufs || exit 1

# Also put stock chain on B so slot B is not a poisoned experimental image
run "$EDL" w boot_b "$RECON/boot_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w vendor_boot_b "$RECON/vendor_boot_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w dtbo_b "$RECON/dtbo_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w vbmeta_b "$RECON/vbmeta_a.bin" --loader="$L" --memory=ufs || exit 1

# Clear BCB
if [ -f "$RECON/analysis/misc-clear-bcb-2k.bin" ]; then
  run "$EDL" w misc "$RECON/analysis/misc-clear-bcb-2k.bin" --loader="$L" --memory=ufs || true
fi

# Activate A and verify
run "$EDL" setactiveslot a --loader="$L"
run "$EDL" getactiveslot --loader="$L"
run "$EDL" printgpt --loader="$L" --memory=ufs | tee "$OUT/printgpt-after-baseline.txt"

# Verify XBL/ABL writeback
run "$EDL" r xbl_a /tmp/xbl_a_post.bin --loader="$L" --memory=ufs
run "$EDL" r abl_a /tmp/abl_a_post.bin --loader="$L" --memory=ufs
sha256sum "$xbl" /tmp/xbl_a_post.bin
sha256sum "$abl" /tmp/abl_a_post.bin
cmp -s "$xbl" /tmp/xbl_a_post.bin && echo XBL_A_OK || echo XBL_A_FAIL
cmp -s "$abl" /tmp/abl_a_post.bin && echo ABL_A_OK || echo ABL_A_FAIL

run "$EDL" reset --loader="$L" --resetmode=reset
echo "=== $(date -Is) baseline restore done; monitor host for adb/fastboot ==="
echo "LOG=$LOG"
