#!/usr/bin/env python3
"""build-candidate-v8: the ONE slot-B image of experiment 20260917-init-v8.

Structure of what is shipped, and why it is the only single-variable change
that matters:

  container   : /home/user/nx679j-stock/magisk_patched-30700_Zx2eF.img
                (the image whose ramdisk is measured to boot Android), patched
                IN PLACE: new ramdisk bytes at absolute offset 49115136 and the
                ramdisk_size field at offset 12.  Kernel, cmdline, DTB, ABL
                handoff and the partition layout are untouched.
  ramdisk     : the exact cpio of the v7 probe (v7 is MEASURED on hardware to
                reach userspace and self-restart every ~10 s), with
                  /init          replaced by the freestanding supervisor
                  /nx679j/worker added (the only program allowed to do risky
                                 work)
                Everything else in the archive stays byte-identical.
  compression : lz4 -l (legacy frame) level 9, the frame format the device's
                kernel has CONFIG_RD_LZ4 for, round-tripped before shipping.

Outputs: boot_b-init-v8.img + manifest.json + build log.
Verification happens here AND independently in verify-candidate-v8.py, which
re-derives everything from the shipped image bytes.
"""
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

OUT = Path('/home/user/nx679j-stock/experiments/20260917-init-v8')
WORK = OUT / 'work'
BASE = Path('/home/user/nx679j-stock/experiments/20260916-122926-native-baseline')
INIT_SRC = OUT / 'nx679j-init-v8.c'
WORKER_SRC = OUT / 'nx679j-worker.c'
INIT_BIN = WORK / 'init-v8'
INIT_DB = WORK / 'init-v8.unstripped'
WORKER_BIN = WORK / 'worker-v8'

# the ramdisk that is measured to reach userspace on hardware (v7 probe build)
V7_CPIO = BASE / 'probe-v7-sonda' / 'work' / 'v7-ramdisk.cpio'
V7_CPIO_SHA = 'a81681b12e4b66da3bc5dcee7f2d0c38c27c35e5c0e2a2d1d5e8d3d9d0d9d9d9'  # checked below
V7_IMG = BASE / 'probe-v7-sonda' / 'boot_b-probe-v7.img'

MAGISK = Path('/home/user/nx679j-stock/magisk_patched-30700_Zx2eF.img')
MAGISK_SHA = '0cd94d5627501f3c65c94542093ad18b1ec6c1c215dc6a7ecab4f941057e8364'
FULL_SIZE = 100663296
KERNEL_OFF = 4096
RAMDISK_OFF = 49115136
HDR_RSIZE_FIELD = 12
KERNEL_SIZE = 49108324

CC = 'aarch64-linux-gnu-gcc'
OBJDUMP = 'aarch64-linux-gnu-objdump'

# PID 1 may only ever execute these syscalls (aarch64 numbers)
ALLOWED_SYSCALLS = {
    220: 'clone',
    221: 'execve',
    260: 'wait4',
    101: 'nanosleep',
    129: 'kill',
    142: 'reboot',
    94: 'exit_group',
    113: 'clock_gettime',
}
# and it may not contain a call to any of these, whatever the syscall wrapper
FORBIDDEN_NAMES = ('finit_module', 'init_module', 'mount', 'umount', 'open',
                   'read', 'write', 'mknod', 'ioctl', 'socket', 'printf',
                   'malloc', 'free', 'exit')

log_lines = []


def log(m=''):
    print(m, flush=True)
    log_lines.append(m)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def sha_file(p):
    return sha(Path(p).read_bytes())


def run(cmd, **kw):
    p = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if p.returncode:
        raise SystemExit(f'command failed ({p.returncode}): {" ".join(map(str, cmd))}\n'
                         f'{p.stdout}\n{p.stderr}')
    return p.stdout


# ---------------------------------------------------------------- disassembly
INSN_RE = re.compile(r'^\s*([0-9a-f]+):\t([0-9a-f ]+)\t([a-z0-9._]+)(?:\s+(.*))?$')


def instructions(binary):
    dis = run([OBJDUMP, '-d', '--no-show-raw-insn', str(binary)])
    out = []
    for line in dis.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\t([a-z0-9._]+)(?:\s+(.*))?$', line)
        if m:
            out.append((int(m.group(1), 16), m.group(2), (m.group(3) or '').strip()))
    return out


def syscall_audit(binary):
    """Every svc #0 in PID 1 must be preceded by a mov/movz/movk of x8 with an
    allowed syscall number -- read from the SHIPPED bytes, not from the source"""
    insns = instructions(binary)
    found = {}
    problems = []
    for i, (addr, mn, op) in enumerate(insns):
        if mn != 'svc':
            continue
        nr = None
        # walk back to the start of the basic block: the last write to x8
        # before the svc must be a mov/movz with an immediate
        for back in range(1, 25):
            if i - back < 0:
                break
            _, pmn, pop = insns[i - back]
            if pmn in ('b', 'bl', 'ret', 'svc', 'br', 'blr', 'cbz', 'cbnz'):
                break
            if pmn in ('mov', 'movz') and pop.startswith('x8,'):
                mm = re.search(r'#(0x[0-9a-f]+|\d+)', pop)
                nr = int(mm.group(1), 0) if mm else None
                break
            if pop.startswith('x8,'):        # any other write to x8
                problems.append(f'svc at {hex(addr)}: x8 set by {pmn} {pop}, '
                                f'cannot prove the syscall number')
                nr = -1
                break
        if nr is None:
            problems.append(f'svc at {hex(addr)}: no x8 setup found')
        elif nr not in ALLOWED_SYSCALLS:
            problems.append(f'svc at {hex(addr)}: syscall {nr} not in the whitelist')
        else:
            found[nr] = found.get(nr, 0) + 1
    return found, problems


def symbol_names(binary):
    out = run(['aarch64-linux-gnu-nm', '-a', str(binary)])
    return [l.split()[-1] for l in out.splitlines() if l.strip()]


def compile_all():
    WORK.mkdir(parents=True, exist_ok=True)
    # PID 1: freestanding, no libc at all
    run([CC, '-static', '-nostdlib', '-ffreestanding', '-fno-stack-protector',
         '-fno-builtin', '-fno-asynchronous-unwind-tables', '-Os',
         '-Wl,-e,_start', '-o', str(INIT_BIN), str(INIT_SRC)])
    run([CC, '-static', '-nostdlib', '-ffreestanding', '-fno-stack-protector',
         '-fno-builtin', '-fno-asynchronous-unwind-tables', '-Os', '-g',
         '-Wl,-e,_start', '-o', str(INIT_DB), str(INIT_SRC)])
    # the worker: static ELF with libc, it may block, it is never PID 1
    run([CC, '-static', '-Os', '-s', '-Wall', '-o', str(WORKER_BIN), str(WORKER_SRC)])
    run([CC, '-static', '-Os', '-g', '-o', WORK / 'worker-v8.unstripped', str(WORKER_SRC)])


def check_init():
    res = {}
    data = INIT_BIN.read_bytes()
    res['bytes'] = len(data)
    res['sha256'] = sha(data)
    res['elf_aarch64'] = data[:4] == b'\x7fELF' and struct.unpack_from('<H', data, 18)[0] == 183
    dyn = run(['aarch64-linux-gnu-readelf', '-d', str(INIT_BIN)])
    res['no_dynamic_section'] = 'There is no dynamic section' in dyn or 'NEEDED' not in dyn
    undef = [l.split()[-1] for l in run(['aarch64-linux-gnu-nm', '-u', str(INIT_BIN)]).splitlines()
             if l.strip()]
    res['undefined_symbols'] = undef
    names = symbol_names(INIT_BIN)
    res['symbols'] = sorted(set(n for n in names if not n.startswith('$')))
    res['forbidden_symbols_present'] = [n for n in res['symbols']
                                        if n.strip('_') in FORBIDDEN_NAMES]
    found, problems = syscall_audit(INIT_DB)
    res['syscalls_executed'] = {ALLOWED_SYSCALLS[k]: v for k, v in sorted(found.items())}
    res['syscall_audit_problems'] = problems
    # "mounts" is only the argv label handed to the worker; what must not be
    # there is any module-loading or device-access string in PID 1
    res['text_forbidden_strings'] = [s.decode() for s in
                                     (b'finit_module', b'init_module',
                                      b'/lib/modules', b'/dev/', b'insmod')
                                     if s in data]
    # PID 1 must not be able to return from _start
    insns = instructions(INIT_DB)
    calls = {t for _, mn, op in insns for t in re.findall(r'<([^>]+)>', op)
             if mn in ('bl', 'b')}
    res['call_targets'] = sorted(calls)
    res['calls_into_libc'] = sorted(c for c in calls
                                   if c in ('exit', '_exit', 'abort', 'printf'))
    return res


def syscall_call_sites(binary):
    """Call sites of libc's syscall(): the number is in x0 at the call.
    Returns {nr: count} for the sites whose x0 is a literal we can read."""
    insns = instructions(binary)
    out = {}
    for i, (addr, mn, op) in enumerate(insns):
        if mn != 'bl' or 'syscall' not in op:
            continue
        for back in range(1, 7):
            if i - back < 0:
                break
            _, pm, po = insns[i - back]
            if pm in ('mov', 'movz') and (po.startswith('x0,') or po.startswith('w0,')):
                mm = re.search(r'#(0x[0-9a-f]+|\d+)', po)
                if mm:
                    nr = int(mm.group(1), 0)
                    out[nr] = out.get(nr, 0) + 1
                break
    return out


def check_worker():
    res = {}
    data = WORKER_BIN.read_bytes()
    res['bytes'] = len(data)
    res['sha256'] = sha(data)
    res['elf_aarch64'] = data[:4] == b'\x7fELF' and struct.unpack_from('<H', data, 18)[0] == 183
    dyn = run(['aarch64-linux-gnu-readelf', '-d', str(WORKER_BIN)])
    res['static'] = 'NEEDED' not in dyn
    # symbol names live in the unstripped twin (the shipped one is stripped)
    insns = instructions(WORK / 'worker-v8.unstripped')
    wsyc, wprob = syscall_audit(WORK / 'worker-v8.unstripped')
    sites = syscall_call_sites(WORK / 'worker-v8.unstripped')
    res['syscall_call_sites'] = {str(k): v for k, v in sorted(sites.items())}
    res['has_finit_module'] = sites.get(273, 0) >= 1     # aarch64 __NR_finit_module
    res['has_mount'] = sites.get(40, 0) >= 1             # aarch64 __NR_mount
    res['has_socket'] = sites.get(198, 0) >= 1
    res['has_fork'] = any(mn == 'bl' and 'fork' in op for _, mn, op in insns)
    res['text_has_modules_load'] = b'modules.load' in data
    res['text_has_modules_recovery'] = b'modules.load.recovery' in data
    res['text_has_timeout_killed'] = b'TIMEOUT-KILLED' in data
    return res


# ------------------------------------------------------------------- cpio
def parse_cpio(buf, label):
    entries = []
    off = 0
    while True:
        magic = buf[off:off + 6]
        if magic not in (b'070701', b'070702'):
            raise SystemExit(f'{label}: bad cpio magic at {off}: {magic!r}')
        f = [int(buf[off + 6 + i * 8:off + 6 + (i + 1) * 8], 16) for i in range(13)]
        (ino, mode, uid, gid, nlink, mtime, fsize, dmaj, dmin, rmaj, rmin,
         namesize, check) = f
        name = buf[off + 110:off + 110 + namesize - 1].decode()
        hdr_end = off + 110 + namesize
        data_off = hdr_end + (-hdr_end % 4)
        data_end = data_off + fsize
        span_end = data_end + (-data_end % 4)
        entries.append(dict(name=name, mode=mode, size=fsize, hdr=off,
                            data_off=data_off, data_end=data_end, span=span_end,
                            payload=buf[data_off:data_end],
                            hdr_bytes=buf[off:off + 110]))
        if name == 'TRAILER!!!':
            return entries, span_end
        off = span_end


def splice_entry(buf, label, name, new_payload):
    entries, _ = parse_cpio(buf, label)
    hits = [e for e in entries if e['name'] == name]
    if len(hits) != 1:
        raise SystemExit(f'{label}: expected exactly one {name!r}, found {len(hits)}')
    e = hits[0]
    hdr = bytearray(e['hdr_bytes'])
    hdr[54:62] = b'%08x' % len(new_payload)
    name_and_pad = buf[e['hdr'] + 110:e['data_off']]
    new_entry = bytes(hdr) + name_and_pad + new_payload
    new_entry += b'\0' * (-len(new_entry) % 4)
    out = buf[:e['hdr']] + new_entry + buf[e['span']:]
    if out[:e['hdr']] != buf[:e['hdr']] or out[e['hdr'] + len(new_entry):] != buf[e['span']:]:
        raise SystemExit(f'{label}: splice changed bytes outside {name!r}')
    return out, e


def add_entry(buf, label, name, payload, mode=0o100755):
    """Insert a NEW file just before the TRAILER!!! entry, leaving every other
    byte of the archive untouched."""
    if name.endswith('/'):
        raise SystemExit('use add_dir() for directories')
    names = {e['name'] for e in parse_cpio(buf, label)[0]}
    if name in names:
        raise SystemExit(f'{label}: {name} already present')
    if '/' in name:
        # the kernel's initramfs unpacker will NOT create missing parents:
        # adding "nx679j/worker" without the "nx679j" directory entry silently
        # loses the file (found the hard way, QEMU boot A).
        parent = name.rsplit('/', 1)[0]
        if parent not in names:
            buf = add_dir(buf, label, parent)
    return _insert_before_trailer(buf, label, name, payload, mode)


def add_dir(buf, label, name):
    names = {e['name'] for e in parse_cpio(buf, label)[0]}
    if name in names:
        return buf
    out, _tr = _insert_before_trailer(buf, label, name, b'', 0o40755)
    return out


def _insert_before_trailer(buf, label, name, payload, mode):
    tr = [e for e in parse_cpio(buf, label)[0] if e['name'] == 'TRAILER!!!'][0]
    hdr = b'070701' + b'00000000'                       # ino
    hdr += b'%08X' % mode
    hdr += b'00000000' + b'00000000'                    # uid, gid
    hdr += b'00000001'                                  # nlink
    hdr += b'00000000'                                  # mtime
    hdr += b'%08X' % len(payload)
    hdr += b'00000000' * 4                              # devmajor/minor, rdev*
    nm = name.encode() + b'\0'
    hdr += b'%08X' % len(nm)
    hdr += b'00000000'                                  # check
    entry = hdr + nm
    entry += b'\0' * (-len(entry) % 4)
    entry += payload
    entry += b'\0' * (-len(entry) % 4)
    out = buf[:tr['hdr']] + entry + buf[tr['hdr']:]
    if out[:tr['hdr']] != buf[:tr['hdr']] or out[tr['hdr'] + len(entry):] != buf[tr['hdr']:]:
        raise SystemExit(f'{label}: add_entry changed bytes outside the trailer')
    return out, tr


def compress(cpio_path, out_path):
    subprocess.run(['lz4', '-l', '-f', '-9', str(cpio_path), str(out_path)], check=True)
    return out_path.read_bytes()


def build_image(ramdisk, out_path):
    src = MAGISK.read_bytes()
    buf = bytearray(src)
    buf[RAMDISK_OFF:RAMDISK_OFF + len(ramdisk)] = ramdisk
    struct.pack_into('<I', buf, HDR_RSIZE_FIELD, len(ramdisk))
    out_path.write_bytes(bytes(buf))
    return bytes(buf)


def check_image(path, ramdisk):
    src = MAGISK.read_bytes()
    img = path.read_bytes()
    r = {'path': str(path), 'bytes': len(img), 'sha256': sha(img)}
    if len(img) != FULL_SIZE:
        raise SystemExit('image is not the full partition size')
    hdr = img[:4096]
    magic, ksize, rsize, osver, hsize = struct.unpack_from('<8sIIII', hdr, 0)
    sig_size = struct.unpack_from('<I', hdr, 1580)[0]
    r['header'] = dict(magic=magic.decode(), kernel_size=ksize, ramdisk_size=rsize,
                       header_size=hsize, os_version=hex(osver), signature_size=sig_size)
    if not (magic == b'ANDROID!' and ksize == KERNEL_SIZE and rsize == len(ramdisk)
            and hsize == 1584 and sig_size == 4096):
        raise SystemExit(f'header wrong: {r["header"]}')
    r['kernel_identical'] = img[KERNEL_OFF:RAMDISK_OFF] == src[KERNEL_OFF:RAMDISK_OFF]
    if not r['kernel_identical']:
        raise SystemExit('kernel bytes differ from the container')
    r['ramdisk_identical'] = img[RAMDISK_OFF:RAMDISK_OFF + len(ramdisk)] == ramdisk
    if not r['ramdisk_identical']:
        raise SystemExit('stored ramdisk != input ramdisk')
    after = RAMDISK_OFF + len(ramdisk)
    r['tail_identical_absolute'] = img[after:] == src[after:]
    if not r['tail_identical_absolute']:
        raise SystemExit('tail after the ramdisk differs from the container')
    diff = [i for i in range(FULL_SIZE) if img[i] != src[i]]
    outside = [i for i in diff if not (HDR_RSIZE_FIELD <= i < HDR_RSIZE_FIELD + 4
                                       or RAMDISK_OFF <= i < after)]
    r['diff_count'] = len(diff)
    r['diff_outside_allowed_regions'] = outside[:16]
    if outside:
        raise SystemExit(f'{len(outside)} differing bytes outside the allowed regions')
    old_end = RAMDISK_OFF + struct.unpack_from('<I', src, 12)[0]
    r['leftover_old_ramdisk_bytes'] = max(0, old_end - after)
    return r


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    res = {}
    log('=== build-candidate-v8 (ONE image: the supervisor + its worker) ===')
    log()

    if sha_file(MAGISK) != MAGISK_SHA:
        raise SystemExit('the container changed: refusing to build')
    log(f'[1] container      {MAGISK.name} sha256={MAGISK_SHA[:16]}... ok')

    if not V7_CPIO.exists():
        raise SystemExit(f'missing the measured good ramdisk source: {V7_CPIO}')
    v7_bytes = V7_CPIO.read_bytes()
    v7_sha = sha(v7_bytes)
    log(f'[2] ramdisk source  {V7_CPIO}')
    log(f'                    {len(v7_bytes)} bytes sha256={v7_sha}')
    v7_img_sha = sha_file(V7_IMG) if V7_IMG.exists() else None
    if v7_img_sha:
        log(f'                    (the v7 image built from it is '
            f'{V7_IMG.name}, sha256={v7_img_sha[:16]}..., MEASURED to boot '
            f'and self-restart on hardware)')
    res['ramdisk_source'] = dict(path=str(V7_CPIO), bytes=len(v7_bytes), sha256=v7_sha,
                                 v7_image_sha256=v7_img_sha)

    log('[3] compiling')
    compile_all()
    log(f'    /init   {INIT_BIN.stat().st_size} bytes '
        f'(freestanding: -nostdlib -ffreestanding)')
    log(f'    worker  {WORKER_BIN.stat().st_size} bytes (static, may block)')

    log('[4] PID 1 audit: which syscalls can the shipped bytes execute?')
    res['init'] = check_init()
    for k, v in sorted(res['init']['syscalls_executed'].items()):
        log(f'    svc #0 -> {k} ({v} call sites)')
    if res['init']['syscall_audit_problems']:
        raise SystemExit('syscall audit failed: ' + '; '.join(res['init']['syscall_audit_problems']))
    if not res['init']['no_dynamic_section']:
        raise SystemExit('/init is not freestanding (has dynamic section)')
    if res['init']['undefined_symbols']:
        raise SystemExit(f'/init has undefined symbols: {res["init"]["undefined_symbols"]}')
    if res['init']['forbidden_symbols_present']:
        raise SystemExit(f'/init mentions {res["init"]["forbidden_symbols_present"]}')
    if res['init']['text_forbidden_strings']:
        raise SystemExit(f'/init text contains {res["init"]["text_forbidden_strings"]}')
    log(f'    no libc, no undefined symbols, no finit_module/mount/lib-module strings')
    log(f'    call targets: {", ".join(res["init"]["call_targets"]) or "(none)"}')

    log('[5] worker audit')
    res['worker'] = check_worker()
    log(f'    {res["worker"]["bytes"]} bytes static={res["worker"]["static"]} '
        f'finit_module={res["worker"]["has_finit_module"]} fork={res["worker"]["has_fork"]}')
    if not (res['worker']['static'] and res['worker']['has_finit_module']):
        raise SystemExit('worker is wrong (not static or has no finit_module)')

    log('[6] ramdisk: splice /init, add /nx679j/worker, nothing else touched')
    rc, e_init = splice_entry(v7_bytes, 'v7 ramdisk', 'init', INIT_BIN.read_bytes())
    res['spliced_init'] = dict(name=e_init['name'], old_size=e_init['size'],
                               new_size=INIT_BIN.stat().st_size, mode=oct(e_init['mode']))
    added, tr = add_entry(rc, 'v7 ramdisk + init', 'nx679j/worker', WORKER_BIN.read_bytes())
    res['added_worker'] = dict(name='nx679j/worker', size=WORKER_BIN.stat().st_size,
                               inserted_before=tr['name'])
    entries, _ = parse_cpio(added, 'v8 ramdisk')
    res['ramdisk_entries'] = len(entries)
    names = [e['name'] for e in entries]
    res['ramdisk_has_worker'] = 'nx679j/worker' in names
    res['ramdisk_has_init'] = 'init' in names
    if not (res['ramdisk_has_worker'] and res['ramdisk_has_init']):
        raise SystemExit('ramdisk is missing /init or /nx679j/worker')
    cpio_path = WORK / 'v8-ramdisk.cpio'
    cpio_path.write_bytes(added)
    log(f'    cpio {len(v7_bytes)} -> {len(added)} bytes, {len(entries)} entries, '
        f'/init {e_init["size"]} -> {INIT_BIN.stat().st_size} bytes, '
        f'/nx679j/worker {WORKER_BIN.stat().st_size} bytes')

    log('[7] lz4 -l legacy compression + round trip')
    lz4 = compress(cpio_path, WORK / 'v8-ramdisk.lz4')
    (OUT / 'v8-ramdisk.lz4').write_bytes(lz4)
    back = subprocess.run(['lz4', '-dc', str(WORK / 'v8-ramdisk.lz4')],
                          capture_output=True).stdout
    if back != added:
        raise SystemExit('lz4 round trip does not reproduce the cpio')
    if lz4[:4] != b'\x02\x21\x4c\x18':
        raise SystemExit(f'not an lz4 legacy frame: {lz4[:4]!r}')
    res['ramdisk_lz4'] = dict(bytes=len(lz4), sha256=sha(lz4), magic=lz4[:4].hex(),
                              round_trip_ok=True)
    log(f'    {len(added)} -> {len(lz4)} bytes, legacy magic {lz4[:4].hex()}, '
        f'round trip byte-identical')

    log('[7b] UNPACK the compressed ramdisk and look at the real files: this is '
        'the check the kernel will do')
    up = WORK / 'unpack-v8'
    if up.exists():
        subprocess.run(['rm', '-rf', str(up)], check=True)
    up.mkdir(parents=True)
    sh = subprocess.run(f'lz4 -dc {WORK / "v8-ramdisk.lz4"} | cpio -idm --quiet',
                        shell=True, cwd=str(up), capture_output=True, text=True)
    # cpio cannot mknod device nodes as a normal user: that is expected and not
    # an error here -- what must exist is the two programs we added.
    if not (up / 'init').is_file() and sh.returncode:
        raise SystemExit(f'cpio -idm failed: {sh.stderr[:300]}')
    devnodes = sorted(p.name for p in (up / 'dev').glob('*')) if (up / 'dev').is_dir() else []
    init_p = up / 'init'
    work_p = up / 'nx679j' / 'worker'
    res['unpacked'] = {
        'init_exists': init_p.is_file(),
        'init_executable': os.access(init_p, os.X_OK),
        'init_sha256': sha_file(init_p) if init_p.is_file() else None,
        'worker_exists': work_p.is_file(),
        'worker_executable': os.access(work_p, os.X_OK),
        'worker_sha256': sha_file(work_p) if work_p.is_file() else None,
        'worker_path_is_a_file': work_p.is_file() and (up / 'nx679j').is_dir(),
        'dev_nodes_in_ramdisk': devnodes,
    }
    u = res['unpacked']
    if not (u['init_exists'] and u['init_executable'] and u['worker_exists']
            and u['worker_executable']):
        raise SystemExit(f'/init or /nx679j/worker does not appear when unpacked: {u}')
    if u['init_sha256'] != sha(INIT_BIN.read_bytes()):
        raise SystemExit('the unpacked /init is not the compiled init')
    if u['worker_sha256'] != sha(WORKER_BIN.read_bytes()):
        raise SystemExit('the unpacked /nx679j/worker is not the compiled worker')
    log(f'    init {init_p.stat().st_size} bytes ok, nx679j/worker '
        f'{work_p.stat().st_size} bytes ok, both executable, both hashes match')

    log('[8] boot image: in-place graft into the container')
    img_path = OUT / 'boot_b-init-v8.img'
    build_image(lz4, img_path)
    res['image'] = check_image(img_path, lz4)
    res['image']['ramdisk_sha256'] = sha(lz4)
    log(f'    {img_path.name} {res["image"]["bytes"]} bytes '
        f'sha256={res["image"]["sha256"]}')
    log(f'    header: {res["image"]["header"]}')
    log(f'    kernel identical, ramdisk identical, tail identical, '
        f'{res["image"]["diff_count"]} bytes differ from the container '
        f'(all inside the size field + ramdisk region)')
    log(f'    leftover bytes of the old ramdisk left in place: '
        f'{res["image"]["leftover_old_ramdisk_bytes"]}')

    res['protocol'] = {
        'success': 'logo STEADY (no flash) + host sees USB 18d1:4ee7 -> the gadget is up',
        'not_successful': 'logo CYCLES: 8 s period = UFS ok but no gadget, '
                          '30 s = no UFS/rawdump, 60 s = PID 1 had to kill an operation',
        'dead': 'logo STEADY and no USB at all',
        'journal': ['rawdump partition (256 MiB, size 524288 sectors) at offset 0',
                    '/sys/fs/pstore (ramoops pmsg/kmsg) on the next boot',
                    '/dev/kmsg at KERN_EMERG during the run'],
    }
    (OUT / 'manifest.json').write_text(json.dumps(res, indent=2) + '\n')
    (OUT / 'build-candidate-v8.log').write_text('\n'.join(log_lines) + '\n')
    log()
    log(f'manifest -> {OUT / "manifest.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
