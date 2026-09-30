#!/usr/bin/env bash
set -u

adb wait-for-device

adb shell "su -c '
echo \"[dload_controls]\"
echo -n \"download_mode=\"; cat /sys/module/qcom_dload_mode/parameters/download_mode 2>/dev/null || true
echo -n \"emmc_dload=\"; cat /sys/kernel/dload/emmc_dload 2>/dev/null || true
echo -n \"dload_mode=\"; cat /sys/kernel/dload/dload_mode 2>/dev/null || true
echo
echo \"[installed_scripts]\"
ls -l /data/adb/crash-capture/capture-crash-artifacts.sh 2>/dev/null || echo \"missing: /data/adb/crash-capture/capture-crash-artifacts.sh\"
ls -l /data/adb/post-fs-data.d/20-crash-capture-postfs.sh 2>/dev/null || echo \"missing: /data/adb/post-fs-data.d/20-crash-capture-postfs.sh\"
ls -l /data/adb/service.d/20-crash-capture-service.sh 2>/dev/null || echo \"missing: /data/adb/service.d/20-crash-capture-service.sh\"
echo
echo \"[capture_storage]\"
ls -la /data/local/tmp/crash-capture 2>/dev/null || echo \"missing: /data/local/tmp/crash-capture\"
if [ -L /data/local/tmp/crash-capture/latest ]; then
  echo -n \"latest-> \"
  readlink /data/local/tmp/crash-capture/latest 2>/dev/null || true
fi
'"
