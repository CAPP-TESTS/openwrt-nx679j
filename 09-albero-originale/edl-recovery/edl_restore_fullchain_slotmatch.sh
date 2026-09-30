#!/bin/bash
# Slot-matched full early-boot chain restore for NX679J bootloop recovery.
# Uses recon dumps for xbl/abl per-slot (NOT cross-slot copies).
# Requires phone already in 9008 EDL.
set -u
EDL=${EDL:-/home/user/venvs/edk2/bin/edl}
L=${LOADER:-/home/user/nx679j-stock/edl-recovery/prog_firehose_ddr.melf}
RECON=/home/user/nx679j-stock/edl-recovery/recon-20260717-183330
R=/home/user/nx679j-stock/edl-recovery/restore-stock-a
PATCH=/home/user/nx679j-stock/native-openwrt-usb-build/headless-upstream/abl-compat-v12/artifacts/partition-analysis/gpt-patch
OUT=/home/user/nx679j-stock/edl-recovery/restore-stock-a
LOG="$OUT/edl-fullchain-slotmatch-$(date -u +%Y%m%d-%H%M%S).log"
mkdir -p "$OUT"
exec > >(tee -a "$LOG") 2>&1

echo "=== $(date -Is) fullchain slotmatch start ==="
lsusb -d 05c6:9008 || { echo "No 9008"; exit 1; }

run() {
  echo "CMD: $*"
  "$@"
  local rc=$?
  echo "RC=$rc"
  return $rc
}

run "$EDL" printgpt --loader="$L" --memory=ufs | tee "$OUT/printgpt-before-fullchain.txt"

# GPT restore for LUNs previously patched
for lun_pri in \
  "1:$PATCH/sdb_restore_pri.bin" \
  "2:$PATCH/sdc_restore_pri.bin" \
  "4:$PATCH/sde_restore_pri.bin"
do
  lun=${lun_pri%%:*}
  pri=${lun_pri#*:}
  [ -f "$pri" ] || continue
  echo "Restoring GPT LUN $lun"
  run "$EDL" w gpt "$pri" --loader="$L" --memory=ufs --lun="$lun" || \
    run "$EDL" ws 0 "$pri" --loader="$L" --memory=ufs --lun="$lun" || true
done

# Slot-matched XBL/ABL/xbl_config from recon (critical fix vs previous cross-slot copy)
run "$EDL" w xbl_a "$RECON/xbl_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w xbl_config_a "$RECON/xbl_config_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w abl_a "$RECON/abl_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w xbl_b "$RECON/xbl_b.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w xbl_config_b "$RECON/xbl_config_b.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w abl_b "$RECON/abl_b.bin" --loader="$L" --memory=ufs || exit 1

# Expand early chain on both slots from padded stock extracts when present
for slot in a b; do
  for part in uefi aop aop_config tz hyp keymaster shrm imagefv uefisecapp featenabler devcfg qupfw cpucp xbl_ramdump multiimgqti multiimgoem; do
    f="$R/${part}.padded.img"
    [ -f "$f" ] || continue
    # multiimg* live on sdb/sdc not sde naming
    case $part in
      multiimgqti|multiimgoem)
        run "$EDL" w "${part}_${slot}" "$f" --loader="$L" --memory=ufs || echo "warn ${part}_${slot}"
        ;;
      *)
        run "$EDL" w "${part}_${slot}" "$f" --loader="$L" --memory=ufs || echo "warn ${part}_${slot}"
        ;;
    esac
  done
done

# Stock Android boot path on A (and mirror stock to B)
run "$EDL" w boot_a "$RECON/boot_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w vendor_boot_a "$RECON/vendor_boot_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w dtbo_a "$RECON/dtbo_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w vbmeta_a "$RECON/vbmeta_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w boot_b "$RECON/boot_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w vendor_boot_b "$RECON/vendor_boot_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w dtbo_b "$RECON/dtbo_a.bin" --loader="$L" --memory=ufs || exit 1
run "$EDL" w vbmeta_b "$RECON/vbmeta_a.bin" --loader="$L" --memory=ufs || exit 1

# vbmeta_system from v411 padded if available
if [ -f /tmp/nx679j-v411-flash/vbmeta_system.img ]; then
  run "$EDL" w vbmeta_system_a /tmp/nx679j-v411-flash/vbmeta_system.img --loader="$L" --memory=ufs || true
  run "$EDL" w vbmeta_system_b /tmp/nx679j-v411-flash/vbmeta_system.img --loader="$L" --memory=ufs || true
fi

if [ -f "$RECON/analysis/misc-clear-bcb-2k.bin" ]; then
  run "$EDL" w misc "$RECON/analysis/misc-clear-bcb-2k.bin" --loader="$L" --memory=ufs || true
fi

run "$EDL" setactiveslot a --loader="$L"
run "$EDL" getactiveslot --loader="$L"

# Verify slot-matched images
run "$EDL" r xbl_a /tmp/xbl_a_post.bin --loader="$L" --memory=ufs
run "$EDL" r abl_a /tmp/abl_a_post.bin --loader="$L" --memory=ufs
run "$EDL" r xbl_b /tmp/xbl_b_post.bin --loader="$L" --memory=ufs
run "$EDL" r abl_b /tmp/abl_b_post.bin --loader="$L" --memory=ufs
sha256sum "$RECON/xbl_a.bin" /tmp/xbl_a_post.bin
sha256sum "$RECON/abl_a.bin" /tmp/abl_a_post.bin
sha256sum "$RECON/xbl_b.bin" /tmp/xbl_b_post.bin
sha256sum "$RECON/abl_b.bin" /tmp/abl_b_post.bin
cmp -s "$RECON/xbl_a.bin" /tmp/xbl_a_post.bin && echo XBL_A_OK || echo XBL_A_FAIL
cmp -s "$RECON/abl_a.bin" /tmp/abl_a_post.bin && echo ABL_A_OK || echo ABL_A_FAIL
cmp -s "$RECON/xbl_b.bin" /tmp/xbl_b_post.bin && echo XBL_B_OK || echo XBL_B_FAIL
cmp -s "$RECON/abl_b.bin" /tmp/abl_b_post.bin && echo ABL_B_OK || echo ABL_B_FAIL

run "$EDL" printgpt --loader="$L" --memory=ufs | tee "$OUT/printgpt-after-fullchain.txt"
run "$EDL" reset --loader="$L" --resetmode=reset
echo "=== $(date -Is) fullchain slotmatch done ==="
echo "LOG=$LOG"
