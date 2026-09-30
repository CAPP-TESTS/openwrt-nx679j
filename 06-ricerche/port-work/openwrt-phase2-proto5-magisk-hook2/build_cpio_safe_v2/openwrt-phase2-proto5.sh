#!/system/bin/sh
stage="$1"
ts="$(/system/bin/date +%s)"

/system/bin/echo "$ts $stage" >> /data/local/tmp/openwrt-phase2-proto5.log
/system/bin/setprop openwrt.phase2.proto5.last_stage "$stage"

if [ "$stage" = "post-fs-data" ]; then
    if /system/bin/grep -q " /data/local/openwrt-rootfs/proc " /proc/mounts; then
        /system/bin/setprop openwrt.phase2.proto5.openwrt_start already_mounted
    elif [ -x /data/local/openwrt-start.sh ]; then
        /system/bin/sh /data/local/openwrt-start.sh >> /data/local/tmp/openwrt-phase2-proto5-openwrt.log 2>&1 &
        /system/bin/setprop openwrt.phase2.proto5.openwrt_start launched
    else
        /system/bin/setprop openwrt.phase2.proto5.openwrt_start missing_script
    fi
fi

if [ "$stage" = "boot-completed" ]; then
    /system/bin/setprop openwrt.phase2.proto5.ready 1
fi
