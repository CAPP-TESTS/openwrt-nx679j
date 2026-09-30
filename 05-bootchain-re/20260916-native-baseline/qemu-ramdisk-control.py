#!/usr/bin/env python3
"""Isolated QEMU control: same current B kernel, intact OpenWrt ramdisk.

No block devices, network, USB passthrough or host filesystem shares.
"""
import hashlib
import json
from pathlib import Path
import selectors
import subprocess
import time

BASE = Path(__file__).resolve().parent
KERNEL = BASE / 'unpacked-current/boot_b/kernel'
RAMDISK = Path('/home/user/nx679j-stock/port-work/openwrt-phase1/openwrt-rootfs.cpio.gz')
LOG = BASE / 'qemu-openwrt-control.log'
cmd = [
    'qemu-system-aarch64', '-machine', 'virt', '-cpu', 'cortex-a57',
    '-accel', 'tcg', '-smp', '2', '-m', '1024', '-nodefaults',
    '-nic', 'none', '-display', 'none', '-monitor', 'none',
    '-serial', 'stdio', '-no-reboot', '-kernel', str(KERNEL),
    '-initrd', str(RAMDISK), '-append',
    'console=ttyAMA0 earlycon=pl011,0x9000000 rdinit=/init panic=1 loglevel=7',
]
proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, bufsize=0)
sel = selectors.DefaultSelector()
sel.register(proc.stdout, selectors.EVENT_READ)
start = time.monotonic()
all_output = bytearray()
sent = False
with LOG.open('wb') as log:
    try:
        while time.monotonic()-start < 45 and proc.poll() is None:
            for key, _ in sel.select(0.2):
                chunk = key.fileobj.read(65536)
                if not chunk:
                    continue
                log.write(chunk)
                log.flush()
                all_output.extend(chunk)
            if not sent and b'Please press Enter to activate this console' in all_output:
                proc.stdin.write(b'\n')
                proc.stdin.flush()
                sent = True
                activated = time.monotonic()
            if sent and time.monotonic()-activated > 1:
                proc.stdin.write(b"printf '\\nNX679J_QEMU_OPENWRT_CONTROL\\n'; uname -r; cat /etc/openwrt_release; printf 'PID1='; readlink /proc/1/exe; printf 'PID1CMD='; tr '\\000' ' ' </proc/1/cmdline; printf '\\n'; cat /proc/uptime; poweroff -f\n")
                proc.stdin.flush()
                activated = float('inf')
    finally:
        if proc.poll() is None:
            proc.terminate()
        try:
            tail, _ = proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            tail, _ = proc.communicate()
        if tail:
            all_output.extend(tail)
            log.write(tail)
        sel.close()
text = all_output.decode(errors='replace')
result = {
    'argv': cmd,
    'kernel_sha256': hashlib.sha256(KERNEL.read_bytes()).hexdigest(),
    'ramdisk_sha256': hashlib.sha256(RAMDISK.read_bytes()).hexdigest(),
    'returncode': proc.returncode,
    'elapsed_seconds': round(time.monotonic()-start, 3),
    'unpack_failed': 'Initramfs unpacking failed' in text,
    'kernel_panic': 'Kernel panic' in text,
    'interactive_probe_sent': sent,
    'pid1_procd_observed': 'PID1=/sbin/procd' in text,
    'log': str(LOG),
}
(BASE / 'qemu-openwrt-control.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result, indent=2))
print(text[-8500:])
