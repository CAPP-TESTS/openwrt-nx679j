#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

run() {
	if [ "${DRY_RUN:-0}" = "1" ]; then
		printf '[DRY_RUN] %s\n' "$*"
	else
		eval "$@"
	fi
}

require_file() {
	if [ ! -f "$1" ]; then
		printf 'Missing required backup file: %s\n' "$1" >&2
		exit 1
	fi
}

for f in \
	boot_b.img \
	vendor_boot_b.img \
	dtbo_b.img \
	vbmeta_b.img \
	vbmeta_system_b.img \
	recovery_b.img
do
	require_file "$SCRIPT_DIR/$f"
done

run "adb reboot bootloader"
run "fastboot flash boot_b \"$SCRIPT_DIR/boot_b.img\""
run "fastboot flash vendor_boot_b \"$SCRIPT_DIR/vendor_boot_b.img\""
run "fastboot flash dtbo_b \"$SCRIPT_DIR/dtbo_b.img\""
run "fastboot flash vbmeta_b \"$SCRIPT_DIR/vbmeta_b.img\""
run "fastboot flash vbmeta_system_b \"$SCRIPT_DIR/vbmeta_system_b.img\""
run "fastboot flash recovery_b \"$SCRIPT_DIR/recovery_b.img\""
run "fastboot set_active a"
run "fastboot reboot"
