#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

adb wait-for-device

adb push "${SCRIPT_DIR}/capture-crash-artifacts.sh" /data/local/tmp/capture-crash-artifacts.sh
adb push "${SCRIPT_DIR}/20-crash-capture-postfs.sh" /data/local/tmp/20-crash-capture-postfs.sh
adb push "${SCRIPT_DIR}/20-crash-capture-service.sh" /data/local/tmp/20-crash-capture-service.sh

adb shell "su -c '
mkdir -p /data/adb/crash-capture /data/local/tmp/crash-capture
cp /data/local/tmp/capture-crash-artifacts.sh /data/adb/crash-capture/capture-crash-artifacts.sh
cp /data/local/tmp/20-crash-capture-postfs.sh /data/adb/post-fs-data.d/20-crash-capture-postfs.sh
cp /data/local/tmp/20-crash-capture-service.sh /data/adb/service.d/20-crash-capture-service.sh
chmod 0755 /data/adb/crash-capture/capture-crash-artifacts.sh
chmod 0755 /data/adb/post-fs-data.d/20-crash-capture-postfs.sh
chmod 0755 /data/adb/service.d/20-crash-capture-service.sh
/data/adb/crash-capture/capture-crash-artifacts.sh install
echo \"installed_files:\"
ls -l /data/adb/crash-capture/capture-crash-artifacts.sh
ls -l /data/adb/post-fs-data.d/20-crash-capture-postfs.sh
ls -l /data/adb/service.d/20-crash-capture-service.sh
'"
