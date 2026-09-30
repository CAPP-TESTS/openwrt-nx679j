#!/bin/bash
# Fix slot B bootloader chain via EDL (9008)
# The broken fastboot is useless — manually enter 9008 mode first
set -u

EDL=${EDL:-/home/user/venvs/edk2/bin/edl}
L=${LOADER:-/home/user/nx679j-stock/edl-recovery/prog_firehose_ddr.melf}
RECON=/home/user/nx679j-stock/edl-recovery/recon-20260717-183330
R=/home/user/nx679j-stock/edl-recovery/restore-stock-a
ARTIFACTS=/home/user/nx679j-stock/native-openwrt-usb-build/headless-upstream/abl-compat-v12/artifacts
LOG=/tmp/nx679j-fix-slotb-$(date -u +%Y%m%d-%H%M%S).log
mkdir -p "$(dirname "$LOG")"
exec > >(tee -a "$LOG") 2>&1

export PYTHONPATH=/home/user/venvs/edk2/lib/python3.14/site-packages

echo "=== $(date -Is) Waiting for 9008 EDL mode ==="
echo "Instructions: power OFF the phone, hold VOL DOWN + VOL UP, plug USB, release when LED red"

# Poll for 9008 mode
for i in $(seq 1 60); do
  if lsusb -d 05c6:9008 >/dev/null 2>&1; then
    echo "✓ 9008 detected after ${i}s"
    break
  fi
  if [ $((i % 5)) -eq 0 ]; then echo "waiting... (${i}s)"; fi
  sleep 1
done

lsusb -d 05c6:9008 || { echo "No 9008"; exit 1; }

run() {
  echo "CMD: $*"
  "$@"
  local rc=$?
  echo "RC=$rc"
  return $rc
}

echo "=== GPT snapshot ==="
run "$EDL" printgpt --loader="$L" --memory=ufs | tee "$LOG.gpt.txt"

echo "=== Clearing misc (bootonce-bootloader) ==="
run "$EDL" w misc /dev/zero --loader="$L" --memory=ufs

echo "=== Flashing slot B bootloader chain from stock ==="
# Core boot chain
for part in xbl xbl_config abl aop aop_config tz hyp keymaster shrm imagefv uefisecapp featenabler devcfg qupfw cpucp xbl_ramdump multiimgqti multiimgoem; do
  SRC="${R}/${part}.padded.img"
  [ -f "$SRC" ] || continue
  echo "  ${part}_b from $SRC"
  run "$EDL" w "${part}_b" "$SRC" --loader="$L" --memory=ufs || echo "WARN: ${part}_b"
done

echo "=== Flashing slot B boot images ==="
# OpenWrt boot image
run "$EDL" w boot_b "$ARTIFACTS/boot-nx679j-headless-openwrt.img" --loader="$L" --memory=ufs || exit 1
run "$EDL" w vendor_boot_b "$ARTIFACTS/vendor_boot-nx679j-headless-openwrt.img" --loader="$L" --memory=ufs || exit 1
run "$EDL" w dtbo_b "$ARTIFACTS/dtbo-nx679j-v12-noop.img" --loader="$L" --memory=ufs || exit 1
run "$EDL" w vbmeta_b "$RECON/vbmeta_b.bin" --loader="$L" --memory=ufs || exit 1

echo "=== Verifying writes ==="
for part in boot_b vendor_boot_b dtbo_b vbmeta_b; do
  run "$EDL" r "${part}_verify" /tmp/"${part}_verify.img" --loader="$L" --memory=ufs
  case $part in
    boot_b) EXPECTED="$ARTIFACTS/boot-nx679j-headless-openwrt.img" ;;
    vendor_boot_b) EXPECTED="$ARTIFACTS/vendor_boot-nx679j-headless-openwrt.img" ;;
    dtbo_b) EXPECTED="$ARTIFACTS/dtbo-nx679j-v12-noop.img" ;;
    vbmeta_b) EXPECTED="$RECON/vbmeta_b.bin" ;;
  esac
  sha256sum "$EXPECTED" /tmp/"${part}_verify.img"
  cmp -s "$EXPECTED" /tmp/"${part}_verify.img" && echo "  $part OK" || echo "  $part MISMATCH"
done

echo "=== Clearing misc AGAIN ==="
run "$EDL" w misc /dev/zero --loader="$L" --memory=ufs

echo "=== Setting active slot b ==="
run "$EDL" setactiveslot b --loader="$L"
run "$EDL" getactiveslot --loader="$L"

echo "=== RESETTING ==="
run "$EDL" reset --loader="$L" --resetmode=reset

echo "=== $(date -Is) Boot observation ==="
echo "Waiting for device to respond..."
for i in $(seq 1 60); do
  if lsusb -d 05c6:9008 >/dev/null 2>&1; then echo "t=${i}s: still 9008"; fi
  if fastboot devices 2>/dev/null | grep -q .; then echo "t=${i}s: FASTBOOT"; break; fi
  if adb devices 2>/dev/null | grep -q "device"; then echo "t=${i}s: ADB device"; break; fi
  if [ $((i % 10)) -eq 0 ]; then echo "t=${i}s: still booting..."; fi
  sleep 1
done

lsusb 2>/dev/null | grep -iE '05c6|18d1' || echo "none"
fastboot devices 2>/dev/null || true
adb devices 2>/dev/null || true

echo "=== DONE ==="
echo "LOG=$LOG"
