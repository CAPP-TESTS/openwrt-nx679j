#!/bin/sh
echo "=== GPT names (sde) ==="
mknod /tmp/sdedisk b 8 64 2>/dev/null
dd if=/tmp/sdedisk bs=512 skip=1 count=64 2>/dev/null | tr -d '\000' | tr -c '[:print:]' '\n' | grep -aiE 'boot|vendor|dtbo|recovery' | head -40
echo "=== first-1MB md5 ==="
for spec in sde13 sde27 sde41 sde54 sde40 sde6; do
  maj=$(grep " $spec\$" /proc/partitions | awk '{print $1}')
  min=$(grep " $spec\$" /proc/partitions | awk '{print $2}')
  mknod /tmp/c_$spec b $maj $min 2>/dev/null
  h=$(dd if=/tmp/c_$spec bs=1M count=1 2>/dev/null | md5sum | cut -d' ' -f1)
  echo "$spec ($maj:$min): $h"
done
echo "=== sizes ==="
grep -E ' sde(13|27|40|41|54|6)\$' /proc/partitions
