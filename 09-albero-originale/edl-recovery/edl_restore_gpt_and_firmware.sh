#!/bin/bash
set -u
EDL=/home/user/venvs/edk2/bin/edl
L=/home/user/nx679j-stock/edl-recovery/prog_firehose_ddr.melf
GP=/home/user/nx679j-stock/native-openwrt-usb-build/headless-upstream/abl-compat-v12/artifacts/partition-analysis
PATCH=/home/user/nx679j-stock/native-openwrt-usb-build/headless-upstream/abl-compat-v12/artifacts/partition-analysis/gpt-patch
R=/home/user/nx679j-stock/edl-recovery/restore-stock-a
RECON=/home/user/nx679j-stock/edl-recovery/recon-20260717-183330
LOG=/home/user/nx679j-stock/edl-recovery/restore-stock-a/edl-gpt-restore.log
mkdir -p /tmp/nx679j-edl-research/logs
exec > >(tee -a "$LOG") 2>&1
echo "=== $(date -Is) GPT+FW restore ==="
lsusb -d 05c6:9008 || { echo no9008; exit 1; }

run(){ echo "CMD: $*"; "$@" || { echo FAIL: $*; return 1; }; }

# Map sdX -> LUN order from prior analysis (common SM8450: need verify via printgpt)
# First printgpt to learn LUN numbering
run "$EDL" printgpt --loader="$L" --memory=ufs | tee /home/user/nx679j-stock/edl-recovery/restore-stock-a/printgpt-before-gpt-restore.txt

# Restore primary GPT headers using edl w gpt if supported, else raw sector write
# Prefer restore_* from gpt-patch (pre-experiment) for sdb/sdc/sde which were patched
for lun_pair in "sdb:?" "sdc:?" "sde:?"; do
  echo "will restore from patch restore bins after detecting LUN map"
done

# Detect which LUN has boot_a by printgpt content
python3 - <<'PY'
import re
from pathlib import Path
text=Path("/home/user/nx679j-stock/edl-recovery/restore-stock-a/printgpt-before-gpt-restore.txt").read_text(errors='ignore')
# edl printgpt marks Lun X
luns={}
cur=None
for line in text.splitlines():
    m=re.search(r'LUN\s*[#:]?\s*(\d+)|Lun\s+(\d+)|lun=(\d+)', line, re.I)
    if m:
        cur=int([g for g in m.groups() if g][0])
        luns.setdefault(cur, [])
    if cur is not None and re.search(r'\bboot_a\b', line):
        luns[cur].append('boot_a')
    if cur is not None and re.search(r'\bxbl_a\b', line):
        luns[cur].append('xbl_a')
    if cur is not None and re.search(r'\bssd\b', line) and 'boot' not in line:
        pass
Path("/home/user/nx679j-stock/edl-recovery/restore-stock-a/lun_map_guess.txt").write_text(str(luns))
print('lun map guess', luns)
PY

# Write GPT using edl: for each restore pri, try matching LUN by containing partition names in gpt-parsed json
# Safer approach: write known gpt_sd*.bin to LUN index from gpt-parsed-4k.json if present
python3 - <<'PY'
import json,re
from pathlib import Path
# Many QC tools use: LUN0=sda... but this device used sdX names from kernel
# From prior work notes: 4K LBA UFS, boot on sde typically for SM8450
# We'll use edl rl style - actually write via:
#   edl w gpt file --memory=ufs --lun=N
print('see printgpt for LUN ids')
PY

# Restore sde (main android partitions) GPT first - usually highest LUN numbers
# Try common mapping: printgpt file labels "Parsing Lun 0" etc.
python3 <<'PY'
import re
from pathlib import Path
text=Path("/home/user/nx679j-stock/edl-recovery/restore-stock-a/printgpt-before-gpt-restore.txt").read_text(errors='ignore')
# find blocks
parts=re.split(r'(?i)(?:LUN|Lun)\s*(\d+)', text)
# re.split keeps delimiters
mapping={}
if len(parts)>1:
    # parts[0] preamble, then id, content, id, content...
    it=iter(parts[1:])
    for lun, content in zip(it, it):
        names=set(re.findall(r'\b([a-zA-Z][a-zA-Z0-9_]+)\b', content))
        mapping[int(lun)]=names
        print('LUN', lun, 'has boot_a', 'boot_a' in names, 'xbl_a', 'xbl_a' in names, 'sample', list(sorted(names))[:12])
Path("/home/user/nx679j-stock/edl-recovery/restore-stock-a/lun_names.json").write_text(__import__('json').dumps({str(k):sorted(list(v))[:50] for k,v in mapping.items()}, indent=2))
# decide restore targets
for lun, names in mapping.items():
    if 'boot_a' in names or 'super' in names:
        print('ANDROID_LUN', lun)
    if 'xbl_a' in names:
        print('XBL_LUN', lun)
PY

# Use restore pri/sec bins - write to start of LUN and backup LBA
# edl supports: edl w gpt gpt_main.bin --lun=N --memory=ufs
# Our restore_pri is 64KiB primary table region

restore_gpt_lun() {
  local lun=$1
  local pri=$2
  local sec=$3
  echo "Restoring GPT on LUN $lun from $pri"
  # primary at LBA 0 area - edl w gpt writes both often
  $EDL w gpt "$pri" --loader="$L" --memory=ufs --lun="$lun" && return 0
  # fallback raw write sector 0
  $EDL ws 0 "$pri" --loader="$L" --memory=ufs --lun="$lun"
}

# Parse ANDROID_LUN / XBL_LUN from python output by re-running simple
ANDROID_LUN=$($EDL printgpt --loader="$L" --memory=ufs 2>/dev/null | python3 -c "
import sys,re
text=sys.stdin.read(); cur=None; mapp={}
for line in text.splitlines():
  m=re.search(r'LUN\s*(\d+)|Lun\s*(\d+)', line, re.I)
  if m: cur=int([x for x in m.groups() if x][0]); mapp.setdefault(cur,set())
  if cur is not None:
    for n in re.findall(r'\b[a-z][a-z0-9_]+\b', line):
      mapp[cur].add(n)
for lun,names in mapp.items():
  if 'boot_a' in names: print(lun)
")
XBL_LUN=$($EDL printgpt --loader="$L" --memory=ufs 2>/dev/null | python3 -c "
import sys,re
text=sys.stdin.read(); cur=None; mapp={}
for line in text.splitlines():
  m=re.search(r'LUN\s*(\d+)|Lun\s*(\d+)', line, re.I)
  if m: cur=int([x for x in m.groups() if x][0]); mapp.setdefault(cur,set())
  if cur is not None:
    for n in re.findall(r'\b[a-z][a-z0-9_]+\b', line):
      mapp[cur].add(n)
for lun,names in mapp.items():
  if 'xbl_a' in names: print(lun)
")
echo ANDROID_LUN=$ANDROID_LUN XBL_LUN=$XBL_LUN

# Prefer pre-patch restore images for sde (android) and sdb/sdc if those LUNs match
# Heuristic from SM8450 Nubia: often LUN4/5 = sde style
if [ -n "$ANDROID_LUN" ]; then
  restore_gpt_lun "$ANDROID_LUN" "$PATCH/sde_restore_pri.bin" "$PATCH/sde_restore_sec.bin" ||   restore_gpt_lun "$ANDROID_LUN" "$GP/gpt_sde.bin" "$PATCH/sde_restore_sec.bin" || true
fi
if [ -n "$XBL_LUN" ]; then
  # xbl often on LUN with xbl - may be sda/sdb - try sdb restore if names match
  restore_gpt_lun "$XBL_LUN" "$PATCH/sdb_restore_pri.bin" "$PATCH/sdb_restore_sec.bin" ||   restore_gpt_lun "$XBL_LUN" "$GP/gpt_sdb.bin" "" || true
fi

# Also restore all LUNs from gpt_sd*.bin in order 0..5 if printgpt shows 6 luns
# Safer: write gpt_sde restore already done; write sdc for modem etc.
for pair in "0:gpt_sda.bin" "1:gpt_sdb.bin" "2:gpt_sdc.bin" "3:gpt_sdd.bin" "4:gpt_sde.bin" "5:gpt_sdf.bin"; do
  lun=${pair%%:*}; f=${pair##*:}
  # Use restore pri when available for patched ones
  case $f in
    gpt_sdb.bin) pri=$PATCH/sdb_restore_pri.bin ;;
    gpt_sdc.bin) pri=$PATCH/sdc_restore_pri.bin ;;
    gpt_sde.bin) pri=$PATCH/sde_restore_pri.bin ;;
    *) pri=$GP/$f ;;
  esac
  [ -f "$pri" ] || pri=$GP/$f
  [ -f "$pri" ] || continue
  echo "Attempt LUN $lun <- $pri"
  $EDL w gpt "$pri" --loader="$L" --memory=ufs --lun="$lun" 2>&1 | tail -5 || true
done

# Re-flash stock xbl/abl/boot chain (idempotent)
for slot in a b; do
  $EDL w xbl_$slot $R/xbl_a_from_b.bin --loader="$L" --memory=ufs
  $EDL w xbl_config_$slot $R/xbl_config_a_from_b.bin --loader="$L" --memory=ufs
  $EDL w abl_$slot $R/abl_a_from_b.bin --loader="$L" --memory=ufs
done
$EDL w boot_a $RECON/boot_a.bin --loader="$L" --memory=ufs
$EDL w vendor_boot_a $RECON/vendor_boot_a.bin --loader="$L" --memory=ufs
$EDL w dtbo_a $RECON/dtbo_a.bin --loader="$L" --memory=ufs
$EDL w vbmeta_a $RECON/vbmeta_a.bin --loader="$L" --memory=ufs
$EDL setactiveslot a --loader="$L" --memory=ufs
$EDL reset --loader="$L" --memory=ufs --resetmode=reset
echo DONE
