#!/bin/sh
# chain.sh — catena modem X65 completa, con log e marker per step.
# Lanciato detached: sh /tmp/chain.sh > /tmp/chain.out 2>&1 &
set -u
L=/tmp/chain.log
say() { echo "[$(cat /proc/uptime | cut -d' ' -f1)] $*" >> "$L"; }
export PATH=/usr/sbin:/usr/bin:/sbin:/bin

say "BEGIN boot=$(cat /proc/sys/kernel/random/boot_id)"

# prerequisiti
touch /tmp/ipa-ingress.done /tmp/data-observed.log /tmp/cellular-staged.log
chmod 755 /tmp/nx679j-modem-prepare.sh /tmp/openwrt-*.sh /tmp/rmnet-config-agg8192
say "MSS=$(cat /sys/class/remoteproc/remoteproc3/state 2>/dev/null)"

sh /tmp/openwrt-observed-bootstrap.sh > /tmp/observed-bootstrap.log 2>&1
say "bootstrap rc=$? ready=$(grep -c READY_FOR_OBSERVED_DMS_TEST /tmp/observed-bootstrap.log)"
[ "$(grep -c READY_FOR_OBSERVED_DMS_TEST /tmp/observed-bootstrap.log)" = "1" ] || { say "STOP bootstrap"; exit 1; }

sh /tmp/openwrt-dms-observed-check.sh > /tmp/dms-check.log 2>&1
r=$?
say "dms rc=$r finished=$(grep -c OBSERVED_CHECK_FINISHED /tmp/dms-check.log)"
[ "$r" = "0" ] || { say "STOP dms"; exit 1; }

sh /tmp/nx679j-modem-prepare.sh > /tmp/prepare.log 2>&1
say "prepare rc=$?"

# holder DPM manuale (il prepare v2 chiude la sessione; la catena lo richiede vivo)
/tmp/qmi-qrtr-observed dpm-session 4 1 2 23 3600 > /tmp/dpm-session.log 2>&1 &
echo $! > /tmp/dpm-session.pid
i=0; while [ $i -lt 30 ] && ! grep -q "OPENED endpoint=4:1" /tmp/dpm-session.log; do sleep 1; i=$((i+1)); done
say "dpm open=$(grep -c OPENED /tmp/dpm-session.log)"

/tmp/rmnet-config-agg8192 rmnet_ipa0 ingress > /tmp/ingress.log 2>&1
say "ingress rc=$? accepted=$(grep -c 'INGRESS accepted' /tmp/ingress.log)"

for s in egress wda mux wds; do
  /tmp/data-staged-std.sh $s > /tmp/data-$s.log 2>&1
  say "data $s rc=$?"
done
grep -a "DATA_BEARER_READY" /tmp/data-wds.log >> /tmp/data-observed.log
say "bearer_ready=$(grep -c DATA_BEARER_READY /tmp/data-observed.log)"
[ "$(grep -c DATA_BEARER_READY /tmp/data-observed.log)" -ge 1 ] || { say "STOP data (niente bearer)"; exit 1; }

for s in inspect mtu address route route-get stats ping; do
  /tmp/openwrt-cellular-staged.sh $s > /tmp/cell-$s.log 2>&1
  say "cell $s rc=$?"
done
say "criteria=$(grep -c CELLULAR_CRITERIA_PASS /tmp/cell-ping.log)"
say "DONE"
