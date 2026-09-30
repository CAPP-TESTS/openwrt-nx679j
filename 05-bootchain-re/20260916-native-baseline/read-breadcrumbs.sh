#!/bin/bash
# Read the slot-B breadcrumb journal from rawdump.
#
# The v2 candidate init appends its journal (bs=4096, conv=notrunc) to the
# rawdump partition, which is 256 MiB and entirely zero on this device, so any
# non-zero content is ours. Run this with Android booted and rooted.
#
# usage: read-breadcrumbs.sh [outdir]
set -u
A="timeout 60 adb -s 0123456789ABCDEF"
OUT="${1:-/tmp}"
TS=$(date +%Y%m%d-%H%M%S)
BIN="$OUT/breadcrumbs-$TS.bin"
mkdir -p "$OUT"

echo "[*] reading rawdump (first 64 KiB)"
$A shell su -c 'head -c 65536 /dev/block/by-name/rawdump' > "$BIN" 2>/dev/null
rc=$?
size=$(stat -c %s "$BIN" 2>/dev/null || echo 0)
echo "    rc=$rc bytes=$size -> $BIN"
if [ "$size" -eq 0 ]; then
    echo "    empty read: is the phone booted to Android with root?"
    exit 1
fi

echo "[*] non-zero check"
if ! grep -qa . "$BIN" 2>/dev/null; then
    echo "    rawdump is still all zero: the OpenWrt init never reached UFS"
    echo "    (breadcrumbs are only written once the UFS modules are loaded)"
    exit 0
fi

echo "[*] journal text"
strings -a "$BIN" | head -60

echo "[*] sha256"
sha256sum "$BIN"
