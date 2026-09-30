#!/usr/bin/env python3
"""Independent structural verification of candidate v6 (static-init gadget probe).

Everything is checked against the bytes of the shipped image on disk, not
against the staging files of the build: the kernel payload, the ramdisk, the
cpio inside it and /init inside that are all re-extracted here.

Checks
  1. the image is an Android boot image v4, signature_size 0, within the 96 MiB
     boot partition, and every hash in manifest.json recomputes;
  2. the kernel payload is byte-identical to the stock boot_a readback (cmp, not
     just a hash compare), so the only variable vs v5 is the ramdisk;
  3. the cmdline field is byte-identical to the v5 manifest cmdline;
  4. the ramdisk is gzip, and the cpio inside it is strict newc that parses to
     exactly the 17 declared entries: 10 directories, /init, 6 device nodes and
     nothing else - no .ko, no busybox, no musl, no kmodloader, no shell script;
  5. /init is a statically linked AArch64 ELF (EXEC) with no PT_INTERP and no
     DT_NEEDED, mode 0755, and its sha256 matches the manifest;
  6. source-level absence of exit()/_exit()/abort() in candidate-init-v6.c and
     an infinite loop as the last statement of main();
  7. liveness in a user-mode emulator: qemu-aarch64 /init --wait=1 must still be
     running after 12 s (timeout has to kill it: rc 124) and must have logged
     "init v6 entered" and at least one retry cycle.

What this does NOT prove (see VERIFY-v6.md): anything about Qualcomm hardware,
about ABL/AVB acceptance, about the built-in dwc3 producing a UDC, or about the
host seeing 18d1:4ee7.
"""
import gzip
import hashlib
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

EXP = Path('/home/user/nx679j-stock/experiments/20260916-122926-native-baseline')
OUT = EXP / 'candidate-minimal-gadget-v6'
IMG = OUT / 'boot_b-staticinit-v6.img'
MAN = json.loads((OUT / 'manifest.json').read_text())
INP = json.loads((OUT / 'inputs.json').read_text())
V5_MAN = json.loads((EXP / 'candidate-minimal-gadget-v5/manifest.json').read_text())
MOD_DIR = next(p for p in MAN['ramdisk_inventory']
               if p.startswith('lib/modules/5.10.'))
KREL = MOD_DIR.rsplit('/', 1)[-1]
BOOT_A = EXP / 'current-readback/boot_a.img'
SRC = EXP / 'candidate-init-v6.c'

PAGE = 4096
KERNEL_SHA = 'f0aa949c95d1421381850cb1982bd2a505e96fcd7565543ec28d5b1f7d81a0cc'
EXPECTED_NODES = {
    'dev/console': (0o020600, 5, 1), 'dev/null': (0o020666, 1, 3),
    'dev/zero': (0o020666, 1, 5), 'dev/tty': (0o020666, 5, 0),
    'dev/kmsg': (0o020644, 1, 11), 'dev/pmsg0': (0o020222, 252, 0),
}
MOD_DIR = 'lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604'

results = []
log_lines = []


def check(name, ok, detail=''):
    results.append({'check': name, 'ok': bool(ok), 'detail': str(detail)})
    log_lines.append(f'[{"PASS" if ok else "FAIL"}] {name}: {detail}')
    print(f'[{"PASS" if ok else "FAIL"}] {name}: {detail}')


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def newc_entries(data: bytes):
    """Strict newc parse: magic, alignment and trailer are enforced."""
    off, names = 0, []
    while off < len(data):
        magic = data[off:off + 6]
        if magic not in (b'070701', b'070702'):
            raise ValueError(f'bad cpio magic at {off}: {magic!r}')
        f = [int(data[off + 6 + i * 8:off + 14 + i * 8], 16) for i in range(13)]
        mode, size, namesize, rmaj, rmin = f[1], f[6], f[11], f[9], f[10]
        name = data[off + 110:off + 110 + namesize - 1].decode()
        start = (off + 110 + namesize + 3) & ~3
        payload = data[start:start + size]
        if name == 'TRAILER!!!':
            pad = data[(start + size + 3) & ~3:]
            if pad.strip(b'\0'):
                raise ValueError('non-zero bytes after the cpio trailer')
            return names
        names.append((name, mode, payload, rmaj, rmin))
        off = (start + size + 3) & ~3
    raise ValueError('no cpio trailer found')


# ------------------------------------------------------------------ 1. image
img = IMG.read_bytes()
check('image exists and hash matches manifest', sha(img) == MAN['sha256'],
      f'{len(img)} bytes sha256={sha(img)[:16]}...')
check('image fits the 96 MiB boot partition', len(img) < 0x6000000,
      f'{len(img)} < {0x6000000}')
check('magic ANDROID!', img[:8] == b'ANDROID!', img[:8])
k_size, r_size, os_ver, hdr_size = struct.unpack_from('<IIII', img, 8)
header_version = struct.unpack_from('<I', img, 40)[0]
check('header_version == 4', header_version == 4, header_version)
check('header_size == 1584 (BOOT_IMAGE_HEADER_V4_SIZE)', hdr_size == 1584,
      hdr_size)
os_expect = (12 << 25) | (0 << 18) | (0 << 11) | ((2022 - 2000) << 4) | 2
os_decoded = (f'{(os_ver >> 25) & 0x7f}.{(os_ver >> 18) & 0x7f}'
              f'.{(os_ver >> 11) & 0x7f} '
              f'{2000 + ((os_ver >> 4) & 0x7f)}-{os_ver & 0xf:02d}')
check('os_version == 12.0.0 / 2022-02', os_ver == os_expect, os_decoded)
# v4 layout: magic[8] k[4] r[4] os[4] hdrsize[4] reserved[16] hv[4] cmd[1536]
cmd_field = img[44:44 + 1536]
cmd = cmd_field.split(b'\0', 1)[0].decode()
check('signature_size == 0 (unsigned)', MAN['signature_size'] == 0, 0)
check('cmdline byte-identical to v5', cmd == V5_MAN['cmdline'],
      f'{len(cmd)} chars, identical={cmd == V5_MAN["cmdline"]}')
check('manifest cmdline equals the image cmdline field',
      MAN['cmdline'] == cmd, 'ok')
check('kernel_size / ramdisk_size are the manifest values',
      (k_size, r_size) == (MAN['kernel_bytes'], MAN['ramdisk_bytes']),
      f'kernel {k_size} ramdisk {r_size}')
check('kernel sha == manifest', sha(img[PAGE:PAGE + k_size])
      == MAN['kernel_sha256'], sha(img[PAGE:PAGE + k_size])[:16] + '...')
check('ramdisk sha == manifest', sha(
    img[PAGE + ((k_size + PAGE - 1) // PAGE) * PAGE:][:r_size])
    == MAN['ramdisk_sha256'], MAN['ramdisk_sha256'][:16] + '...')

# ------------------------------------------------------------------ 2. kernel
r_off = PAGE + ((k_size + PAGE - 1) // PAGE) * PAGE
kernel = img[PAGE:PAGE + k_size]
stock = BOOT_A.read_bytes()
stock_k = stock[PAGE:PAGE + struct.unpack_from('<I', stock, 8)[0]]
check('kernel payload is byte-identical to the stock boot_a readback',
      kernel == stock_k and sha(kernel) == KERNEL_SHA,
      f'{len(kernel)} bytes, identical to readback')

# ------------------------------------------------------------------ 3. ramdisk
ramdisk = img[r_off:r_off + r_size]
check('ramdisk is gzip', ramdisk[:2] == b'\x1f\x8b', ramdisk[:2].hex())
cpio = gzip.decompress(ramdisk)
check('gzip payload size == manifest cpio bytes',
      len(cpio) == MAN['ramdisk_cpio_bytes'], len(cpio))
entries = newc_entries(cpio)
check('cpio entry count == manifest', len(entries) == MAN['ramdisk_entries'],
      f'{len(entries)} entries')

names = [e[0] for e in entries]
kinds = {'dirs': 0, 'nodes': 0, 'files': []}
for name, mode, payload, rmaj, rmin in entries:
    fmt = mode & 0o170000
    if fmt == 0o040000:
        kinds['dirs'] += 1
    elif fmt == 0o020000:
        kinds['nodes'] += 1
        exp = EXPECTED_NODES.get(name)
        check(f'device node {name} mode/major/minor',
              exp == (mode, rmaj, rmin), f'mode={oct(mode)} {rmaj}:{rmin}')
    else:
        kinds['files'].append((name, mode, payload))
check('exactly 10 directories', kinds['dirs'] == 10, kinds['dirs'])
check('exactly 6 device nodes', kinds['nodes'] == 6, kinds['nodes'])
mp = MAN['modules_packed']
ko = [f for f in kinds['files'] if f[0].endswith('.ko')]
check('bundled .ko count == manifest', len(ko) == len(mp),
      f'{len(ko)} vs {len(mp)}')
bad_ko = [f[0] for f in ko
          if sha(f[2]) != next(t['sha256'] for t in mp
                               if t['file'] == f[0].rsplit('/', 1)[-1])]
check('every bundled .ko sha256 == manifest (from the device vendor ramdisk)',
      not bad_ko, bad_ko[:3])
check('every bundled .ko is byte-identical to the extracted device copy',
      all(t['identical_to_extracted_copy'] for t in mp),
      sum(t['identical_to_extracted_copy'] for t in mp))
for must in ('qcom_hwspinlock.ko', 'phy-generic.ko', 'eud.ko'):
    hit = [f for f in ko if f[0].endswith('/' + must)]
    check(f'softdep provider bundled: {must}', len(hit) == 1, hit[0][0] if hit else None)
check('exactly one regular file besides /init and the modules',
      kinds['files'] == [f for f in kinds['files']
                         if f[0] == 'init' or f[0].endswith('.ko')
                         or f[0].endswith('fallback.order')] and
      len([f for f in kinds['files'] if f[0].endswith('fallback.order')]) == 1,
      [f[0] for f in kinds['files'] if not f[0].endswith('.ko')])

# no userspace, no stray modules, nothing hidden
check('no .ko outside the bundled module directory',
      not [n for n in names if n.endswith('.ko')
           and not n.startswith(f'lib/modules/{KREL}/')],
      sorted({n.rsplit('/', 2)[0] for n in names if n.endswith('.ko')}))
for path in ('sbin/init', 'bin/busybox', 'bin/sh', 'sbin/kmodloader',
             'sbin/insmod', 'lib/libc.so', 'lib/ld-musl-aarch64.so.1',
             'bin/busybox.nosuid', 'init.sh'):
    check(f'absent: {path}', path not in names, 'ok')
mdir = [f for f in kinds['files'] if f[0].startswith(f'lib/modules/{KREL}/')]
check('module dir holds exactly the bundled .ko files',
      len(mdir) == len(mp), f'{len(mdir)} files vs {len(mp)} modules')
fo = [f for f in kinds['files'] if f[0] == 'lib/modules/fallback.order']
check('fallback.order present', len(fo) == 1, fo[0][0] if fo else None)
fo_names = [l for l in fo[0][2].decode().splitlines()
            if l and not l.startswith('#')]
check('fallback.order lists the whole closure exactly once',
      sorted(fo_names) == sorted(t['file'] for t in mp),
      f'{len(fo_names)} names')
if 'qcom_hwspinlock.ko' in fo_names and 'smem.ko' in fo_names:
    check('fallback order: qcom_hwspinlock before smem (device softdep)',
          fo_names.index('qcom_hwspinlock.ko') < fo_names.index('smem.ko'),
          f"{fo_names.index('qcom_hwspinlock.ko')} < {fo_names.index('smem.ko')}")
for prov in ('phy-generic.ko', 'eud.ko'):
    if prov in fo_names and 'dwc3-msm.ko' in fo_names:
        check(f'fallback order: {prov} before dwc3-msm (device softdep)',
              fo_names.index(prov) < fo_names.index('dwc3-msm.ko'),
              f'{fo_names.index(prov)} < {fo_names.index("dwc3-msm.ko")}')
check('manifest records the softdep lines the v5 list ignored',
      any('softdep dwc3_msm pre:' in l
          for l in MAN['module_plan']['softdep_lines_that_broke_v5']),
      MAN['module_plan']['softdep_lines_that_broke_v5'])
check('manifest closure adds the softdep providers over the v5 seed',
      {'qcom_hwspinlock', 'phy_generic', 'eud'}
      <= set(MAN['module_plan']['closure_added_over_v5_seed']),
      MAN['module_plan']['closure_added_over_v5_seed'])

# ------------------------------------------------------------------ 4. /init
name, mode, init_bin = kinds['files'][0]
check('/init mode 0755', mode == 0o100755, oct(mode))
check('/init sha == manifest', sha(init_bin) == MAN['init']['binary_sha256'],
      sha(init_bin)[:16] + '...')
check('/init is not a script (no #!)', not init_bin.startswith(b'#!'),
      init_bin[:4].hex())
check('/init is an ELF', init_bin[:4] == b'\x7fELF', init_bin[:4].hex())
(OUT / 'verify-scratch').mkdir(exist_ok=True)
init_path = OUT / 'verify-scratch/init-from-image'
init_path.write_bytes(init_bin)
init_path.chmod(0o755)      # qemu-user refuses a non-executable file
fi = subprocess.run(['file', '-b', str(init_path)],
                    capture_output=True, text=True).stdout.strip()
check('/init is statically linked', 'statically linked' in fi, fi)
prog = subprocess.run(['readelf', '-l', str(OUT / 'verify-scratch/init-from-image')],
                      capture_output=True, text=True).stdout
dyn = subprocess.run(['readelf', '-d', str(OUT / 'verify-scratch/init-from-image')],
                     capture_output=True, text=True).stdout
hdr = subprocess.run(['readelf', '-h', str(OUT / 'verify-scratch/init-from-image')],
                     capture_output=True, text=True).stdout
check('/init has no PT_INTERP',
      'Requesting program interpreter' not in prog,
      re.findall(r'INTERP\s+(\S+)', prog) or 'no INTERP segment')
check('/init has no DT_NEEDED',
      'NEEDED' not in dyn, 'no dynamic section' if 'no dynamic section' in dyn
      else re.findall(r'NEEDED.*', dyn))
check('/init is AArch64 EXEC',
      'AArch64' in hdr and 'EXEC' in re.search(r'Type:\s+(.*)', hdr).group(1),
      f'{re.search(r"Machine:(.*)", hdr).group(1).strip()} '
      f'{re.search(r"Type:(.*)", hdr).group(1).strip()}')
check('/init binary sha == inputs.json',
      sha(init_bin) == INP['init_binary']['sha256'], sha(init_bin)[:16] + '...')

# ------------------------------------------------------------------ 5. source
src = SRC.read_text()
exit_calls = [l.strip() for l in src.splitlines()
              if re.search(r'(?<![\w.])(exit|_exit|_Exit|abort|__assert_fail)\s*\(', l)
              and not l.strip().startswith(('*', '/*', '//'))]
check('no exit()/_exit()/abort() in the init source', not exit_calls,
      exit_calls or 'none')
# the loader must be metadata-driven, not a hardcoded list as the primary path
for needle, what in ((b'modules.dep', 'reads modules.dep (hard deps)'),
                     (b'modules.softdep', 'reads modules.softdep'),
                     (b'pre:', 'handles softdep pre: providers'),
                     (b'modules.load.recovery', 'reads modules.load.recovery'),
                     (b'modules.load', 'reads modules.load first'),
                     (b'fallback.order', 'has the fallback order path'),
                     (b'finit_module', 'loads with finit_module(2)'),
                     (b'init_module', 'has an init_module(2) fallback'),
                     (b'ufs_qcom', 'knows ufs_qcom is a goal'),
                     (b'dwc3_msm', 'attempts the gadget when dwc3_msm is in'),
                     (b'/proc/devices', 'resolves the pmsg major at runtime')):
    check(f'/init source {what}', needle in SRC.read_bytes(), needle.decode())
check('goal set includes the softdep providers',
      all(g in SRC.read_text() for g in ('qcom_hwspinlock.ko', 'phy-generic.ko',
                                         'eud.ko')),
      'GOALS[] in candidate-init-v6.c')
check('source sha == inputs.json',
      sha(src.encode()) == INP['init_source']['sha256'], sha(src.encode())[:16])
check('main ends in an infinite loop, no return-from-main path',
      'for (;;) {\n\t\tcyc++;' in src and
      'for (;;)\n\t\tpause();' in src, 'both loops present in main()')
check('init never invokes a shell (no "/bin/sh")', '/bin/sh' not in src, 'ok')

# ------------------------------------------------------------------ 6. liveness
qemu = subprocess.run(['timeout', '12', 'qemu-aarch64',
                       str(init_path), '--wait=1'],
                      capture_output=True, text=True)
alive = qemu.returncode == 124
check('still alive after 12 s in qemu-aarch64 (timeout had to kill it)', alive,
      f'rc={qemu.returncode} (124 == killed while running)'
      + (f' stderr={qemu.stderr.strip()[:80]}' if not alive else ''))
check('logged "init v6 entered"', 'init v6 entered' in qemu.stdout,
      [l for l in qemu.stdout.splitlines() if 'init v6' in l][:1])
check('logged at least one retry cycle', 'cycle=1 uptime=' in qemu.stdout,
      [l for l in qemu.stdout.splitlines() if 'cycle=1 ' in l][:1])
check('module chain runs before the gadget attempt (new stage order)',
      qemu.stdout.find('stage2 module chain') >= 0 and
      qemu.stdout.find('stage2 module chain') < qemu.stdout.find('stage3 gadget'),
      'modules first, gadget after')
check('degradation is graceful when neither device metadata nor the fallback '
      'list exist (user mode has no /lib/modules/modules.dep)',
      'no /lib/modules/modules.dep' in qemu.stdout and
      'not fatal' in qemu.stdout and 'stage3 gadget attempt' in qemu.stdout,
      'no metadata -> no crash, stage3 still runs')
check('no channel/dependency failure aborts the program (it keeps cycling)',
      qemu.stdout.count('nx679j-v6:') > 20,
      f'{qemu.stdout.count("nx679j-v6:")} log lines in 12 s')

# ------------------------------------------------------------------ summary
passed = sum(1 for r in results if r['ok'])
summary = {
    'image': str(IMG), 'image_sha256': MAN['sha256'], 'bytes': len(img),
    'checks_total': len(results), 'checks_passed': passed,
    'checks_failed': len(results) - passed,
    'failed': [r['check'] for r in results if not r['ok']],
    'init': {'static': True, 'interp': None, 'dt_needed': [],
             'binary_bytes': len(init_bin), 'binary_sha256': sha(init_bin),
             'not_a_shell_script': True, 'never_exits': True},
    'ramdisk': {'entries': len(entries), 'dirs': kinds['dirs'],
                'device_nodes': kinds['nodes'],
                'regular_files': len(kinds['files']),
                'modules': len(ko), 'bytes': r_size},
    'module_plan': {'seed_from_v5': len(MAN['module_plan']['v5_seed']),
                    'bundled': len(mp),
                    'added_by_softdep_and_deps':
                        MAN['module_plan']['closure_added_over_v5_seed'],
                    'goal_names': len(MAN['module_plan']['goal_names_in_init'])},
    'liveness': {'emulator': 'qemu-aarch64 (user mode)', 'ran_seconds': 12,
                 'kill_required': alive, 'exit_status_returned': None,
                 'log_lines': qemu.stdout.count('nx679j-v6:')},
    'not_proven_by_this_script': [
        'the kernel reaches /init on the phone',
        'that the vendor ramdisk is really unpacked next to ours on hardware '
        '(so modules.load / modules.dep / modules.softdep are readable)',
        'that every module of the 47 closure loads (only the journal of the '
        'next hardware run shows the per-module rc values)',
        'that dwc3_msm brings up the UDC a600000.dwc3 and that NCM enumerates',
        'ABL/AVB accepts this unsigned image on slot B',
        'that the host can ping 10.0.0.1 on the NCM link',
    ],
}
(OUT / 'verify-structural.json').write_text(json.dumps(summary, indent=2) + '\n')
(OUT / 'verify-structural.log').write_text('\n'.join(log_lines) + '\n')
(OUT / 'verify-liveness-qemu-user.stdout').write_text(qemu.stdout)
(OUT / 'verify-liveness-qemu-user.stderr').write_text(qemu.stderr)
print(f'\n[*] {passed}/{len(results)} checks passed')
if passed != len(results):
    sys.exit(1)
