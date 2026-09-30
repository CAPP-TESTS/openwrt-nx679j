#!/usr/bin/env bash
set -u

DEST_ROOT="${1:-./captures}"
TS="$(date +%Y%m%d-%H%M%S)"
DEST_DIR="${DEST_ROOT%/}/crash-capture-${TS}"

mkdir -p "${DEST_DIR}"

adb wait-for-device
mkdir -p "${DEST_DIR}/crash-capture"

mapfile -t STAGE_DIRS < <(
	adb shell "su -c 'for d in /data/local/tmp/crash-capture/*; do [ -d \"\$d\" ] && [ ! -L \"\$d\" ] && basename \"\$d\"; done'" | tr -d '\r'
)

for stage in "${STAGE_DIRS[@]}"; do
	adb pull "/data/local/tmp/crash-capture/${stage}" "${DEST_DIR}/crash-capture/"
done

printf 'Artifacts pulled to: %s\n' "${DEST_DIR}/crash-capture"
