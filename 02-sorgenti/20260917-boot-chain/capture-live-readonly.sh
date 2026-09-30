#!/bin/bash
# Raccolta SOLO LETTURA dal device (NX679J). Nessuna scrittura, nessun reboot.
set -u
D=0123456789ABCDEF
A=(adb -s $D)
OUT=/home/user/nx679j-stock/experiments/20260917-boot-chain/live
mkdir -p "$OUT"
: > "$OUT/commands.log"
run() { local n="$1"; shift; echo "### $n :: $*" >> "$OUT/commands.log"
  "${A[@]}" exec-out su -c "$*" > "$OUT/$n.txt" 2> "$OUT/$n.err"; echo "rc=$? b=$(stat -c%s "$OUT/$n.txt")" >> "$OUT/commands.log"; }

run identity 'id; uname -r; getprop ro.boot.slot_suffix; getprop ro.boot.verifiedbootstate; getprop ro.boot.veritymode; getprop ro.boot.dtb_idx; getprop ro.boot.dtbo_idx; getprop ro.boot.hardware; getprop ro.boot.baseband; getprop ro.boot.serialno; cat /proc/uptime'
run cmdline 'cat /proc/cmdline'
run bootconfig 'cat /proc/bootconfig'
run firmware_android 'ls -l /proc/device-tree/firmware/android/; for f in /proc/device-tree/firmware/android/*; do echo "--- $f"; cat "$f"; echo; done'
run dt_chosen 'for f in /proc/device-tree/chosen/*; do echo "--- $f"; head -c 300 "$f"; echo; done'
run dt_model 'tr -d "\0" < /proc/device-tree/model; echo; tr -d "\0" < /proc/device-tree/compatible; echo'
run dt_root 'ls /proc/device-tree/ | head -50'
run init1 'ls -l /proc/1/exe; tr "\0" " " < /proc/1/cmdline; echo; cat /proc/1/comm'
run mounts 'cat /proc/mounts'
run devices_chr 'head -25 /proc/devices; echo ===; ls -l /dev | head -25'
run modules_count 'wc -l /proc/modules'
run modules_list 'cat /proc/modules'
run lib_modules 'ls /lib/modules/ | wc -l; ls /lib/modules/ | head -8; echo "--- modules.load:"; cat /lib/modules/modules.load; echo "--- fine modules.load"'
run lib_modules_recovery 'wc -l /lib/modules/modules.load.recovery; head -4 /lib/modules/modules.load.recovery'
run modules_dep 'head -6 /lib/modules/modules.dep; echo ===softdep; head -6 /lib/modules/modules.softdep; echo ===alias; head -4 /lib/modules/modules.alias; echo ===blocklist; cat /lib/modules/modules.blocklist'
run ko_dirs 'ls -d /vendor/lib/modules /vendor_dlkm/lib/modules /system/lib/modules 2>&1; ls /vendor/lib/modules 2>&1 | head -5'
run kernel_modprobe_path 'cat /proc/sys/kernel/modprobe; ls -l /system/bin/modprobe /vendor/bin/modprobe /sbin/modprobe 2>&1'
run initstate_sample 'for m in gh_virt_wdt qcom_wdt_core smem socinfo qrtr boot_stats icnss2 cnss2 cdsp-loader atmel_mxt_ts aw9620x fsa4480_i2c mhi_dev_drv mhi_dev_netdev ipa_fmwk; do printf "%-18s initstate=%s\n" "$m" "$(cat /sys/module/$m/initstate 2>/dev/null || echo ASSENTE)"; done'
run rc_vendor 'ls /vendor/etc/init/'
run rc_vendor_hw 'ls -R /vendor/etc/init/hw/ 2>&1 | head -30'
run rc_system 'ls /system/etc/init/'
run rc_hw 'ls -R /system/etc/init/hw/ 2>&1 | head -20'
run rc_grep_modules 'grep -rnE "insmod|modprobe|modules.load|finit_module" /vendor/etc/init/ /system/etc/init/ 2>/dev/null | head -40'
run rc_grep_uevent 'grep -rnE "insmod|modprobe" /vendor/etc/ueventd.rc /system/etc/ueventd.rc /vendor/ueventd.rc 2>/dev/null | head -20'
run selinux 'getenforce; getprop | grep -iE "magisk|selinux|verity|vbmeta" | head -20'
run magiskver 'magisk -v 2>&1; ls -l /data/adb/magisk 2>&1 | head -25; echo ===; cat /data/adb/magisk/config 2>&1'
run pstore 'ls -laR /sys/fs/pstore/ 2>&1 | head -20'
run dmesg_head 'dmesg | head -3'
run dmesg_modules 'dmesg | grep -iE "Loading module|modules.load|finit_module|init: " | head -30'
run dmesg_verity 'dmesg | grep -iE "verity|avb|bootconfig|dtbo" | head -30'
run gpt_sde_entries 'dd if=/dev/block/sde bs=4096 skip=2 count=1 2>/dev/null | base64 -w0'
run gpt_sde_header 'dd if=/dev/block/sde bs=4096 skip=1 count=1 2>/dev/null | base64 -w0'
run gpt_sdb_entries 'dd if=/dev/block/sdb bs=4096 skip=2 count=1 2>/dev/null | base64 -w0'
run gpt_sdc_entries 'dd if=/dev/block/sdc bs=4096 skip=2 count=1 2>/dev/null | base64 -w0'
run misc_head 'dd if=/dev/block/by-name/misc bs=4096 count=1 2>/dev/null | base64 -w0'
run byname 'ls -l /dev/block/by-name/'
run hashes_boot 'sha256sum /dev/block/by-name/boot_a /dev/block/by-name/boot_b /dev/block/by-name/vendor_boot_a /dev/block/by-name/vendor_boot_b /dev/block/by-name/dtbo_a /dev/block/by-name/dtbo_b /dev/block/by-name/vbmeta_a /dev/block/by-name/vbmeta_b'
run hashes_bl 'sha256sum /dev/block/by-name/xbl_a /dev/block/by-name/xbl_b /dev/block/by-name/abl_a /dev/block/by-name/abl_b'
run blocksize 'cat /sys/block/sde/queue/logical_block_size /sys/block/sde/queue/physical_block_size /sys/block/sde/size /sys/block/sdb/size /sys/block/sdc/size'
echo done
