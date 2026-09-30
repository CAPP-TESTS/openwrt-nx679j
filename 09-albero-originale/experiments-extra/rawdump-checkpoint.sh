#!/bin/sh
# Uses only the last 20 slots of the already-authorized 400-slot scratch window.
# BusyBox on this image documents conv=fsync as physical write before return.
set -eu
index=${1:?checkpoint index required}
label=${2:?checkpoint label required}
log=${3:?log path required}
case "$index" in [0-9]|1[0-9]) ;; *) exit 2;; esac
L=/proc/1/root/dev/rd
test -b "$L"
grep -qx 'PARTNAME=rawdump' /sys/block/sda/sda11/uevent
{
  printf 'CHECKPOINT index=%s label=%s boot=%s uptime=%s\n' "$index" "$label" \
    "$(cat /proc/sys/kernel/random/boot_id)" "$(cat /proc/uptime)"
  tail -80 "$log"
} > /tmp/checkpoint-slot.txt
test "$(wc -c < /tmp/checkpoint-slot.txt)" -le 32768
dd if=/tmp/checkpoint-slot.txt of="$L" bs=32768 count=1 \
  seek=$((32 + 380 + index)) conv=sync,notrunc,fsync 2>/dev/null
printf 'CHECKPOINT_DURABLE index=%s label=%s\n' "$index" "$label"
