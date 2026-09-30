#!/bin/sh
echo "=== FULL SCAN: cerco first-1MB = v55 (139dd010b9df1a2046337cc4c55bcc72) ==="
while read maj min blocks nam; do
  [ "$nam" = "name" ] && continue
  case "$nam" in *:*) continue;; esac
  n=/tmp/fsc_$nam
  mknod $n b $maj $min 2>/dev/null
  h=$(dd if=$n bs=1M count=1 2>/dev/null | md5sum | cut -d' ' -f1)
  case "$h" in
    139dd010b9df1a2046337cc4c55bcc72) echo "*** V55 TROVATO IN: $nam ($maj:$min, $blocks KB)";;
    d41d8cd98f00b204e9800998ecf8427e|"") : ;;
  esac
done < /proc/partitions
echo "=== scan completo ==="
