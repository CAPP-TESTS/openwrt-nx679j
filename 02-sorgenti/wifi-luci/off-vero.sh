#!/bin/sh
# Spegnimento VERO del device (non un riavvio): i rail delle periferiche vengono
# tagliati. Serve per il test del touch (25/09): un reset del SoC non basta.
# Eseguito dall'host, guida adb.
echo "=== spegnimento vero in corso ==="
adb shell su -c "reboot -p" 2>&1 | head -2 || adb shell reboot -p 2>&1 | head -2
sleep 12
echo "=== il device risponde ancora? (deve essere spento) ==="
ping -c2 -W3 10.0.0.1 2>&1 | tail -2
adb devices 2>&1 | head -3
echo "=== se e spento: riaccendilo col tasto laterale ==="
