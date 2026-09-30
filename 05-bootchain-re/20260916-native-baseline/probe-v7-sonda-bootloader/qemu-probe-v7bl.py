#!/usr/bin/env python3
"""QEMU full-system smoke test for the RESTART2 / 'bootloader' probe images.

WHAT THIS SHOWS, from the bytes of the two shipped images:
  * the kernel in the image unpacks the ramdisk and runs /init as PID 1, so the
    probe really executes in userspace (linking fine is not enough);
  * /init's restart goes down the RESTART2 path: the kernel takes the reason
    string from the userspace pointer and logs it ("Restarting system with
    command '...'"), which is the only part of this mechanism that can be
    observed without the phone;
  * the kernel then performs the restart and, with -no-reboot, QEMU exits --
    i.e. the same self-reset the phone test looks for.

WHAT IT CANNOT SHOW (stated so the report does not overclaim):
  * that ABL/AVB accepts the image on the phone;
  * that the bootloader of this unit acts on the reason and presents fastboot
    (18d1:d00d): QEMU has no ABL. The phone-side effect remains a hypothesis.
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
           '/probe-v7-sonda-bootloader')
WORK = OUT / 'qemu'
WORK.mkdir(parents=True, exist_ok=True)
MAN = json.loads((OUT / 'manifest.json').read_text())
PAGE = 4096
MARK = 'nx679j-sonda: '
DEADLINE = 240
IMAGES = (('v7', 'sonda-bootloader-v7.img'), ('v7b', 'sonda-bootloader-v7b.img'))
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
    check(f'{tag}: kernel+ramdisk extracted from the image match the manifest',
          hashlib.sha256(kern).hexdigest() == MAN['container']['kernel_sha256']
          and hashlib.sha256(rd).hexdigest() == man['ramdisk_sha256']
          and len(img) == 100663296,
          f'kernel {hashlib.sha256(kern).hexdigest()[:16]} '
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
    reboot_lines = [l for l in text.splitlines()
                    if 'Restarting system' in l or 'reboot:' in l]
    with_reason = [l for l in text.splitlines()
                   if 'Restarting system with command' in l]
    reason_lines = [l for l in with_reason if "bootloader" in l]
    panics = [m for m in ('Kernel panic', 'Attempted to kill init', 'not syncing',
                          'Unable to handle kernel', 'Internal error: Oops',
                          'init exited', 'Requested init /init failed')
              if m in text]
    res['images'][tag] = {
        'image': str(img_path), 'image_sha256': MAN['images'][tag]['sha256'],
        'ramdisk_sha256': man['ramdisk_sha256'], 'ramdisk_bytes': r_size,
        'qemu_cmd': cmd, 'qemu_log': str(log), 'qemu_exit_code': rc,
        'qemu_exited_by_itself': exited_by_itself,
        'marker_lines': lines, 'reboot_lines': reboot_lines,
        'restarting_system_lines': with_reason, 'panics': panics,
    }
    check(f'{tag}: kernel reached userspace and ran /init as PID 1',
          any('pid 1' in l for l in lines),
          lines[0] if lines else 'no marker line at all')
    check(f'{tag}: the marker channel worked (kmsg created/resolved by /init)',
          any('channel' in l for l in lines),
          [l for l in lines if 'channel' in l][:1])
    check(f'{tag}: /init announced the RESTART2 call with the bootloader reason',
          any('LINUX_REBOOT_CMD_RESTART2' in l for l in lines),
          [l for l in lines if 'RESTART2' in l][:1])
    check(f'{tag}: the kernel took the reason string from the pointer and logged '
          f"it (Restarting system with command 'bootloader')",
          bool(reason_lines), reason_lines[:1] or
          f'no such line; reboot lines seen: {reboot_lines[:3]}')
    check(f'{tag}: the kernel performed the restart (QEMU -no-reboot exited)',
          exited_by_itself and any('Restarting system' in l for l in reboot_lines),
          f'qemu exit code {rc}')
    check(f'{tag}: no panic and PID 1 never died before the restart',
          not panics, panics or 'clean')


def main():
    print('=== qemu-probe-v7bl: does the RESTART2 probe run and reboot? ===')
    for tag, name in IMAGES:
        run_one(tag, name)
    passed = sum(1 for c in res['checks'] if c['pass'])
    res['summary'] = {'passed': passed, 'total': len(res['checks'])}
    res['cannot_show'] = [
        'that ABL/AVB accepts these images on the phone',
        "that this unit's ABL acts on the reason 'bootloader' and presents "
        'fastboot (USB 18d1:d00d) -- no bootloader exists in QEMU',
    ]
    (OUT / 'qemu-probe-v7bl.json').write_text(json.dumps(res, indent=2) + '\n')
    print(f'\n=== {passed}/{len(res["checks"])} checks passed -> '
          f'{OUT / "qemu-probe-v7bl.json"}')
    return 0 if passed == len(res['checks']) else 1


if __name__ == '__main__':
    sys.exit(main())
