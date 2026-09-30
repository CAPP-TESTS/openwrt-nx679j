#!/system/bin/sh
stage="$1"
ts="$(/system/bin/date +%s)"

echo "$ts $stage" >> /data/local/tmp/openwrt-phase2-proto1.log
/system/bin/setprop openwrt.phase2.proto1.last_stage "$stage"

if [ "$stage" = "boot-completed" ]; then
	/system/bin/setprop openwrt.phase2.proto1.ready 1
fi
