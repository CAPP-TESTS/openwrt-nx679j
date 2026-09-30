#!/bin/sh
# Riporta il device sullo slot B (OpenWrt): il soggiorno su Android era per la
# prova dell'oracolo, non e' la configurazione di lavoro.
echo "=== entro nel bootloader da Android ==="
adb reboot bootloader 2>&1 | head -1
sleep 30
fastboot devices 2>&1 | head -2
echo "=== slot B e avvio ==="
fastboot --set-active=b 2>&1 | tail -2
fastboot continue 2>&1 | tail -2
echo "=== attendo OpenWrt ==="
sleep 200
timeout 20 ssh -T -q -i ~/.ssh/nx679j_key -o ConnectTimeout=6 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@10.0.0.1 '
echo "U=$(cut -d. -f1 /proc/uptime) ui=$(pidof nx679j-ui|wc -w) ping=$(ping -c1 -W3 1.1.1.1 >/dev/null 2>&1 && echo OK || echo KO)"
md5sum /usr/lib/nx679j/modem/nx679j-ui 2>/dev/null | cut -c1-16
ls /dev/input/ | tr "\n" " "
echo; grep -a TELEMETRIA /tmp/display-late.log | tail -1' 2>&1 | head -8
