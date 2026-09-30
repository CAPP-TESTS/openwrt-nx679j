#!/system/bin/sh
# Magisk Module: Gunyah Watchdog Pet
# Keeps watchdog happy across kernel transitions

MODDIR=${0%/*}
LOG=/data/local/tmp/watchdog_pet.log

echo "[$(date)] Watchdog pet module started" >> $LOG

# Find watchdog device
WATCHDOG=$(find /sys/devices -name "*gh*wdt*" -o -name "*gunyah*" 2>/dev/null | head -1)

if [ -z "$WATCHDOG" ]; then
    echo "[$(date)] No Gunyah watchdog found" >> $LOG
    exit 0
fi

echo "[$(date)] Found watchdog: $WATCHDOG" >> $LOG

# Pet watchdog continuously in background
while true; do
    # Try multiple methods
    echo "1" > /dev/watchdog0 2>/dev/null
    echo "V" > /dev/watchdog 2>/dev/null
    
    # Try sysfs
    for f in $(find $WATCHDOG -name "timeout" -o -name "nowayout" 2>/dev/null); do
        echo "300" > $f 2>/dev/null
    done
    
    sleep 1
done &

echo "[$(date)] Watchdog pet daemon running in background (PID: $!)" >> $LOG
