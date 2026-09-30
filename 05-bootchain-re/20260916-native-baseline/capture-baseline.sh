#!/usr/bin/env bash
set -u

OUT="$(cd "$(dirname "$0")" && pwd)"
SERIAL="0123456789ABCDEF"
LOG="$OUT/commands.log"

printf 'baseline_start=%s\n' "$(date --iso-8601=seconds)" >>"$LOG"
printf 'serial=%s\n' "$SERIAL" >>"$LOG"
printf 'host_kernel='; uname -a >>"$LOG"

capture() {
    local name="$1"
    shift
    {
        printf '\n=== %s ===\n' "$name"
        printf 'argv:'
        printf ' %q' "$@"
        printf '\nstarted=%s\n' "$(date --iso-8601=seconds)"
    } >>"$LOG"
    timeout 30 "$@" >"$OUT/$name" 2>&1
    local rc=$?
    printf 'finished=%s rc=%s\n' "$(date --iso-8601=seconds)" "$rc" >>"$LOG"
}

capture host-usb.txt lsusb -nn
capture adb-state.txt adb -s "$SERIAL" get-state
capture adb-device.txt adb -s "$SERIAL" devices -l
capture adb-props.txt adb -s "$SERIAL" shell getprop
capture adb-id.txt adb -s "$SERIAL" shell su -c id
capture adb-uname.txt adb -s "$SERIAL" shell su -c uname -a
capture adb-security.txt adb -s "$SERIAL" shell su -c 'getenforce; printf "cmdline="; tr "\\000" " " </proc/cmdline; printf "\\n"'
capture adb-partitions.txt adb -s "$SERIAL" shell su -c 'for n in boot_a boot_b vendor_boot_a vendor_boot_b dtbo_a dtbo_b vbmeta_a vbmeta_b misc rawdump logdump abl_a abl_b xbl_a xbl_b xbl_ramdump_a xbl_ramdump_b; do p=/dev/block/by-name/$n; if [ -e "$p" ]; then printf "%s -> " "$n"; readlink -f "$p"; stat -c "size=%s mode=%a" "$p" 2>&1; else printf "%s -> MISSING\\n" "$n"; fi; done'
capture adb-mounts.txt adb -s "$SERIAL" shell su -c mount
capture adb-pstore-list.txt adb -s "$SERIAL" shell su -c 'for p in /sys/fs/pstore/*; do [ -e "$p" ] && { printf "%s " "$p"; stat -c "size=%s mode=%a" "$p"; }; done'
capture adb-udc.txt adb -s "$SERIAL" shell su -c 'for p in /sys/class/udc/*; do [ -e "$p" ] && { printf "udc=%s target=" "$p"; readlink -f "$p"; printf "state="; [ -r "$p/state" ] && tr "\\000" " " <"$p/state"; printf "\\n"; }; done'
capture adb-usb-dumpsys.txt adb -s "$SERIAL" shell dumpsys usb
capture adb-network.txt adb -s "$SERIAL" shell su -c 'ip -details link; printf "--- addresses ---\\n"; ip -details address'
capture adb-dmesg.txt adb -s "$SERIAL" shell su -c 'dmesg -T'
capture adb-logcat-all.txt adb -s "$SERIAL" logcat -b all -d -v threadtime

printf 'baseline_end=%s\n' "$(date --iso-8601=seconds)" >>"$LOG"
printf 'output_dir=%s\n' "$OUT"
