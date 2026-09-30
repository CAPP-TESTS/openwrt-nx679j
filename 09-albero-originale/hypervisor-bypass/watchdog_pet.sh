#!/system/bin/sh
# Pet the Gunyah watchdog every second to prevent timeout

WATCHDOG_DEV="/sys/devices/platform/soc/soc:qcom,gh-virt-wdt/watchdog/watchdog0"

while true; do
    if [ -e "$WATCHDOG_DEV/nowayout" ]; then
        echo "V" > "$WATCHDOG_DEV/nowayout" 2>/dev/null || true
    fi
    if [ -e "/dev/watchdog0" ]; then
        echo "1" > /dev/watchdog0 2>/dev/null || true
    fi
    sleep 1
done
