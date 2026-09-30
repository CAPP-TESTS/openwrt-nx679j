#!/bin/bash
# Mappa: quale partizione contiene le stringhe che governano il boot? (solo lettura)
set -u
D=0123456789ABCDEF
OUT=/home/user/nx679j-stock/experiments/20260917-boot-chain/live
: > "$OUT/string-map.txt"
for p in uefi_a xbl_a abl_a xbl_config_a imagefv_a recovery_a tz_a aop_a devcfg_a qupfw_a vm-bootsys_a; do
  timeout 120 adb -s $D exec-out su -c "
    f=/dev/block/by-name/$p
    sz=\$(cat /sys/class/block/\$(basename \$(readlink -f \$f))/size 2>/dev/null)
    echo \"### $p  settori_512=\$sz\"
    for pat in 'ANDROID!' 'VNDRBOOT' 'boot.img' 'dtbo' 'vbmeta' 'slot_suffix' 'fastboot' 'AVB0' 'LinuxLoader' 'Android Bootloader' 'avbtool' '/lib/modules' 'modules.load' 'fstab.qcom' 'bootconfig' 'ufshc'; do
      printf '   %-20s %s\n' \"\$pat\" \"\$(grep -a -o -F \"\$pat\" \$f 2>/dev/null | head -4000 | wc -l)\"
    done
  " >> "$OUT/string-map.txt" 2>>"$OUT/string-map.err"
  echo "fatto $p"
done
echo done
