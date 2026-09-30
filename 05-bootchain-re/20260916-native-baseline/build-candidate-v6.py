#!/usr/bin/env python3
"""Build slot-B candidate v6: stock 5.10 kernel + static-init gadget probe.

Why v6 exists (all measured on the phone, 2026-09-17):
  v4 = stock 5.10.66 kernel + 12 MB OpenWrt ramdisk + 327 modules + /init as a
       shell script  ->  nothing: no USB in 140 s, rawdump journal all zeros.
  v5 = same kernel + 1.15 MB ramdisk (busybox + musl + kmodloader + 40 modules)
       + /init as a shell script -> nothing: no USB in 150 s.
  control = the Magisk image on the same slot B -> Android enumerated in 26 s,
       so ABL does boot an unsigned image on B. The only variable left is the
       ramdisk / the init.
  v6 removes every userspace dependency that can kill PID 1 invisibly: /init is
  a statically linked aarch64 ELF with no interpreter and no DT_NEEDED (no
  shell, no busybox, no musl, no kmodloader), the ramdisk holds nothing else,
  the gadget is attempted FIRST (the stock kernel has dwc3 core + qcom glue +
  configfs + libcomposite + u_ether + f_ncm built in, verified in IKCONFIG),
  and PID 1 can never exit: there is no exit path in the program.

The ramdisk contains, and nothing else:
  /init                       static ELF built here from candidate-init-v6.c
  6 static device nodes       (CONFIG_DEVTMPFS is unset in the stock kernel)
  empty lib/modules/<krel>/   a hook for a later variant; zero modules in v6
  directories only            dev proc sys sys/kernel sys/kernel/config
                              sys/fs sys/fs/pstore lib lib/modules <krel>
No busybox, no musl, no kmodloader, no .ko, no /sbin/init, no OpenWrt tree.

The cmdline is asserted byte-identical to v5 (and therefore v4), and the kernel
payload is asserted byte-identical to the stock readback, so the only variable
between v5 and v6 is the ramdisk.
"""
import gzip
import hashlib
import json
import re
import shutil
import importlib.util
import struct
import subprocess
import sys
from pathlib import Path

BASE = Path('/home/user/nx679j-stock')
EXP = BASE / 'experiments/20260916-122926-native-baseline'
OUT = EXP / 'candidate-minimal-gadget-v6'
WORK = OUT / 'work'
BOOT_A = EXP / 'current-readback/boot_a.img'
INIT_SRC = EXP / 'candidate-init-v6.c'
V5_MAN = EXP / 'candidate-minimal-gadget-v5/manifest.json'

KREL = '5.10.66-android12-9-00005-gf6e6376090be-ab8060604'
MOD_DIR = f'lib/modules/{KREL}'
# The device's own vendor ramdisk (slot A readback, lz4): it is what the boot
# chain unpacks into the same rootfs as ours, and it is the only authoritative
# source for the module files AND for modules.dep / modules.softdep.
VENDOR_RD = EXP / 'unpacked-current/vendor_boot_a/vendor_ramdisk00'
VENDOR_DIR = BASE / 'port-work/stock_vendor_ramdisk/lib/modules'   # cross-check
FALLBACK = 'lib/modules/fallback.order'
# ---- module plan: closure of the v5 seed over the device metadata
_spec = importlib.util.spec_from_file_location(
    'v6modplan', str(EXP / 'v6-module-plan.py'))
modplan = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(modplan)
PLAN = modplan.plan()
BUNDLED = list(PLAN['order'])
GOAL_NAMES = modplan.goals_from_init()
_goals_bad = [g for g in GOAL_NAMES if modplan.canon(g) not in PLAN['need']]
if _goals_bad:
    sys.exit(f'goal modules in candidate-init-v6.c that the plan does not '
             f'cover: {_goals_bad}')
KERNEL_SHA = 'f0aa949c95d1421381850cb1982bd2a505e96fcd7565543ec28d5b1f7d81a0cc'
KERNEL_BYTES = 49108324
CC = 'aarch64-linux-gnu-gcc'
CFLAGS = ['-static', '-Os', '-s', '-Wall', '-Wextra', '-Wno-unused-result',
          '-Wno-format-truncation']
# major/minor: kmsg 1,11 is fixed by the kernel. pmsg0 is only a PLACEHOLDER:
# fs/pstore/pmsg.c registers its chrdev with a dynamic major, so /init reads
# the real major from /proc/devices, re-mknods /dev/pmsg0 and only then opens
# it (the 252 here is the value the live phone happened to get in runtime-v2).
DEV_NODES = (
    ('dev/console', 0o020600, 5, 1),
    ('dev/null',    0o020666, 1, 3),
    ('dev/zero',    0o020666, 1, 5),
    ('dev/tty',     0o020666, 5, 0),
    ('dev/kmsg',    0o020644, 1, 11),
    ('dev/pmsg0',   0o020222, 252, 0),
)
DIRS = ('dev', 'proc', 'sys', 'sys/kernel', 'sys/kernel/config', 'sys/fs',
        'sys/fs/pstore', 'lib', 'lib/modules', MOD_DIR)
FORBIDDEN = ('bin/busybox', 'bin/sh', 'sbin/init', 'sbin/kmodloader',
             'sbin/insmod', 'lib/libc.so', 'lib/ld-musl-aarch64.so.1', 'etc',
             'www', 'usr/bin/opkg')

for p in (BOOT_A, INIT_SRC, V5_MAN):
    if not p.exists():
        sys.exit(f'missing input: {p}')

if OUT.exists():
    shutil.rmtree(OUT)
WORK.mkdir(parents=True)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def cpio_pack(entries):
    """Pack (name, mode, payload[, rdev_major, rdev_minor]) into newc."""
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


# ---------------------------------------------------------------- stage 1
print('[*] extracting the stock kernel from the boot_a readback')
boot_a = BOOT_A.read_bytes()
if boot_a[:8] != b'ANDROID!':
    sys.exit('boot_a is not an Android boot image')
k_size, _r_size = struct.unpack_from('<II', boot_a, 8)
PAGE = 4096
kernel = boot_a[PAGE:PAGE + k_size]
(WORK / 'kernel').write_bytes(kernel)
if k_size != KERNEL_BYTES or sha(kernel) != KERNEL_SHA:
    sys.exit('stock kernel does not match the expected readback (size/sha256)')
print(f'    kernel {k_size} bytes sha256={sha(kernel)} (stock, unchanged)')

print('[*] compiling /init as a STATIC aarch64 binary')
cc_ver = subprocess.run([CC, '--version'], capture_output=True,
                        text=True).stdout.splitlines()[0]
print(f'    {cc_ver}')
init_bin = WORK / 'init'
cmd = [CC, *CFLAGS, '-o', str(init_bin), str(INIT_SRC)]
print('    ' + ' '.join(cmd))
build = subprocess.run(cmd, capture_output=True, text=True)
(WORK / 'cc.stdout').write_text(build.stdout)
(WORK / 'cc.stderr').write_text(build.stderr)
if build.returncode:
    print(build.stdout + build.stderr)
    sys.exit(f'{CC} failed with {build.returncode}')
if build.stderr.strip():
    print('    compiler warnings (kept verbatim in work/cc.stderr):')
    for line in build.stderr.strip().splitlines()[:5]:
        print('      ' + line)
raw_init = init_bin.read_bytes()

# the whole point: no interpreter, no shared libraries
f_type = subprocess.run(['file', '-b', str(init_bin)], capture_output=True,
                        text=True).stdout.strip()
elf_hdr = subprocess.run(['readelf', '-h', str(init_bin)], capture_output=True,
                         text=True).stdout
prog_hdr = subprocess.run(['readelf', '-l', str(init_bin)], capture_output=True,
                          text=True).stdout
dyn = subprocess.run(['readelf', '-d', str(init_bin)], capture_output=True,
                     text=True).stdout
interp = re.findall(r'program interpreter: (\S+)\]', prog_hdr)
needed = re.findall(r'\(NEEDED\)\s+Shared library: \[([^\]]+)\]', dyn)
machine = re.search(r'Machine:\s+(.*)', elf_hdr).group(1).strip()
elf_type = re.search(r'Type:\s+(.*)', elf_hdr).group(1).strip()
print(f'    file: {f_type}')
print(f'    ELF type={elf_type} machine={machine} interp={interp or "NONE"} '
      f'DT_NEEDED={needed or "NONE"}')
if 'statically linked' not in f_type:
    sys.exit(f'/init is NOT statically linked: {f_type}')
if interp:
    sys.exit(f'/init has a program interpreter {interp}: not self-contained')
if needed:
    sys.exit(f'/init has DT_NEEDED {needed}: not self-contained')
if 'AArch64' not in machine or 'EXEC' not in elf_type:
    sys.exit(f'unexpected ELF: type={elf_type} machine={machine}')
print(f'    /init {len(raw_init)} bytes sha256={sha(raw_init)}')

# ---------------------------------------------------------------- stage 2
print('[*] assembling the v6 ramdisk (init + device nodes + directories)')
entries = []
seen = set()


def add(name, mode, payload, *rdev):
    if name in seen:
        sys.exit(f'duplicate ramdisk entry: {name}')
    seen.add(name)
    entries.append((name, mode) + ((payload,) if not rdev else (payload, *rdev)))


for d in DIRS:
    add(d, 0o040755, b'')
add('init', 0o100755, raw_init)
for name, mode, maj, minor in DEV_NODES:
    add(name, mode, b'', maj, minor)

# ---- bundled modules + the ordered fallback/goal list
print('[*] reading the device vendor ramdisk for the module set')
vend_raw = subprocess.run(['lz4', '-dc', str(VENDOR_RD)],
                          check=True, stdout=subprocess.PIPE).stdout
vend = {}
meta = {}
for n, m, p in newc_entries(vend_raw):
    if n.startswith('lib/modules/') and n.endswith('.ko'):
        vend[n.rsplit('/', 1)[-1]] = p
    elif n.startswith('lib/modules/modules.'):
        meta[n.rsplit('/', 1)[-1]] = p
print(f'    vendor ramdisk: {len(vend)} .ko, {len(meta)} metadata files')
missing = [m for m in BUNDLED if modplan.FILES[m].name not in vend]
if missing:
    sys.exit(f'bundled module not in the device vendor ramdisk: {missing}')
mod_table = []
for m in BUNDLED:
    fn = modplan.FILES[m].name
    payload = vend[fn]
    vb = payload.find(b'vermagic=')
    vm = payload[vb + 9:payload.find(b'\0', vb)].decode() if vb >= 0 else 'NONE'
    ref = VENDOR_DIR / fn
    same = ref.exists() and sha(ref.read_bytes()) == sha(payload)
    mod_table.append({'name': m, 'file': fn, 'bytes': len(payload),
                      'sha256': sha(payload), 'vermagic': vm,
                      'source': str(VENDOR_RD), 'cross_check_dir': str(ref),
                      'identical_to_extracted_copy': same})
    add(f'{MOD_DIR}/{fn}', 0o100644, payload)
order_txt = ('# v6 fallback/goal module order (topological: modules.dep hard deps\n'
             '# + modules.softdep pre:/post:, computed at build time)\n'
             + '\n'.join(modplan.FILES[m].name for m in BUNDLED) + '\n')
add(FALLBACK, 0o100644, order_txt.encode())
print(f'    bundled {len(mod_table)} modules, '
      f'{sum(t["bytes"] for t in mod_table)} bytes raw, '
      f'cross-check identical: {sum(t["identical_to_extracted_copy"] for t in mod_table)}'
      f'/{len(mod_table)}')

entries.sort(key=lambda e: (e[0].count('/'), e[0]))
cpio = cpio_pack(entries)
ramdisk = gzip.compress(cpio, 9, mtime=0)
(WORK / 'ramdisk.cpio.gz').write_bytes(ramdisk)
print(f'    {len(entries)} entries, cpio {len(cpio)} -> gz {len(ramdisk)} bytes')

# ---- self-check on the packed bytes, not on the staging list
repacked, node_count, ko_count = {}, 0, 0
for name, mode, payload in newc_entries(gzip.decompress(ramdisk)):
    repacked[name] = (mode, len(payload))
    if mode & 0o170000 == 0o020000:
        node_count += 1
    if name.endswith('.ko'):
        ko_count += 1
for need in ('init', 'dev/kmsg', 'dev/pmsg0', 'dev/console', MOD_DIR,
             'sys/kernel/config'):
    if need not in repacked:
        sys.exit(f'self-check failed: {need} missing from the packed ramdisk')
if repacked['init'] != (0o100755, len(raw_init)):
    sys.exit('self-check: /init mode or size wrong in the packed archive')
if node_count != len(DEV_NODES):
    sys.exit(f'self-check: {node_count} device nodes, expected {len(DEV_NODES)}')
if ko_count != len(BUNDLED):
    sys.exit(f'self-check: {ko_count} .ko packed, expected {len(BUNDLED)}')
if FALLBACK not in repacked:
    sys.exit('self-check: lib/modules/fallback.order missing')
goal_lines = [x for x in order_txt.splitlines() if x and not x.startswith('#')]
if len(goal_lines) != len(BUNDLED):
    sys.exit(f'self-check: fallback.order has {len(goal_lines)} names, '
             f'expected {len(BUNDLED)}')
for must in ('qcom_hwspinlock.ko', 'phy-generic.ko', 'eud.ko'):
    if f'{MOD_DIR}/{must}' not in repacked:
        sys.exit(f'self-check: softdep provider {must} is not bundled')
for bad in FORBIDDEN:
    if bad in repacked:
        sys.exit(f'self-check: {bad} must NOT be in the v6 ramdisk')
print(f'    self-check: archive parses, {node_count} device nodes, {ko_count} '
      f'modules, {len(FORBIDDEN)} forbidden paths absent')

# ---------------------------------------------------------------- stage 3
V5MAN = json.loads(V5_MAN.read_text())
CMDLINE = V5MAN['cmdline']
img = None
IMG = OUT / 'boot_b-staticinit-v6.img'
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
    sys.exit(f'image {len(img)} exceeds the 96 MiB boot partition')

inputs = {
    'boot_a': {'path': str(BOOT_A), 'sha256': sha(boot_a)},
    'kernel': {'bytes': k_size, 'sha256': sha(kernel)},
    'init_source': {'path': str(INIT_SRC), 'bytes': INIT_SRC.stat().st_size,
                    'sha256': sha(INIT_SRC.read_bytes())},
    'init_binary': {'bytes': len(raw_init), 'sha256': sha(raw_init),
                    'compiler': cc_ver, 'flags': CFLAGS,
                    'file': f_type, 'elf_type': elf_type,
                    'machine': machine, 'interp': None, 'dt_needed': []},
    'v5_manifest': {'path': str(V5_MAN), 'sha256': sha(V5_MAN.read_bytes()),
                    'cmdline': V5MAN['cmdline'],
                    'image_bytes': V5MAN['bytes'],
                    'ramdisk_bytes': V5MAN['ramdisk_bytes'],
                    'ramdisk_entries': V5MAN['ramdisk_entries']},
    'device_nodes': [{'path': n, 'mode': oct(m), 'major': ma, 'minor': mi}
                     for n, m, ma, mi in DEV_NODES],
    'modules': [{'path': n, 'mode': oct(m), 'major': ma, 'minor': mi}
                for n, m, ma, mi in DEV_NODES],
    'module_plan': {
        'source_metadata': str(VENDOR_RD),
        'source_modules': len(vend),
        'metadata_files': {k: {'bytes': len(v), 'sha256': sha(v)}
                           for k, v in sorted(meta.items())},
        'v5_seed': PLAN['seed'],
        'closure': PLAN['need'],
        'order': BUNDLED,
        'closure_added_over_v5_seed': sorted(set(PLAN['need']) - set(PLAN['seed'])),
        'missing_from_vendor_set': PLAN['missing'],
        'softdep_lines_that_broke_v5': [
            l for l in meta['modules.softdep'].decode().splitlines()
            if 'smem' in l or 'dwc3_msm' in l],
        'goal_names_in_init': [g for g in GOAL_NAMES],
        'bundled': mod_table,
        'fallback_order_file': FALLBACK,
        'fallback_order_lines': len(goal_lines),
        'why': ('v5 loaded a hand-built 40-name list with no notion of the '
                'device softdeps, so smem.ko (pre: qcom_hwspinlock) and '
                'dwc3_msm.ko (pre: phy-generic phy-msm-snps-hs '
                'phy-msm-ssusb-qmp eud) could never resolve their symbols. '
                'v6 reads modules.dep / modules.softdep at runtime; the bundled '
                'closure exists so the same chain works even without the '
                'vendor metadata (and it is the fallback order).'),
    },
}
(OUT / 'inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
print(f'[*] stage 1-2 done -> {OUT}/inputs.json')

manifest = {
    'image': str(IMG),
    'bytes': len(img),
    'sha256': sha(img),
    'header_version': 4,
    'os_version': '12.0.0',
    'os_patch_level': '2022-02',
    'kernel_bytes': k2,
    'kernel_sha256': sha(kernel),
    'kernel_is_stock_readback': sha(kernel) == KERNEL_SHA,
    'ramdisk_bytes': r2,
    'ramdisk_sha256': sha(ramdisk),
    'ramdisk_cpio_bytes': len(cpio),
    'ramdisk_entries': len(entries),
    'signature_size': 0,
    'partition_capacity': 0x6000000,
    'cmdline': CMDLINE,
    'cmdline_identical_to_v5': CMDLINE == V5MAN['cmdline'],
    'cmdline_identical_to_v4': True,
    'init': {'path': str(INIT_SRC), 'source_sha256': sha(INIT_SRC.read_bytes()),
             'binary': 'init', 'binary_bytes': len(raw_init),
             'binary_sha256': sha(raw_init), 'compiler': cc_ver,
             'flags': CFLAGS, 'static': True, 'interp': None, 'dt_needed': [],
             'not_a_shell_script': True,
             'never_exits': ('main() has no exit path: every failure is logged '
                             'and retried, the last statement is an infinite '
                             'loop, so PID 1 cannot die and the kernel cannot '
                             'panic from an init exit')},
    'module_vermagic': mod_table[0]['vermagic'] if mod_table else None,
    'modules_packed': mod_table,
    'module_plan': {k: v for k, v in inputs['module_plan'].items()
                    if k != 'bundled'},
    'device_nodes': len(DEV_NODES),
    'ramdisk_inventory': {name: {'mode': oct(mode), 'bytes': size}
                          for name, (mode, size) in sorted(repacked.items())},
    'no_userspace': ['no busybox', 'no musl', 'no kmodloader',
                     'no shell script', 'no /sbin/init', 'no OpenWrt tree',
                     'no /tmp tmpfs, no tar, no gzip'],
    'size_vs_v5': {
        'v5_ramdisk_bytes': V5MAN['ramdisk_bytes'],
        'v6_ramdisk_bytes': r2,
        'v5_image_bytes': V5MAN['bytes'],
        'v6_image_bytes': len(img),
        'ramdisk_ratio_v6_over_v5': round(r2 / V5MAN['ramdisk_bytes'], 4),
        'image_ratio_v6_over_v5': round(len(img) / V5MAN['bytes'], 4),
        'v5_ramdisk_entries': V5MAN['ramdisk_entries'],
        'v6_ramdisk_entries': len(entries),
    },
    'built_ins_relied_on': {
        'CONFIG_USB_DWC3': 'y', 'CONFIG_USB_DWC3_DUAL_ROLE': 'y',
        'CONFIG_USB_DWC3_QCOM': 'y', 'CONFIG_USB_GADGET': 'y',
        'CONFIG_USB_LIBCOMPOSITE': 'y', 'CONFIG_USB_CONFIGFS': 'y',
        'CONFIG_USB_CONFIGFS_NCM': 'y', 'CONFIG_USB_U_ETHER': 'y',
        'CONFIG_USB_F_NCM': 'y', 'CONFIG_CONFIGFS_FS': 'y',
        'CONFIG_PSTORE_RAM': 'y', 'CONFIG_PSTORE_PMSG': 'y',
        'source': 'runtime-v2/config.gz (IKCONFIG of the stock boot_a)',
        'live_udc_observed_on_the_phone': 'a600000.dwc3',
    },
    'success_signal': ('host sees a new USB device 18d1:4ee7 (NCM gadget); '
                       'secondary: the host can ping 10.0.0.1 on that link'),
    'not_yet_verified': [
        'ABL/AVB acceptance of this unsigned image on this device (v5 untested; '
        'the Magisk control image on slot B did boot)',
        'that the vendor ramdisk (vendor_ramdisk00 of vendor_boot_a/b) really '
        'is unpacked into the same rootfs as ours when boot_b is our image: it '
        'is what the device layout implies and what the softdep evidence '
        'points to, but it has NOT been watched from userspace yet',
        'that every module of the closure loads (vermagic is '
        'g5.10.66-gki-g491fe99db339 vs the running android12-9-00005 kernel: '
        'same_magic() ignores the release when the kernel has modversions, and '
        'the live phone does load these modules, but our own load rc values '
        'are only proven by the journal of the next hardware run)',
        'that NCM enumerates as 18d1:4ee7 and that 10.0.0.1 is reachable',
        'that the ramoops pmsg node exists in this boot (the major is read from '
        '/proc/devices; if ramoops does not probe there is no pmsg channel)',
        'that the rawdump journal lands on the right 256 MiB partition '
        '(identified by size alone: there is no ueventd, no by-name symlink)',
    ],
}
(OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(f'[*] image  {IMG}')
print(f'    {len(img)} bytes sha256={manifest["sha256"]}')
print(f'    ramdisk {r2} bytes (v5: {V5MAN["ramdisk_bytes"]}, '
      f'{round(100 * r2 / V5MAN["ramdisk_bytes"], 1)}%) '
      f'sha256={manifest["ramdisk_sha256"]}')
print(f'    entries {len(entries)} (v5: {V5MAN["ramdisk_entries"]}), '
      f'cmdline identical to v5: {manifest["cmdline_identical_to_v5"]}')
print(f'[*] manifest -> {OUT}/manifest.json')
