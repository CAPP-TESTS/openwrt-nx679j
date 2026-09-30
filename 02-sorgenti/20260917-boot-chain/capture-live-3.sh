#!/bin/bash
# Terza raccolta SOLO LETTURA (nessuna scrittura sul device).
set -u
D=0123456789ABCDEF
OUT=/home/user/nx679j-stock/experiments/20260917-boot-chain/live
r() { local n="$1"; shift
  echo "### $n :: $*" >> "$OUT/commands.log"
  timeout 90 adb -s $D exec-out su -c "$*" > "$OUT/$n.txt" 2> "$OUT/$n.err"
  echo "rc=$? b=$(stat -c%s "$OUT/$n.txt")" >> "$OUT/commands.log"; }

r boottime 'getprop | grep -E "ro.boottime|ro.boot.veritymode|ro.boot.slot"'
r chosen_bootargs 'cat /proc/device-tree/chosen/bootargs | base64 -w0; echo; cat /proc/device-tree/chosen/stdout-path | base64 -w0; echo; od -An -tx1 /proc/device-tree/chosen/linux,initrd-start; od -An -tx1 /proc/device-tree/chosen/linux,initrd-end'
r fstab_dt 'for f in dev type mnt_flags fsmgr_flags compatible status; do echo -n "+ fstab/vendor/$f : "; timeout 5 cat /proc/device-tree/firmware/android/fstab/vendor/$f | base64 -w0; echo; done'
r vbmeta_dt_parts 'timeout 5 cat /proc/device-tree/firmware/android/vbmeta/parts | base64 -w0; echo'
r gpt_sde_all 'dd if=/dev/block/sde bs=4096 skip=2 count=3 2>/dev/null | base64 -w0'
r modules_order 'cat /proc/modules | cut -d" " -f1 | tail -40'
r modules_count2 'grep -c . /proc/modules; ls /sys/module | wc -l'
r ko_vendor_count 'ls /vendor/lib/modules/*.ko 2>/dev/null | wc -l; ls /vendor_dlkm/lib/modules/*.ko 2>/dev/null | wc -l; ls /vendor/lib/modules/ | wc -l; ls /vendor_dlkm/lib/modules/ | wc -l'
r vendor_modprobe_sh 'cat /vendor/bin/vendor_modprobe.sh'
r init_qti_kernel_rc 'sed -n "1,60p" /vendor/etc/init/hw/init.qti.kernel.rc; echo =====; sed -n "170,200p" /vendor/etc/init/hw/init.qti.kernel.rc'
r init_rc_head 'sed -n "1,40p" /system/etc/init/hw/init.rc'
r dmesg_init 'dmesg | grep -iE "init:|Loading module|modules.load" | head -30'
r dmesg_module_timing 'dmesg | grep -iE "ueventd|Loading module|modprobe" | head -20'
r sys_module_all 'ls /sys/module | tr "\n" " "'
r pstore2 'ls -la /sys/fs/pstore/ 2>&1; cat /proc/pstore 2>&1 | head -5'
r ufs_info 'for d in /sys/class/block/sde /sys/class/block/sdb /sys/class/block/sdc; do echo "== $d"; cat $d/size $d/queue/logical_block_size 2>/dev/null; done'
r aliases 'od -An -c /proc/device-tree/aliases/ufshc 2>/dev/null; echo; od -An -c /proc/device-tree/aliases/serial0 2>/dev/null'
echo done
