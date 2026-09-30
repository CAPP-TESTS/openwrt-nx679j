#!/usr/bin/env python3
"""Structural + static-runtime verification of the v5 minimal gadget image.

Read-only (writes only its own report/JSON and a scratch extraction directory).

Checks, all against the bytes on disk:
  1. Android boot header: magic, header_version == 4, declared sizes.
  2. header sizes == manifest values; image sha256 == manifest.
  3. kernel payload sha256 == the stock kernel in current-readback/boot_a.img.
  4. image <= 0x6000000 (96 MiB) partition capacity.
  5. cmdline: panic=10 present, byte-identical to the v4 cmdline.
  6. ramdisk: gzip integrity, newc parses to TRAILER!!!, entry count == manifest.
  7. ramdisk membership: /init, busybox, /bin/sh, kmodloader + /sbin/insmod
     symlink, musl loader + libs, 6 static device nodes, sys/kernel/config,
     and exactly the 40 .ko the init names - no more.
  8. MINIMALITY: no OpenWrt userspace at all (/sbin/init, procd, netifd,
     uhttpd, dropbear, opkg, /etc/config, /www absent), entry count under a
     budget, .ko count == 40, and every .ko byte-identical to the vendor copy.
  9. shared-library closure recomputed from the ramdisk's own ELF headers: every
     NEEDED soname and every interpreter path must exist inside the ramdisk.
 10. the ramdisk's OWN busybox, run under qemu-aarch64 user emulation, provides
     every applet the init calls, and its ash accepts the shipped init with
     `-n` (syntax check).
 11. init v5 vs v4: module list token-for-token identical; the journaling
     helpers, the module loop and the UFS check byte-identical; no
     `exec /sbin/init`, no telnetd, `sleep 15` retry loop present, breadcrumb
     524288-sector lookup and the NCM gadget attributes present, no set -e.
 12. the 40 names resolve against the packed ramdisk exactly as the shell does.
 13. cross-check with unpack_bootimg if installed.
 14. size comparison against v4, so the reader can see whether the size
     hypothesis is still testable.
"""
import gzip
import hashlib
import json
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
from pathlib import Path

EXP = Path('/home/user/nx679j-stock/experiments/20260916-122926-native-baseline')
OUT = EXP / 'candidate-minimal-gadget-v5'
IMG = OUT / 'boot_b-minimal-gadget-v5.img'
MAN = json.loads((OUT / 'manifest.json').read_text())
V4MAN = json.loads((EXP / 'candidate-stockkernel-v4/manifest.json').read_text())
VENDOR_RD = EXP / 'unpacked-current/vendor_boot_a/vendor_ramdisk00'
PAGE = 4096
KERNEL_SHA_EXPECTED = 'f0aa949c95d1421381850cb1982bd2a505e96fcd7565543ec28d5b1f7d81a0cc'
MOD_DIR = 'lib/modules/5.10.66-android12-9-00005-gf6e6376090be-ab8060604'
SCRATCH = OUT / 'verify-scratch'
OPENWRT_FORBIDDEN = ('sbin/init', 'sbin/procd', 'sbin/netifd', 'sbin/logd',
                     'usr/sbin/uhttpd', 'usr/sbin/dropbear', 'usr/sbin/dnsmasq',
                     'bin/opkg', 'lib/libubus.so.20250102', 'etc/config/network',
                     'www/index.html', 'usr/lib/libubus.so')

checks = []


def check(name, ok, detail=''):
    checks.append((name, bool(ok), detail))
    print(f'[{"PASS" if ok else "FAIL"}] {name}{(" :: " + detail) if detail else ""}')


img = IMG.read_bytes()
print(f'image {IMG}')
print(f'      {len(img)} bytes sha256={hashlib.sha256(img).hexdigest()}')

# ---- 1. header -------------------------------------------------------------
check('header magic ANDROID!', img[:8] == b'ANDROID!', repr(img[:8]))
k_size, r_size = struct.unpack_from('<II', img, 8)
os_version, header_size = struct.unpack_from('<II', img, 16)
header_version = struct.unpack_from('<I', img, 40)[0]
print(f'      kernel_size={k_size} ramdisk_size={r_size} '
      f'os_version=0x{os_version:08x} header_size={header_size} '
      f'header_version={header_version}')
check('header_version == 4', header_version == 4, str(header_version))
cmdline = img[44:44 + 1536].split(b'\0')[0].decode()

# ---- 2. manifest agreement -------------------------------------------------
check('kernel_size == manifest', k_size == MAN['kernel_bytes'],
      f'{k_size}/{MAN["kernel_bytes"]}')
check('ramdisk_size == manifest', r_size == MAN['ramdisk_bytes'],
      f'{r_size}/{MAN["ramdisk_bytes"]}')
check('image bytes == manifest', len(img) == MAN['bytes'],
      f'{len(img)}/{MAN["bytes"]}')
check('image sha256 == manifest',
      hashlib.sha256(img).hexdigest() == MAN['sha256'])

# ---- 3. kernel identity ----------------------------------------------------
kernel = img[PAGE:PAGE + k_size]
k_sha = hashlib.sha256(kernel).hexdigest()
check('kernel sha256 == device stock kernel', k_sha == KERNEL_SHA_EXPECTED, k_sha)
check('kernel != trailing zeros', kernel[-4096:] != b'\0' * 4096)
check('kernel identical to the v4 kernel', k_sha == V4MAN['kernel_sha256'])

# ---- 4. size ---------------------------------------------------------------
check('size <= 96 MiB (0x6000000)', len(img) <= 0x6000000,
      f'{len(img)} <= {0x6000000} ({0x6000000 - len(img)} bytes headroom)')

# ---- 5. cmdline ------------------------------------------------------------
check('cmdline carries panic=10', 'panic=10' in cmdline.split())
check('cmdline has exactly one panic= token',
      sum(1 for t in cmdline.split() if t.startswith('panic=')) == 1,
      ','.join(t for t in cmdline.split() if t.startswith('panic=')))
check('cmdline byte-identical to v4', cmdline == V4MAN['cmdline'])
check('header cmdline == manifest cmdline', cmdline == MAN['cmdline'])
check('cmdline keeps "rootwait ro init=/init"',
      cmdline.endswith('rootwait ro init=/init panic=10'), cmdline[-40:])

# ---- 6. ramdisk ------------------------------------------------------------
r_off = PAGE + ((k_size + PAGE - 1) // PAGE) * PAGE
ramdisk = img[r_off:r_off + r_size]
r_sha = hashlib.sha256(ramdisk).hexdigest()
check('ramdisk sha256 == manifest', r_sha == MAN['ramdisk_sha256'], r_sha)
gz = subprocess.run(['gzip', '-t'], input=ramdisk, capture_output=True)
check('gzip -t accepts ramdisk', gz.returncode == 0, gz.stderr.decode().strip())

raw = gzip.decompress(ramdisk)
off, entries, nodes, ko_payload = 0, {}, 0, {}
strict = True
while off < len(raw):
    if raw[off:off + 6] not in (b'070701', b'070702'):
        strict = False
        break
    f = [int(raw[off + 6 + i * 8:off + 14 + i * 8], 16) for i in range(13)]
    mode, size, namesize = f[1], f[6], f[11]
    rmaj, rmin = f[9], f[10]
    name = raw[off + 110:off + 110 + namesize - 1].decode()
    start = (off + 110 + namesize + 3) & ~3
    if name == 'TRAILER!!!':
        break
    entries[name] = (mode, raw[start:start + size], rmaj, rmin)
    if mode & 0o170000 == 0o020000:
        nodes += 1
    if name.endswith('.ko'):
        ko_payload[name] = raw[start:start + size]
    off = (start + size + 3) & ~3
check('newc parses to TRAILER!!!', strict and bool(entries))
check('entry count == manifest', len(entries) == MAN['ramdisk_entries'],
      f'{len(entries)} vs {MAN["ramdisk_entries"]}')
check('compressed ramdisk == manifest size', len(ramdisk) == MAN['ramdisk_bytes'])

# ---- 7. membership ---------------------------------------------------------
for need in ('init', 'bin/busybox', 'bin/sh', 'bin/mount',
             'sbin/kmodloader', 'sbin/insmod', 'lib/libc.so', 'lib/libgcc_s.so.1',
             'lib/libubox.so.20240329', 'lib/ld-musl-aarch64.so.1',
             'dev/console', 'dev/kmsg', 'dev/pmsg0', 'dev/null', 'dev/zero',
             'dev/tty', 'proc', 'sys', 'sys/kernel/config', 'sys/fs/pstore',
             'tmp', 'lib/modules', MOD_DIR):
    check(f'ramdisk contains {need}', need in entries)
check('6 static device nodes', nodes == 6, str(nodes))
check('device node dev/kmsg is 1:11', entries.get('dev/kmsg', (0,))[2:] == (1, 11))
check('device node dev/pmsg0 is 252:0',
      entries.get('dev/pmsg0', (0,))[2:] == (252, 0))
check('/bin/sh is a symlink (shebang target)', entries.get('bin/sh', (0,))[0] == 0o120777,
      oct(entries.get('bin/sh', (0,))[0]))
check('/bin/sh -> busybox', entries.get('bin/sh', (0, b''))[1] == b'busybox')
check('/sbin/insmod -> kmodloader',
      entries.get('sbin/insmod', (0, b''))[1] == b'kmodloader')
check('/lib/ld-musl-aarch64.so.1 -> libc.so',
      entries.get('lib/ld-musl-aarch64.so.1', (0, b''))[1] == b'libc.so')
check('init is executable script', entries.get('init', (0,))[0] == 0o100755,
      oct(entries.get('init', (0,))[0]))
init_bytes = entries['init'][1]
init_sha = hashlib.sha256(init_bytes).hexdigest()
check('packed init sha256 == on-disk init sha256',
      init_sha == hashlib.sha256((EXP / 'candidate-init-v5.sh').read_bytes()).hexdigest(),
      init_sha)
check('packed init == manifest init sha256', init_sha == MAN['init']['sha256'])
check('init starts with #!/bin/sh', init_bytes.startswith(b'#!/bin/sh\n'))

# ---- 8. minimality ---------------------------------------------------------
founds = [n for n in OPENWRT_FORBIDDEN if n in entries]
check('no OpenWrt userspace in the ramdisk', not founds,
      f'forbidden present: {founds}' if founds else
      f'{len(OPENWRT_FORBIDDEN)} forbidden paths absent')
ko_names = sorted(n.rsplit('/', 1)[-1] for n in ko_payload)
check('exactly 40 .ko packed', len(ko_names) == 40, str(len(ko_names)))
check('all .ko under one versioned modules dir',
      all(n.startswith(MOD_DIR + '/') for n in ko_payload), MOD_DIR)
check('ramdisk entry count <= 100 (minimality budget)', len(entries) <= 100,
      f'{len(entries)} entries')
check('ramdisk <= 2 MB compressed', len(ramdisk) <= 2 * 1024 * 1024,
      f'{len(ramdisk)} bytes')

# every packed .ko byte-identical to the vendor ramdisk copy
def vendor_kos():
    data = subprocess.run(['lz4', '-dc', str(VENDOR_RD)], check=True,
                          stdout=subprocess.PIPE).stdout
    o, out = 0, {}
    while o < len(data):
        if data[o:o + 6] not in (b'070701', b'070702'):
            break
        f = [int(data[o + 6 + i * 8:o + 14 + i * 8], 16) for i in range(13)]
        size, namesize = f[6], f[11]
        name = data[o + 110:o + 110 + namesize - 1].decode()
        st = (o + 110 + namesize + 3) & ~3
        if name == 'TRAILER!!!':
            break
        if name.endswith('.ko'):
            out[name.rsplit('/', 1)[-1]] = data[st:st + size]
        o = (st + size + 3) & ~3
    return out


vendor = vendor_kos()
diff = [k for k in ko_names if vendor.get(k) != ko_payload[f'{MOD_DIR}/{k}']]
check('all 40 .ko byte-identical to the vendor ramdisk copies', not diff,
      f'differing: {diff}' if diff else f'40/40 identical ({len(vendor)} vendor .ko available)')

# ---- 9. library closure from the ramdisk bytes -----------------------------
if SCRATCH.exists():
    shutil.rmtree(SCRATCH)
SCRATCH.mkdir(parents=True)


def extract(entries, dest: Path):
    made = {'dir': 0, 'file': 0, 'link': 0, 'node': 0}
    for name, (mode, payload, rmaj, rmin) in entries.items():
        p = dest / name
        kind = mode & 0o170000
        if kind == 0o040000:
            p.mkdir(parents=True, exist_ok=True)
            made['dir'] += 1
        elif kind == 0o100000:
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(payload)
            os.chmod(p, mode & 0o7777)
            made['file'] += 1
        elif kind == 0o120000:
            p.parent.mkdir(parents=True, exist_ok=True)
            if p.is_symlink() or p.exists():
                p.unlink()
            os.symlink(payload.decode(), p)
            made['link'] += 1
        elif kind == 0o020000:
            made['node'] += 1          # needs root: recorded, not created
    return made


made = extract(entries, SCRATCH)
print(f'      extracted to {SCRATCH}: {made}')


def elf_info(path: Path):
    d = subprocess.run(['readelf', '-d', str(path)], capture_output=True, text=True).stdout
    l = subprocess.run(['readelf', '-l', str(path)], capture_output=True, text=True).stdout
    if not l:
        return None
    needed = sorted(set(re.findall(r'\(NEEDED\)\s+Shared library: \[([^\]]+)\]', d)))
    m = re.search(r'Requesting program interpreter: (\S+)\]', l)
    return {'needed': needed, 'interp': m.group(1) if m else None}


present_libs = {n for n in entries if re.match(r'lib/[^/]+\.so', n)}
print(f'      libraries in the ramdisk: {sorted(present_libs)}')
missing_needed, interp_bad, elfs = [], [], []
for name in sorted(entries):
    kind = entries[name][0] & 0o170000
    if kind not in (0o100000, 0o120000):
        continue
    if name.endswith('.ko'):
        continue        # kernel modules: ELF too, but no user-space NEEDED list
    p = SCRATCH / name
    if kind == 0o120000:
        p = (SCRATCH / name).resolve()
    if not p.exists():
        continue
    info = elf_info(p)
    if not info:
        continue
    elfs.append(name)
    for need in info['needed']:
        if f'lib/{need}' not in present_libs:
            missing_needed.append((name, need))
    if info['interp']:
        it = info['interp'].lstrip('/')
        if it not in entries or (SCRATCH / it).resolve().name not in {
                Path(x).name for x in present_libs}:
            interp_bad.append((name, info['interp']))
check('every dynamic user-space ELF in the ramdisk is listed', len(elfs) >= 5,
      f'{len(elfs)} ELFs: {elfs}')
check('every NEEDED soname resolved inside the ramdisk', not missing_needed,
      f'unresolved: {missing_needed}' if missing_needed
      else f'{len(elfs)} ELFs checked')
check('every interpreter path exists inside the ramdisk', not interp_bad,
      f'bad: {interp_bad}' if interp_bad else 'all ELFs use /lib/ld-musl-aarch64.so.1')

# ---- 10. the ramdisk's own busybox under qemu-aarch64 ----------------------
QEMU_USER = shutil.which('qemu-aarch64')
print(f'      qemu-aarch64: {QEMU_USER or "not installed"}')
init_text = init_bytes.decode()
called = set(re.findall(r'\$BB ([a-z0-9]+)', init_text))
called |= {'sh', 'uname', 'cat', 'ls', 'wc', 'basename', 'which', 'rm', 'dd',
           'head', 'tail', 'ip'}          # bare PATH commands in the init
called -= {'insmod'}      # sbin/insmod (kmodloader) is tested on its own below;
# the "$BB insmod" in the init is the unreachable fallback branch
applet_results, applet_missing = {}, []
if QEMU_USER:
    for a in sorted(called):
        r = subprocess.run([QEMU_USER, '-L', str(SCRATCH), str(SCRATCH / 'bin' / a),
                            '--help'], capture_output=True, text=True, timeout=60)
        blob = r.stdout + r.stderr
        ok = 'BusyBox v' in blob
        applet_results[a] = {'rc': r.returncode, 'busybox_banner': ok}
        if not ok:
            applet_missing.append((a, r.returncode, blob.strip()[:80]))
check('the ramdisk busybox serves every applet the init calls (qemu-user)',
      QEMU_USER is not None and not applet_missing,
      f'{len(applet_results)} applets run under qemu-aarch64; problems: {applet_missing}'
      if QEMU_USER else 'qemu-aarch64 not installed')
syntax = None
if QEMU_USER:
    s = subprocess.run([QEMU_USER, '-L', str(SCRATCH), str(SCRATCH / 'bin' / 'sh'),
                        '-n', str(EXP / 'candidate-init-v5.sh')],
                       capture_output=True, text=True, timeout=60)
    syntax = {'rc': s.returncode, 'stderr': s.stderr.strip()[:200],
              'stdout': s.stdout.strip()[:200]}
check('the ramdisk sh accepts the shipped init with -n (syntax)',
      syntax is not None and syntax['rc'] == 0, json.dumps(syntax))
if QEMU_USER:
    s = subprocess.run([QEMU_USER, '-L', str(SCRATCH), str(SCRATCH / 'sbin' / 'insmod'),
                        '/tmp/v5-verify-nonexistent.ko'],
                       capture_output=True, text=True, timeout=60)
    out = (s.stdout + s.stderr).strip()
    check('the ramdisk /sbin/insmod runs as the kmodloader insmod applet',
          'Failed to find' in out and s.returncode == 255,
          f'rc={s.returncode} out={out[:80]}')
    print('      (the "$BB insmod" branch in the init is dead code: '
          '/sbin/insmod is present)')

# every symlink in the ramdisk resolves to a packed target
broken = []
for name, (mode, payload, rmaj, rmin) in entries.items():
    if mode & 0o170000 != 0o120000:
        continue
    tgt = payload.decode()
    full = f'lib/{tgt}' if name.startswith('lib/') else f'{name.rsplit("/", 1)[0]}/{tgt}'
    if full not in entries:
        broken.append((name, tgt, full))
check('every symlink in the ramdisk resolves to a packed entry', not broken,
      f'broken: {broken}' if broken else 'all symlinks resolve')

# ---- 11. init v5 vs v4 -----------------------------------------------------
i4 = (EXP / 'candidate-init-v4.sh').read_text()
i5 = (EXP / 'candidate-init-v5.sh').read_text()


def seg(text, first_re, last_re):
    """Slice of text between the line matching first_re and the line matching
    last_re (exclusive) - no dash counting, no exact-marker guessing."""
    lines = text.splitlines(keepends=True)
    a = next(i for i, l in enumerate(lines) if re.search(first_re, l))
    b = next(i for i, l in enumerate(lines) if i > a and re.search(last_re, l))
    return ''.join(lines[a:b])


def modblk(text):
    return seg(text, r'^for ko in ', r'^done\s*$')


def strip_comments(text):
    return '\n'.join(l for l in text.splitlines()
                     if l.strip() and not l.lstrip().startswith('#'))


v4_helpers = seg(i4, r'^PATH=', r'stage 0\s*$')
v5_helpers = seg(i5, r'^PATH=', r'stage 0\s*$')
check('journaling helper code (log/find_rawdump/flush) identical to v4',
      strip_comments(v4_helpers) == strip_comments(v5_helpers)
      and len(v5_helpers) > 1000,
      f'{len(v5_helpers)} bytes of text, '
      f'{len(strip_comments(v5_helpers))} bytes of code, '
      f'code identical={strip_comments(v4_helpers) == strip_comments(v5_helpers)}')
check('module list + loop byte-identical to v4', modblk(i4) == modblk(i5),
      f'{len(modblk(i5))} bytes, identical={modblk(i4) == modblk(i5)}')
v4_ufs = seg(i4, r'UFS check\s*$', r'^find_rawdump\s*$')
v5_ufs = seg(i5, r'UFS check\s*$', r'^find_rawdump\s*$')
check('UFS check block byte-identical to v4', v4_ufs == v5_ufs,
      f'{len(v5_ufs)} bytes, identical={v4_ufs == v5_ufs}')


def modlist(text):
    blk = text.split('for ko in ', 1)[1].split('\ndo\n', 1)[0]
    return blk.replace('\\', ' ').split()


check('module list identical token-for-token to v4',
      modlist(i4) == modlist(i5), f'{len(modlist(i5))} names')
v5_code = '\n'.join(l for l in i5.splitlines() if not l.lstrip().startswith('#'))
for what, needle, want in (
        ('no exec /sbin/init handover', 'exec /sbin/init', False),
        ('no set -e (never aborts)', 'set -e', False),
        ('the 15 s retry loop', 'sleep 15', True),
        ('the infinite hold loop', 'while true; do', True),
        ('the breadcrumb journal on the 524288-sector partition', '524288', True),
        ('the rawdump flush()', 'flush()', True),
        ('the NCM configfs gadget', 'functions/ncm.usb0', True),
        ('idVendor 0x18d1', '0x18d1', True),
        ('idProduct 0x4ee7', '0x4ee7', True),
        ('MaxPower 250', 'MaxPower', True),
        ('the usb0 address 10.0.0.1/24', '10.0.0.1/24', True),
        ('the dummy_udc filter', 'dummy_udc', True),
        ('the 30 s first UDC wait', 'gadget_try 30', True),
        ('the per-cycle gadget retry', 'gadget_try 3', True),
        ('/sbin/insmod as the live insmod path', '[ -x /sbin/insmod ]', True),
        ('INSMOD=/sbin/insmod as the resolved tool', 'INSMOD=/sbin/insmod', True)):
    body = i5 if want else v5_code      # negative checks look at code, not comments
    check(f'init v5 has {what}', (needle in body) == want, needle)
v5_code = '\n'.join(l for l in i5.splitlines() if not l.lstrip().startswith('#'))
check('init v5 has no telnetd in code (comments may mention it)',
      'telnetd' not in v5_code, 'telnetd absent from code')
check('init v5 never execs anything (code lines only)',
      'exec ' not in v5_code and 'exec\t' not in v5_code,
      'no exec statement in v5 code')
check('v5 mixed-spelling table identical to v4',
      re.findall(r'nvmem_qcom_spmi_sdam\)\s*cand="\$M/([^"]+)"', i4) ==
      re.findall(r'nvmem_qcom_spmi_sdam\)\s*cand="\$M/([^"]+)"', i5),
      str(re.findall(r'nvmem_qcom_spmi_sdam\)\s*cand="\$M/([^"]+)"', i5)))

# ---- 12. module-name resolution against the packed ramdisk -----------------
alias = {}
for line in i5.splitlines():
    m = re.match(r'\s*([A-Za-z0-9_]+)\)\s*cand="\$M/([A-Za-z0-9_.-]+)\.ko"', line)
    if m:
        alias[m.group(1)] = m.group(2) + '.ko'
names = modlist(i5)
packed = set(ko_names)
tbl = []
for nm in names:
    hit = None
    for how, cand in (('literal', f'{nm}.ko'),
                      ('all-hyphens', nm.replace('_', '-') + '.ko'),
                      ('all-underscores', nm.replace('-', '_') + '.ko'),
                      ('mixed-spelling alias', alias.get(nm, '\x00'))):
        if cand in packed:
            hit = (cand, how)
            break
    tbl.append((nm, hit[0] if hit else None, hit[1] if hit else 'NOT RESOLVED'))
unres = [t for t in tbl if t[1] is None]
for nm, cand, how in tbl:
    print(f'      {nm:30s} -> {cand or "-":34s} [{how}]')
check('all init v5 module names resolve in the ramdisk', not unres,
      f'{len(tbl) - len(unres)}/{len(tbl)} resolved; unresolved: {[u[0] for u in unres]}')
check('no module in the ramdisk is unused by the init',
      packed == {t[1] for t in tbl if t[1]},
      f'packed={len(packed)} used={len({t[1] for t in tbl if t[1]})}')
check('exactly one name needs the documented mixed-spelling alias',
      len([t for t in tbl if t[2] == 'mixed-spelling alias']) == 1,
      str([(t[0], t[1]) for t in tbl if t[2] == 'mixed-spelling alias']))
json.dump([{'name': n, 'file': c, 'how': h} for n, c, h in tbl],
          (OUT / 'module-resolution.json').open('w'), indent=2)

# ---- 13. independent tooling ----------------------------------------------
if shutil.which('unpack_bootimg'):
    ub = subprocess.run(['unpack_bootimg', '--boot_img', str(IMG), '--out', '/tmp/v5-unpack'],
                        capture_output=True, text=True)
    line = ' | '.join(l for l in ub.stdout.strip().splitlines() if l.strip())
    print(f'      unpack_bootimg: {line or ub.stderr.strip()}')
    if ub.returncode == 0:
        check('unpack_bootimg kernel sha256 matches',
              hashlib.sha256(Path('/tmp/v5-unpack/kernel').read_bytes()).hexdigest() == k_sha)
        check('unpack_bootimg ramdisk sha256 matches',
              hashlib.sha256(Path('/tmp/v5-unpack/ramdisk').read_bytes()).hexdigest() == r_sha)
else:
    print('      unpack_bootimg: not installed')

# ---- 14. size comparison ---------------------------------------------------
size_cmp = {
    'v4_ramdisk_bytes': V4MAN['ramdisk_bytes'],
    'v5_ramdisk_bytes': r_size,
    'v5_over_v4_ramdisk_pct': round(100 * r_size / V4MAN['ramdisk_bytes'], 1),
    'v4_image_bytes': V4MAN['bytes'],
    'v5_image_bytes': len(img),
    'v5_over_v4_image_pct': round(100 * len(img) / V4MAN['bytes'], 1),
    'v4_ramdisk_entries': V4MAN['ramdisk_entries'],
    'v5_ramdisk_entries': len(entries),
    'android_reference_ramdisk_bytes': 2200983,
    'v5_over_android_reference_pct': round(100 * r_size / 2200983, 1),
    'v4_modules': 327, 'v5_modules': len(ko_names),
}
print('      size comparison: ' + json.dumps(size_cmp))
check('ramdisk is far below v4 (size hypothesis still testable)',
      r_size < V4MAN['ramdisk_bytes'] / 2,
      f'{r_size} vs {V4MAN["ramdisk_bytes"]} ({size_cmp["v5_over_v4_ramdisk_pct"]}%)')
check('ramdisk is in the same order of magnitude as the Android reference',
      r_size <= 2 * 2200983,
      f'{r_size} vs Android boot_a ramdisk 2200983 bytes '
      f'({size_cmp["v5_over_android_reference_pct"]}%)')

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
    'ramdisk_cpio_bytes': len(raw),
    'ramdisk_entries': len(entries),
    'ramdisk_modules': len(ko_names),
    'checks_total': len(checks),
    'checks_failed': [c[0] for c in failed],
    'cmdline_bytes': len(cmdline),
    'size_comparison': size_cmp,
    'extraction': made,
    'elfs_checked': elfs,
    'applets_checked': applet_results,
    'init_syntax_check': syntax,
}
(OUT / 'verify-structural.json').write_text(json.dumps(summary, indent=2) + '\n')
print()
print(f'{len(checks) - len(failed)}/{len(checks)} checks passed')
if failed:
    print('FAILED: ' + ', '.join(c[0] for c in failed))
print(json.dumps({k: v for k, v in summary.items()
                  if k not in ('extraction', 'applets_checked')}, indent=2))
sys.exit(1 if failed else 0)
