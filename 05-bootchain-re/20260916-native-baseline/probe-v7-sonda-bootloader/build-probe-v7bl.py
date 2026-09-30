#!/usr/bin/env python3
"""Build the two NX679J slot-B probe images with the RESTART2 / 'bootloader'
reason (the "sonda BOOTLOADER" series).

Same container graft as the v7/v7b build, same static init (no PT_INTERP, no
DT_NEEDED, cannot return from main), same ramdisk sources; the ONLY change in
the payload is the reboot call:

    v7 / v7b         reboot(LINUX_REBOOT_CMD_RESTART)
    this series      reboot(LINUX_REBOOT_CMD_RESTART2, "bootloader")

which makes the outcome visible to the HOST (ABL is supposed to present fastboot
= USB 18d1:d00d) instead of only on the screen.

v7  = the proven Magisk container + OUR v6 ramdisk with /init replaced
v7b = the same container + the Android ramdisk (30 entries) with /init replaced

Container graft: the source Magisk image is patched IN PLACE -- new ramdisk
bytes at absolute offset 49115136, ramdisk_size in the header updated -- so the
kernel stays byte-identical, the 4096-byte signature field is unchanged, every
byte after the ramdisk keeps the source value at the SAME absolute offset, and
the file stays exactly 100663296 bytes.

The disassembly check below is not decorative: it proves, from the bytes of the
shipped /init, that the syscall is SYS_reboot (__NR_reboot read from the
toolchain's own headers) with cmd == LINUX_REBOOT_CMD_RESTART2 (0xa1b2c3d4, also
read from the header) and with the 4th argument a pointer that, mapped through
the ELF program headers, points at the literal "bootloader\\0".
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

BASE = Path('/home/user/nx679j-stock/experiments/20260916-122926-native-baseline')
OUT = BASE / 'probe-v7-sonda-bootloader'
WORK = OUT / 'work'
INPUTS = OUT / 'inputs'

CC = 'aarch64-linux-gnu-gcc'
CFLAGS = ['-static', '-Os', '-s', '-Wall', '-Wextra', '-Wno-unused-result']

INIT_SRC = OUT / 'candidate-init-sonda-bootloader.c'
INIT_BIN = WORK / 'init-sonda-bootloader'

MAGISK = Path('/home/user/nx679j-stock/magisk_patched-30700_Zx2eF.img')
MAGISK_SHA = '0cd94d5627501f3c65c94542093ad18b1ec6c1c215dc6a7ecab4f941057e8364'
MAGISK_LEN = 100663296

V6_RAMDISK_GZ = BASE / 'candidate-minimal-gadget-v6' / 'work' / 'ramdisk.cpio.gz'
V6_RAMDISK_GZ_SHA = '8378e9a3398f55bfee72166b8bcd2df94b5cca098418d948cf3bf44019a9d035'
V6_RAMDISK_GZ_LEN = 901952

# the two ramdisk sources are shared with the v7/v7b build (same bytes)
ANDROID_CPIO = BASE / 'probe-v7-sonda' / 'inputs' / 'magisk-android-ramdisk.cpio'
ANDROID_CPIO_SRC = Path('/tmp/magisk.cpio')
ANDROID_CPIO_SHA = '6fd2630909eab5458196edc25b8c41b4c15b401c0c92fdeab65fedaec13a0594'

KERNEL_OFF = 4096
RAMDISK_OFF = 49115136
HEADER_RAMDISK_SIZE_FIELD = 12
FULL_SIZE = MAGISK_LEN
STARTUP_SEC = 6          # stop early once the reboot is visible in the log

P6_CPIO = Path('/tmp/p6.cpio')
P6_LZ4_SHA = 'c90e61fbf60ad0e4a32360386e8d07c383e80a06eeef0b9e2827c47933aeeaf0'
LZ4_ARGS = ['lz4', '-l', '-9']

IMAGE_NAMES = {'v7': 'sonda-bootloader-v7.img', 'v7b': 'sonda-bootloader-v7b.img'}
RAMDISK_REASON = b'bootloader\x00'

log_lines = []


def log(msg=''):
    print(msg, flush=True)
    log_lines.append(str(msg))


def sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


def sha256_file(p):
    return sha256_bytes(Path(p).read_bytes())


# --------------------------------------------------------------------------
# kernel/headers: take the syscall number and the reboot command values from
# the toolchain's own headers instead of hardcoding them in this script
# --------------------------------------------------------------------------
def cpp_include_dirs():
    """The <...> search list the cross compiler actually uses."""
    txt = subprocess.run([CC, '-E', '-Wp,-v', '-'],
                         input='', capture_output=True, text=True).stderr
    dirs, on = [], False
    for line in txt.splitlines():
        if 'search starts here' in line:
            on = True
            continue
        if 'End of search list' in line:
            break
        if on and line.strip().startswith('/'):
            dirs.append(Path(line.strip()))
    return dirs


def header_path(rel):
    for d in cpp_include_dirs():
        if (d / rel).exists():
            return d / rel
    raise SystemExit(f'{rel} not found in the cross include path')


def cpp_macro_value(name, header):
    """Value the compiler itself resolves for `name` after including `header`."""
    out = subprocess.run([CC, '-dM', '-E', '-include', str(header), '-'],
                         input='', capture_output=True, text=True, check=True).stdout
    pat = re.compile(r'#define\s+' + re.escape(name) + r'\s+(.*)$')
    for line in out.splitlines():
        m = pat.match(line)
        if m:
            return m.group(1).strip().split('/*')[0].strip()
    raise SystemExit(f'{name} not defined by {header}')


def int_literal(s):
    return int(s, 0)


# --------------------------------------------------------------------------
# disassembly reading: main() cannot return, and the reboot call is RESTART2
# with a pointer to the literal 'bootloader'
# --------------------------------------------------------------------------
def parse_insns(dis):
    out = []
    for line in dis.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\t([0-9a-f ]+)\t([a-z0-9._]+)(?:\s+(.*))?$',
                     line)
        if m:
            ops = re.split(r'\s*//', (m.group(4) or '').strip())[0]
            ops = ops.split(' ; ')[0].strip()
            out.append((int(m.group(1), 16), m.group(3), ops))
    return out


def main_cannot_return(dis):
    """PID 1 cannot fall off the end of main() (which would call exit(3)).

    Three facts checked on the disassembly of the SHIPPED code: no return
    instruction inside main(), no branch or call target named exit/_exit/abort,
    and the last instruction of main() an unconditional branch.
    """
    insns = parse_insns(dis)
    rets = [hex(a) for a, mn, _ in insns if mn.startswith('ret')]
    bad = sorted({t for _, _, op in insns for t in re.findall(r'<([^>]+)>', op)
                  if t in ('exit', '_exit', '_Exit', 'abort', 'quick_exit')})
    last = insns[-1] if insns else (0, '', '')
    ok = (not rets) and (not bad) and last[1] == 'b'
    detail = (f'{len(insns)} instructions, return instructions: {rets or "none"}, '
              f'branches to exit/abort: {bad or "none"}, last instruction: '
              f'{last[1]} {last[2]}')
    return ok, detail


def track_registers(insns):
    """Minimal symbolic tracker: which address/value each register last held.

    Understands the mov/movz/movk immediate chains gcc emits for a 32-bit
    constant and the adrp/add pair it emits for a static address. A register
    with no tracked value is simply absent from the dict. 32-bit and 64-bit
    views of the same register are kept in sync (writing wN zeroes the top half
    of xN; writing xN changes what wN reads as), otherwise a stale value from an
    earlier call would be read back as the current one.
    """
    def set_reg(defs, name, val):
        if re.match(r'^[wx]\d+$', name):
            for p in ('x' + name[1:], 'w' + name[1:]):
                if val is None:
                    defs.pop(p, None)
                else:
                    defs[p] = val
        elif val is None:
            defs.pop(name, None)
        else:
            defs[name] = val

    defs = {}
    steps = []
    for addr, mn, ops in insns:
        parts = [p.strip() for p in ops.split(',')] if ops else []
        if mn == 'adrp' and len(parts) == 2:
            m = re.match(r'^(?:0x)?([0-9a-f]+) <', parts[1])
            if m:
                set_reg(defs, parts[0], ('page', int(m.group(1), 16)))
                steps.append((addr, mn, ops, dict(defs)))
                continue
        if mn == 'add' and len(parts) == 3 and re.match(r'^#', parts[2]):
            base = defs.get(parts[1])
            if base and base[0] == 'page':
                set_reg(defs, parts[0], ('addr', base[1] + int(parts[2][1:], 16)))
                steps.append((addr, mn, ops, dict(defs)))
                continue
        if mn in ('mov', 'movz', 'movk') and len(parts) >= 2:
            dst, src = parts[0], parts[1]
            if src.startswith('#'):
                imm = int(src[1:], 0)
                shift = 0
                if len(parts) >= 3 and parts[2].startswith('lsl #'):
                    shift = int(parts[2][5:], 0)
                if mn == 'movk':
                    old = defs.get(dst)
                    if isinstance(old, tuple) and old[0] == 'imm':
                        val = (old[1] & ~(0xffff << shift)) | ((imm & 0xffff) << shift)
                    else:
                        val = (imm & 0xffff) << shift
                elif mn == 'movz':
                    val = (imm & 0xffff) << shift
                else:
                    val = imm & 0xffffffff
                if dst[0] == 'w':
                    val &= 0xffffffff
                set_reg(defs, dst, ('imm', val))
            else:
                set_reg(defs, dst, defs.get(src))
            steps.append((addr, mn, ops, dict(defs)))
            continue
        # any other instruction that writes a register invalidates it
        if parts and re.match(r'^[wx]\d+$', parts[0]):
            set_reg(defs, parts[0], None)
        steps.append((addr, mn, ops, dict(defs)))
    return steps


def va_to_file_off(elf, va):
    """Map a virtual address to a file offset through the PT_LOAD headers."""
    ph = subprocess.run(['aarch64-linux-gnu-readelf', '-lW', str(elf)],
                        capture_output=True, text=True, check=True).stdout
    for block in ph.split('Program Headers:')[1:]:
        for line in block.splitlines():
            m = re.match(r'\s*LOAD\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+'
                         r'0x[0-9a-f]+\s+(0x[0-9a-f]+)\s+', line)
            if m:
                off, vaddr, filesz = (int(m.group(1), 16), int(m.group(2), 16),
                                      int(m.group(3), 16))
                if vaddr <= va < vaddr + filesz:
                    return off + (va - vaddr)
    return None


def prove_reboot_call(dis, init_bytes, nr_reboot, magic1, magic2, restart2,
                      reason_bytes, label, expect_magic=False):
    """Prove from main()'s disassembly that the restart call is RESTART2 with
    the reason string as 4th argument (plus the magics when they are set in
    main rather than by the libc wrapper).

    Every call to syscall() in main() is examined; the check passes when at
    least one of them has x0 == __NR_reboot, x3 == LINUX_REBOOT_CMD_RESTART2
    and x4 a pointer whose bytes, read from the SHIPPED binary at the address
    the program headers map it to, are exactly "bootloader\\0".

    Returns (ok, detail, evidence_line).
    """
    insns = parse_insns(dis)
    steps = track_registers(insns)
    calls = [(addr, defs) for addr, mn, ops, defs in steps
             if mn == 'bl' and re.search(r'<syscall>', ops)]
    if not calls:
        return False, f'{label}: no call to syscall() found in main()', None

    best = None
    for addr, defs in calls:
        got_nr = defs.get('x0') or defs.get('w0')
        got_cmd = defs.get('x3') or defs.get('w3')
        got_a1 = defs.get('x1') or defs.get('w1')
        got_a2 = defs.get('x2') or defs.get('w2')
        got_ptr = defs.get('x4') or defs.get('w4')

        problems = []
        if not got_nr or got_nr[0] != 'imm' or got_nr[1] != nr_reboot:
            problems.append(f'x0 (syscall number) = {got_nr}, expected '
                            f'{nr_reboot} (__NR_reboot)')
        if not got_cmd or got_cmd[0] != 'imm' or got_cmd[1] != restart2:
            problems.append(f'x3 (cmd) = {got_cmd}, expected {restart2:#x} '
                            f'(LINUX_REBOOT_CMD_RESTART2)')
        if expect_magic:
            if not got_a1 or got_a1[0] != 'imm' or got_a1[1] != magic1:
                problems.append(f'x1 (magic1) = {got_a1}, expected {magic1:#x}')
            if not got_a2 or got_a2[0] != 'imm' or got_a2[1] != magic2:
                problems.append(f'x2 (magic2) = {got_a2}, expected {magic2:#x}')
        off = None
        if not got_ptr or got_ptr[0] != 'addr':
            problems.append(f'x4 (reason pointer) = {got_ptr}, expected a '
                            f'static address')
        else:
            off = va_to_file_off(INIT_BIN, got_ptr[1])
            if off is None:
                problems.append(f'{got_ptr[1]:#x} is not inside any PT_LOAD '
                                f'segment')
            else:
                got = init_bytes[off:off + len(reason_bytes)]
                if got != reason_bytes:
                    problems.append(f'the pointer {got_ptr[1]:#x} maps to file '
                                    f'offset {off} = {got!r}, not '
                                    f'{reason_bytes!r}')
        line = (f'{addr:#x}: bl syscall  with x0={nr_reboot} (__NR_reboot), '
                f'x1={magic1:#x} (MAGIC1), x2={magic2:#x} (MAGIC2), '
                f'x3={restart2:#x} (LINUX_REBOOT_CMD_RESTART2), '
                f'x4={got_ptr[1]:#x} -> file offset {off} = '
                f'{init_bytes[off:off + len(reason_bytes)]!r}'
                if not problems else
                f'{addr:#x}: bl syscall  [not the restart2 call: '
                f'{"; ".join(problems)}]')
        if not problems:
            return True, line, line
        if best is None:
            best = line
    return False, (f'{len(calls)} call(s) to syscall() in main(), none of them '
                   f'the RESTART2 restart with the bootloader reason. Last '
                   f'analysed: {best}'), best


# --------------------------------------------------------------------------
# cpio newc: parse and splice (never re-serialise: everything except the entry
# we replace stays byte-identical, including the trailer)
# --------------------------------------------------------------------------
def parse_cpio(buf, label):
    entries = []
    off = 0
    while True:
        magic = buf[off:off + 6]
        if magic not in (b'070701', b'070702'):
            raise SystemExit(f'{label}: bad cpio magic at {off}: {magic!r}')
        fields = [int(buf[off + 6 + i * 8:off + 6 + (i + 1) * 8], 16)
                  for i in range(13)]
        (ino, mode, uid, gid, nlink, mtime, fsize, dmaj, dmin, rmaj, rmin,
         namesize, check) = fields
        name = buf[off + 110:off + 110 + namesize - 1].decode()
        hdr_end = off + 110 + namesize
        data_off = hdr_end + (-hdr_end % 4)
        data_end = data_off + fsize
        span_end = data_end + (-data_end % 4)
        entries.append(dict(
            name=name, ino=ino, mode=mode, uid=uid, gid=gid, nlink=nlink,
            mtime=mtime, size=fsize, devmajor=dmaj, devminor=dmin,
            rdevmajor=rmaj, rdevminor=rmin, check=check,
            hdr=off, data_off=data_off, data_end=data_end, span=span_end,
            payload=buf[data_off:data_end],
            hdr_bytes=buf[off:off + 110],
        ))
        if name == 'TRAILER!!!':
            return entries, span_end
        off = span_end


def splice_entry(buf, label, name, new_payload, expect_mode_oct):
    entries, end = parse_cpio(buf, label)
    hits = [e for e in entries if e['name'] == name]
    if len(hits) != 1:
        raise SystemExit(f'{label}: expected exactly one {name!r}, found {len(hits)}')
    e = hits[0]
    if expect_mode_oct is not None and oct(e['mode']) != expect_mode_oct:
        raise SystemExit(f'{label}: {name} mode {oct(e["mode"])} != {expect_mode_oct}')
    if e['check'] != 0:
        raise SystemExit(f'{label}: {name} has a non-zero newc CHECK field')

    hdr = bytearray(e['hdr_bytes'])
    hdr[54:62] = b'%08x' % len(new_payload)     # field 6 = filesize
    name_and_pad = buf[e['hdr'] + 110:e['data_off']]
    new_entry = bytes(hdr) + name_and_pad + new_payload
    new_entry += b'\0' * (-len(new_entry) % 4)

    out = buf[:e['hdr']] + new_entry + buf[e['span']:]
    if out[:e['hdr']] != buf[:e['hdr']] or out[e['hdr'] + len(new_entry):] != buf[e['span']:]:
        raise SystemExit(f'{label}: splice changed bytes outside the {name!r} entry')
    return out, e, entries


def compress_lz4_legacy(cpio_path: Path, out_path: Path):
    subprocess.run(['lz4', '-l', '-f', '-9', str(cpio_path), str(out_path)],
                   check=True)
    return out_path.read_bytes()


def build_image(ramdisk: bytes, out_path: Path, label: str):
    src = MAGISK.read_bytes()
    if len(src) != FULL_SIZE:
        raise SystemExit('source container is not the full partition size')
    buf = bytearray(src)
    if RAMDISK_OFF + len(ramdisk) > FULL_SIZE:
        raise SystemExit('ramdisk does not fit the partition')
    buf[RAMDISK_OFF:RAMDISK_OFF + len(ramdisk)] = ramdisk
    struct.pack_into('<I', buf, HEADER_RAMDISK_SIZE_FIELD, len(ramdisk))
    out_path.write_bytes(bytes(buf))
    return bytes(buf)


def check_image(path: Path, ramdisk: bytes, label: str):
    src = MAGISK.read_bytes()
    img = path.read_bytes()
    res = {'path': str(path), 'bytes': len(img), 'sha256': sha256_bytes(img)}
    res['length_ok'] = len(img) == FULL_SIZE
    if not res['length_ok']:
        raise SystemExit(f'{label}: length {len(img)} != {FULL_SIZE}')

    hdr = img[:4096]
    (magic, ksize, rsize, osver, hsize, r0, r1, r2, r3, hver) = struct.unpack_from(
        '<8sIIIIIIIII', hdr, 0)
    sig_size = struct.unpack_from('<I', hdr, 1580)[0]
    res['header'] = dict(magic=magic.decode(), kernel_size=ksize,
                         ramdisk_size=rsize, header_size=hsize,
                         header_version=hver, signature_size=sig_size,
                         os_version=hex(osver))
    res['header_ok'] = (magic == b'ANDROID!' and ksize == 49108324
                        and rsize == len(ramdisk) and hsize == 1584
                        and hver == 4 and sig_size == 4096)
    if not res['header_ok']:
        raise SystemExit(f'{label}: header wrong: {res["header"]}')

    res['kernel_identical'] = img[KERNEL_OFF:RAMDISK_OFF] == src[KERNEL_OFF:RAMDISK_OFF]
    if not res['kernel_identical']:
        raise SystemExit(f'{label}: kernel bytes differ from the source container')

    res['ramdisk_identical'] = img[RAMDISK_OFF:RAMDISK_OFF + len(ramdisk)] == ramdisk
    if not res['ramdisk_identical']:
        raise SystemExit(f'{label}: ramdisk bytes stored differ from the input')

    after = RAMDISK_OFF + len(ramdisk)
    res['tail_identical_absolute'] = img[after:] == src[after:]
    res['tail_bytes'] = FULL_SIZE - after
    if not res['tail_identical_absolute']:
        raise SystemExit(f'{label}: tail after the ramdisk is not the source tail')

    old_ramdisk_end = RAMDISK_OFF + struct.unpack_from('<I', src, 12)[0]
    res['leftover_old_ramdisk_bytes'] = max(0, old_ramdisk_end - after)
    res['leftover_all_zero'] = (img[after:old_ramdisk_end] == b'\0' * (old_ramdisk_end - after)
                               if old_ramdisk_end > after else True)

    diff = [i for i in range(FULL_SIZE) if img[i] != src[i]]
    res['diff_count'] = len(diff)
    if diff:
        res['diff_min'] = diff[0]
        res['diff_max'] = diff[-1]
        outside = [i for i in diff if not (HEADER_RAMDISK_SIZE_FIELD <= i <= HEADER_RAMDISK_SIZE_FIELD + 3
                                          or RAMDISK_OFF <= i < after)]
        res['diff_outside_allowed_regions'] = outside[:16]
        if outside:
            raise SystemExit(f'{label}: {len(outside)} differing bytes outside the ramdisk/size field')
    return res


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    INPUTS.mkdir(parents=True, exist_ok=True)

    log('=== build-probe-v7bl: two slot-B probe images with '
        'LINUX_REBOOT_CMD_RESTART2 reason=bootloader ===')
    log()

    if sha256_file(MAGISK) != MAGISK_SHA:
        raise SystemExit('the Magisk container changed: refusing to build')
    log(f'[1] container  {MAGISK}')
    log(f'    {MAGISK_LEN} bytes sha256={MAGISK_SHA}')
    log('    (the control run on 2026-09-17 booted this image from slot B)')

    if not ANDROID_CPIO.exists():
        if not ANDROID_CPIO_SRC.exists():
            raise SystemExit(f'neither {ANDROID_CPIO} nor {ANDROID_CPIO_SRC} exists')
        shutil.copy2(ANDROID_CPIO_SRC, ANDROID_CPIO)
    android = ANDROID_CPIO.read_bytes()
    if sha256_bytes(android) != ANDROID_CPIO_SHA:
        raise SystemExit('the Android ramdisk is not the measured one')
    a_entries, a_end = parse_cpio(android, 'android ramdisk')
    log(f'[2] Android ramdisk  {ANDROID_CPIO}')
    log(f'    {len(android)} bytes sha256={sha256_bytes(android)}')
    log(f'    {len(a_entries)} entries (incl. TRAILER!!!), parses to {a_end} bytes')

    v6_gz = V6_RAMDISK_GZ.read_bytes()
    if len(v6_gz) != V6_RAMDISK_GZ_LEN or sha256_bytes(v6_gz) != V6_RAMDISK_GZ_SHA:
        raise SystemExit('v6 ramdisk.gz does not match the recorded v6 build')
    v6 = gzip.decompress(v6_gz)
    v_entries, v_end = parse_cpio(v6, 'v6 ramdisk')
    log(f'[3] v6 ramdisk       {V6_RAMDISK_GZ}')
    log(f'    gz {len(v6_gz)} bytes sha256={sha256_bytes(v6_gz)}')
    log(f'    cpio {len(v6)} bytes sha256={sha256_bytes(v6)}')
    log(f'    {len(v_entries)} entries, parses to {v_end} bytes')

    # ---- the syscall numbers, straight out of the toolchain's headers -----
    unistd_h = header_path('asm/unistd.h')
    reboot_h = header_path('linux/reboot.h')
    nr_reboot = int_literal(cpp_macro_value('__NR_reboot', unistd_h))

    def rb_val(name):
        return int_literal(cpp_macro_value(name, reboot_h))

    magic1 = rb_val('LINUX_REBOOT_MAGIC1')
    magic2 = rb_val('LINUX_REBOOT_MAGIC2')
    cmd_restart = rb_val('LINUX_REBOOT_CMD_RESTART')
    cmd_restart2 = rb_val('LINUX_REBOOT_CMD_RESTART2')
    log()
    log('[4] values taken from the toolchain headers (not hardcoded here)')
    log(f'    {unistd_h}')
    log(f'    {reboot_h}')
    log(f'    __NR_reboot={nr_reboot}  LINUX_REBOOT_MAGIC1={magic1:#x} '
        f'MAGIC2={magic2:#x}')
    log(f'    LINUX_REBOOT_CMD_RESTART={cmd_restart:#x}  '
        f'LINUX_REBOOT_CMD_RESTART2={cmd_restart2:#x}')
    if cmd_restart2 != 0xa1b2c3d4:
        raise SystemExit('unexpected LINUX_REBOOT_CMD_RESTART2 value')

    # ---- compile ---------------------------------------------------------
    cc_ver = subprocess.run([CC, '--version'], capture_output=True, text=True,
                            check=True).stdout.splitlines()[0]
    cmd = [CC, *CFLAGS, '-o', str(INIT_BIN), str(INIT_SRC)]
    build = subprocess.run(cmd, capture_output=True, text=True)
    (WORK / 'cc.stdout').write_text(build.stdout)
    (WORK / 'cc.stderr').write_text(build.stderr)
    if build.returncode != 0:
        raise SystemExit(f'{CC} failed:\n{build.stderr}')
    log()
    log('[5] probe /init compiled statically')
    log(f'    {cc_ver}')
    log(f'    {" ".join(cmd)}')
    log('    compiler diagnostics: ' +
        (f'\n{build.stderr}' if build.stderr.strip() else 'none (-Wall -Wextra clean)'))

    init = INIT_BIN.read_bytes()
    init_sha = sha256_bytes(init)
    f_type = subprocess.run(['file', '-b', str(INIT_BIN)], capture_output=True,
                            text=True, check=True).stdout.strip()
    elf_h = subprocess.run(['readelf', '-h', str(INIT_BIN)], capture_output=True,
                           text=True, check=True).stdout
    elf_l = subprocess.run(['readelf', '-l', str(INIT_BIN)], capture_output=True,
                           text=True, check=True).stdout
    elf_d = subprocess.run(['readelf', '-d', str(INIT_BIN)], capture_output=True,
                           text=True, check=True).stdout
    (WORK / 'readelf-h.txt').write_text(elf_h)
    (WORK / 'readelf-l.txt').write_text(elf_l)
    (WORK / 'readelf-d.txt').write_text(elf_d)
    elf_type = [l.split()[1] for l in elf_h.splitlines() if 'Type:' in l][0]
    machine = [l.split()[1] for l in elf_h.splitlines() if 'Machine:' in l][0]
    interp = [l.strip() for l in elf_l.splitlines() if 'INTERP' in l]
    needed = [l.strip() for l in elf_d.splitlines() if 'NEEDED' in l]
    log(f'    file: {f_type}')
    log(f'    readelf -h: Type={elf_type} Machine={machine}')
    log(f'    readelf -l: PT_INTERP entries = {len(interp)} {interp}')
    log(f'    readelf -d: DT_NEEDED entries = {len(needed)} {needed}')
    log(f'    /init {len(init)} bytes sha256={init_sha}')
    if 'ARM aarch64' not in f_type or 'statically linked' not in f_type:
        raise SystemExit('/init is not a static aarch64 ELF')
    if elf_type != 'EXEC' or machine != 'AArch64' or interp or needed:
        raise SystemExit('/init has an interpreter or a DT_NEEDED entry')
    if RAMDISK_REASON not in init:
        raise SystemExit("the literal 'bootloader\\0' is not in the shipped binary")

    src = INIT_SRC.read_text()
    stripped = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    stripped = re.sub(r'//[^\n]*', ' ', stripped)
    stripped = re.sub(r'"(?:[^"\\]|\\.)*"', '""', stripped)
    forbidden = sorted(set(re.findall(
        r'\b(_exit|_Exit|exit|abort|quick_exit|at_quick_exit|pthread_exit)\s*\(',
        stripped)))
    log(f'    source grep (comments/strings stripped) for '
        f'exit/_exit/abort/quick_exit: {forbidden or "none"}')
    if forbidden:
        raise SystemExit(f'forbidden calls in the source: {forbidden}')
    (WORK / 'init-sonda-bootloader-source-stripped.txt').write_text(stripped)

    # -s strips the symbol table, so main() is invisible in the shipped file:
    # rebuild the same source without -s and prove it is the same code before
    # reading the disassembly below.
    dbg = WORK / 'init-sonda-bootloader.unstripped'
    ret = subprocess.run([CC, *[f for f in CFLAGS if f != '-s'], '-o', str(dbg),
                          str(INIT_SRC)], capture_output=True, text=True)
    if ret.returncode != 0:
        raise SystemExit(f'unstripped rebuild failed:\n{ret.stderr}')

    def text_sha(path):
        raw = subprocess.run(['aarch64-linux-gnu-objcopy', '-O', 'binary',
                              '--only-section=.text', str(path), '/dev/stdout'],
                             capture_output=True, check=True).stdout
        return sha256_bytes(raw), len(raw)

    def alloc_sections(path):
        out = subprocess.run(['aarch64-linux-gnu-readelf', '-S', '-W', str(path)],
                             capture_output=True, text=True, check=True).stdout
        secs = []
        for line in out.splitlines():
            m = re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+'
                         r'([0-9a-f]+)\s+([0-9a-f]+)\s+\S+\s+(\S*)\s', line)
            if m and 'A' in m.group(6) and m.group(2) != 'NOBITS':
                secs.append((m.group(1), int(m.group(4), 16), int(m.group(5), 16)))
        return secs

    t_shipped, t_len = text_sha(INIT_BIN)
    t_dbg, _ = text_sha(dbg)
    raw_ship = INIT_BIN.read_bytes()
    raw_dbg = dbg.read_bytes()
    diffs = [i for i in range(min(len(raw_ship), len(raw_dbg)))
             if raw_ship[i] != raw_dbg[i]]
    e_shoff = struct.unpack_from('<Q', raw_ship, 0x28)[0]
    exempt = [r for r in alloc_sections(INIT_BIN) if 'build-id' in r[0]]
    alloc_ranges = [(s, s + n) for name, s, n in alloc_sections(INIT_BIN)
                    if 'build-id' not in name]
    inside_alloc = [i for i in diffs
                    if any(s <= i < e for s, e in alloc_ranges)]
    log(f'    .text section: {t_len} bytes sha256={t_shipped}')
    log(f'    shipped vs unstripped rebuild: {len(diffs)} differing bytes, '
        f'confined to the ELF header (e_shoff={e_shoff}), the build-id note '
        f'{exempt} and the section name table: {not inside_alloc}; differing '
        f'bytes inside any other allocated section: {len(inside_alloc)}')
    if t_shipped != t_dbg or inside_alloc:
        raise SystemExit('the unstripped rebuild is not the same code as the '
                         'shipped /init: the disassembly below would be meaningless')

    dis = subprocess.run(['aarch64-linux-gnu-objdump', '-d', '--disassemble=main',
                          str(dbg)], capture_output=True, text=True)
    if dis.returncode != 0:
        raise SystemExit(f'objdump failed:\n{dis.stderr}')
    dis = dis.stdout
    (WORK / 'objdump-main-sonda-bootloader.txt').write_text(dis)
    cannot_return, detail = main_cannot_return(dis)
    log(f'    objdump main(): {len(dis.splitlines())} lines -> '
        f'work/objdump-main-sonda-bootloader.txt')
    log(f'    main() cannot return to the C runtime (which would call exit(3)): '
        f'{cannot_return} [{detail}]')
    if not cannot_return:
        raise SystemExit('main() could return: PID 1 might die')

    # ---- the reboot call itself, from the disassembly ---------------------
    ok_call, detail_call, line_call = prove_reboot_call(
        dis, init, nr_reboot, magic1, magic2, cmd_restart2, RAMDISK_REASON,
        'shipped /init', expect_magic=True)
    log()
    log('[6] disassembly proof of the restart2 call (main(), shipped /init)')
    log(f'    {line_call}')
    log(f'    verdict: {ok_call} [{detail_call}]')
    if not ok_call:
        raise SystemExit('the disassembly does not show a RESTART2 reboot with '
                         f'the bootloader reason: {detail_call}')

    # ---- lz4 self-test: reproduce probe6 exactly -------------------------
    lz4 = None
    if P6_CPIO.exists():
        lz4 = subprocess.run(['lz4', '--version'], capture_output=True, text=True,
                             check=True).stdout.strip().splitlines()[0]
        tmp = WORK / 'p6-selftest.lz4'
        subprocess.run([*LZ4_ARGS, '-f', str(P6_CPIO), str(tmp)], check=True)
        got = sha256_file(tmp)
        log()
        log('[7] lz4 self-test against the frame measured to boot Android')
        log(f'    {lz4}')
        log(f'    {" ".join(LZ4_ARGS)}  {P6_CPIO}  -> {tmp.stat().st_size} bytes '
            f'sha256={got}')
        log(f'    probe6 ramdisk in the booted image: {P6_LZ4_SHA}')
        log('    identical: this is the exact compressor of the probe6 image'
            if got == P6_LZ4_SHA else
            '    NOTE: does not reproduce the probe6 frame byte-for-byte '
            '(recorded for comparison; the round trip below still proves the '
            'frame decodes to the intended cpio)')
    else:
        log()
        log(f'[7] lz4 self-test skipped ({P6_CPIO} absent)')

    log()
    log('[8] ramdisk v7 = v6 ramdisk (66 entries: 65 + TRAILER!!!) with /init '
        'replaced by the probe')
    v7_cpio, vend, _ = splice_entry(v6, 'v6 ramdisk', 'init', init, '0o100755')
    (WORK / 'v7-ramdisk.cpio').write_bytes(v7_cpio)
    log(f'    cpio {len(v7_cpio)} bytes sha256={sha256_bytes(v7_cpio)} '
        f'(v6 was {len(v6)})')
    log(f'    prefix bytes 0..{vend["hdr"]} and the suffix after the init entry '
        f'are byte-identical to the v6 ramdisk')

    log()
    log('[9] ramdisk v7b = Android ramdisk (30 entries) with its own /init replaced')
    v7b_cpio, aend, _ = splice_entry(android, 'android ramdisk', 'init', init,
                                     '0o100750')
    (WORK / 'v7b-ramdisk.cpio').write_bytes(v7b_cpio)
    log(f'    cpio {len(v7b_cpio)} bytes sha256={sha256_bytes(v7b_cpio)} '
        f'(android was {len(android)})')
    log(f'    Android /init was {aend["size"]} bytes (mode {oct(aend["mode"])}), '
        f'probe is {len(init)} bytes (same mode)')

    log()
    log('[10] lz4 -l legacy compression + round trip')
    ramdisks = {}
    for label, cpio, cpio_path in (('v7', v7_cpio, WORK / 'v7-ramdisk.cpio'),
                                   ('v7b', v7b_cpio, WORK / 'v7b-ramdisk.cpio')):
        raw = compress_lz4_legacy(cpio_path, WORK / f'{label}-ramdisk.lz4')
        (OUT / f'{label}-ramdisk.lz4').write_bytes(raw)
        back = subprocess.run(['lz4', '-dc', str(WORK / f'{label}-ramdisk.lz4')],
                              capture_output=True, check=True).stdout
        if back != cpio:
            raise SystemExit(f'{label}: lz4 round trip does not reproduce the cpio')
        magic = raw[:4].hex()
        if raw[:4] != bytes.fromhex('02214c18'):
            raise SystemExit(f'{label}: not an lz4 legacy frame ({magic})')
        ents, end = parse_cpio(back, f'{label} round trip')
        init_ent = [e for e in ents if e['name'] == 'init'][0]
        if init_ent['payload'] != init:
            raise SystemExit(f'{label}: /init in the round-tripped archive differs')
        ramdisks[label] = raw
        log(f'    {label}: cpio {len(cpio)} -> lz4 {len(raw)} bytes '
            f'(legacy magic {magic}, level 9)')
        log(f'        round trip OK: decompresses to the exact cpio, '
            f'{len(ents)} entries, /init sha256={init_sha[:16]}')
        log(f'        sha256={sha256_bytes(raw)}')

    log()
    log('[11] container graft (in-place patch of the Magisk image)')
    results = {}
    for label, ramdisk in (('v7', ramdisks['v7']), ('v7b', ramdisks['v7b'])):
        out_path = OUT / IMAGE_NAMES[label]
        build_image(ramdisk, out_path, label)
        res = check_image(out_path, ramdisk, label)
        results[label] = res
        log(f'    {label}: {out_path}')
        log(f'        {res["bytes"]} bytes sha256={res["sha256"]}')
        log(f'        header: kernel_size={res["header"]["kernel_size"]} '
            f'ramdisk_size={res["header"]["ramdisk_size"]} '
            f'signature_size={res["header"]["signature_size"]} '
            f'header_version={res["header"]["header_version"]}')
        log(f'        kernel byte-identical: {res["kernel_identical"]}')
        log(f'        ramdisk stored as intended: {res["ramdisk_identical"]}')
        log(f'        tail identical at the same absolute offsets: '
            f'{res["tail_identical_absolute"]} ({res["tail_bytes"]} bytes)')
        log(f'        bytes differing from the source container: {res["diff_count"]} '
            f'(min {res.get("diff_min")} max {res.get("diff_max")}), none outside '
            f'the size field / ramdisk region')
        if res['leftover_old_ramdisk_bytes']:
            log(f'        NOT overwritten: {res["leftover_old_ramdisk_bytes"]} bytes '
                f'of the PREVIOUS ramdisk between the new ramdisk end and the old '
                f'one (all-zero: {res["leftover_all_zero"]}); outside the declared '
                f'ramdisk_size, so the kernel never reads them')

    magisk_bytes = MAGISK.read_bytes()
    images_manifest = {}
    for k, v in results.items():
        cpio_path = WORK / f'{k}-ramdisk.cpio'
        cpio_bytes = cpio_path.read_bytes()
        images_manifest[k] = dict(
            name=IMAGE_NAMES[k], path=v['path'], bytes=v['bytes'],
            sha256=v['sha256'], header=v['header'],
            kernel_identical=v['kernel_identical'],
            ramdisk_bytes=len(ramdisks[k]),
            ramdisk_sha256=sha256_bytes(ramdisks[k]),
            ramdisk_lz4_offset=RAMDISK_OFF,
            ramdisk_cpio_bytes=len(cpio_bytes),
            ramdisk_cpio_sha256=sha256_bytes(cpio_bytes),
            init_sha256=init_sha, init_bytes=len(init),
            tail_identical_absolute=v['tail_identical_absolute'],
            diff_count=v['diff_count'],
            leftover_old_ramdisk_bytes=v['leftover_old_ramdisk_bytes'])

    manifest = dict(
        purpose="prove from the host that the probe ran: reboot with "
                "LINUX_REBOOT_CMD_RESTART2 reason='bootloader' -> ABL is "
                "expected to present fastboot (18d1:d00d)",
        built_by='build-probe-v7bl.py',
        differs_from='probe-v7-sonda (same container/ramdisks/init, only the '
                     'reboot command and reason differ)',
        toolchain=dict(cc=cc_ver, cflags=CFLAGS, source=str(INIT_SRC)),
        init=dict(bytes=len(init), sha256=init_sha, file=f_type,
                  elf_type=elf_type, machine=machine,
                  pt_interp=len(interp), dt_needed=len(needed),
                  disasm_reboot_call=line_call,
                  disasm_check_pass=ok_call),
        reboot=dict(syscall_nr=nr_reboot, magic1=magic1, magic2=magic2,
                    cmd_restart2=cmd_restart2, reason='bootloader',
                    reason_bytes=RAMDISK_REASON.hex(),
                    header=str(reboot_h),
                    semantics='host sees the phone come up as USB 18d1:d00d '
                              '(fastboot) if this unit\'s ABL honours the '
                              'reason -- HYPOTHESIS, not proven offline'),
        container=dict(path=str(MAGISK), sha256=MAGISK_SHA,
                       kernel_offset=KERNEL_OFF, ramdisk_offset=RAMDISK_OFF,
                       full_size=FULL_SIZE, signature_size=4096,
                       header_version=4, kernel_size=49108324,
                       kernel_sha256=sha256_bytes(
                           magisk_bytes[KERNEL_OFF:KERNEL_OFF + 49108324]),
                       kernel_area_bytes=RAMDISK_OFF - KERNEL_OFF,
                       kernel_area_sha256=sha256_bytes(
                           magisk_bytes[KERNEL_OFF:RAMDISK_OFF]),
                       ramdisk_original_bytes=struct.unpack_from(
                           '<I', magisk_bytes, 12)[0]),
        ramdisk_sources=dict(
            v6=dict(path=str(V6_RAMDISK_GZ), gz_bytes=len(v6_gz),
                    gz_sha256=sha256_bytes(v6_gz), cpio_bytes=len(v6),
                    cpio_sha256=sha256_bytes(v6), entries=len(v_entries)),
            android=dict(path=str(ANDROID_CPIO), cpio_bytes=len(android),
                         cpio_sha256=sha256_bytes(android),
                         entries=len(a_entries))),
        images=images_manifest,
        lz4=dict(args=LZ4_ARGS, version=lz4),
    )
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (OUT / 'build-probe-v7bl.log').write_text('\n'.join(log_lines) + '\n')
    log()
    log(f'manifest -> {OUT / "manifest.json"}')
    log(f'log      -> {OUT / "build-probe-v7bl.log"}')
    log()
    log('result:')
    for k in ('v7', 'v7b'):
        log(f'  {k:<4} {results[k]["path"]}')
        log(f'       sha256={results[k]["sha256"]}')
    log()
    log('next: verify-probe-v7bl.py, then qemu-probe-v7bl.py (no phone involved)')


if __name__ == '__main__':
    sys.exit(main())
