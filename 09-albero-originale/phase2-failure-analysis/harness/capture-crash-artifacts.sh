#!/system/bin/sh

STAGE="${1:-manual}"
BASE="/data/local/tmp/crash-capture"

TS="$(/system/bin/date +%s 2>/dev/null)"
case "${TS}" in
	''|*[!0-9]*)
		TS=0
		;;
esac
if [ "${TS}" -lt 1000000000 ]; then
	UPTIME_S="$(/system/bin/cat /proc/uptime 2>/dev/null | /system/bin/cut -d. -f1)"
	case "${UPTIME_S}" in
		''|*[!0-9]*)
			UPTIME_S=0
			;;
	esac
	TS="early-${UPTIME_S}"
fi
BOOTID="$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)"
[ -n "$BOOTID" ] || BOOTID="unknown-bootid"

OUT="${BASE}/${TS}-${BOOTID}-${STAGE}"
/system/bin/mkdir -p "${OUT}/pstore"

{
	/system/bin/echo "stage=${STAGE}"
	/system/bin/echo "timestamp_epoch=${TS}"
	/system/bin/echo "boot_id=${BOOTID}"
} > "${OUT}/meta.txt"

/system/bin/getprop ro.boot.bootreason > "${OUT}/prop.bootreason.txt" 2>/dev/null
/system/bin/getprop ro.boot.slot_suffix > "${OUT}/prop.slot_suffix.txt" 2>/dev/null
/system/bin/getprop ro.boot.verifiedbootstate > "${OUT}/prop.verifiedbootstate.txt" 2>/dev/null
/system/bin/getprop ro.hardware > "${OUT}/prop.ro.hardware.txt" 2>/dev/null
/system/bin/getprop > "${OUT}/getprop.txt" 2>/dev/null
/system/bin/cat /proc/cmdline > "${OUT}/proc_cmdline.txt" 2>/dev/null

/system/bin/ls -la /sys/fs/pstore > "${OUT}/pstore_ls.txt" 2>/dev/null
/system/bin/cp -a /sys/fs/pstore/. "${OUT}/pstore/" 2>/dev/null

if [ -f /proc/last_kmsg ]; then
	/system/bin/cp /proc/last_kmsg "${OUT}/last_kmsg.txt" 2>/dev/null
fi

/system/bin/dmesg > "${OUT}/dmesg.txt" 2>/dev/null
/system/bin/logcat -b kernel -d > "${OUT}/logcat_kernel.txt" 2>/dev/null
/system/bin/logcat -d > "${OUT}/logcat_main.txt" 2>/dev/null

/system/bin/echo "download_mode=$(cat /sys/module/qcom_dload_mode/parameters/download_mode 2>/dev/null)" > "${OUT}/dload_mode.txt"
/system/bin/echo "emmc_dload=$(cat /sys/kernel/dload/emmc_dload 2>/dev/null)" >> "${OUT}/dload_mode.txt"
/system/bin/echo "dload_mode=$(cat /sys/kernel/dload/dload_mode 2>/dev/null)" >> "${OUT}/dload_mode.txt"

/system/bin/ln -sfn "${OUT}" "${BASE}/latest"
/system/bin/chmod -R a+rX "${OUT}" 2>/dev/null
