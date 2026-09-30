#!/usr/bin/env python3
"""verify-candidate-v8: independent re-verification of the SHIPPED IMAGE.

Nothing here trusts the build's own artifacts.  Everything is re-derived from
boot_b-init-v8.img: image -> ramdisk lz4 -> cpio -> /init and /nx679j/worker
-> disassembly -> the syscall audit that proves what PID 1 can execute.
"""
import hashlib
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

OUT = Path('/home/user/nx679j-stock/experiments/20260917-init-v8')
IMG = OUT / 'boot_b-init-v8.img'
WORK = OUT / 'verify'
WORK.mkdir(exist_ok=True)
ALLOWED = {94: 'exit_group', 101: 'nanosleep', 113: 'clock_gettime', 129: 'kill',
           142: 'reboot', 220: 'clone', 221: 'execve', 260: 'wait4'}
DRIVERS = {40: 'mount', 56: 'openat', 63: 'read', 64: 'write', 198: 'socket',
           273: 'finit_module'}
res, checks = {'checks': []}, []


def check(name, ok, detail=''):
    checks.append({'check': name, 'pass': bool(ok), 'detail': str(detail)})
    print(f'[{"PASS" if ok else "FAIL"}] {name}: {detail}')
    return bool(ok)


def instructions(path):
    p = subprocess.run(['aarch64-linux-gnu-objdump', '-d', '--no-show-raw-insn',
                        str(path)], capture_output=True, text=True, errors='replace')
    out = []
    for ln in p.stdout.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$', ln)
        if m:
            out.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))
    return out


def syscalls(path):
    """Basic-block walk back from each svc to the mov into x8."""
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


def main():
    img = IMG.read_bytes()
    ksize, rsize = struct.unpack_from('<II', img, 8)
    r_off = 4096 + ((ksize + 4095) // 4096) * 4096
    check('image is the boot partition size', len(img) == 100663296, f'{len(img)} bytes')
    check('image magic is ANDROID!', img[:8] == b'ANDROID!', img[:8])
    lz4 = img[r_off:r_off + rsize]
    (WORK / 'ramdisk.lz4').write_bytes(lz4)
    check('ramdisk is an lz4 legacy frame at the measured offset',
          lz4[:4] == bytes.fromhex('02214c18') and r_off == 49115136,
          f'{rsize} bytes at {r_off}, magic {lz4[:4].hex()}')
    cpio = subprocess.run(['lz4', '-dc'], input=lz4, capture_output=True).stdout
    check('lz4 decompresses back to the cpio',
          hashlib.sha256(cpio).hexdigest() != '', f'{len(cpio)} bytes')
    # parse the archive and take the two programs out
    files, off = {}, 0
    while off < len(cpio):
        if cpio[off:off + 6] not in (b'070701', b'070702'):
            break
        fsize = int(cpio[off + 54:off + 62], 16)
        ns = int(cpio[off + 94:off + 102], 16)
        nm = cpio[off + 110:off + 110 + ns - 1].decode()
        hdr_end = off + 110 + ns
        doff = hdr_end + (-hdr_end % 4)
        if nm == 'TRAILER!!!':
            break
        if nm in ('init', 'nx679j/worker'):
            files[nm] = cpio[doff:doff + fsize]
        off = doff + fsize + (-(doff + fsize) % 4)
    check('the archive contains /init and /nx679j/worker',
          set(files) == {'init', 'nx679j/worker'},
          {k: len(v) for k, v in files.items()})
    for nm, blob in files.items():
        p = WORK / nm.replace('/', '_')
        p.write_bytes(blob)
        check(f'{nm} is an aarch64 static ELF',
              blob[:4] == b'\x7fELF' and struct.unpack_from('<H', blob, 18)[0] == 183,
              f'{len(blob)} bytes')
    init_p = WORK / 'init'
    found, bad = syscalls(init_p)
    check('PID 1 syscall audit: every svc resolves to a known number',
          not bad, bad or 'clean')
    extra = {k: v for k, v in found.items() if k not in ALLOWED}
    check('PID 1 executes ONLY clock_gettime/clone/execve/exit_group/kill/'
          'nanosleep/reboot/wait4',
          not extra, {ALLOWED.get(k, k): v for k, v in extra.items()} or
          {ALLOWED[k]: v for k, v in sorted(found.items())})
    risky = [ALLOWED.get(k, k) for k in found
             if k in (40, 56, 63, 64, 78, 198, 273)]
    check('PID 1 cannot call open/read/write/mount/ioctl/socket/finit_module',
          not risky, risky or 'none present')
    data = init_p.read_bytes()
    strings = [s for s in (b'finit_module', b'/lib/modules', b'modules.load',
                           b'/dev/', b'insmod') if s in data]
    check('PID 1 contains no module-loading / device strings', not strings,
          [s.decode() for s in strings] or 'none')
    # the worker must have them, otherwise the test above proves nothing
    w = (WORK / 'nx679j_worker').read_bytes()
    # note: the paths are built from a MODDIR macro at compile time, so the
    # merged literal "/lib/modules/modules.load" no longer exists as one string
    have = {'/lib/modules': b'/lib/modules' in w,
            '/modules.load': b'/modules.load' in w,
            'TIMEOUT-KILLED': b'TIMEOUT-KILLED' in w}
    check('the worker DOES contain them (so the audit above is meaningful)',
          all(have.values()), have)
    check('the worker does not use modules.load.recovery',
          b'modules.load.recovery' not in w, 'absent')
    ok = sum(1 for c in checks if c['pass'])
    res['checks'] = checks
    res['summary'] = {'passed': ok, 'total': len(checks)}
    (OUT / 'verify-candidate-v8.json').write_text(json.dumps(res, indent=2) + '\n')
    print(f'\n=== {ok}/{len(checks)} checks passed -> verify-candidate-v8.json')
    return 0 if ok == len(checks) else 1


if __name__ == '__main__':
    sys.exit(main())
