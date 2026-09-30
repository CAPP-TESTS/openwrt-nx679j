#!/usr/bin/env python3
"""Boot the candidate ramdisk in isolated QEMU with the stock 5.10 kernel.

Scope of this test, stated honestly:
  IT CAN show that the archive unpacks, that PID 1 runs, and that the
  OpenWrt userspace comes up.
  IT CANNOT show that the Qualcomm modules bind: QEMU 'virt' has no
  Waipio device tree, so dwc3-msm/phy-msm-* will not probe. Their
  insmod result here is not evidence about the phone.
"""
import gzip
import json
from pathlib import Path
import re
import struct
import subprocess

BASE = Path(__file__).resolve().parent
IMG = BASE / 'candidate-stockkernel-v1/boot_b-stockkernel-openwrt-v1.img'
WORK = BASE / 'candidate-stockkernel-v1/qemu'
WORK.mkdir(parents=True, exist_ok=True)

img = IMG.read_bytes()
k_size, r_size = struct.unpack_from('<II', img, 8)
k_off = 4096
r_off = 4096 + ((k_size + 4095) // 4096) * 4096
(WORK / 'kernel').write_bytes(img[k_off:k_off + k_size])
(WORK / 'ramdisk.gz').write_bytes(img[r_off:r_off + r_size])

cmd = [
    'timeout', '90',
    'qemu-system-aarch64',
    '-machine', 'virt', '-cpu', 'cortex-a57', '-smp', '2', '-m', '2048',
    '-kernel', str(WORK / 'kernel'),
    '-initrd', str(WORK / 'ramdisk.gz'),
    '-append', 'console=ttyAMA0 earlycon loglevel=7 panic=5 rdinit=/init',
    '-nographic', '-no-reboot',
    '-nodefaults', '-serial', 'stdio',
]
proc = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
log = proc.stdout + proc.stderr
(WORK / 'boot.log').write_text(log)

markers = {
    'unpack_ok': 'Unpacking initramfs...' in log,
    'unpack_failed': 'Initramfs unpacking failed' in log,
    'stage1_reached': 'stage1 reached' in log,
    'insmod_lines': len(re.findall(r'insmod \S+ rc=', log)),
    'udc_line': next((l.strip() for l in log.splitlines() if 'udc=' in l), None),
    'exec_sbin_init': 'exec /sbin/init' in log,
    'procd': 'procd' in log,
    'openwrt_banner': bool(re.search(r'OpenWrt|BusyBox', log)),
    'panic': 'Kernel panic' in log,
}
print(json.dumps(markers, indent=2))
print(f'log -> {WORK / "boot.log"} ({len(log)} chars)')
