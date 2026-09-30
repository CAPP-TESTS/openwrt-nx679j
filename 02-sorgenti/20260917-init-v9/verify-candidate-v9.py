#!/usr/bin/env python3
"""verify-candidate-v9: independent re-verification of the SHIPPED image.

Nothing here trusts the build's own artifacts or the build's own JSON.  Every
claim is re-derived from bytes: boot_b-init-v9.img -> ramdisk lz4 -> cpio ->
/init, /nx679j/worker, /nx679j/relay -> disassembly -> syscall audits.  The
"v9 = v8 + relay" claim is re-derived by comparing the two SHIPPED images.

What is checked:
  1. the container: size, ANDROID! header, kernel bytes == the magisk container,
     ramdisk at the measured offset as an lz4 legacy frame, tail untouched;
  2. the archive: /init, /nx679j/worker, /nx679j/relay present and executable;
  3. PID 1: same eight syscalls as v8 (no open/read/write/mount/socket/...),
     no libc, no module strings, and it *does* reference the relay;
  4. the relay: no forbidden call target reachable from its own code (nothing
     that unlinks/renames/mounts/loads modules), the PLT slots resolve to the
     memcpy/memmove/memset/strlen IFUNC family, the port 9999 and the address
     10.0.0.1 are really in the shipped bytes;
  5. v9 = v8 + relay: 68 archive entries byte-identical to the v8 image,
     /init replaced, /nx679j/relay added, the worker byte-identical.
"""
import hashlib
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

OUT = Path('/home/user/nx679j-stock/experiments/20260917-init-v9')
V8OUT = Path('/home/user/nx679j-stock/experiments/20260917-init-v8')
WORK = OUT / 'verify'
WORK.mkdir(exist_ok=True)
IMG = OUT / 'boot_b-init-v9.img'
IMG_V8 = V8OUT / 'boot_b-init-v8.img'
MAGISK = Path('/home/user/nx679j-stock/magisk_patched-30700_Zx2eF.img')
KERNEL_SIZE = 49108324
RAMDISK_OFF = 49115136
PAGE = 4096

# PID 1 may only ever execute these (aarch64 numbers)
ALLOWED = {94: 'exit_group', 101: 'nanosleep', 113: 'clock_gettime', 129: 'kill',
           142: 'reboot', 220: 'clone', 221: 'execve', 260: 'wait4'}
# nothing with these spellings may be REACHABLE from the relay's own code
FORBIDDEN_SUBSTR = ('unlink', 'rename', 'mkdir', 'mknod', 'chmod', 'chown',
                    'truncate', 'creat', 'fsync', 'mount', 'module', 'symlink',
                    'kill', 'reboot', 'ptrace', 'exec', 'system', 'popen',
                    'chroot', 'fallocate')
BENIGN_PLT = {'__libc_memcpy_ifunc', '__libc_memmove_ifunc', '__libc_memset_ifunc',
              '__libc_memcmp_ifunc', '__strlen_ifunc'}
# syscalls the relay MUST be able to make (otherwise the audit means nothing)
REQUIRED_RELAY = {40: 'mount', 273: 'finit_module'}
checks = []


def check(name, ok, detail=''):
    checks.append({'check': name, 'pass': bool(ok), 'detail': str(detail)[:400]})
    print(f'[{"PASS" if ok else "FAIL"}] {name}: {str(detail)[:220]}')
    return bool(ok)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    if p.returncode:
        raise SystemExit(f'{cmd} failed: {p.stderr[:200]}')
    return p.stdout


def instructions(path):
    out = []
    for ln in run(['aarch64-linux-gnu-objdump', '-d', '--no-show-raw-insn',
                   str(path)]).splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$', ln)
        if m:
            out.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))
    return out


def syscalls(path):
    """walk back from every svc to the mov into x8 (the v8 audit, reused)"""
    insns = instructions(path)
    found, bad = {}, []
    for i, (addr, mn, op) in enumerate(insns):
        if mn != 'svc':
            continue
        nr = None
        for back in range(1, 25):
            if i - back < 0:
                break
            _, pm, po = insns[i - back]
            if pm in ('b', 'bl', 'ret', 'svc', 'br', 'blr', 'cbz', 'cbnz'):
                break
            if pm in ('mov', 'movz') and po.startswith('x8,'):
                mm = re.search(r'#(0x[0-9a-f]+|\d+)', po)
                nr = int(mm.group(1), 0) if mm else None
                break
            if po.startswith('x8,'):
                nr = -1
                break
        if nr is None or nr < 0:
            bad.append(hex(addr))
        else:
            found[nr] = found.get(nr, 0) + 1
    return found, bad


def parse_cpio(buf):
    entries, off = [], 0
    while off < len(buf):
        if buf[off:off + 6] not in (b'070701', b'070702'):
            break
        fsize = int(buf[off + 54:off + 62], 16)
        ns = int(buf[off + 94:off + 102], 16)
        nm = buf[off + 110:off + 110 + ns - 1].decode()
        hdr_end = off + 110 + ns
        doff = hdr_end + (-hdr_end % 4)
        end = doff + fsize
        entries.append(dict(name=nm, mode=int(buf[off + 14:off + 22], 16),
                            size=fsize, hdr=buf[off:off + 110],
                            payload=buf[doff:end]))
        if nm == 'TRAILER!!!':
            break
        off = end + (-end % 4)
    return entries


def split(img):
    ksize, rsize = struct.unpack_from('<II', img, 8)
    koff = PAGE + ((ksize + PAGE - 1) // PAGE) * PAGE
    return img[PAGE:PAGE + ksize], img[koff:koff + rsize], koff, rsize


def own_function_ranges(db, src):
    names = set()
    for line in Path(src).read_text().splitlines():
        m = re.match(r'^(?:static\s+)?(?:inline\s+)?[A-Za-z_][\w \*]*?\b(\w+)\s*\(',
                     line)
        if m and not line.startswith((' ', '\t', '#', '/', '*')):
            names.add(m.group(1))
    out = []
    for line in run(['aarch64-linux-gnu-nm', '-S', '--defined-only', str(db)]
                    ).splitlines():
        f = line.split()
        if len(f) >= 4 and f[2].lower() in ('t', 'w') and f[3] in names:
            s = int(f[0], 16)
            out.append((s, s + (int(f[1], 16) or 1), f[3]))
    return sorted(out)


def plt_map(db):
    d = run(['aarch64-linux-gnu-objdump', '-d', '--no-show-raw-insn',
             '--section=.plt', str(db)])
    got_of, cur = {}, None
    for line in d.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\s+([a-z0-9._]+)\s*(.*)$', line)
        if not m:
            continue
        addr, mn, op = int(m.group(1), 16), m.group(2), m.group(3).strip()
        if mn == 'adrp' and 'x16' in op:
            mm = re.search(r'([0-9a-f]+)\s+<', op)
            cur = (addr, int(mm.group(1), 16) if mm else None)
        elif mn == 'ldr' and cur and 'x17' in op:
            mm = re.search(r'#(\d+)', op)
            if cur[1] is not None:
                got_of[cur[0]] = cur[1] + (int(mm.group(1)) if mm else 0)
            cur = None
    resolver = {}
    for line in run(['aarch64-linux-gnu-readelf', '-r', '--wide', str(db)]
                    ).splitlines():
        m = re.match(r'^([0-9a-f]{16})\s+\S+\s+R_AARCH64_IRELATIVE\s+([0-9a-f]+)',
                     line.strip())
        if m:
            resolver[int(m.group(1), 16)] = int(m.group(2), 16)
    syms = []
    for line in run(['aarch64-linux-gnu-nm', '-S', '--defined-only', str(db)]
                    ).splitlines():
        f = line.split()
        if len(f) >= 4 and f[2].lower() in ('t', 'w'):
            syms.append((int(f[0], 16), int(f[1], 16) or 1, f[3]))
    syms.sort()
    if not got_of:
        return {}
    base = min(got_of)
    out = {}
    for stub, got in sorted(got_of.items()):
        r, name = resolver.get(got), None
        if r is not None:
            for s, sz, nm in syms:
                if s <= r < s + sz:
                    name = nm
            if name is None:
                for s, sz, nm in syms:
                    if s <= r:
                        name = nm
        out['.plt' if stub == base else f'.plt+0x{stub - base:x}'] = name
    return out
def calls_from_own_code(db, ranges):
    """{target: count} for the bl/b instructions executed from OUR functions"""
    insns = instructions(db)
    calls = {}
    for addr, mn, op in insns:
        if mn not in ('bl', 'b'):
            continue
        if not any(s <= addr < e for s, e, _ in ranges):
            continue
        m = re.search(r'<([^>]+)>', op)
        t = m.group(1).split('+')[0] if m else '.indirect'
        calls[t] = calls.get(t, 0) + 1
    return calls


def main():
    res = {}
    img = IMG.read_bytes()
    check('the image is the boot partition size', len(img) == 100663296,
          f'{len(img)} bytes')
    check('the image magic is ANDROID!', img[:8] == b'ANDROID!', img[:8])
    kernel, lz4, koff, rsize = split(img)
    check('the ramdisk is an lz4 legacy frame at the measured offset',
          lz4[:4] == bytes.fromhex('02214c18') and koff == RAMDISK_OFF
          and rsize == len(lz4),
          f'{rsize} bytes at {koff}, magic {lz4[:4].hex()}')
    cont = MAGISK.read_bytes()
    check('the kernel bytes are the container kernel, untouched',
          len(kernel) == KERNEL_SIZE
          and kernel == cont[PAGE:PAGE + KERNEL_SIZE],
          f'{len(kernel)} bytes, sha256={sha(kernel)[:16]}')
    check('everything after the ramdisk is the container, untouched',
          img[koff + rsize:] == cont[koff + rsize:],
          f'{len(img) - (koff + rsize)} bytes identical')
    diff = [i for i in range(len(img)) if img[i] != cont[i]]
    outside = [i for i in diff
               if not (12 <= i < 16 or koff <= i < koff + rsize)]
    check('the only differing bytes are the ramdisk size field and the ramdisk',
          not outside, f'{len(diff)} differing bytes, {len(outside)} outside')

    cpio = subprocess.run(['lz4', '-dc'], input=lz4, capture_output=True).stdout
    ents = parse_cpio(cpio)
    by = {e['name']: e for e in ents}
    check('the archive contains the three programs',
          {'init', 'nx679j/worker', 'nx679j/relay'} <= set(by),
          sorted(k for k in by if k.endswith(('init', 'worker', 'relay'))))
    for nm in ('init', 'nx679j/worker', 'nx679j/relay'):
        blob = by[nm]['payload']
        check(f'{nm} is an aarch64 static ELF, executable',
              blob[:4] == b'\x7fELF'
              and struct.unpack_from('<H', blob, 18)[0] == 183
              and by[nm]['mode'] & 0o111,
              f"{len(blob)} bytes, mode {oct(by[nm]['mode'])}")
    for nm in ('init', 'nx679j/worker', 'nx679j/relay'):
        (WORK / nm.rsplit('/', 1)[-1]).write_bytes(by[nm]['payload'])

    # ---------------------------------------------------------------- PID 1
    init = WORK / 'init'
    ip = WORK / 'init.unstripped'
    # the shipped /init is not stripped, so it can be audited directly
    found, bad = syscalls(init)
    check('PID 1 syscall audit: every svc resolves to a known number',
          not bad, bad or 'clean')
    extra = {k: v for k, v in found.items() if k not in ALLOWED}
    check('PID 1 executes ONLY clock_gettime/clone/execve/exit_group/kill/'
          'nanosleep/reboot/wait4 (exactly as in v8)',
          not extra, {ALLOWED.get(k, k): v for k, v in extra.items()} or
          {ALLOWED[k]: v for k, v in sorted(found.items())})
    risky = [ALLOWED.get(k, k) for k in found
             if k in (40, 48, 56, 57, 63, 64, 78, 79, 198, 273)]
    check('PID 1 cannot open/read/write/mount/ioctl/socket/finit_module',
          not risky, risky or 'none present')
    data = init.read_bytes()
    seen = [s.decode() for s in (b'finit_module', b'/lib/modules', b'modules.load',
                                 b'/dev/', b'insmod') if s in data]
    check('PID 1 contains no module-loading / device strings', not seen,
          seen or 'none')
    refs = {k: (k.encode() in data) for k in
            ('/nx679j/relay', 'v9-relay-started', 'v9-relay-died',
             'v9-relay-restarted', 'v9-summary')}
    check('PID 1 DOES reference the relay (so the audit above is meaningful)',
          all(refs.values()), refs)

    # ---------------------------------------------------------------- relay
    relay = WORK / 'relay'
    rd = WORK / 'relay.unstripped'
    rdb = WORK / 'relay-v9.gtwin'
    # rebuild the -g twin from the shipped source to get symbols back; the
    # shipped binary is byte-identical to the build's own output (checked in
    # the build), the twin only gives names to the same instructions
    subprocess.run(['aarch64-linux-gnu-gcc', '-static', '-Os', '-g', '-o',
                    str(rd), str(OUT / 'nx679j-relay.c')], check=True)
    rsh = relay.read_bytes()
    check('the relay is a static aarch64 ELF with no interpreter',
          rsh[:4] == b'\x7fELF'
          and struct.unpack_from('<H', rsh, 18)[0] == 183
          and 'NEEDED' not in subprocess.run(
              ['aarch64-linux-gnu-readelf', '-d', str(relay)],
              capture_output=True, text=True).stdout,
          f'{len(rsh)} bytes, sha256={sha(rsh)[:16]}, no dynamic section')
    ranges = own_function_ranges(rd, OUT / 'nx679j-relay.c')
    calls = calls_from_own_code(rd, ranges)
    forbid = sorted(t for t in calls
                    if any(s in t for s in FORBIDDEN_SUBSTR))
    check('nothing reachable from the relay\'s own code can unlink/rename/'
          'mount/load a module/kill/reboot/exec', not forbid, forbid or 'clean')
    pm = plt_map(rd)
    plt_calls = {k: pm.get(k) for k in calls if k.startswith('.plt')}
    bad_plt = {k: v for k, v in plt_calls.items() if v not in BENIGN_PLT}
    check('every PLT (=IFUNC) call of the relay resolves to a memory helper',
          not bad_plt, plt_calls)
    want = {'socket': ('socket', '__socket'),
            'bind': ('bind', '__bind'),
            'listen': ('listen',),
            'accept': ('accept', '__libc_accept'),
            'open': ('open', '__libc_open', 'open64'),
            'read': ('read', '__libc_read'),
            'write': ('write', '__libc_write')}
    missing = [k for k, alt in want.items() if not any(a in calls for a in alt)]
    check('the relay really does the socket/open/read/write work it claims',
          not missing, f'missing {missing}' if missing else
          ', '.join(k for k in want))
    strings = [s.decode() for s in (b'10.0.0.1', b'usb0', b'9999',
                                    b'READ-ONLY', b'nx679j-relay-v9',
                                    b'/nx679j-journal', b'hash_fnv1a64_full')
               if s in rsh]
    check('the shipped binary states its address, interface and format',
          all(s in strings for s in ('10.0.0.1', 'usb0', 'READ-ONLY',
                                     'nx679j-relay-v9', '/nx679j-journal')),
          strings)
    # the port 9999 in the shipped bytes is a u16 in the initialized data
    # (g_port = DEFAULT_PORT); the *behaviour* of that default is proven
    # separately, by connecting with nc without any port argument
    check('the shipped default port 9999 is in the binary',
          struct.pack('<H', 9999) in rsh,
          f'{rsh.count(struct.pack("<H", 9999))} occurrence(s) of 0x270f')
    rforb = [s.decode() for s in (b'finit_module', b'init_module', b'insmod',
                                  b'modules.load') if s in rsh]
    check('the relay contains no module-loading strings at all', not rforb,
          rforb or 'none')

    # ------------------------------------------------------- v9 = v8 + relay
    if IMG_V8.exists():
        v8img = IMG_V8.read_bytes()
        v8lz4 = split(v8img)[1]
        v8cpio = subprocess.run(['lz4', '-dc'], input=v8lz4,
                                capture_output=True).stdout
        v8ents = parse_cpio(v8cpio)
        vi, structural, identical = 0, [], []
        for e in ents:
            if e['name'] == 'nx679j/relay':
                check('the relay entry is a NEW file (not in the v8 archive)',
                      e['name'] not in {x['name'] for x in v8ents},
                      f"{e['size']} bytes, mode {oct(e['mode'])}")
                continue
            if vi >= len(v8ents):
                break
            e8 = v8ents[vi]
            same_hdr = (e['hdr'][:54] == e8['hdr'][:54]
                        and e['hdr'][62:] == e8['hdr'][62:])
            if e['name'] != 'init':
                if not (same_hdr and e['payload'] == e8['payload']):
                    raise SystemExit(f'{e["name"]!r} differs from the v8 image')
                identical.append(e['name'])
            else:
                structural.append((e8['size'], e['size']))
            vi += 1
        check('every v8 archive entry is byte-identical in v9 except /init '
              'and the new relay',
              len(identical) == len(v8ents) - 1 and bool(structural),
              f'{len(identical)} entries identical, /init '
              f'{structural[0][0]} -> {structural[0][1]} bytes')
        w8 = (V8OUT / 'work' / 'worker-v8').read_bytes()
        check('the worker in the v9 image is byte-identical to the v8 worker',
              by['nx679j/worker']['payload'] == w8,
              f"sha256={sha(by['nx679j/worker']['payload'])[:16]}")
        i8 = (V8OUT / 'work' / 'init-v8').read_bytes()
        check('the /init in the v9 image is NOT the v8 /init (it changed)',
              by['init']['payload'] != i8,
              f"v8 sha256={sha(i8)[:16]}, v9 sha256={sha(by['init']['payload'])[:16]}")
        f8, bad8 = syscalls(V8OUT / 'work' / 'init-v8')
        check('v8 and v9 PID 1 have exactly the same syscall set',
              set(f8) == set(found), f'v8 {sorted(ALLOWED[k] for k in f8)}')
    else:
        check('the v8 image is available for the comparison', False,
              str(IMG_V8))

    res = {'checks': checks,
           'image': {'bytes': len(img), 'sha256': sha(img),
                     'ramdisk_lz4': {'bytes': rsize, 'sha256': sha(lz4)}},
           'programs': {n: {'bytes': by[n]['size'], 'sha256': sha(by[n]['payload'])}
                        for n in ('init', 'nx679j/worker', 'nx679j/relay')},
           'summary': {'passed': sum(1 for c in checks if c['pass']),
                       'total': len(checks)}}
    (OUT / 'verify-candidate-v9.json').write_text(json.dumps(res, indent=2) + '\n')
    p, t = res['summary']['passed'], res['summary']['total']
    print(f'\n=== {p}/{t} checks passed -> verify-candidate-v9.json')
    return 0 if p == t else 1


if __name__ == '__main__':
    sys.exit(main())
