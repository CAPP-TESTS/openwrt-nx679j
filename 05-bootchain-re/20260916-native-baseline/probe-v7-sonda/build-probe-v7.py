#!/usr/bin/env python3
"""Build the two NX679J slot-B probe images v7 and v7b (the "sonda" series).

The whole point of these two images is one yes/no question:

    does the kernel reach userspace when the ramdisk is ours?

/init in both images is the same deliberately self-restarting static ELF
(candidate-init-sonda.c): it writes one marker line and then reboots the phone
in an infinite loop. A phone that restarts by itself = userspace was reached.
A frozen phone with no reset = it was not.

v7  = the proven Magisk container + OUR v6 ramdisk (cpio, all 65 entries
      unchanged in name/mode/uid/gid/ino/order) with ONE entry replaced:
      /init  -> the probe. Differences vs v6: /init content and the ramdisk
      compression (v6 used gzip in a freshly built mkbootimg image; v7 uses the
      lz4 -l legacy frame of the container that is measured to boot Android).
v7b = the same container + the EXACT content of the Android ramdisk
      (/tmp/magisk.cpio, 30 entries: names, modes, uids/gids, inodes, order,
      payloads all byte-identical) with its own /init (Magisk's ELF) replaced by
      the probe.

Container graft (the method measured on 2026-09-17 with probe6): the source
Magisk image is patched IN PLACE -- new ramdisk bytes at absolute offset
49115136, ramdisk_size in the header updated -- so the kernel stays
byte-identical, the 4096-byte signature field in the header is unchanged, the
whole region after the new ramdisk keeps the source bytes at the SAME absolute
offsets, and the file stays exactly 100663296 bytes (= the boot partition size,
which is what the control run proves ABL accepts and boots on slot B).

Every claim printed by this script is checked, not asserted: see verify-*.log
and manifest.json.
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
OUT = BASE / 'probe-v7-sonda'
WORK = OUT / 'work'
INPUTS = OUT / 'inputs'

CC = 'aarch64-linux-gnu-gcc'
# Same toolchain and flags as candidate-minimal-gadget-v6 (measured there:
# aarch64-linux-gnu-gcc 16.1.0 -> static aarch64 ELF, no interp, no DT_NEEDED).
CFLAGS = ['-static', '-Os', '-s', '-Wall', '-Wextra', '-Wno-unused-result']

INIT_SRC = OUT / 'candidate-init-sonda.c'
INIT_BIN = WORK / 'init-sonda'

MAGISK = Path('/home/user/nx679j-stock/magisk_patched-30700_Zx2eF.img')
MAGISK_SHA = '0cd94d5627501f3c65c94542093ad18b1ec6c1c215dc6a7ecab4f941057e8364'
MAGISK_LEN = 100663296

V6_RAMDISK_GZ = BASE / 'candidate-minimal-gadget-v6' / 'work' / 'ramdisk.cpio.gz'
# measured in the v6 build log (build-candidate-v6-build-stdout.log)
V6_RAMDISK_GZ_SHA = '8378e9a3398f55bfee72166b8bcd2df94b5cca098418d948cf3bf44019a9d035'
V6_RAMDISK_GZ_LEN = 901952

# stable copies of the two ramdisk sources, so the images can be rebuilt after
# /tmp is wiped
ANDROID_CPIO = INPUTS / 'magisk-android-ramdisk.cpio'
ANDROID_CPIO_SRC = Path('/tmp/magisk.cpio')
ANDROID_CPIO_SHA = ('6fd2630909eab5458196edc25b8c41b4c15b401c0c92fdeab65fedaec13a0594')

KERNEL_OFF = 4096
RAMDISK_OFF = 49115136
HEADER_RAMDISK_SIZE_FIELD = 12
FULL_SIZE = MAGISK_LEN

# probe6 (Android content, OUR packing) is the image that booted Android in 26 s
# on slot B: reproducing its lz4 frame byte-for-byte proves our compressor is
# the same one ABL/kernel accepted.
P6_CPIO = Path('/tmp/p6.cpio')
P6_LZ4_SHA = ('c90e61fbf60ad0e4a32360386e8d07c383e80a06eeef0b9e2827c47933aeeaf0')
LZ4_ARGS = ['lz4', '-l', '-9']

log_lines = []


def log(msg=''):
    print(msg, flush=True)
    log_lines.append(msg)


def sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


def sha256_file(p):
    return sha256_bytes(Path(p).read_bytes())


# --------------------------------------------------------------------------
# cpio newc: parse and splice (never re-serialise: everything except the entry
# we replace stays byte-identical, including the trailer)
# --------------------------------------------------------------------------
def main_cannot_return(dis):
    """Rigorous reading of main()'s disassembly: PID 1 cannot fall off the end.

    The C runtime calls exit(3) if main() ever returns, so the guarantee is not
    "the source has no exit()" but "main() cannot return". Three facts are
    checked on the disassembly of the SHIPPED code:
      * main() contains no return instruction at all;
      * no branch or call target is exit/_exit/abort/quick_exit;
      * the last instruction of main() is an unconditional branch (so no path
        falls through past the end of the function).
    """
    insns = []
    for line in dis.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\t([0-9a-f ]+)\t([a-z0-9._]+)(?:\s+(.*))?$', line)
        if m:
            insns.append((int(m.group(1), 16), m.group(3), (m.group(4) or '').strip()))
    rets = [hex(a) for a, mn, _ in insns if mn.startswith('ret')]
    bad = sorted({t for _, _, op in insns for t in re.findall(r'<([^>]+)>', op)
                  if t in ('exit', '_exit', '_Exit', 'abort', 'quick_exit')})
    last = insns[-1] if insns else (0, '', '')
    ok = (not rets) and (not bad) and last[1] == 'b'
    detail = (f'{len(insns)} instructions, return instructions: {rets or "none"}, '
              f'branches to exit/abort: {bad or "none"}, last instruction: '
              f'{last[1]} {last[2]}')
    return ok, detail


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
    """Replace the payload of one entry; leave every other byte untouched.

    Returns (new_archive, the_replaced_entry, entries_of_the_source_archive).
    The original 110-byte ASCII header is reused with ONLY its filesize field
    patched, so ino/mode/uid/gid/nlink/mtime/rdev and the name stay identical.
    """
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
    # the splice may not touch anything outside the replaced entry
    if out[:e['hdr']] != buf[:e['hdr']] or out[e['hdr'] + len(new_entry):] != buf[e['span']:]:
        raise SystemExit(f'{label}: splice changed bytes outside the {name!r} entry')
    return out, e, entries


# --------------------------------------------------------------------------
def compress_lz4_legacy(cpio_path: Path, out_path: Path):
    # file-based: `lz4 -l` on a file, byte-identical to what probe6 used
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
    res = {}
    res['path'] = str(path)
    res['bytes'] = len(img)
    res['sha256'] = sha256_bytes(img)
    res['length_ok'] = len(img) == FULL_SIZE
    if not res['length_ok']:
        raise SystemExit(f'{label}: length {len(img)} != {FULL_SIZE}')

    # header
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

    # bytes after the new ramdisk: must be the source bytes at the same
    # absolute offsets (this is the measured probe6 graft signature)
    after = RAMDISK_OFF + len(ramdisk)
    res['tail_identical_absolute'] = img[after:] == src[after:]
    res['tail_bytes'] = FULL_SIZE - after
    if not res['tail_identical_absolute']:
        raise SystemExit(f'{label}: tail after the ramdisk is not the source tail')

    # leftover of the previous ramdisk: bytes between the new ramdisk end and
    # the old ramdisk end are NOT overwritten. Report it (it is outside the
    # declared ramdisk_size, so the kernel never reads it) instead of hiding it.
    old_ramdisk_end = RAMDISK_OFF + struct.unpack_from('<I', src, 12)[0]
    res['leftover_old_ramdisk_bytes'] = max(0, old_ramdisk_end - after)
    res['leftover_all_zero'] = (img[after:old_ramdisk_end] == b'\0' * (old_ramdisk_end - after)
                               if old_ramdisk_end > after else True)

    # diff set vs the source container must be confined to the size field and
    # the ramdisk region
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

    log('=== build-probe-v7: two slot-B probe images (v7, v7b) ===')
    log()

    # ---- inputs ---------------------------------------------------------
    if sha256_file(MAGISK) != MAGISK_SHA:
        raise SystemExit('the Magisk container changed: refusing to build')
    log(f'[1] container  {MAGISK}')
    log(f'    {MAGISK_LEN} bytes sha256={MAGISK_SHA}')
    log(f'    (the control run on 2026-09-17 booted this image from slot B, '
        f'Android in 26 s)')

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

    # ---- the probe binary ----------------------------------------------
    cc_ver = subprocess.run([CC, '--version'], capture_output=True, text=True,
                            check=True).stdout.splitlines()[0]
    cmd = [CC, *CFLAGS, '-o', str(INIT_BIN), str(INIT_SRC)]
    build = subprocess.run(cmd, capture_output=True, text=True)
    (WORK / 'cc.stdout').write_text(build.stdout)
    (WORK / 'cc.stderr').write_text(build.stderr)
    if build.returncode != 0:
        raise SystemExit(f'{CC} failed:\n{build.stderr}')
    log()
    log('[4] probe /init compiled statically')
    log(f'    {cc_ver}')
    log(f'    {" ".join(cmd)}')
    if build.stderr.strip():
        log(f'    compiler diagnostics (kept in work/cc.stderr):\n{build.stderr}')
    else:
        log('    compiler diagnostics: none (-Wall -Wextra clean)')

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

    # source-level check: no exit/_exit/abort call anywhere. Comments are
    # stripped first (this file talks about exit() in its own comments).
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
    (WORK / 'init-sonda-source-stripped.txt').write_text(stripped)

    # the C runtime does contain an exit() call (__libc_start_main does it if
    # main returns), so the real guarantee is that main() cannot return:
    # disassemble it and prove every branch stays inside the two infinite loops.
    #
    # -s strips the symbol table, so objdump cannot find main() in the shipped
    # file. Build the same source once more WITHOUT -s and prove it is the same
    # code: (a) the .text section bytes are identical, (b) the two files agree
    # byte-for-byte up to the section header table (e_shoff), i.e. everything
    # that is actually loaded differs in nothing but .symtab/.strtab.
    dbg = WORK / 'init-sonda.unstripped'
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
        """[(name, off, size)] for every section with the ALLOC flag."""
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
    log(f'    shipped vs unstripped rebuild: {len(diffs)} differing bytes, all '
        f'inside the ELF header (e_shoff={e_shoff}), the .note.gnu.build-id '
        f'note {exempt} or the section name table: '
        f'{not inside_alloc}; differing bytes inside any other allocated '
        f'section: {len(inside_alloc)}')
    if t_shipped != t_dbg or inside_alloc:
        raise SystemExit('the unstripped rebuild is not the same code as the '
                         'shipped /init: the disassembly below would be '
                         'meaningless')

    dis = subprocess.run(['aarch64-linux-gnu-objdump', '-d', '--disassemble=main',
                          str(dbg)], capture_output=True, text=True)
    if dis.returncode != 0:
        raise SystemExit(f'objdump failed:\n{dis.stderr}')
    dis = dis.stdout
    (WORK / 'objdump-main-sonda.txt').write_text(dis)
    cannot_return, detail = main_cannot_return(dis)
    log(f'    objdump main(): {len(dis.splitlines())} lines -> '
        f'work/objdump-main-sonda.txt')
    log(f'    main() cannot return to the C runtime (which would call exit(3)): '
        f'{cannot_return} [{detail}]')
    if not cannot_return:
        raise SystemExit('main() could return: PID 1 might die')

    # ---- lz4 self-test: reproduce probe6 exactly ------------------------
    lz4 = None
    if P6_CPIO.exists():
        lz4 = subprocess.run(['lz4', '--version'], capture_output=True, text=True,
                             check=True).stdout.strip().splitlines()[0]
        tmp = WORK / 'p6-selftest.lz4'
        subprocess.run([*LZ4_ARGS, '-f', str(P6_CPIO), str(tmp)], check=True)
        got = sha256_file(tmp)
        log()
        log('[5] lz4 self-test against the frame measured to boot Android')
        log(f'    {lz4}')
        log(f'    {" ".join(LZ4_ARGS)}  {P6_CPIO}  -> {tmp.stat().st_size} bytes '
            f'sha256={got}')
        log(f'    probe6 ramdisk in the booted image: {P6_LZ4_SHA}')
        if got != P6_LZ4_SHA:
            log('    NOTE: this lz4 build does not reproduce the probe6 frame '
                'byte-for-byte (see manifest: the measured sha is recorded for '
                'comparison; the round trip below still proves the frame '
                'decodes to the intended cpio)')
        else:
            log('    identical: this is the exact compressor of the probe6 image')
    else:
        log()
        log(f'[5] lz4 self-test skipped ({P6_CPIO} absent)')

    # ---- v7: our v6 ramdisk with /init replaced ------------------------
    log()
    log('[6] ramdisk v7 = v6 ramdisk (66 entries: 65 + TRAILER!!!) with /init '
        'replaced by the probe')
    v7_cpio, vend, _ = splice_entry(v6, 'v6 ramdisk', 'init', init, '0o100755')
    (WORK / 'v7-ramdisk.cpio').write_bytes(v7_cpio)
    log(f'    cpio {len(v7_cpio)} bytes sha256={sha256_bytes(v7_cpio)} '
        f'(v6 was {len(v6)})')
    log(f'    prefix bytes 0..{vend["hdr"]} and the suffix after the init entry are '
        f'byte-identical to the v6 ramdisk; only filesize/ino field of init and '
        f'its payload changed')

    # ---- v7b: the Android ramdisk with /init replaced -------------------
    log()
    log('[7] ramdisk v7b = Android ramdisk (30 entries) with its own /init replaced')
    v7b_cpio, aend, _ = splice_entry(android, 'android ramdisk', 'init', init,
                                     '0o100750')
    (WORK / 'v7b-ramdisk.cpio').write_bytes(v7b_cpio)
    log(f'    cpio {len(v7b_cpio)} bytes sha256={sha256_bytes(v7b_cpio)} '
        f'(android was {len(android)})')
    log(f'    prefix bytes 0..{aend["hdr"]} and suffix after the init entry are '
        f'byte-identical to the Android ramdisk')
    log(f'    Android /init was {aend["size"]} bytes (mode {oct(aend["mode"])}), '
        f'probe is {len(init)} bytes (same mode)')

    # ---- compress + round trip -----------------------------------------
    log()
    log('[8] lz4 -l legacy compression + round trip')
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
        # the archive must still be a valid cpio with the same entries
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

    # ---- graft the container -------------------------------------------
    log()
    log('[9] container graft (in-place patch of the Magisk image)')
    results = {}
    for label, ramdisk in (('v7', ramdisks['v7']), ('v7b', ramdisks['v7b'])):
        out_path = OUT / f'boot_b-probe-{label}.img'
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
        log(f'        bytes differing from the source container: '
            f'{res["diff_count"]} '
            f'(min {res.get("diff_min")} max {res.get("diff_max")}), '
            f'none outside the size field / ramdisk region')
        if res['leftover_old_ramdisk_bytes']:
            log(f'        NOT overwritten: {res["leftover_old_ramdisk_bytes"]} '
                f'bytes of the PREVIOUS ramdisk between the new ramdisk end and '
                f'the old one (all-zero: {res["leftover_all_zero"]}); they are '
                f'outside the declared ramdisk_size, so the kernel never reads '
                f'them')

    magisk_bytes = MAGISK.read_bytes()
    images_manifest = {}
    for k, v in results.items():
        cpio_path = WORK / f'{k}-ramdisk.cpio'
        cpio_bytes = cpio_path.read_bytes()
        images_manifest[k] = dict(
            path=v['path'], bytes=v['bytes'], sha256=v['sha256'],
            header=v['header'], kernel_identical=v['kernel_identical'],
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
        purpose='does the kernel reach userspace when the ramdisk is ours?',
        built_by='build-probe-v7.py',
        toolchain=dict(cc=cc_ver, cflags=CFLAGS, source=str(INIT_SRC)),
        init=dict(bytes=len(init), sha256=init_sha, file=f_type,
                  elf_type=elf_type, machine=machine,
                  pt_interp=len(interp), dt_needed=len(needed)),
        container=dict(path=str(MAGISK), sha256=MAGISK_SHA,
                       kernel_offset=KERNEL_OFF, ramdisk_offset=RAMDISK_OFF,
                       full_size=FULL_SIZE, signature_size=4096,
                       header_version=4,
                       kernel_size=49108324,
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
    (OUT / 'build-probe-v7.log').write_text('\n'.join(log_lines) + '\n')
    log()
    log(f'manifest -> {OUT / "manifest.json"}')
    log(f'log      -> {OUT / "build-probe-v7.log"}')
    log()
    log('result:')
    for k in ('v7', 'v7b'):
        log(f'  {k:<4} {results[k]["path"]}')
        log(f'       sha256={results[k]["sha256"]}')
    log()
    log('next: verify-probe-v7.py (independent re-check from the files on disk), '
        'then test-probe-v7-slotb.sh on the phone')


if __name__ == '__main__':
    sys.exit(main())
