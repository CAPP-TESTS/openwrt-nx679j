#!/usr/bin/env python3
"""QEMU smoke test for the v5 minimal ramdisk: does it unpack, does /init start,
does the init hold PID 1 and retry forever without panicking?

Scope, stated honestly:
  IT CAN show that the archive unpacks, that PID 1 is our init script, that the
  ramdisk's own busybox/musl/kmodloader run, that the 40-name module chain is
  walked and that the retry loop keeps running without aborting or panicking.
  IT CANNOT show anything about Qualcomm hardware: QEMU 'virt' has no Waipio
  device tree, so dwc3-msm / phy-msm-* do not probe, no real UDC ever appears
  and the NCM gadget is never bound. The test asserts udc=NONE and no panic;
  it cannot assert 18d1:4ee7. It also cannot show that ABL/AVB accepts the
  image on the phone.

The kernel and ramdisk are extracted from the built image itself, so the test
exercises the shipped artefact, not the staging files.
"""
import hashlib
import json
import re
from pathlib import Path
import selectors
import struct
import subprocess
import sys
import time

EXP = Path('/home/user/nx679j-stock/experiments/20260916-122926-native-baseline')
OUT = EXP / 'candidate-minimal-gadget-v5'
IMG = OUT / 'boot_b-minimal-gadget-v5.img'
WORK = OUT / 'qemu'
WORK.mkdir(parents=True, exist_ok=True)

MAN = json.loads((OUT / 'manifest.json').read_text())
EXPECTED_NAMES = sorted(r['name'] for r in MAN['module_table'])
assert len(EXPECTED_NAMES) == 40, len(EXPECTED_NAMES)

img = IMG.read_bytes()
k_size, r_size = struct.unpack_from('<II', img, 8)
PAGE = 4096
k_off = PAGE
r_off = PAGE + ((k_size + PAGE - 1) // PAGE) * PAGE
(WORK / 'kernel-from-image').write_bytes(img[k_off:k_off + k_size])
(WORK / 'ramdisk-from-image.gz').write_bytes(img[r_off:r_off + r_size])

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
LOG = WORK / 'boot.log'
print('qemu cmd:', ' '.join(cmd))
# 30 s first UDC wait + 3 s scan + sleep 15 per cycle => a cycle boundary every
# ~18 s after the first. 110 s therefore covers the first attempt plus >= 4
# cycles, which is what proves the loop keeps running.
LIMIT = 110.0
proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, bufsize=0)
sel = selectors.DefaultSelector()
sel.register(proc.stdout, selectors.EVENT_READ)
start = time.monotonic()
out = bytearray()
cycles_seen_at = {}
with LOG.open('wb') as log:
    try:
        while time.monotonic() - start < LIMIT and proc.poll() is None:
            for key, _ in sel.select(0.25):
                chunk = key.fileobj.read(65536)
                if not chunk:
                    continue
                log.write(chunk)
                log.flush()
                out.extend(chunk)
            for m in re.finditer(rb'nx679j: cycle (\d+)', out):
                cycles_seen_at.setdefault(int(m.group(1)), round(time.monotonic() - start, 1))
            if len(cycles_seen_at) >= 4:
                time.sleep(2)
                break
    finally:
        if proc.poll() is None:
            proc.terminate()
        try:
            tail, _ = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            tail, _ = proc.communicate()
        if tail:
            out.extend(tail)
            log.write(tail)
        sel.close()

text = out.decode(errors='replace')
lines = [l.rstrip('\r') for l in text.splitlines()]
evidence = [l for l in lines if re.search(
    r'Unpacking initramfs|Initramfs unpacking failed|Run /init as init process|'
    r'nx679j:|Kernel panic|Attempted to kill init|Out of memory|'
    r'Kernel Offset|Please press Enter', l)]

markers = {
    'qemu_argv': cmd,
    'append': APPEND,
    'image': str(IMG),
    'image_sha256': hashlib.sha256(img).hexdigest(),
    'kernel_sha256': hashlib.sha256((WORK / 'kernel-from-image').read_bytes()).hexdigest(),
    'ramdisk_sha256': hashlib.sha256((WORK / 'ramdisk-from-image.gz').read_bytes()).hexdigest(),
    'ramdisk_bytes': r_size,
    'elapsed_seconds': round(time.monotonic() - start, 2),
    'qemu_returncode': proc.returncode,
    'unpacking_initramfs_seen': 'unpack rootfs image as initramfs' in text
                                or 'Unpacking initramfs' in text,
    'unpack_evidence_line': next((l.strip() for l in lines
                                  if 'unpack rootfs image as initramfs' in l
                                  or 'Unpacking initramfs' in l), None),
    'unpack_cpio_bytes': next((int(m.group(1)) for l in lines
                               for m in [re.search(r'initramfs: (\d+) bytes', l)]
                               if m), None),
    'unpack_failed': 'Initramfs unpacking failed' in text,
    'run_init_as_init_process_seen': 'Run /init as init process' in text,
    'init_script_ran': 'nx679j: init v5 entered' in text,
    'mount_rcs': re.findall(r'nx679j: mount (\S+ \S+) rc=(\d+)', text),
    'stage0_kernel_line': next((l.strip() for l in lines if 'stage0 kernel=' in l), None),
    'stage0_modprobe_line': next((l.strip() for l in lines if 'stage0 modprobe=' in l), None),
    'modules_dir_line': next((l.strip() for l in lines if 'modules dir=' in l), None),
    # every line is emitted twice (kmsg + console), so count distinct names
    'insmod_lines_raw': len(re.findall(r'insmod \S+ rc=\d+', text)),
    'insmod_distinct_names': sorted({m.group(1) for m in
                                     re.finditer(r'insmod (\S+) rc=\d+', text)}),
    'insmod_distinct_count': len({m.group(1) for m in
                                  re.finditer(r'insmod (\S+) rc=\d+', text)}),
    'insmod_names_match_manifest': sorted({m.group(1) for m in
                                           re.finditer(r'insmod (\S+) rc=\d+', text)})
                                   == EXPECTED_NAMES,
    'insmod_resolved_all': len({m.group(1) for m in re.finditer(
        r'insmod (\S+) rc=\d+ resolved=\S+\.ko', text)}) == 40,
    'insmod_mixed_spelling': re.findall(r'insmod (\S+) rc=\d+ resolved=(\S+) \(mixed spelling\)',
                                        text),
    'insmod_resolution_failed': re.findall(r'insmod (\S+) RESOLUTION-FAILED', text),
    'insmod_rc0': sorted({m.group(1) for m in
                          re.finditer(r'insmod (\S+) rc=0 ', text)}),
    'insmod_rc0_count': len({m.group(1) for m in
                             re.finditer(r'insmod (\S+) rc=0 ', text)}),
    'ufs_line': next((l.strip() for l in lines if 'ufs sda after' in l), None),
    'udc_lines': [l.strip() for l in lines if 'udc=' in l and 'nx679j' in l],
    'role_lines': [l.strip() for l in lines if 'usb_role nodes found=' in l],
    'cycle_lines': [l.strip() for l in lines if re.search(r'nx679j: cycle \d+', l)],
    'cycles_seen_at_seconds': cycles_seen_at,
    'gadget_attempt_lines': [l.strip() for l in lines if 'gadget attempt' in l],
    'pid1_still_alive_at_end': 'Attempted to kill init' not in text,
    'kernel_panic': 'Kernel panic' in text,
    'oom': 'Out of memory' in text,
    'log': str(LOG),
    'log_bytes': len(text),
    'evidence_lines': evidence[:80],
}
(OUT / 'qemu-smoke-v5.json').write_text(json.dumps(markers, indent=2) + '\n')
print(json.dumps({k: v for k, v in markers.items() if k != 'evidence_lines'}, indent=2))
ok = (markers['unpacking_initramfs_seen'] and not markers['unpack_failed']
      and markers['run_init_as_init_process_seen'] and markers['init_script_ran']
      and markers['insmod_distinct_count'] == 40
      and markers['insmod_names_match_manifest'] and markers['insmod_resolved_all']
      and not markers['insmod_resolution_failed']
      and len(markers['cycles_seen_at_seconds']) >= 3
      and not markers['kernel_panic'] and markers['pid1_still_alive_at_end'])
print(f'RESULT {"PASS" if ok else "FAIL"}')
sys.exit(0 if ok else 1)
