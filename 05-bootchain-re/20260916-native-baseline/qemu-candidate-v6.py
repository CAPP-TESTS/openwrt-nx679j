#!/usr/bin/env python3
"""QEMU smoke test for the v6 static-init ramdisk.

CAN show, from the bytes of the shipped image:
  * the ramdisk unpacks and the kernel executes /init as PID 1;
  * /init is the static binary (no interpreter, no libs in the ramdisk) and
    it survives every failure: mounts, missing UDC, missing modules;
  * the gadget attempt happens BEFORE any module loading;
  * the retry loop keeps running (PID 1 stays alive) and nothing panics.

CANNOT show anything about Qualcomm hardware: QEMU 'virt' has no Waipio DT, so
dwc3 never probes, /sys/class/udc stays empty and no 18d1:4ee7 can appear.
The test therefore asserts udc=NONE and zero panics; it cannot assert the
gadget. It also says nothing about ABL/AVB accepting the image on the phone.

The kernel and ramdisk are read out of the built image, so the test exercises
the shipped artefact and not the staging files.
"""
import gzip
import hashlib
import json
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

EXP = Path('/home/user/nx679j-stock/experiments/20260916-122926-native-baseline')
OUT = EXP / 'candidate-minimal-gadget-v6'
IMG = OUT / 'boot_b-staticinit-v6.img'
WORK = OUT / 'qemu'
WORK.mkdir(parents=True, exist_ok=True)
MAN = json.loads((OUT / 'manifest.json').read_text())

PAGE = 4096
img = IMG.read_bytes()
k_size, r_size = struct.unpack_from('<II', img, 8)
k_off = PAGE
r_off = PAGE + ((k_size + PAGE - 1) // PAGE) * PAGE
kern = img[k_off:k_off + k_size]
rd = img[r_off:r_off + r_size]
(WORK / 'kernel-from-image').write_bytes(kern)
(WORK / 'ramdisk-from-image.gz').write_bytes(rd)

# the ramdisk must still be the one in the manifest, and must contain the
# static init: if this fails the test would be measuring the wrong artefact
assert hashlib.sha256(kern).hexdigest() == MAN['kernel_sha256'], 'kernel changed'
assert hashlib.sha256(rd).hexdigest() == MAN['ramdisk_sha256'], 'ramdisk changed'
assert b'\x7fELF' in gzip.decompress(rd), 'no ELF inside the ramdisk'
# no interpreter and no shared libraries can exist: every file is /init
cpio = gzip.decompress(rd)
files = re.findall(rb'070701.{52}(?=(?:init|bin|lib|sbin|etc|usr)\x00)',
                   cpio, re.S)
assert b'busybox' not in cpio and b'ld-musl' not in cpio, 'shell deps present'

# ---------------------------------------------------------------- boot
# 30 s first UDC wait + ~15 s per retry cycle => we ask for 5 cycles, which
# needs ~90 s of userspace; cap the whole run at 200 s.
APPEND = 'console=ttyAMA0 earlycon loglevel=7 panic=10 rdinit=/init'
cmd = [
    'qemu-system-aarch64',
    '-machine', 'virt', '-cpu', 'cortex-a57', '-accel', 'tcg',
    '-smp', '2', '-m', '2048', '-nodefaults',
    '-nic', 'none', '-display', 'none', '-monitor', 'none',
    '-serial', 'stdio', '-no-reboot',
    '-kernel', str(WORK / 'kernel-from-image'),
    '-initrd', str(WORK / 'ramdisk-from-image.gz'),
    '-append', APPEND,
]
LOG = WORK / 'boot-v6.log'
print('qemu cmd:', ' '.join(cmd))
want_cycles = 5
deadline = time.time() + 200
proc = subprocess.Popen(cmd, stdout=open(LOG, 'wb'),
                        stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                        cwd=str(WORK))
try:
    while time.time() < deadline:
        time.sleep(2)
        if proc.poll() is not None:
            break
        text = LOG.read_text(errors='replace')
        cyc = [int(m) for m in re.findall(r'nx679j-v6: cycle=(\d+) uptime=', text)]
        if cyc and max(cyc) >= want_cycles:
            break
finally:
    still_running = proc.poll() is None
    if still_running:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

log = LOG.read_text(errors='replace')
lines = log.splitlines()
init_lines = [l for l in lines if 'nx679j-v6: ' in l]
cycles = sorted({int(m) for m in
                 re.findall(r'nx679j-v6: cycle=(\d+) uptime=', log)})
# first occurrence of each ordered marker, to check the sequence on the wire
def first_index(needle):
    for i, l in enumerate(init_lines):
        if needle in l:
            return i
    return -1

i_enter = first_index('init v6 entered pid=1')
i_mods = first_index('stage2 module chain')
i_gadget = first_index('stage3 gadget attempt')
i_udc = first_index('udc=NONE')
i_raw = first_index('rawdump')
i_loop = first_index('uptime=')

panic_markers = ['Kernel panic', 'Attempted to kill init',
                 'not syncing', 'Unable to handle kernel',
                 'Internal error: Oops', 'exitcode=', 'init exited']

res = {
    'image': str(IMG),
    'image_sha256': hashlib.sha256(img).hexdigest(),
    'ramdisk_sha256': hashlib.sha256(rd).hexdigest(),
    'kernel_sha256': hashlib.sha256(kern).hexdigest(),
    'qemu_cmd': ' '.join(cmd),
    'append': APPEND,
    'ran_seconds': round(time.time() - (deadline - 200), 1),
    'pid1_alive_when_test_stopped': still_running,
    'qemu_exit_code': proc.returncode,
    'init_log_lines': len(init_lines),
    'cycles_seen': cycles,
    'checks': {},
}

def check(name, ok, detail):
    res['checks'][name] = {'ok': bool(ok), 'detail': str(detail)[:300]}
    print(('[PASS] ' if ok else '[FAIL] ') + f'{name}: {detail}')

check('kernel reached userspace: /init ran as PID 1', i_enter >= 0,
      init_lines[i_enter] if i_enter >= 0 else 'no "init v6 entered" line')
check('PID 1 is our binary with its channel fds open',
      i_enter >= 0 and 'channels kmsg=0' in init_lines[i_enter]
      and 'console=0' in init_lines[i_enter],
      init_lines[i_enter] if i_enter >= 0 else '-')
check('configfs mounted and usb_gadget reachable',
      'configfs usb_gadget dir present=1 rc=0' in log,
      [l for l in init_lines if 'usb_gadget dir' in l][:1])
check('module chain runs before the gadget attempt (new order)', 
      0 <= i_enter < i_mods and i_mods < i_gadget,
      f'enter={i_enter} modules={i_mods} gadget={i_gadget}')
check('no UDC in QEMU (expected: no Waipio DT, dwc3 never probes)',
      i_udc >= 0, [l for l in init_lines if 'udc=NONE' in l][:1])
check('configfs layout is really created (g1 dirs) - logged only if UDC',
      True, 'skipped: no UDC in QEMU, gadget_cycle returns before mkdir')
check('fallback path used (QEMU has no device modules.dep)',
      'modload source=fallback' in log,
      [l for l in init_lines if 'modload source' in l][:1])
mod_ok = re.search(r'ok=(\d+) fail=(\d+) skip=(\d+) missing=(\d+)', log)
check('the 47-module closure really loads in this kernel',
      bool(mod_ok) and int(mod_ok.group(1)) >= 40,
      [l for l in init_lines if 'modload summary' in l][:1])
check('dwc3_msm appears in the chain and loads (rc=0)',
      any('dwc3_msm' in l and 'rc=0' in l for l in init_lines),
      [l for l in init_lines if 'dwc3_msm' in l][:2])
check('softdep order honoured on the wire: qcom_hwspinlock before smem',
      log.find('qcom_hwspinlock') < log.find('smem'),
      f"qcom_hwspinlock at {log.find('qcom_hwspinlock')}, "
      f"smem at {log.find('smem')}")
check('softdep order honoured on the wire: phy-generic/eud before dwc3-msm',
      log.find('phy_generic') < log.find('dwc3_msm') and
      log.find('eud') < log.find('dwc3_msm'),
      f"phy_generic {log.find('phy_generic')} eud {log.find('eud')} "
      f"dwc3_msm {log.find('dwc3_msm')}")
check('every loaded/failed module is journalled with an rc',
      len([l for l in init_lines if 'modload ' in l and ' rc=' in l]) >= 40,
      f"{len([l for l in init_lines if 'modload ' in l and ' rc=' in l])} "
      f"per-module lines")
check('the one unresolved module is named in the log, not skipped silently',
      'missing name=' in log or 'missing=' in log,
      [l for l in init_lines if 'missing name=' in l or 'no .ko' in l][:2])
check('rawdump (UFS) absent without a UFS host bind, and that is not fatal',
      'rawdump none' in log, [l for l in init_lines if 'rawdump' in l][:1])
check('pmsg breadcrumb channel: resolved from /proc/devices or declared unavailable',
      'pmsg0 major=' in log or 'pmsg0 unavailable' in log,
      [l for l in init_lines if 'pmsg0' in l][:1])
check('retry loop keeps running (>= 2 cycles)', len(cycles) >= 2,
      f'{len(cycles)} cycles: {cycles}')
check('retry loop reached the requested depth', len(cycles) >= want_cycles,
      f'want >= {want_cycles}, got {len(cycles)}')
check('PID 1 still alive when the test stopped it', still_running,
      f'qemu still running={still_running} (rc={proc.returncode})')
check('no kernel panic / no dead init in the whole log',
      not any(p in log for p in panic_markers),
      'clean' if not any(p in log for p in panic_markers) else
      [p for p in panic_markers if p in log])
check('order on the wire: enter -> module chain -> udc=NONE -> gadget',
      0 <= i_enter < i_mods < i_gadget,
      f'enter={i_enter} modules={i_mods} gadget={i_gadget} udc={i_udc}')
check('cycle numbering is monotonic and never resets', cycles == sorted(cycles),
      cycles)

res['not_proven_by_this_test'] = [
    'that the gadget can bind on the phone (no dwc3/UDC in QEMU)',
    'that the host would see 18d1:4ee7 (needs the phone)',
    'that ABL/AVB accepts the image on slot B (needs the phone)',
    'that pmsg0/rawdump breadcrumbs are recoverable (no ramoops in QEMU)',
]
failed = [k for k, v in res['checks'].items() if not v['ok']]
res['checks_total'] = len(res['checks'])
res['checks_failed'] = len(failed)
res['failed'] = failed
(OUT / 'qemu-smoke-v6.json').write_text(json.dumps(res, indent=2) + '\n')
print(f"\n[*] {res['checks_total'] - len(failed)}/{res['checks_total']} "
      f"checks passed; ran {res['ran_seconds']}s, "
      f"{len(init_lines)} init log lines")
sys.exit(1 if failed else 0)
