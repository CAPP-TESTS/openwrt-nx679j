#!/bin/bash
# Restore stock XBL/ABL/xbl_config onto slot A (and mirror to B) via Firehose.
# Requires phone in 9008 EDL with working prog_firehose_ddr.melf
set -u
EDL=${EDL:-/home/user/venvs/edk2/bin/edl}
L=${LOADER:-/home/user/nx679j-stock/edl-recovery/prog_firehose_ddr.melf}
R=/home/user/nx679j-stock/edl-recovery/restore-stock-a
LOG=/home/user/nx679j-stock/edl-recovery/restore-stock-a/edl-restore.log
mkdir -p /tmp/nx679j-edl-research/logs
exec > >(tee -a "$LOG") 2>&1
echo "=== $(date -Is) EDL restore start ==="
lsusb -d 05c6:9008 || { echo "No 9008 device"; exit 1; }
# Prefer B dumps (exact partition-size images known stock-matching)
# Fall back to padded stock extracts
xbl_img=$R/xbl_a_from_b.bin
abl_img=$R/abl_a_from_b.bin
xc_img=$R/xbl_config_a_from_b.bin
[ -f "$xbl_img" ] || xbl_img=$R/xbl.padded.img
[ -f "$abl_img" ] || abl_img=$R/abl.padded.img
[ -f "$xc_img" ] || xc_img=$R/xbl_config.padded.img

run() { echo "CMD: $*"; "$@"; }

# Write both slots to stock
for slot in a b; do
  run "$EDL" --loader="$L" --memory=ufs w xbl_${slot} "$xbl_img" || exit 1
  run "$EDL" --loader="$L" --memory=ufs w xbl_config_${slot} "$xc_img" || exit 1
  run "$EDL" --loader="$L" --memory=ufs w abl_${slot} "$abl_img" || exit 1
done

# Optional chain pieces that fastboot allowed partially
for slot in a b; do
  for part in uefi imagefv shrm aop devcfg featenabler xbl_ramdump; do
    f=$R/${part}.padded.img
    [ -f "$f" ] || continue
    run "$EDL" --loader="$L" --memory=ufs w ${part}_${slot} "$f" || echo "warn: failed ${part}_${slot}"
  done
done

# Stock boot path on A
RECON=/home/user/nx679j-stock/edl-recovery/recon-20260717-183330
run "$EDL" --loader="$L" --memory=ufs w boot_a "$RECON/boot_a.bin"
run "$EDL" --loader="$L" --memory=ufs w vendor_boot_a "$RECON/vendor_boot_a.bin"
run "$EDL" --loader="$L" --memory=ufs w dtbo_a "$RECON/dtbo_a.bin"
run "$EDL" --loader="$L" --memory=ufs w vbmeta_a "$RECON/vbmeta_a.bin"
# clear misc BCB again
run "$EDL" --loader="$L" --memory=ufs w misc "$RECON/analysis/misc-clear-bcb-2k.bin" || \
  run "$EDL" --loader="$L" --memory=ufs w misc /dev/zero  # may fail size

# verify xbl_a matches stock source
run "$EDL" --loader="$L" --memory=ufs r xbl_a /tmp/xbl_a_post.bin
run "$EDL" --loader="$L" --memory=ufs r abl_a /tmp/abl_a_post.bin
sha256sum "$xbl_img" /tmp/xbl_a_post.bin
sha256sum "$abl_img" /tmp/abl_a_post.bin
cmp -s "$xbl_img" /tmp/xbl_a_post.bin && echo XBL_A_OK || echo XBL_A_FAIL
cmp -s "$abl_img" /tmp/abl_a_post.bin && echo ABL_A_OK || echo ABL_A_FAIL

run "$EDL" --loader="$L" --memory=ufs reset
echo "=== $(date -Is) EDL restore done ==="
