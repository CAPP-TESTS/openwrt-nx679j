#!/system/bin/sh
# Read-only: cerca le firme letterali in tutte le partizioni by-name piccole.
# Output: partizione<TAB>firma<TAB>offset
OUT=/data/local/tmp/sigscan.txt
: > $OUT
for p in /dev/block/by-name/*; do
  n=$(basename $p)
  case "$n" in sda|sdb|sdc|sdd|sde|sdf) continue;; esac
  tgt=$(readlink -f $p)
  sz=$(blockdev --getsize64 $tgt 2>/dev/null) || continue
  [ -z "$sz" ] && continue
  if [ "$sz" -gt 314572800 ]; then continue; fi
  for sig in 'ANDROID!' 'VNDRBOOT'; do
    grep -abo -- "$sig" "$tgt" 2>/dev/null | while IFS=: read off rest; do
      printf '%s\t%s\t%s\n' "$n" "$sig" "$off" >> $OUT
    done
  done
done
echo DONE >> $OUT
