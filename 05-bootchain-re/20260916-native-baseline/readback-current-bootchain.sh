#!/usr/bin/env bash
# Read-only Android/Magisk readback of the active boot-chain evidence.
set -euo pipefail

serial="0123456789ABCDEF"
base="$(cd -- "$(dirname -- "$0")" && pwd)"
out="$base/current-readback"
mkdir -p "$out"

parts=(
  boot_a boot_b
  vendor_boot_a vendor_boot_b
  dtbo_a dtbo_b
  vbmeta_a vbmeta_b
  misc rawdump logdump
  xbl_ramdump_a xbl_ramdump_b
)

run_adb() {
  timeout 60 adb -s "$serial" "$@"
}

remote_block_for_part() {
  run_adb shell "su -c 'readlink -f /dev/block/by-name/$1'" |
    tr -d '\r\n' | xargs basename
}

remote_size_for_part() {
  local block
  block="$(remote_block_for_part "$1")"
  run_adb shell "su -c 'cat /sys/class/block/$block/size'" |
    tr -d '\r\n'
}

printf 'started=%s\n' "$(date --iso-8601=seconds)" > "$out/metadata.txt"
printf 'serial=%s\n' "$serial" >> "$out/metadata.txt"
run_adb devices -l > "$out/adb-devices.txt"
run_adb shell "su -c 'id; getprop ro.boot.slot_suffix; getprop ro.boot.dtb_idx; getprop ro.boot.dtbo_idx; getprop ro.boot.vbmeta.digest'" > "$out/device-state.txt"

: > "$out/manifest.tsv"
printf 'partition\tremote_block\tsectors_512\texpected_bytes\tactual_bytes\tsha256\n' > "$out/manifest.tsv"

for part in "${parts[@]}"; do
  block="$(remote_block_for_part "$part")"
  sectors="$(remote_size_for_part "$part")"
  expected=$((sectors * 512))
  partial="$out/$part.img.partial"
  image="$out/$part.img"

  printf '[%s] reading %s (%s bytes)\n' "$(date --iso-8601=seconds)" "$part" "$expected" | tee -a "$out/readback.log"
  rm -f "$partial"
  # adb exec-out can merge remote stderr into binary stdout: silence dd's
  # progress remotely, then enforce both length and an independent digest.
  timeout 120 adb -s "$serial" exec-out "su -c 'exec dd if=/dev/block/by-name/$part bs=4194304 2>/dev/null'" > "$partial"
  actual="$(stat -c '%s' "$partial")"

  if [[ "$actual" != "$expected" ]]; then
    printf 'ERROR: %s expected %s bytes, read %s bytes; retaining %s\n' "$part" "$expected" "$actual" "$partial" >&2
    exit 1
  fi

  hash_line="$(sha256sum "$partial")"
  hash="${hash_line%% *}"
  remote_line="$(run_adb shell "su -c 'toybox sha256sum /dev/block/by-name/$part'" | tr -d '\r')"
  remote_hash="${remote_line%% *}"
  if [[ "$hash" != "$remote_hash" ]]; then
    printf 'ERROR: digest mismatch for %s: local=%s remote=%s\n' "$part" "$hash" "$remote_hash" >&2
    exit 1
  fi
  printf '%s\n' "$remote_line" >> "$out/remote-SHA256SUMS"
  mv "$partial" "$image"
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$part" "$block" "$sectors" "$expected" "$actual" "$hash" | tee -a "$out/manifest.tsv"
done

sha256sum "$out"/*.img > "$out/SHA256SUMS"
printf 'completed=%s\n' "$(date --iso-8601=seconds)" >> "$out/metadata.txt"
