#!/usr/bin/env python3
"""QEMU smoke test for the v7 / v7b probe ramdisks ("sonda").

WHAT THIS SHOWS, from the bytes of the two shipped images:
  * the kernel in the image unpacks the ramdisk and runs /init as PID 1, so the
    probe really executes in userspace (linking fine is not enough);
  * /init writes its marker and asks the kernel to RESTART;
  * the kernel honours the restart: the guest reboots, and with -no-reboot QEMU
    exits -- exactly the self-reset the phone test looks for.

WHAT IT CANNOT SHOW: anything about ABL/AVB accepting the image on the phone,
and nothing about whether ABL burns slot-retry-count:b (no bootloader in QEMU).
"""
import hashlib
import json
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

OUT = Path('/home/user/nx679j-stock/experiments/20260916-122926-native-baseline'
           '/probe-v7-sonda')
WORK = OUT / 'qemu'
WORK.mkdir(parents=True, exist_ok=True)
MAN = json.loads((OUT / 'manifest.json').read_text())
PAGE = 4096
MARK = 'nx679j-sonda: '
DEADLINE = 240
res = {'checks': [], 'images': {}}


def check(name, ok, detail=''):
    res['checks'].append({'check': name, 'pass': bool(ok), 'detail': str(detail)})
    print(f'[{"PASS" if ok else "FAIL"}] {name}: {detail}')
    return bool(ok)


def run_one(tag, img_name):
    img_path = OUT / img_name
    img = img_path.read_bytes()
    k_size, r_size = struct.unpack_from('<II', img, 8)
    r_off = PAGE + ((k_size + PAGE - 1) // PAGE) * PAGE
    kern = img[PAGE:PAGE + k_size]
    rd = img[r_off:r_off + r_size]
    (WORK / f'{tag}-kernel').write_bytes(kern)
    (WORK / f'{tag}-ramdisk').write_bytes(rd)
    man = MAN['images'][tag]
    ok_hashes = (hashlib.sha256(kern).hexdigest() == MAN['container']['kernel_sha256']
                 and hashlib.sha256(rd).hexdigest() == man['ramdisk_sha256']
                 and len(img) == 100663296)
    check(f'{tag}: kernel+ramdisk extracted from the image match the manifest',
          ok_hashes, f'kernel {hashlib.sha256(kern).hexdigest()[:16]} '
                     f'ramdisk {hashlib.sha256(rd).hexdigest()[:16]} '
                     f'({r_size} B at offset {r_off})')

    log = WORK / f'boot-{tag}.log'
    append = 'console=ttyAMA0 earlycon loglevel=8 panic=10 rdinit=/init'
    cmd = ['qemu-system-aarch64', '-machine', 'virt', '-cpu', 'cortex-a57',
           '-accel', 'tcg', '-smp', '2', '-m', '2048', '-nodefaults',
           '-nic', 'none', '-display', 'none', '-monitor', 'none',
           '-serial', 'stdio', '-no-reboot',
           '-kernel', str(WORK / f'{tag}-kernel'),
           '-initrd', str(WORK / f'{tag}-ramdisk'), '-append', append]
    print(f'[$] {tag} qemu cmd: {" ".join(cmd)}')
    proc = subprocess.Popen(cmd, stdout=open(log, 'wb'),
                            stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                            cwd=str(WORK))
    deadline = time.time() + DEADLINE
    try:
        while time.time() < deadline:
            time.sleep(2)
            if proc.poll() is not None:
                break
            t = log.read_text(errors='replace')
            if 'Restarting system' in t or 'reboot: ' in t:
                time.sleep(3)
                break
    finally:
        exited_by_itself = proc.poll() is not None
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
    rc = proc.returncode
    text = log.read_text(errors='replace')
    lines = [l for l in text.splitlines() if MARK in l]
    panics = [m for m in ('Kernel panic', 'Attempted to kill init', 'not syncing',
                          'Unable to handle kernel', 'Internal error: Oops',
                          'init exited', 'Requested init /init failed')
              if m in text]
    res['images'][tag] = {
        'image': str(img_path), 'image_sha256': MAN['images'][tag]['sha256'],
        'ramdisk_sha256': man['ramdisk_sha256'], 'ramdisk_bytes': r_size,
        'qemu_cmd': cmd, 'qemu_log': str(log), 'qemu_exit_code': rc,
        'qemu_exited_by_itself': exited_by_itself,
        'marker_lines': lines, 'panics': panics,
    }
    check(f'{tag}: kernel reached userspace and ran /init as PID 1',
          any('pid 1' in l or 'pid=1' in l for l in lines),
          lines[:2] if lines else 'no marker line at all')
    check(f'{tag}: the marker channel worked (kmsg created/resolved by /init)',
          any('channel' in l for l in lines), lines[:1])
    check(f'{tag}: /init called reboot(2) LINUX_REBOOT_CMD_RESTART',
          'Restarting system' in text,
          [l for l in text.splitlines() if 'Restarting system' in l][:1])
    check(f'{tag}: the kernel performed the restart (QEMU -no-reboot exited)',
          exited_by_itself, f'qemu exit code {rc}')
    check(f'{tag}: no panic and PID 1 never died before the restart',
          not panics, panics or 'clean')


def main():
    print('=== qemu-probe-v7: does the probe really run and really reboot? ===')
    for tag, name in (('v7', 'boot_b-probe-v7.img'),
                      ('v7b', 'boot_b-probe-v7b.img')):
        run_one(tag, name)
    passed = sum(1 for c in res['checks'] if c['pass'])
    res['summary'] = {'passed': passed, 'total': len(res['checks'])}
    (OUT / 'qemu-probe-v7.json').write_text(json.dumps(res, indent=2) + '\n')
    print(f'\n=== {passed}/{len(res["checks"])} checks passed -> '
          f'{OUT / "qemu-probe-v7.json"}')
    return 0 if passed == len(res['checks']) else 1


if __name__ == '__main__':
    sys.exit(main())
