#!/usr/bin/env python3
"""Quick single QEMU run (A or B) for iterating on the journal, without the
full suite.  Usage: quick-qemu.py A|B [deadline_s]"""
import hashlib
import json
import struct
import subprocess
import sys
import time
from pathlib import Path

OUT = Path('/home/user/nx679j-stock/experiments/20260917-init-v8')
WORK = OUT / 'qemu'
PAGE = 4096
tag = sys.argv[1] if len(sys.argv) > 1 else 'A'
deadline = int(sys.argv[2]) if len(sys.argv) > 2 else 180

img = (OUT / 'boot_b-init-v8.img').read_bytes()
k_size, r_size = struct.unpack_from('<II', img, 8)
r_off = PAGE + ((k_size + PAGE - 1) // PAGE) * PAGE
kernel = img[PAGE:PAGE + k_size]
lz4 = img[r_off:r_off + r_size]
WORK.mkdir(exist_ok=True)
if tag == 'B':
    man = json.loads((OUT / 'manifest.json').read_text())
    victim = 'phy-qcom-emu.ko'
    lz4 = (OUT / 'v8-ramdisk-poisoned.lz4').read_bytes()
    print(f'[B] poisoned lz4 {len(lz4)} bytes, victim {victim}')
(WORK / f'{tag}-kernel').write_bytes(kernel)
(WORK / f'{tag}-ramdisk').write_bytes(lz4)
log = WORK / f'{tag}.log'
cmd = ['qemu-system-aarch64', '-machine', 'virt', '-cpu', 'cortex-a57',
       '-accel', 'tcg', '-smp', '2', '-m', '2048', '-nodefaults', '-nic', 'none',
       '-display', 'none', '-monitor', 'none', '-serial', 'stdio', '-no-reboot',
       '-kernel', str(WORK / f'{tag}-kernel'), '-initrd', str(WORK / f'{tag}-ramdisk'),
       '-append', 'console=ttyAMA0 earlycon loglevel=8 panic=10 rdinit=/init']
print('[$] ' + ' '.join(cmd))
t0 = time.time()
p = subprocess.Popen(cmd, stdout=open(log, 'wb'), stderr=subprocess.STDOUT,
                     stdin=subprocess.DEVNULL)
try:
    while time.time() - t0 < deadline:
        time.sleep(2)
        if p.poll() is not None:
            break
        if 'Restarting system' in log.read_text(errors='replace'):
            time.sleep(2)
            break
finally:
    if p.poll() is None:
        p.terminate()
        try:
            p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
text = log.read_text(errors='replace')
lines = [l for l in text.splitlines() if 'nx679j' in l]
print(f'[done] {round(time.time() - t0, 1)} s, qemu exit {p.returncode}, '
      f'rebooted={"Restarting system" in text}, journal lines={len(lines)}')
for l in lines[:60]:
    print('   ', l.split('] ', 1)[-1])
print('--- last 12 ---')
for l in lines[-12:]:
    print('   ', l.split('] ', 1)[-1])
