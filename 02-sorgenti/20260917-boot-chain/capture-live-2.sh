#!/bin/bash
# Seconda raccolta SOLO LETTURA: interfacce AVB nel DT, bootargs completi, stato moduli, timing init.
set -u
D=0123456789ABCDEF
A=(adb -s $D)
OUT=/home/user/nx679j-stock/experiments/20260917-boot-chain/live
run() { local n="$1"; shift; echo "### $n :: $*" >> "$OUT/commands.log"
  "${A[@]}" exec-out su -c "$*" > "$OUT/$n.txt" 2> "$OUT/$n.err"; echo "rc=$? b=$(stat -c%s "$OUT/$n.txt")" >> "$OUT/commands.log"; }

run avb_dt 'ls -lR /proc/device-tree/firmware/android/vbmeta/ /proc/device-tree/firmware/android/fstab/'
run avb_dt_vals 'for d in /proc/device-tree/firmware/android/vbmeta /proc/device-tree/firmware/android/fstab; do echo "=== $d"; for f in $d/*; do echo "--- $f"; tr "\0" "\n" < "$f"; done; done'
run chosen_bootargs_full 'cat /proc/device-tree/chosen/bootargs; echo; wc -c < /proc/device-tree/chosen/bootargs'
run boottime 'getprop | grep -E "ro.boottime|ro.boot.hardware|ro.boot.veritymode|ro.boot.slot"'
run root_mount 'grep -E "rootfs|/mnt|ramdisk|tmpfs / " /proc/mounts; echo ===; head -12 /proc/mounts'
run ramdisk_dirs 'ls -la /mnt 2>&1; ls -la /first_stage_ramdisk 2>&1 | head; ls -la / 2>&1 | head -30'
run modules_order_tail 'cat /proc/modules | tail -20'
run modules_names 'cat /proc/modules | awk "{print \$1}"'
run magiskinit_hash 'sha256sum /data/adb/magisk/magiskinit /data/adb/magisk/magiskboot 2>&1'
run rc_insmod 'grep -rn "insmod" /vendor/etc/init/ /system/etc/init/ /system/etc/init/hw/ 2>/dev/null'
run rc_modprobe 'grep -rn "modprobe" /vendor/etc/init/ /system/etc/init/ /system/etc/init/hw/ 2>/dev/null'
run rc_loadlist 'grep -rn "modules.load\|/lib/modules" /vendor/etc/ /system/etc/ 2>/dev/null | head -20'
run dmesg_range 'dmesg | head -1; dmesg | tail -1; dmesg | wc -l'
run dmesg_init_lines 'dmesg | grep -E "init: |Loading module" | head -40'
run vendor_dlkm 'ls /vendor_dlkm/lib/modules | wc -l; ls /vendor_dlkm/lib/modules | head -5; echo ===; ls /vendor/lib/modules | wc -l'
run dlkm_rc 'ls /vendor/etc/init/ | grep -i dlkm; cat /vendor/etc/init/init.qcom.modules.rc 2>/dev/null'
run gpt_attrs_raw 'for p in boot_a boot_b vbmeta_a vbmeta_b vendor_boot_a dtbo_a xbl_a abl_a recovery_a; do echo -n "$p "; readlink -f /dev/block/by-name/$p; done'
run dtbo_count 'wc -c < /proc/device-tree/model; ls /proc/device-tree/__symbols__ 2>/dev/null | head -3'
run sys_module_count 'ls /sys/module | wc -l'
run init_props 'getprop | grep -E "^\[persist.sys|^\[init.svc" | head -10'
echo done
