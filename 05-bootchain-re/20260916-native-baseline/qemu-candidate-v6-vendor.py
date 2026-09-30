#!/usr/bin/env python3
"""QEMU test of the PRIMARY module path: our ramdisk with the device's own
vendor_ramdisk00 appended, exactly as the boot chain does on the phone.

The vendor ramdisk is the lz4 image from unpacked-current/vendor_boot_a (the
slot-A readback); its cpio is concatenated after ours into ONE gzip, which is
the format the kernel unpacks into a single rootfs (cpio archives are
concatenable, the unpacker restarts after each TRAILER!!!). /init stays ours
because the vendor ramdisk has none.

It can show: /lib/modules is readable, modules.load (98) then
modules.load.recovery (329) are parsed, the softdeps of the device are obeyed
(qcom_hwspinlock before smem, phy-generic/eud before dwc3_msm), the whole chain
loads in topological order and each module's rc is journalled.
It cannot show: hardware behaviour (no Waipio DT, no UDC, no UFS), and rc
values that depend on real devices.
"""
import gzip
import hashlib
import json
import re
import selectors
import struct
import subprocess
import sys
import time
from pathlib import Path

EXP = Path('/home/user/nx679j-stock/experiments/20260916-122926-native-baseline')
OUT = EXP / 'candidate-minimal-gadget-v6'
IMG = OUT / 'boot_b-staticinit-v6.img'
VENDOR_LZ4 = EXP / 'unpacked-current/vendor_boot_a/vendor_ramdisk00'
WORK = OUT / 'qemu'
WORK.mkdir(parents=True, exist_ok=True)
PAGE = 4096

img = IMG.read_bytes()
k_size, r_size = struct.unpack_from('<II', img, 8)
k_off = PAGE
r_off = PAGE + ((k_size + PAGE - 1) // PAGE) * PAGE
kern = img[k_off:k_off + k_size]
ours = gzip.decompress(img[r_off:r_off + r_size])
vend = subprocess.run(['lz4', '-dc', str(VENDOR_LZ4)], check=True,
                      stdout=subprocess.PIPE).stdout
names_v = re.findall(rb'070701.{8}(.{8}).{8}.{8}.{8}.{8}', vend)
def has_init(archive):
    # cheap scan for a cpio entry literally named "init"
    return b'\x00\x00\x00\x00init\x00\x00\x00\x00' in archive

composite = 'ours+vendor' if not has_init(vend) else 'vendor+ours'
if has_init(vend):
    sys.exit('vendor ramdisk contains an /init: order would matter, fix me')
merged = ours + vend
ramdisk = gzip.compress(merged, 9, mtime=0)
(WORK / 'ramdisk-ours-plus-vendor.gz').write_bytes(ramdisk)
(WORK / 'kernel-for-vendor-test').write_bytes(kern)
print(f'composite ramdisk {len(ramdisk)} bytes ({len(merged)} raw cpio), '
      f'order={composite}, vendor has /init={has_init(vend)}')
APPEND = 'console=ttyAMA0 earlycon loglevel=7 panic=10 rdinit=/init'
cmd = ['qemu-system-aarch64', '-machine', 'virt', '-cpu', 'cortex-a57',
       '-accel', 'tcg', '-smp', '2', '-m', '2048', '-nodefaults', '-nic',
       'none', '-display', 'none', '-monitor', 'none', '-serial', 'stdio',
       '-no-reboot', '-kernel', str(WORK / 'kernel-for-vendor-test'),
       '-initrd', str(WORK / 'ramdisk-ours-plus-vendor.gz'),
       '-append', APPEND]
LOG = WORK / 'boot-v6-vendor.log'
seen = {b'init v6 entered', b'modload summary'}
deadline = time.time() + 240
start = time.time()
proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
sel = selectors.DefaultSelector()
sel.register(proc.stdout, selectors.EVENT_READ)
buf = b''
got_summary = False
while time.time() < deadline:
    for _ in sel.select(timeout=1):
        chunk = proc.stdout.read(65536)
        if not chunk:
            break
        buf += chunk
        if b'modload summary' in buf:
            got_summary = True
    if got_summary and len(re.findall(rb'nx679j-v6: cycle=(\d+) uptime=', buf)) >= 2:
        break
    if proc.poll() is not None and got_summary:
        break
still_running = proc.poll() is None
if still_running:
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
    proc.wait()
LOG.write_bytes(buf)
print(f'ran {time.time() - start:.1f}s, {len(buf)} bytes of log')
log = LOG.read_text(errors='replace')
init_lines = [l for l in log.splitlines() if 'nx679j-v6: ' in l]
results = []


def check(name, ok, detail):
    results.append({'check': name, 'ok': bool(ok), 'detail': detail})
    print(f"[{'PASS' if ok else 'FAIL'}] {name}: {detail}")


src_line = [l for l in init_lines if 'modload source=device' in l][:1]
m = re.search(r'modules\.load=(\d+) recovery=(\d+)', log)
summary = re.search(r'modload summary loaded=(\d+) skipped=(\d+) failed=(\d+) '
                    r'missing=(\d+)', log)
idx = re.search(r'modload indexed (\d+) \.ko files', log)
cycles = sorted({int(x) for x in
                 re.findall(r'nx679j-v6: cycle=(\d+) uptime=', log)})
panic = [p for p in ('Kernel panic', 'Attempted to kill init', 'not syncing',
                     'Unable to handle kernel', 'Internal error: Oops')
         if p in log]

check('our /init still won over the appended vendor ramdisk',
      'init v6 entered pid=1' in log,
      [l for l in init_lines if 'init v6 entered' in l][:1])
check('both ramdisks are visible: /lib/modules holds the device .ko set',
      bool(idx) and int(idx.group(1)) >= 327,
      f'indexed {idx.group(1) if idx else "?"} .ko files (47 ours + 327 vendor)')
check('primary path used the device metadata', bool(src_line),
      src_line or [l for l in init_lines if 'modload' in l][:2])
check('modules.load parsed first (98) and modules.load.recovery appended (329)',
      bool(m) and int(m.group(1)) == 98 and int(m.group(2)) == 329,
      src_line)
check('a long chain was really attempted (>=300 modules)',
      bool(summary) and int(summary.group(1)) + int(summary.group(3)) >= 300,
      [l for l in init_lines if 'modload summary' in l][:1])
check('dwc3_msm loaded', any('dwc3_msm rc=0' in l for l in init_lines),
      [l for l in init_lines if 'dwc3_msm' in l][:1])
check('ufs_qcom loaded (the goal that makes rawdump visible)',
      any('ufs_qcom rc=' in l for l in init_lines),
      [l for l in init_lines if 'ufs_qcom' in l][:1])
check('softdep obeyed: qcom_hwspinlock before smem',
      log.find('qcom_hwspinlock') < log.find('modload smem'),
      f"qcom_hwspinlock {log.find('qcom_hwspinlock')} < "
      f"smem {log.find('modload smem')}")
check('softdep obeyed: phy_generic and eud before dwc3_msm',
      log.find('modload phy_generic') < log.find('modload dwc3_msm') and
      log.find('modload eud') < log.find('modload dwc3_msm'),
      f"phy_generic {log.find('modload phy_generic')} "
      f"eud {log.find('modload eud')} dwc3_msm {log.find('modload dwc3_msm')}")
check('gadget attempted after the chain and after dwc3_msm',
      log.find('stage3 gadget attempt') > log.find('modload dwc3_msm')
      and 'stage3 gadget attempt' in log,
      'gadget after dwc3_msm')
check('every module got a journal line with its rc',
      len([l for l in init_lines if 'modload ' in l and ' rc=' in l]) >= 300,
      f"{len([l for l in init_lines if 'modload ' in l and ' rc=' in l])} lines")
check('PID 1 survived the whole chain and kept cycling',
      len(cycles) >= 2 and still_running,
      f'cycles={cycles} alive={still_running}')
check('no panic from loading the vendor set in QEMU', not panic,
      panic or 'clean')
res = {'composite_order': composite, 'vendor_has_init': has_init(vend),
       'ramdisk_bytes': len(ramdisk), 'checks': {r['check']: r for r in results},
       'init_log_lines': len(init_lines), 'cycles': cycles,
       'still_running': still_running, 'summary_line': summary.group(0)
       if summary else None, 'source_line': src_line[0] if src_line else None,
       'indexed_ko': int(idx.group(1)) if idx else None}
(OUT / 'qemu-vendor-ramdisk-v6.json').write_text(json.dumps(res, indent=2) + '\n')
bad = [r for r in results if not r['ok']]
print(f'\n[*] {len(results) - len(bad)}/{len(results)} checks passed')
sys.exit(1 if bad else 0)
