#!/usr/bin/env python3
"""Build slot-B candidate v4: stock 5.10 kernel + OpenWrt rootfs + stock modules.

v4 is v3 with exactly one change: the init script now resolves module file
names (candidate-init-v4.sh). v3's list used /proc/modules underscore names but
the ramdisk files are hyphenated, so 25 of 40 insmod calls logged ABSENT and the
USB/UFS drivers never loaded. Kernel, cmdline (panic=10 included), vendor
modules, OpenWrt rootfs and ramdisk assembly are unchanged from v3.

Helper functions (newc_entries, cpio_pack, sha) and the ramdisk assembly are
reused verbatim from build-candidate-v3.py / build-candidate-v2.py.

Writes nothing to the phone. Output is an image file plus manifests.
"""
import hashlib
import json
import gzip
import io
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

BASE = Path('/home/user/nx679j-stock')
EXP = BASE / 'experiments/20260916-122926-native-baseline'
OUT = EXP / 'candidate-stockkernel-v4'
WORK = OUT / 'work'
ROOTFS_SRC = BASE / 'port-work/openwrt-phase1/openwrt-rootfs.cpio.gz'
BOOT_A = EXP / 'current-readback/boot_a.img'
VENDOR_RD = EXP / 'unpacked-current/vendor_boot_a/vendor_ramdisk00'

for p in (ROOTFS_SRC, BOOT_A, VENDOR_RD):
    if not p.exists():
        sys.exit(f'missing input: {p}')

if OUT.exists():
    shutil.rmtree(OUT)
WORK.mkdir(parents=True)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def newc_entries(data: bytes):
    """Yield (name, mode, payload) for a newc archive; strict on padding."""
    off = 0
    while off < len(data):
        magic = data[off:off + 6]
        if magic not in (b'070701', b'070702'):
            raise ValueError(f'bad cpio magic at {off}: {magic!r}')
        f = [int(data[off + 6 + i * 8:off + 14 + i * 8], 16) for i in range(13)]
        mode, size, namesize = f[1], f[6], f[11]
        name = data[off + 110:off + 110 + namesize - 1].decode()
        start = (off + 110 + namesize + 3) & ~3
        payload = data[start:start + size]
        if name == 'TRAILER!!!':
            return
        yield name, mode, payload
        off = (start + size + 3) & ~3


def cpio_pack(entries):
    """Pack (name, mode, payload[, rdev_major, rdev_minor]) into newc.

    Device nodes are required because the stock 5.10 kernel is built with
    CONFIG_DEVTMPFS unset (runtime-v2/config.gz), so /dev is NOT populated
    automatically: without static nodes /dev/kmsg and /dev/pmsg0 do not
    exist and the diagnostic log is silent (observed in the first QEMU run).
    """
    out = bytearray()
    ino = 1
    for entry in entries:
        name, mode, payload = entry[0], entry[1], entry[2]
        rmaj, rmin = (entry[3], entry[4]) if len(entry) > 4 else (0, 0)
        nb = name.encode() + b'\0'
        hdr = b'070701' + b''.join(
            b'%08X' % v for v in (
                ino, mode, 0, 0, 1, 0, len(payload), 0, 0, rmaj, rmin,
                len(nb), 0))
        out += hdr + nb
        out += b'\0' * ((-len(out)) % 4)
        out += payload
        out += b'\0' * ((-len(out)) % 4)
        ino += 1
    nb = b'TRAILER!!!\0'
    out += b'070701' + b''.join(b'%08X' % v for v in
                                (0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, len(nb), 0))
    out += nb
    out += b'\0' * ((-len(out)) % 4)
    return bytes(out)


print('[*] extracting stock kernel from boot_a readback')
boot_a = BOOT_A.read_bytes()
if boot_a[:8] != b'ANDROID!':
    sys.exit('boot_a is not an Android boot image')
k_size, r_size = struct.unpack_from('<II', boot_a, 8)
page = 4096
k_off = page
kernel = boot_a[k_off:k_off + k_size]
(WORK / 'kernel').write_bytes(kernel)
print(f'    kernel {k_size} bytes sha256={sha(kernel)}')

print('[*] reading OpenWrt rootfs')
openwrt = list(newc_entries(gzip.decompress(ROOTFS_SRC.read_bytes())))
print(f'    {len(openwrt)} entries')

print('[*] reading stock vendor modules')
vend_raw = subprocess.run(['lz4', '-dc', str(VENDOR_RD)], check=True,
                          stdout=subprocess.PIPE).stdout
modules = [(n, m, p) for n, m, p in newc_entries(vend_raw)
           if n.startswith('lib/modules/') and n.endswith('.ko')]
print(f'    {len(modules)} .ko modules')

# FACT (adb /proc/version + modinfo): the running kernel accepts
# '5.10.66-gki-g491fe99db339 SMP preempt mod_unload modversions aarch64'
# and dwc3-msm/phy-msm-snps-hs are currently loaded with it.
# Two unrelated .ko carry a '-dirty' variant; they are not in the USB chain.
EXPECTED_VERMAGIC = ('5.10.66-gki-g491fe99db339 SMP preempt mod_unload '
                     'modversions aarch64')
USB_CHAIN = ('dwc3-msm.ko', 'phy-msm-snps-hs.ko', 'phy-msm-ssusb-qmp.ko',
             'phy-msm-snps-eusb2.ko', 'ucsi_glink.ko', 'pmic_glink.ko',
             'altmode-glink.ko', 'fsa4480-i2c.ko',
             'ssusb-redriver-nb7vpq904m.ko')

def vermagic_of(payload: bytes) -> str:
    i = payload.find(b'vermagic=')
    return payload[i + 9:payload.find(b'\0', i)].decode() if i >= 0 else 'NONE'

by_base = {n.split('/')[-1]: vermagic_of(p) for n, _, p in modules}
missing = [m for m in USB_CHAIN if m not in by_base]
if missing:
    sys.exit(f'USB-chain modules absent from vendor ramdisk: {missing}')
bad = {m: by_base[m] for m in USB_CHAIN if by_base[m] != EXPECTED_VERMAGIC}
if bad:
    sys.exit(f'USB-chain vermagic mismatch vs running kernel: {bad}')
vermagic = EXPECTED_VERMAGIC
print(f'    USB-chain vermagic verified: {vermagic}')

(OUT / 'inputs.json').write_text(json.dumps({
    'boot_a': {'path': str(BOOT_A), 'sha256': sha(boot_a)},
    'kernel': {'bytes': k_size, 'sha256': sha(kernel)},
    'openwrt_rootfs': {'path': str(ROOTFS_SRC),
                       'sha256': sha(ROOTFS_SRC.read_bytes()),
                       'entries': len(openwrt)},
    'vendor_ramdisk00': {'path': str(VENDOR_RD), 'sha256': sha(VENDOR_RD.read_bytes()),
                         'modules': len(modules), 'vermagic': vermagic},
}, indent=2) + '\n')
print(f'[*] stage 1 done -> {OUT}/inputs.json')

# ---------------------------------------------------------------- stage 2
# Assemble the ramdisk: OpenWrt userspace + stock modules + diagnostic init.
INIT_SRC = EXP / 'candidate-init-v4.sh'
if not INIT_SRC.exists():
    sys.exit(f'missing {INIT_SRC}')

print('[*] assembling ramdisk')
entries = []
seen = set()

# OpenWrt tree, minus its own /init (replaced) and any stale lib/modules.
for name, mode, payload in openwrt:
    clean = name[2:] if name.startswith('./') else name
    if clean in ('', '.', 'init'):
        continue
    if clean.startswith('lib/modules/'):
        continue          # built for 6.6.73; wrong kernel
    if clean in seen:
        continue
    seen.add(clean)
    entries.append((clean, mode, payload))

# Directories the init needs that the rootfs may not provide.
for d in ('dev', 'proc', 'sys', 'tmp', 'lib', 'lib/modules',
          'sys/kernel', 'sys/kernel/config', 'sys/fs', 'sys/fs/pstore'):
    if d not in seen:
        seen.add(d)
        entries.append((d, 0o040755, b''))

# Static device nodes: the stock kernel has CONFIG_DEVTMPFS unset, so
# nothing populates /dev before init runs. Major/minor taken from the live
# phone (runtime-v2: /dev/kmsg 1,11 ; /dev/pmsg0 252,0 ; console 5,1).
DEV_NODES = (
    ('dev/console', 0o020600, 5, 1),
    ('dev/null',    0o020666, 1, 3),
    ('dev/zero',    0o020666, 1, 5),
    ('dev/tty',     0o020666, 5, 0),
    ('dev/kmsg',    0o020644, 1, 11),
    ('dev/pmsg0',   0o020222, 252, 0),
)
for name, mode, maj, minor in DEV_NODES:
    if name in seen:
        continue
    seen.add(name)
    entries.append((name, mode, b'', maj, minor))

# Stock Qualcomm modules under the version directory kmodloader expects.
# FACT (first QEMU run): a flat lib/modules/ made kmodloader report
# 'no module folders for kernel version 5.10.66-android12-9-...'.
KREL = '5.10.66-android12-9-00005-gf6e6376090be-ab8060604'
mod_dir = f'lib/modules/{KREL}'
if mod_dir not in seen:
    seen.add(mod_dir)
    entries.append((mod_dir, 0o040755, b''))
for name, mode, payload in modules:
    versioned = f'{mod_dir}/' + name.split('/')[-1]
    if versioned in seen:
        continue
    seen.add(versioned)
    entries.append((versioned, 0o100644, payload))

# Diagnostic init as PID 1.
entries.append(('init', 0o100755, INIT_SRC.read_bytes()))

entries.sort(key=lambda e: (e[0].count('/'), e[0]))
cpio = cpio_pack(entries)
ramdisk = gzip.compress(cpio, 9, mtime=0)
(WORK / 'ramdisk.cpio.gz').write_bytes(ramdisk)
print(f'    {len(entries)} entries, cpio {len(cpio)} -> gz {len(ramdisk)} bytes')

# Self-check: the packed archive must parse cleanly and contain the essentials.
repacked = {}
off = 0
raw = gzip.decompress(ramdisk)
node_count = 0
while off < len(raw):
    if raw[off:off + 6] not in (b'070701', b'070702'):
        break
    fl = [int(raw[off + 6 + i * 8:off + 14 + i * 8], 16) for i in range(13)]
    mode, size, ns = fl[1], fl[6], fl[11]
    nm = raw[off + 110:off + 110 + ns - 1].decode()
    st = (off + 110 + ns + 3) & ~3
    if nm == 'TRAILER!!!':
        break
    repacked[nm] = mode
    if mode & 0o170000 == 0o020000:
        node_count += 1
    off = (st + size + 3) & ~3

for need in ('init', 'sbin/init', 'bin/busybox',
             'dev/kmsg', 'dev/pmsg0', 'dev/console',
             f'{mod_dir}/dwc3-msm.ko', f'{mod_dir}/phy-msm-snps-hs.ko'):
    if need not in repacked:
        sys.exit(f'self-check failed: {need} missing from packed ramdisk')
if node_count < len(DEV_NODES):
    sys.exit(f'self-check failed: only {node_count} device nodes packed')
print(f'    self-check: archive parses, {node_count} device nodes, '
      'required members present')

# ---------------------------------------------------------------- stage 3
# Pack an Android boot v4 image with the stock header parameters.
# v4 CMDLINE = the v3 cmdline verbatim (v2 text + ' panic=10').
V2_CMDLINE = ('stack_depot_disable=on kasan.stacktrace=off kvm-arm.mode=protected '
              'cgroup_disable=pressure cgroup.memory=nokmem console=ttyMSM0,115200n8 '
              'loglevel=6 kpti=0 log_buf_len=256K kernel.panic_on_rcu_stall=1 '
              'swiotlb=noforce loop.max_part=7 cgroup.memory=nokmem,nosocket '
              'pcie_ports=compat service_locator.enable=1 msm_rtb.filter=0x237 '
              'allow_mismatched_32bit_el0 cpufreq.default_governor=performance pelt=8 '
              'kasan=off rcupdate.rcu_expedited=1 rcu_nocbs=0-7 irqaffinity=0-3 '
              'ftrace_dump_on_oops pstore.compress=none fsa4480_i2c.async_probe=1 '
              'can.stats_timer=0 video=vfb:640x400,bpp=32,memsize=3072000 '
              'qcom-dload-mode.download_mode=1 bootconfig '
              'msm_drm.dsi_display0=qcom,mdss_dsi_r6130_1080_2400_amoled_cmd: '
              'rootwait ro init=/init')
CMDLINE = V2_CMDLINE + ' panic=10'
if CMDLINE[:len(V2_CMDLINE)] != V2_CMDLINE:
    sys.exit('cmdline prefix drift')

IMG = OUT / 'boot_b-stockkernel-openwrt-v4.img'
subprocess.run([
    'mkbootimg',
    '--header_version', '4',
    '--os_version', '12.0.0',
    '--os_patch_level', '2022-02',
    '--kernel', str(WORK / 'kernel'),
    '--ramdisk', str(WORK / 'ramdisk.cpio.gz'),
    '--cmdline', CMDLINE,
    '--output', str(IMG),
], check=True)

img = IMG.read_bytes()
k2, r2 = struct.unpack_from('<II', img, 8)
if k2 != k_size or r2 != len(ramdisk):
    sys.exit(f'header mismatch: kernel {k2}/{k_size} ramdisk {r2}/{len(ramdisk)}')
if len(img) > 0x6000000:
    sys.exit(f'image {len(img)} exceeds 96 MiB boot partition')

manifest = {
    'image': str(IMG),
    'bytes': len(img),
    'sha256': sha(img),
    'header_version': 4,
    'kernel_bytes': k2,
    'kernel_sha256': sha(kernel),
    'ramdisk_bytes': r2,
    'ramdisk_sha256': sha(ramdisk),
    'ramdisk_entries': len(entries),
    'signature_size': 0,
    'partition_capacity': 0x6000000,
    'cmdline': CMDLINE,
    'cmdline_v2_prefix_identical': True,
    'cmdline_v3_identical': True,
    'init': {'path': str(INIT_SRC), 'sha256': sha(INIT_SRC.read_bytes())},
    'module_vermagic': vermagic,
    'not_yet_verified': [
        'ABL/AVB acceptance of an unsigned boot image on this device',
        'module load success on the real DT (QEMU cannot bind Qualcomm nodes)',
        'DWC3 peripheral role and NCM enumeration on hardware',
    ],
}
(OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(f'[*] image  {IMG}')
print(f'    {len(img)} bytes sha256={manifest["sha256"]}')
print(f'[*] cmdline ({len(CMDLINE)} bytes) panic=10 appended; v2 prefix intact')
print('[*] manifest -> manifest.json')
