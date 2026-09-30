#!/usr/bin/env python3
"""Structural verification of the slot-B v3 image. Read-only.

Checks, all against the bytes on disk:
  1. Android boot header: magic, header_version == 4, declared sizes.
  2. header kernel_size / ramdisk_size == manifest values.
  3. kernel payload sha256 == the sha256 of the stock kernel extracted from
     current-readback/boot_a.img.
  4. image size <= 0x6000000 (96 MiB) partition capacity.
  5. header cmdline carries panic=10 and everything before it is byte-identical
     to the v2 cmdline.
  6. ramdisk: gzip integrity + newc cpio parse to TRAILER!!!, entry count,
     presence of /init, /sbin/init, busybox, the static device nodes and the
     three USB-chain modules.
"""
import gzip
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

EXP = Path('/home/user/nx679j-stock/experiments/20260916-122926-native-baseline')
OUT = EXP / 'candidate-stockkernel-v3'
IMG = OUT / 'boot_b-stockkernel-openwrt-v3.img'
MAN = json.loads((OUT / 'manifest.json').read_text())
V2MAN = json.loads((EXP / 'candidate-stockkernel-v2/manifest.json').read_text())
PAGE = 4096
KERNEL_SHA_EXPECTED = 'f0aa949c95d1421381850cb1982bd2a505e96fcd7565543ec28d5b1f7d81a0cc'

checks = []


def check(name, ok, detail=''):
    checks.append((name, bool(ok), detail))
    print(f'[{"PASS" if ok else "FAIL"}] {name}{(" :: " + detail) if detail else ""}')


img = IMG.read_bytes()
print(f'image {IMG}')
print(f'      {len(img)} bytes sha256={hashlib.sha256(img).hexdigest()}')

# ---- 1. header -----------------------------------------------------------
check('header magic ANDROID!', img[:8] == b'ANDROID!', repr(img[:8]))
k_size, r_size = struct.unpack_from('<II', img, 8)
os_version, header_size = struct.unpack_from('<II', img, 16)
header_version = struct.unpack_from('<I', img, 40)[0]
print(f'      kernel_size={k_size} ramdisk_size={r_size} os_version=0x{os_version:08x} '
      f'header_size={header_size} header_version={header_version}')
check('header_version == 4', header_version == 4, str(header_version))
cmdline = img[44:44 + 1536].split(b'\0')[0].decode()
print(f'      cmdline({len(cmdline)} bytes) = {cmdline}')

# ---- 2. manifest agreement ----------------------------------------------
check('kernel_size == manifest', k_size == MAN['kernel_bytes'], f'{k_size}/{MAN["kernel_bytes"]}')
check('ramdisk_size == manifest', r_size == MAN['ramdisk_bytes'], f'{r_size}/{MAN["ramdisk_bytes"]}')
check('image bytes == manifest', len(img) == MAN['bytes'], f'{len(img)}/{MAN["bytes"]}')

# ---- 3. kernel identity --------------------------------------------------
k_off = PAGE
kernel = img[k_off:k_off + k_size]
k_sha = hashlib.sha256(kernel).hexdigest()
check('kernel sha256 == device stock kernel', k_sha == KERNEL_SHA_EXPECTED, k_sha)
check('kernel != trailing zeros', kernel[-4096:] != b'\0' * 4096)

# ---- 4. size -------------------------------------------------------------
check('size <= 96 MiB (0x6000000)', len(img) <= 0x6000000,
      f'{len(img)} <= {0x6000000} ({0x6000000 - len(img)} bytes headroom)')

# ---- 5. cmdline ----------------------------------------------------------
v2_cmdline = V2MAN['cmdline'] if 'cmdline' in V2MAN else None
check('cmdline carries panic=10', 'panic=10' in cmdline.split())
check('cmdline has exactly one panic= token',
      sum(1 for t in cmdline.split() if t.startswith('panic=')) == 1,
      ','.join(t for t in cmdline.split() if t.startswith('panic=')))
v2_src = (EXP / 'build-candidate-v2.py').read_text()
v2_literal = v2_src.split("'--cmdline', '")[1].split("'")[0]
check('cmdline == v2 cmdline + " panic=10"', cmdline == v2_literal + ' panic=10')
check('cmdline prefix byte-identical to v2', cmdline[:len(v2_literal)] == v2_literal)
check('v2 cmdline tail "rootwait ro init=/init" preserved verbatim',
      cmdline.endswith('rootwait ro init=/init panic=10'),
      cmdline[-40:])
check('header cmdline == manifest cmdline', cmdline == MAN['cmdline'])

# ---- 6. ramdisk ----------------------------------------------------------
r_off = PAGE + ((k_size + PAGE - 1) // PAGE) * PAGE
ramdisk = img[r_off:r_off + r_size]
r_sha = hashlib.sha256(ramdisk).hexdigest()
check('ramdisk sha256 == manifest', r_sha == MAN['ramdisk_sha256'], r_sha)
gzip_t = subprocess.run(['gzip', '-t'], input=ramdisk, capture_output=True)
check('gzip -t accepts ramdisk', gzip_t.returncode == 0, gzip_t.stderr.decode().strip())

raw = gzip.decompress(ramdisk)
off, entries, nodes = 0, {}, 0
strict = True
while off < len(raw):
    if raw[off:off + 6] not in (b'070701', b'070702'):
        strict = False
        break
    f = [int(raw[off + 6 + i * 8:off + 14 + i * 8], 16) for i in range(13)]
    mode, size, namesize = f[1], f[6], f[11]
    name = raw[off + 110:off + 110 + namesize - 1].decode()
    start = (off + 110 + namesize + 3) & ~3
    if name == 'TRAILER!!!':
        entries[name] = mode
        break
    entries[name] = mode
    if mode & 0o170000 == 0o020000:
        nodes += 1
    off = (start + size + 3) & ~3
check('newc parses to TRAILER!!!', strict and 'TRAILER!!!' in entries)
check('entry count == manifest', len(entries) - 1 == MAN['ramdisk_entries'],
      f'{len(entries) - 1} vs {MAN["ramdisk_entries"]}')
MOD_DIR = 'lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604'
for need in ('init', 'sbin/init', 'bin/busybox', 'dev/console', 'dev/kmsg',
             'dev/pmsg0', 'proc', 'sys', 'sys/kernel/config',
             f'{MOD_DIR}/dwc3-msm.ko', f'{MOD_DIR}/phy-msm-snps-hs.ko',
             f'{MOD_DIR}/fsa4480-i2c.ko'):
    check(f'ramdisk contains {need}', need in entries)
check('6 static device nodes', nodes == 6, str(nodes))
ko = sum(1 for n in entries if n.endswith('.ko'))
check('327 vendor modules packed', ko == 327, str(ko))
check('init is executable script', entries.get('init') == 0o100755,
      oct(entries.get('init', 0)))

# init file identity
init_sha = hashlib.sha256((EXP / 'candidate-init-v3.sh').read_bytes()).hexdigest()
check('init sha256 == manifest', init_sha == MAN['init']['sha256'], init_sha)

# ---- independent tooling (if present) ------------------------------------
for tool, args in (('unpack_bootimg', ['--boot_img', str(IMG), '--out', '/tmp/v3-unpack']),
                   ('cpio', None)):
    found = subprocess.run(['sh', '-c', f'command -v {tool}'], capture_output=True, text=True)
    print(f'      tool {tool}: {found.stdout.strip() or "not installed"}')
if subprocess.run(['sh', '-c', 'command -v unpack_bootimg'], capture_output=True).returncode == 0:
    ub = subprocess.run(['unpack_bootimg', '--boot_img', str(IMG), '--out', '/tmp/v3-unpack'],
                        capture_output=True, text=True)
    print('      unpack_bootimg:', ub.stdout.strip().replace('\n', ' | ') or ub.stderr.strip())
    if ub.returncode == 0:
        kf = Path('/tmp/v3-unpack/kernel')
        check('unpack_bootimg kernel sha256 matches',
              hashlib.sha256(kf.read_bytes()).hexdigest() == k_sha)

failed = [c for c in checks if not c[1]]
summary = {
    'image': str(IMG),
    'bytes': len(img),
    'sha256': hashlib.sha256(img).hexdigest(),
    'header_version': header_version,
    'kernel_bytes': k_size,
    'kernel_sha256': k_sha,
    'ramdisk_bytes': r_size,
    'ramdisk_sha256': r_sha,
    'ramdisk_entries': len(entries) - 1,
    'checks_total': len(checks),
    'checks_failed': [c[0] for c in failed],
    'cmdline_bytes': len(cmdline),
    'cmdline': cmdline,
}
(OUT / 'verify-structural.json').write_text(json.dumps(summary, indent=2) + '\n')
print()
print(f'{len(checks) - len(failed)}/{len(checks)} checks passed')
print(json.dumps(summary, indent=2))
sys.exit(1 if failed else 0)
