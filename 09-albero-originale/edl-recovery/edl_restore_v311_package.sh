#!/bin/bash
# Coherent V311 package restore for NX679J bootloop recovery.
# Requires phone in 9008 EDL.
set -u
EDL=${EDL:-/home/user/venvs/edk2/bin/edl}
L=${LOADER:-/home/user/nx679j-stock/edl-recovery/prog_firehose_ddr.melf}
P=/home/user/nx679j-stock/edl-recovery/v311-padded
PATCH=/home/user/nx679j-stock/native-openwrt-usb-build/headless-upstream/abl-compat-v12/artifacts/partition-analysis/gpt-patch
OUT=/home/user/nx679j-stock/edl-recovery/restore-stock-a
LOG="$OUT/edl-v311-package-$(date -u +%Y%m%d-%H%M%S).log"
mkdir -p "$OUT"
exec > >(tee -a "$LOG") 2>&1
echo "=== $(date -Is) v311 package restore start ==="
lsusb -d 05c6:9008 || { echo No9008; exit 1; }
run(){ echo "CMD: $*"; "$@"; echo RC=$?; }
# GPT
for lun_pri in "1:$PATCH/sdb_restore_pri.bin" "2:$PATCH/sdc_restore_pri.bin" "4:$PATCH/sde_restore_pri.bin"; do
  lun=${lun_pri%%:*}; pri=${lun_pri#*:}
  [ -f "$pri" ] || continue
  run "$EDL" w gpt "$pri" --loader="$L" --memory=ufs --lun="$lun" || run "$EDL" ws 0 "$pri" --loader="$L" --memory=ufs --lun="$lun" || true
done
# XBL chain (same package image on both slots)
for slot in a b; do
  run "$EDL" w xbl_$slot "$P/xbl.bin" --loader="$L" --memory=ufs || exit 1
  run "$EDL" w xbl_config_$slot "$P/xbl_config.bin" --loader="$L" --memory=ufs || exit 1
  run "$EDL" w multiimgqti_$slot "$P/multiimgqti.bin" --loader="$L" --memory=ufs || true
  run "$EDL" w multiimgoem_$slot "$P/multiimgoem.bin" --loader="$L" --memory=ufs || true
done
# SDE early boot + android path
for slot in a b; do
  for part in uefi aop aop_config tz hyp modem bluetooth abl dsp keymaster boot devcfg qupfw vbmeta dtbo uefisecapp imagefv shrm cpucp featenabler vendor_boot qweslicstore recovery xbl_ramdump; do
    f="$P/${part}.bin"
    [ -f "$f" ] || continue
    run "$EDL" w ${part}_$slot "$f" --loader="$L" --memory=ufs || { echo FAIL $part $slot; exit 1; }
  done
done
# shared
run "$EDL" w vbmeta_system_a "$P/vbmeta_system.bin" --loader="$L" --memory=ufs || true
run "$EDL" w vbmeta_system_b "$P/vbmeta_system.bin" --loader="$L" --memory=ufs || true
# clear misc with zeros via small write if clear file exists
RECON=/home/user/nx679j-stock/edl-recovery/recon-20260717-183330
[ -f "$RECON/analysis/misc-clear-bcb-2k.bin" ] && run "$EDL" w misc "$RECON/analysis/misc-clear-bcb-2k.bin" --loader="$L" --memory=ufs || true
run "$EDL" setactiveslot a --loader="$L"
run "$EDL" getactiveslot --loader="$L"
# verify critical
run "$EDL" r xbl_a /tmp/v311_xbl_a.bin --loader="$L" --memory=ufs
run "$EDL" r abl_a /tmp/v311_abl_a.bin --loader="$L" --memory=ufs
run "$EDL" r uefi_a /tmp/v311_uefi_a.bin --loader="$L" --memory=ufs
run "$EDL" r tz_a /tmp/v311_tz_a.bin --loader="$L" --memory=ufs
cmp -s "$P/xbl.bin" /tmp/v311_xbl_a.bin && echo XBL_OK || echo XBL_FAIL
cmp -s "$P/abl.bin" /tmp/v311_abl_a.bin && echo ABL_OK || echo ABL_FAIL
cmp -s "$P/uefi.bin" /tmp/v311_uefi_a.bin && echo UEFI_OK || echo UEFI_FAIL
cmp -s "$P/tz.bin" /tmp/v311_tz_a.bin && echo TZ_OK || echo TZ_FAIL
run "$EDL" reset --loader="$L" --resetmode=reset
echo "=== $(date -Is) v311 package restore done LOG=$LOG ==="
