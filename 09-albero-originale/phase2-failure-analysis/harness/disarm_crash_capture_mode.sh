#!/usr/bin/env bash
set -u

adb wait-for-device
adb shell "su -c '
echo 1 > /sys/module/qcom_dload_mode/parameters/download_mode
echo 1 > /sys/kernel/dload/emmc_dload
/data/adb/crash-capture/capture-crash-artifacts.sh manual-disarm 2>/dev/null || true
echo -n \"download_mode=\"
cat /sys/module/qcom_dload_mode/parameters/download_mode 2>/dev/null || true
echo -n \"emmc_dload=\"
cat /sys/kernel/dload/emmc_dload 2>/dev/null || true
echo -n \"dload_mode=\"
cat /sys/kernel/dload/dload_mode 2>/dev/null || true
'"
