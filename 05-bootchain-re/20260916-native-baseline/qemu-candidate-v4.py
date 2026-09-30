#!/usr/bin/env python3
"""QEMU smoke test for the v4 ramdisk: does it unpack, does /init start?

Scope, stated honestly:
  IT CAN show that the archive unpacks, that PID 1 is our init script, that
  the module chain is walked and that the handover to /sbin/init happens.
  IT CANNOT show anything about Qualcomm hardware: QEMU 'virt' has no Waipio
  device tree, so dwc3-msm / phy-msm-* do not probe and the UDC never appears.
  It also cannot show that ABL/AVB accepts the image on the phone.

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
OUT = EXP / 'candidate-stockkernel-v4'
IMG = OUT / 'boot_b-stockkernel-openwrt-v4.img'
WORK = OUT / 'qemu'
WORK.mkdir(parents=True, exist_ok=True)

img = IMG.read_bytes()
k_size, r_size = struct.unpack_from('<II', img, 8)
PAGE = 4096
k_off = PAGE
r_off = PAGE + ((k_size + PAGE - 1) // PAGE) * PAGE
(WORK / 'kernel-from-image').write_bytes(img[k_off:k_off + k_size])
(WORK / 'ramdisk-from-image.gz').write_bytes(img[r_off:r_off + r_size])

APPEND = ('console=ttyAMA0 earlycon loglevel=7 panic=10 rdinit=/init')
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
proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, bufsize=0)
sel = selectors.DefaultSelector()
sel.register(proc.stdout, selectors.EVENT_READ)
start = time.monotonic()
out = bytearray()
handover_at = None
LIMIT = 150.0
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
            if handover_at is None and b'handover init=' in out:
                handover_at = time.monotonic()
                print(f'    handover line seen at {handover_at - start:.1f}s')
            # keep watching 25 s past the handover to see whether procd starts
            if handover_at and time.monotonic() - handover_at > 25:
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
    r'nx679j:|procd:|Kernel panic|Please press Enter|\binit\b.*(entered|handover)',
    l)]

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
    'unpacking_initramfs_seen': ('Trying to unpack rootfs image as initramfs...' in text
                                 or 'Unpacking initramfs...' in text),
    'unpack_evidence_line': next((l.strip() for l in lines if 'unpack rootfs image as initramfs' in l
                                  or 'Unpacking initramfs' in l), None),
    'unpack_failed': 'Initramfs unpacking failed' in text,
    'run_init_as_init_process_seen': 'Run /init as init process' in text,
    'init_script_ran': 'nx679j: init v2 entered' in text,
    'stage0_cmdline_logged': 'stage0 cmdline=' in text,
    'insmod_lines': len(re.findall(r'insmod \S+ rc=', text)),
    'insmod_resolved_literal_or_converted': len(re.findall(r'rc=\d+ resolved=\S+\.ko ', text)),
    'insmod_resolved_mixed': len(re.findall(r'resolved=\S+ \(mixed spelling\)', text)),
    'insmod_resolution_failed_lines': len(re.findall(r'insmod \S+ RESOLUTION-FAILED', text)),
    'insmod_absent_lines_legacy_v3_grep': len(re.findall(r'insmod \S+ ABSENT', text)),
    'resolution_failed_names': re.findall(r'insmod (\S+) RESOLUTION-FAILED', text),
    'resolved_pairs': re.findall(r'insmod (\S+) rc=\d+ resolved=(\S+)', text),
    'ufs_line': next((l.strip() for l in lines if 'ufs sda after' in l), None),
    'udc_line': next((l.strip() for l in lines if 'udc=' in l and 'nx679j' in l), None),
    'handover_line': next((l.strip() for l in lines if 'handover init=' in l), None),
    'procd_seen': 'procd: ' in text,
    'kernel_panic': 'Kernel panic' in text,
    'log': str(LOG),
    'log_bytes': len(text),
    'evidence_lines': evidence[:60],
}
(OUT / 'qemu-smoke-v4.json').write_text(json.dumps(markers, indent=2) + '\n')
print(json.dumps(markers, indent=2))
sys.exit(0 if (markers['unpacking_initramfs_seen'] and not markers['unpack_failed']
               and markers['run_init_as_init_process_seen']
               and markers['init_script_ran']) else 1)
