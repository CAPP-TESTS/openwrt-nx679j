#!/usr/bin/env python3
"""Build slot-B candidate v5: stock 5.10 kernel + MINIMAL gadget-probe ramdisk.

Why: on the phone, v4 (stock kernel + 12 MB OpenWrt ramdisk, 327 modules) gave
no USB enumeration in 140 s and an all-zero breadcrumb journal, while the
known-good Magisk image on the same slot enumerated in 26 s. v5 removes the
bulk and nothing else, so the next hardware run can tell apart
  (a) "the ramdisk size/content was the problem" vs
  (b) "the kernel never reached /init at all".

The ramdisk contains, and nothing else:
  /init                       candidate-init-v5.sh (PID 1, never hands over)
  /bin/busybox + applet symlinks, /sbin/insmod (kmodloader) + /lib/{libc.so,
  libgcc_s.so.1, libubox.so.20240329, ld-musl-aarch64.so.1}
  6 static device nodes (CONFIG_DEVTMPFS is unset in the stock kernel)
  /lib/modules/<krel>/  = exactly the 40 modules named in candidate-init-v5.sh
No OpenWrt tree, no /sbin/init, no procd, no other 287 vendor modules.

The module set is parsed out of the init script and resolved against the stock
vendor ramdisk with the same 4-step algorithm the shell uses, so the packed
files cannot drift from the names the init will look for.
"""
import gzip
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

BASE = Path('/home/user/nx679j-stock')
EXP = BASE / 'experiments/20260916-122926-native-baseline'
OUT = EXP / 'candidate-minimal-gadget-v5'
WORK = OUT / 'work'
ROOTFS_SRC = BASE / 'port-work/openwrt-phase1/openwrt-rootfs.cpio.gz'
BOOT_A = EXP / 'current-readback/boot_a.img'
VENDOR_RD = EXP / 'unpacked-current/vendor_boot_a/vendor_ramdisk00'
INIT_SRC = EXP / 'candidate-init-v5.sh'

KREL = '5.10.66-android12-9-00005-gf6e6376090be-ab8060604'
MOD_DIR = f'lib/modules/{KREL}'
EXPECTED_VERMAGIC = ('5.10.66-gki-g491fe99db339 SMP preempt mod_unload '
                     'modversions aarch64')
USB_CHAIN = ('dwc3-msm.ko', 'phy-msm-snps-hs.ko', 'phy-msm-ssusb-qmp.ko',
             'phy-msm-snps-eusb2.ko', 'ucsi_glink.ko', 'pmic_glink.ko',
             'altmode-glink.ko', 'fsa4480-i2c.ko',
             'ssusb-redriver-nb7vpq904m.ko')
# the only applets the init needs: shebang interpreter, bare PATH commands and
# the $BB applets it calls explicitly
APPLETS = ('sh', 'uname', 'cat', 'ls', 'wc', 'basename', 'which', 'rm', 'dd',
           'head', 'tail', 'ip', 'mkdir', 'mount', 'mknod', 'ln', 'sync',
           'sleep')
# major/minor taken from the live phone (runtime-v2: kmsg 1,11 ; pmsg0 252,0)
DEV_NODES = (
    ('dev/console', 0o020600, 5, 1),
    ('dev/null',    0o020666, 1, 3),
    ('dev/zero',    0o020666, 1, 5),
    ('dev/tty',     0o020666, 5, 0),
    ('dev/kmsg',    0o020644, 1, 11),
    ('dev/pmsg0',   0o020222, 252, 0),
)
DIRS = ('dev', 'proc', 'sys', 'sys/kernel', 'sys/kernel/config', 'sys/fs',
        'sys/fs/pstore', 'tmp', 'bin', 'sbin', 'lib', 'lib/modules', MOD_DIR)

for p in (ROOTFS_SRC, BOOT_A, VENDOR_RD, INIT_SRC):
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
    CONFIG_DEVTMPFS unset, so /dev is NOT populated automatically.
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


# ---------------------------------------------------------------- stage 1
print('[*] extracting stock kernel from boot_a readback')
boot_a = BOOT_A.read_bytes()
if boot_a[:8] != b'ANDROID!':
    sys.exit('boot_a is not an Android boot image')
k_size, r_size = struct.unpack_from('<II', boot_a, 8)
PAGE = 4096
kernel = boot_a[PAGE:PAGE + k_size]
(WORK / 'kernel').write_bytes(kernel)
print(f'    kernel {k_size} bytes sha256={sha(kernel)}')
if k_size != 49108324 or sha(kernel) != (
        'f0aa949c95d1421381850cb1982bd2a505e96fcd7565543ec28d5b1f7d81a0cc'):
    sys.exit('stock kernel does not match the expected readback (size/sha256)')

print('[*] unwrapping OpenWrt rootfs (source of busybox + musl + kmodloader)')
OWRT = WORK / 'owrt'
OWRT.mkdir()
subprocess.run(['sh', '-c',
                f'gzip -dc {ROOTFS_SRC} | cpio -idm --quiet -D {OWRT}'],
               check=True)
print(f'    extracted to {OWRT} ({len(list(OWRT.rglob("*")))} paths)')

print('[*] reading stock vendor modules')
vend_raw = subprocess.run(['lz4', '-dc', str(VENDOR_RD)], check=True,
                          stdout=subprocess.PIPE).stdout
modules = {}
mod_modes = {}
for n, m, p in newc_entries(vend_raw):
    if n.startswith('lib/modules/') and n.endswith('.ko'):
        modules[n.split('/')[-1]] = p
        mod_modes[n.split('/')[-1]] = m
print(f'    {len(modules)} .ko modules in the vendor ramdisk')


def vermagic_of(payload: bytes) -> str:
    i = payload.find(b'vermagic=')
    return payload[i + 9:payload.find(b'\0', i)].decode() if i >= 0 else 'NONE'


# ---- module set = exactly the names in the v5 init, resolved like the shell
init_text = INIT_SRC.read_text()
blk = init_text.split('for ko in ', 1)[1].split('\ndo\n', 1)[0]
names = blk.replace('\\', ' ').split()
alias = {}
for line in init_text.splitlines():
    m = re.match(r'\s*([A-Za-z0-9_]+)\)\s*cand="\$M/([A-Za-z0-9_.-]+)\.ko"', line)
    if m:
        alias[m.group(1)] = m.group(2) + '.ko'
print(f'[*] resolving {len(names)} module names from candidate-init-v5.sh')
resolved, unresolved, table = [], [], []
for nm in names:
    cands = [('literal', f'{nm}.ko'),
             ('all-hyphens', nm.replace('_', '-') + '.ko'),
             ('all-underscores', nm.replace('-', '_') + '.ko')]
    if nm in alias:
        cands.append(('mixed-spelling alias', alias[nm]))
    hit = next(((file, how) for how, file in cands if file in modules), None)
    if hit is None:
        unresolved.append(nm)
        table.append({'name': nm, 'file': None, 'how': None})
        continue
    file, how = hit
    resolved.append(file)
    table.append({'name': nm, 'file': file, 'how': how,
                  'bytes': len(modules[file]),
                  'vermagic': vermagic_of(modules[file]),
                  'vermagic_ok': vermagic_of(modules[file]) == EXPECTED_VERMAGIC})
if unresolved:
    sys.exit(f'module names that resolve to no vendor .ko: {unresolved}')
packed = sorted(set(resolved))
print(f'    {len(resolved)} names -> {len(packed)} distinct .ko files, '
      f'{sum(len(modules[f]) for f in packed)} bytes raw')
bad_verm = [r['name'] for r in table if not r['vermagic_ok']]
print(f'    vermagic != "{EXPECTED_VERMAGIC}": {bad_verm or "none"}')
missing_usb = [m for m in USB_CHAIN if m not in packed]
if missing_usb:
    sys.exit(f'USB-chain module not in the packed set: {missing_usb}')

# ---- library closure for every dynamic ELF that goes into the ramdisk
def readelf_needed(path: Path):
    d = subprocess.run(['readelf', '-d', str(path)], capture_output=True,
                       text=True).stdout
    return sorted(set(re.findall(r'\(NEEDED\)\s+Shared library: \[([^\]]+)\]', d)))


def readelf_interp(path: Path):
    d = subprocess.run(['readelf', '-l', str(path)], capture_output=True,
                       text=True).stdout
    m = re.search(r'Requesting program interpreter: (\S+)\]', d)
    return m.group(1) if m else None


def find_lib(soname: str):
    for d in ('lib', 'usr/lib'):
        p = OWRT / d / soname
        if p.exists():
            return p
    return None


print('[*] computing the shared-library closure of the ramdisk binaries')
BINARIES = {'bin/busybox': OWRT / 'bin/busybox',
            'sbin/kmodloader': OWRT / 'sbin/kmodloader'}
libs = {}
interps = {}
queue = list(BINARIES.values())
seen_q = set()
while queue:
    elffile = queue.pop()
    if elffile in seen_q:
        continue
    seen_q.add(elffile)
    it = readelf_interp(elffile)
    if it:
        interps[str(elffile)] = it
        if not (OWRT / it.lstrip('/')).exists():
            sys.exit(f'interpreter {it} missing from the OpenWrt rootfs')
    for need in readelf_needed(elffile):
        p = find_lib(need)
        if p is None:
            sys.exit(f'{elffile.name} needs {need}: not in the OpenWrt rootfs')
        libs[need] = p
        queue.append(p)
for so, p in sorted(libs.items()):
    print(f'    {so:24s} <- {p.relative_to(OWRT)} ({p.stat().st_size} bytes)')
for k, v in sorted(interps.items()):
    print(f'    interpreter of {k}: {v}')
if 'libc.so' not in libs or 'ld-musl-aarch64.so.1' not in {
        Path(i).name for i in interps.values()}:
    sys.exit('unexpected musl layout: libc.so / ld-musl-aarch64.so.1 not both used')

# ---- applet availability: ground truth is the OpenWrt busybox symlink farm
farm = {}
for d in ('bin', 'sbin', 'usr/bin', 'usr/sbin'):
    src = OWRT / d
    if not src.is_dir():
        continue
    for p in src.iterdir():
        if p.is_symlink() and 'busybox' in (p.readlink().as_posix()):
            farm[p.name] = f'{d}/{p.name}'
missing_applets = [a for a in APPLETS if a not in farm]
if missing_applets:
    sys.exit(f'applets not provided by this busybox build: {missing_applets}')
print(f'    all {len(APPLETS)} needed applets exist as busybox symlinks in the '
      'OpenWrt rootfs')

(OUT / 'inputs.json').write_text(json.dumps({
    'boot_a': {'path': str(BOOT_A), 'sha256': sha(boot_a)},
    'kernel': {'bytes': k_size, 'sha256': sha(kernel)},
    'openwrt_rootfs': {'path': str(ROOTFS_SRC),
                       'sha256': sha(ROOTFS_SRC.read_bytes())},
    'vendor_ramdisk00': {'path': str(VENDOR_RD),
                         'sha256': sha(VENDOR_RD.read_bytes()),
                         'modules_total': len(modules)},
    'init': {'path': str(INIT_SRC), 'sha256': sha(INIT_SRC.read_bytes())},
    'module_table': table,
    'libraries': {so: {'from': str(p.relative_to(OWRT)),
                       'bytes': p.stat().st_size} for so, p in sorted(libs.items())},
    'interpreters': interps,
    'applets': {a: farm[a] for a in APPLETS},
}, indent=2) + '\n')
print(f'[*] stage 1 done -> {OUT}/inputs.json')

# ---------------------------------------------------------------- stage 2
print('[*] assembling the minimal ramdisk')
entries = []
seen = set()


def add(name, mode, payload, *rdev):
    if name in seen:
        sys.exit(f'duplicate ramdisk entry: {name}')
    seen.add(name)
    entries.append((name, mode) + ((payload,) if not rdev else (payload, *rdev)))


for d in DIRS:
    add(d, 0o040755, b'')

add('init', 0o100755, INIT_SRC.read_bytes())
add('bin/busybox', 0o100755, (OWRT / 'bin/busybox').read_bytes())
add('sbin/kmodloader', 0o100755, (OWRT / 'sbin/kmodloader').read_bytes())
add('sbin/insmod', 0o120777, b'kmodloader')
for a in sorted(APPLETS):
    add(f'bin/{a}', 0o120777, b'busybox')

for so, p in sorted(libs.items()):
    add(f'lib/{so}', 0o100755, p.read_bytes())
add('lib/ld-musl-aarch64.so.1', 0o120777, b'libc.so')

for name, mode, maj, minor in DEV_NODES:
    add(name, mode, b'', maj, minor)

for f in packed:
    add(f'{MOD_DIR}/{f}', mod_modes[f], modules[f])

entries.sort(key=lambda e: (e[0].count('/'), e[0]))
cpio = cpio_pack(entries)
ramdisk = gzip.compress(cpio, 9, mtime=0)
(WORK / 'ramdisk.cpio.gz').write_bytes(ramdisk)
print(f'    {len(entries)} entries, cpio {len(cpio)} -> gz {len(ramdisk)} bytes')

# ---- self-check: the packed archive must parse and contain the essentials
raw = gzip.decompress(ramdisk)
repacked, node_count, ko_count = {}, 0, 0
off = 0
while off < len(raw):
    if raw[off:off + 6] not in (b'070701', b'070702'):
        sys.exit(f'self-check: bad cpio magic at {off}')
    fl = [int(raw[off + 6 + i * 8:off + 14 + i * 8], 16) for i in range(13)]
    mode, size, ns = fl[1], fl[6], fl[11]
    nm = raw[off + 110:off + 110 + ns - 1].decode()
    st = (off + 110 + ns + 3) & ~3
    if nm == 'TRAILER!!!':
        break
    repacked[nm] = mode
    if mode & 0o170000 == 0o020000:
        node_count += 1
    if nm.endswith('.ko'):
        ko_count += 1
    off = (st + size + 3) & ~3
for need in ('init', 'bin/busybox', 'bin/sh', 'sbin/insmod',
             'sbin/kmodloader', 'lib/libc.so', 'lib/ld-musl-aarch64.so.1',
             'dev/kmsg', 'dev/pmsg0', 'dev/console', 'sys/kernel/config',
             MOD_DIR):
    if need not in repacked:
        sys.exit(f'self-check failed: {need} missing from packed ramdisk')
if node_count != len(DEV_NODES):
    sys.exit(f'self-check: {node_count} device nodes, expected {len(DEV_NODES)}')
if ko_count != len(packed):
    sys.exit(f'self-check: {ko_count} .ko packed, expected {len(packed)}')
if 'sbin/init' in repacked:
    sys.exit('self-check: /sbin/init must NOT be in the minimal ramdisk')
print(f'    self-check: archive parses, {node_count} device nodes, {ko_count} '
      'modules, no /sbin/init')

# ---------------------------------------------------------------- stage 3
# Android boot v4 header, identical cmdline parameters to v4 (incl. panic=10).
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
V4MAN = json.loads((EXP / 'candidate-stockkernel-v4/manifest.json').read_text())
if CMDLINE != V4MAN['cmdline']:
    sys.exit('cmdline drifted from v4')

IMG = OUT / 'boot_b-minimal-gadget-v5.img'
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

V4_RAMDISK = V4MAN['ramdisk_bytes']
V4_IMG = V4MAN['bytes']
manifest = {
    'image': str(IMG),
    'bytes': len(img),
    'sha256': sha(img),
    'header_version': 4,
    'kernel_bytes': k2,
    'kernel_sha256': sha(kernel),
    'ramdisk_bytes': r2,
    'ramdisk_sha256': sha(ramdisk),
    'ramdisk_cpio_bytes': len(cpio),
    'ramdisk_entries': len(entries),
    'signature_size': 0,
    'partition_capacity': 0x6000000,
    'cmdline': CMDLINE,
    'cmdline_identical_to_v4': True,
    'init': {'path': str(INIT_SRC), 'sha256': sha(INIT_SRC.read_bytes())},
    'module_vermagic': EXPECTED_VERMAGIC,
    'module_names': len(names),
    'modules_packed': packed,
    'module_table': table,
    'libraries': sorted(libs),
    'applets': sorted(APPLETS),
    'device_nodes': len(DEV_NODES),
    'no_userspace': ['no /sbin/init', 'no procd', 'no OpenWrt tree',
                     'no other 287 vendor modules'],
    'size_vs_v4': {
        'v4_ramdisk_bytes': V4_RAMDISK,
        'v5_ramdisk_bytes': r2,
        'v4_image_bytes': V4_IMG,
        'v5_image_bytes': len(img),
        'ramdisk_ratio_v5_over_v4': round(r2 / V4_RAMDISK, 4),
        'image_ratio_v5_over_v4': round(len(img) / V4_IMG, 4),
    },
    'success_signal': ('host sees a new USB device 18d1:4ee7 (NCM gadget) - '
                       'secondary: the host can ping 10.0.0.1 on the ncm link'),
    'not_yet_verified': [
        'ABL/AVB acceptance of an unsigned boot image on this device',
        'module load success on the real DT (QEMU cannot bind Qualcomm nodes)',
        'DWC3 peripheral role, role-switch poke and NCM enumeration on hardware',
    ],
}
(OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(f'[*] image  {IMG}')
print(f'    {len(img)} bytes sha256={manifest["sha256"]}')
print(f'    ramdisk {r2} bytes (v4: {V4_RAMDISK}, '
      f'{round(100 * r2 / V4_RAMDISK, 1)}%) sha256={manifest["ramdisk_sha256"]}')
print(f'    entries {len(entries)} (v4: {V4MAN["ramdisk_entries"]})')
print(f'[*] manifest -> {OUT}/manifest.json')
