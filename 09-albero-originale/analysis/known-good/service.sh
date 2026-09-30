#!/system/bin/sh
LOG=/data/local/tmp/openwrt-phase2-module-service.log
SETPROP=/system/bin/setprop

echo "[$(/system/bin/date +%s)] module service enter" >> "$LOG"
$SETPROP openwrt.phase2.module.service enter
$SETPROP openwrt.phase2.module.service_result pending
IP=/system/bin/ip

has_rmnet_default_route() {
	"$IP" -4 route show table all 2>/dev/null | grep -q '^default .* dev rmnet_data[0-9]'
}

BOOT_OK=0
I=0
while [ "$I" -lt 180 ]; do
	if [ "$(/system/bin/getprop sys.boot_completed)" = "1" ]; then
		BOOT_OK=1
		break
	fi
	/system/bin/sleep 1
	I=$((I + 1))
done

if [ "$BOOT_OK" != "1" ]; then
	echo "[$(/system/bin/date +%s)] boot_complete_timeout" >> "$LOG"
	$SETPROP openwrt.phase2.module.service_result boot_complete_timeout
	$SETPROP openwrt.phase2.module.service done
	exit 0
fi

$SETPROP openwrt.phase2.module.service boot_complete_seen
NET_READY=0
J=0
while [ "$J" -lt 180 ]; do
	NET_STATE_NOW="$(/system/bin/getprop openwrt.phase2.net.state)"
	if [ "$NET_STATE_NOW" = "failed" ]; then
		break
	fi
	if [ "$NET_STATE_NOW" = "applied" ] && has_rmnet_default_route; then
		NET_READY=1
		break
	fi
	/system/bin/sleep 1
	J=$((J + 1))
done
echo "[$(/system/bin/date +%s)] net_ready=$NET_READY wait_s=${J}" >> "$LOG"
NETD_STATE="$(/system/bin/getprop init.svc.netd)"
if [ "$NET_READY" = "1" ] && [ "$NETD_STATE" = "running" ]; then
	/system/bin/setprop ctl.stop netd
	/system/bin/sleep 2
	NETD_STATE="$(/system/bin/getprop init.svc.netd)"
	if ! has_rmnet_default_route; then
		/system/bin/setprop ctl.start netd
		/system/bin/sleep 2
		NETD_STATE="$(/system/bin/getprop init.svc.netd)"
		$SETPROP openwrt.phase3.android.netd "rollback_${NETD_STATE}"
		echo "[$(/system/bin/date +%s)] netd rollback (route_lost) state=$NETD_STATE" >> "$LOG"
	else
		$SETPROP openwrt.phase3.android.netd "$NETD_STATE"
		echo "[$(/system/bin/date +%s)] netd_state=$NETD_STATE" >> "$LOG"
	fi
elif [ "$NET_READY" != "1" ]; then
	$SETPROP openwrt.phase3.android.netd defer_no_route
	echo "[$(/system/bin/date +%s)] netd_state=defer_no_route" >> "$LOG"
else
	$SETPROP openwrt.phase3.android.netd "$NETD_STATE"
	echo "[$(/system/bin/date +%s)] netd_state=$NETD_STATE" >> "$LOG"
fi

WIFICOND_STATE="$(/system/bin/getprop init.svc.wificond)"
if [ "$WIFICOND_STATE" = "running" ]; then
	/system/bin/setprop ctl.stop wificond
	/system/bin/sleep 2
fi
WIFICOND_STATE="$(/system/bin/getprop init.svc.wificond)"
$SETPROP openwrt.phase3.android.wificond "$WIFICOND_STATE"
echo "[$(/system/bin/date +%s)] wificond_state=$WIFICOND_STATE" >> "$LOG"

CNSS_STATE="$(/system/bin/getprop init.svc.cnss-daemon)"
if [ "$CNSS_STATE" = "running" ]; then
	/system/bin/setprop ctl.stop cnss-daemon
	/system/bin/sleep 2
fi
CNSS_STATE="$(/system/bin/getprop init.svc.cnss-daemon)"
$SETPROP openwrt.phase3.android.cnss "$CNSS_STATE"
echo "[$(/system/bin/date +%s)] cnss_state=$CNSS_STATE" >> "$LOG"

if [ "$NET_READY" = "1" ]; then
	ZS_STATE="$(/system/bin/getprop init.svc.zygote_secondary)"
	if [ "$ZS_STATE" = "running" ]; then
		/system/bin/setprop ctl.stop zygote_secondary
		/system/bin/sleep 2
	fi
	ZS_STATE="$(/system/bin/getprop init.svc.zygote_secondary)"
	$SETPROP openwrt.phase4.android.zygote_secondary "$ZS_STATE"
	echo "[$(/system/bin/date +%s)] zygote_secondary_state=$ZS_STATE" >> "$LOG"

	SF_STATE="$(/system/bin/getprop init.svc.surfaceflinger)"
	if [ "$SF_STATE" = "running" ]; then
		/system/bin/setprop ctl.stop surfaceflinger
		/system/bin/sleep 2
	fi
	SF_STATE="$(/system/bin/getprop init.svc.surfaceflinger)"
	$SETPROP openwrt.phase4.android.surfaceflinger "$SF_STATE"
	echo "[$(/system/bin/date +%s)] surfaceflinger_state=$SF_STATE" >> "$LOG"

	ZP="$(/system/bin/pidof zygote64 2>/dev/null)"
	if [ -z "$ZP" ]; then
		$SETPROP openwrt.phase4.android.zygote_primary no_pid
		echo "[$(/system/bin/date +%s)] zygote_primary_state=no_pid" >> "$LOG"
	else
		/system/bin/kill -STOP "$ZP" 2>/dev/null || true
		/system/bin/sleep 3
		ZP_STATE="$(/system/bin/ps -A | /system/bin/grep ' zygote64$' | /system/bin/head -n1 | /system/bin/tr -s ' ' | /system/bin/cut -d' ' -f9)"
		PING9=1
		PING1=1
		/system/bin/ping -c 1 -W 3 9.9.9.9 >/dev/null 2>&1 && PING9=0 || true
		/system/bin/ping -c 1 -W 3 1.0.0.1 >/dev/null 2>&1 && PING1=0 || true
		if [ "$PING9" = "0" ] || [ "$PING1" = "0" ]; then
			$SETPROP openwrt.phase4.android.zygote_primary "suspended_${ZP_STATE}"
			echo "[$(/system/bin/date +%s)] zygote_primary_state=suspended_${ZP_STATE} ping9=$PING9 ping1=$PING1" >> "$LOG"
		else
			/system/bin/kill -CONT "$ZP" 2>/dev/null || true
			/system/bin/sleep 1
			ZP_STATE="$(/system/bin/ps -A | /system/bin/grep ' zygote64$' | /system/bin/head -n1 | /system/bin/tr -s ' ' | /system/bin/cut -d' ' -f9)"
			$SETPROP openwrt.phase4.android.zygote_primary "rollback_${ZP_STATE}"
			echo "[$(/system/bin/date +%s)] zygote_primary_state=rollback_${ZP_STATE} ping9=$PING9 ping1=$PING1" >> "$LOG"
		fi
	fi
else
	$SETPROP openwrt.phase4.android.zygote_secondary defer_no_route
	$SETPROP openwrt.phase4.android.surfaceflinger defer_no_route
	$SETPROP openwrt.phase4.android.zygote_primary defer_no_route
	echo "[$(/system/bin/date +%s)] phase4_framework_cut=defer_no_route" >> "$LOG"
fi
NET_STATE="$(/system/bin/getprop openwrt.phase2.net.state)"
NET_REASON="$(/system/bin/getprop openwrt.phase2.net.reason)"
if [ -n "$NET_STATE" ] && [ -n "$NET_REASON" ]; then
	$SETPROP openwrt.phase2.module.service_result "net_${NET_STATE}_${NET_REASON}"
elif [ -n "$NET_STATE" ]; then
	$SETPROP openwrt.phase2.module.service_result "net_${NET_STATE}"
else
	$SETPROP openwrt.phase2.module.service_result net_state_unknown
fi

$SETPROP openwrt.phase2.module.service done
echo "[$(/system/bin/date +%s)] module service done" >> "$LOG"
