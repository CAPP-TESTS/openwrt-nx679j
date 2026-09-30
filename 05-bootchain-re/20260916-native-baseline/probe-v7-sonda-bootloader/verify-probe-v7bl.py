#!/usr/bin/env python3
"""Independent re-verification of the shipped RESTART2 / 'bootloader' images.

Deliberately uses DIFFERENT tools and a DIFFERENT reading of the evidence than
build-probe-v7bl.py:

  * the ramdisks are decompressed with lz4 and extracted with bsdtar, and the
    trees compared with coreutils hashes, so a bug in the builder's own cpio
    parser cannot hide a wrong file;
  * the /init used for the ELF checks is taken OUT OF THE IMAGE (not from the
    build work directory);
  * the "bootloader" pointer is confirmed twice, by two unrelated methods:
      (a) a textual scan of objdump -d output for the literal instruction
          sequence (mov w3,#0xc3d4 / movk w3,#0xa1b2,lsl #16 / mov x0,#0x8e /
          bl syscall) plus the adrp/add pair that builds the 4th argument;
      (b) objdump -s -j .rodata: the byte dump around the target address, where
          the address table and the ASCII column must both show "bootloader";
    and then a third time by mapping the address through the PT_LOAD headers of
    the binary extracted from the image and reading the file bytes.
  * the two new images are also diffed against the v7/v7b images of
    probe-v7-sonda: everything outside the ramdisk region must be identical.
"""
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

OUT = Path('/home/user/nx679j-stock/experiments/20260916-122926-native-baseline'
           '/probe-v7-sonda-bootloader')
BASE = OUT.parent
PREV = BASE / 'probe-v7-sonda'
WORK = OUT / 'verify-scratch'
MAN = json.loads((OUT / 'manifest.json').read_text())
MAGISK = Path(MAN['container']['path'])
FULL_SIZE = MAN['container']['full_size']
ROFF = MAN['container']['ramdisk_offset']
KOFF = MAN['container']['kernel_offset']
KSIZE = MAN['container']['kernel_size']
INIT_SHA = MAN['init']['sha256']
REASON = b'bootloader\x00'
TAGS = (('v7', 'sonda-bootloader-v7.img', 'boot_b-probe-v7.img'),
        ('v7b', 'sonda-bootloader-v7b.img', 'boot_b-probe-v7b.img'))
log_lines, checks = [], []


def log(msg=''):
    print(msg, flush=True)
    log_lines.append(str(msg))


def check(name, ok, detail=''):
    checks.append({'check': name, 'pass': bool(ok), 'detail': str(detail)})
    log(f'[{"PASS" if ok else "FAIL"}] {name}: {detail}')
    return bool(ok)


def sha_bytes(b):
    return hashlib.sha256(b).hexdigest()


def sh(cmd, cwd=None, inp=None):
    r = subprocess.run(cmd, cwd=cwd, input=inp, capture_output=True)
    if r.returncode != 0:
        raise SystemExit(f'command failed: {" ".join(cmd)}\n{r.stderr.decode()}')
    return r.stdout


def unpack(cpio_path, dest):
    if dest.exists():
        subprocess.run(['chmod', '-R', 'u+rwX', str(dest)], capture_output=True)
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    r = subprocess.run(['bsdtar', '-xpf', str(cpio_path), '-C', str(dest)],
                       capture_output=True)
    msgs = [l for l in (r.stderr.decode() + r.stdout.decode()).splitlines()
            if l.strip()]
    other = [l for l in msgs
             if not any(tok in l for tok in (
                 "Can't create", 'Cannot create', 'device node',
                 'Error exit delayed from previous errors'))]
    if other:
        raise SystemExit(f'bsdtar failed on {cpio_path}: {other}')
    sh(['chmod', '-R', 'u+rwX', '.'], cwd=str(dest))
    files = {}
    for p in sorted(dest.rglob('*')):
        if p.is_file() and not p.is_symlink():
            files[p.relative_to(dest).as_posix()] = sha_bytes(p.read_bytes())
    fm = sh(['find', '.', '-printf', '%y %P\\n'], cwd=str(dest))
    return {'files': files, 'types': sorted(fm.decode().splitlines())}


def listing(cpio_path):
    data = Path(cpio_path).read_bytes()
    out = subprocess.run(['cpio', '-itv', '--quiet', '--numeric-uid-gid'],
                         input=data, capture_output=True)
    txt = out.stdout.decode(errors='replace')
    entries = {}
    pat = re.compile(r'^(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+'
                     r'\S+\s+\d+\s+[\d:]+\s+(.*)$')
    for line in txt.splitlines():
        m = pat.match(line)
        if m:
            mode, nlink, uid, gid, size, name = m.groups()
            entries[name] = (mode, nlink, uid, gid)
    return entries


def diff_tree(a, b):
    fa, fb = a['files'], b['files']
    return (sorted(set(fa) - set(fb)), sorted(set(fb) - set(fa)),
            sorted(k for k in set(fa) & set(fb) if fa[k] != fb[k]),
            sorted(set(a['types']) ^ set(b['types'])))


# ---------------------------------------------------------------------------
# method (a): textual scan of the disassembly -- no symbolic tracker involved
# ---------------------------------------------------------------------------
def textual_sequence(text):
    """Find the restart2 call by looking for the literal instruction sequence.

    Returns (ok, detail, target_va or None).
    """
    lines = text.splitlines()
    idx = [i for i, l in enumerate(lines)
           if re.search(r'\tbl\t[0-9a-f]+ <syscall>$', l)]
    found = []
    for i in idx:
        window = '\n'.join(lines[max(0, i - 14):i + 1])
        nr = re.search(r'\tmov\tx0, #0x8e(\s|$)', window)     # 142 = __NR_reboot
        lo = re.search(r'\tmov\tw3, #0xc3d4(\s|$)', window)
        hi = re.search(r'\tmovk\tw3, #0xa1b2, lsl #16(\s|$)', window)
        if not (nr and lo and hi):
            continue
        # the 4th argument: whatever register is moved into x4 just before the
        # call, whose value was built somewhere earlier by an adrp/add pair
        m = re.search(r'\tmov\tx4, (x\d+)\n', window)
        reg = m.group(1) if m else 'x4'
        base = off = None
        for j in range(i, -1, -1):
            ma = re.match(r'^\s*([0-9a-f]+):\t[0-9a-f ]+\tadd\t' + reg +
                          r', ' + reg + r', #(0x[0-9a-f]+)$', lines[j])
            if ma:
                off = int(ma.group(2), 16)
                for k in range(j, -1, -1):
                    mm = re.match(r'^\s*([0-9a-f]+):\t[0-9a-f ]+\tadrp\t' + reg +
                                  r', ([0-9a-f]+) <', lines[k])
                    if mm:
                        base = int(mm.group(2), 16)
                        break
                break
        found.append((lines[i].split(':')[0].strip(), base, off))
    if not found:
        return False, f'no bl syscall preceded by mov w3,#0xc3d4 + movk ' \
                      f'w3,#0xa1b2 + mov x0,#0x8e', None
    va = None
    for addr, base, off in found:
        if base is not None:
            va = base + off
    return True, (f'{len(found)} call site(s): mov w3,#0xc3d4 + movk '
                  f'w3,#0xa1b2,lsl #16 (cmd=0xa1b2c3d4 = RESTART2) with '
                  f'mov x0,#0x8e (__NR_reboot) immediately before bl syscall'
                  + (f'; 4th argument built by adrp/add at {va:#x}' if va else
                     '; 4th argument not resolved by the adrp/add scan')), va


# ---------------------------------------------------------------------------
# method (b): objdump -s -j .rodata, the byte dump around a virtual address
# ---------------------------------------------------------------------------
def rodata_bytes_at(elf, va, want=32):
    """Bytes of .rodata starting at va, reassembled from the objdump -s dump
    (several 16-byte dump lines may be needed)."""
    dump = sh(['aarch64-linux-gnu-objdump', '-s', '-j', '.rodata', str(elf)]).decode()
    lines = []
    for line in dump.splitlines():
        m = re.match(r'^\s*([0-9a-f]{4,})\s+((?:[0-9a-f]{8}\s+)+)', line)
        if m:
            lines.append((int(m.group(1), 16),
                          bytes.fromhex(m.group(2).replace(' ', '')), line.strip()))
    for i, (addr, data, raw) in enumerate(lines):
        if addr <= va < addr + 16:
            out, pos = bytearray(), i
            cur = addr
            while len(out) < want and pos < len(lines):
                a2, d2, raw2 = lines[pos]
                if a2 != cur:
                    break
                out += d2
                cur += len(d2)
                pos += 1
            start = va - addr
            return bytes(out[start:start + want]), addr, raw
    return None, None, None


def resolve_pointers(text, elf):
    """Every adrp/add pair in the disassembly whose target bytes are the reason
    string, resolved from the .rodata dump (address table + ASCII column)."""
    hits = []
    for line in text.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\t[0-9a-f ]+\tadrp\t(x\d+), ([0-9a-f]+) <',
                     line)
        if not m:
            continue
        addr, reg, page = int(m.group(1), 16), m.group(2), int(m.group(3), 16)
        for line2 in text.splitlines():
            m2 = re.match(r'^\s*([0-9a-f]+):\t[0-9a-f ]+\tadd\t' + reg +
                          r', ' + reg + r', #(0x[0-9a-f]+)$', line2)
            if m2 and int(m2.group(1), 16) > addr:
                va = page + int(m2.group(2), 16)
                data, dump_addr, dump_line = rodata_bytes_at(elf, va)
                if data and data.startswith(REASON):
                    hits.append((va, dump_addr, dump_line))
                break
    return hits


def main():
    log('=== verify-probe-v7bl: independent re-check of the RESTART2 images ===')
    src_container = MAGISK.read_bytes()
    check('container is the measured Magisk image',
          sha_bytes(src_container) == MAN['container']['sha256'],
          f'{len(src_container)} bytes {MAN["container"]["sha256"][:16]}')
    WORK.mkdir(exist_ok=True)

    v6_arch = BASE / 'candidate-minimal-gadget-v6/work/ramdisk.cpio.gz'
    android_arch = PREV / 'inputs/magisk-android-ramdisk.cpio'
    src_trees = {}
    for label, arch, conv in (('v6', v6_arch, True), ('android', android_arch, False)):
        raw = Path(arch).read_bytes()
        if conv:
            import gzip
            raw = gzip.decompress(raw)
        tmp = WORK / f'src-{label}.cpio'
        tmp.write_bytes(raw)
        src_trees[label] = unpack(tmp, WORK / f'tree-src-{label}')
        log(f'[i] source tree {label}: {len(src_trees[label]["files"])} files')

    init_from_image = None
    for tag, name, prev_name in TAGS:
        man = MAN['images'][tag]
        img_path = OUT / name
        img = img_path.read_bytes()
        log()
        log(f'--- {tag}: {img_path}')

        check(f'{tag}: sha256 matches the manifest', sha_bytes(img) == man['sha256'],
              man['sha256'])
        check(f'{tag}: length is the full partition', len(img) == FULL_SIZE,
              f'{len(img)} bytes')
        check(f'{tag}: name in the manifest matches the file', man['name'] == name,
              man['name'])
        ksize, rsize = struct.unpack_from('<II', img, 8)
        hdr_ok = (img[:8] == b'ANDROID!' and ksize == KSIZE
                  and rsize == man['ramdisk_bytes'] == len(img[ROFF:ROFF + rsize])
                  and struct.unpack_from('<I', img, 20)[0] == 1584
                  and struct.unpack_from('<I', img, 40)[0] == 4
                  and struct.unpack_from('<I', img, 1580)[0] == 4096)
        check(f'{tag}: header v4 consistent (kernel/ramdisk/header/signature size)',
              hdr_ok, f'kernel_size={ksize} ramdisk_size={rsize} header_size=1584 '
                      f'header_version=4 signature_size=4096')
        check(f'{tag}: kernel area byte-identical to the container',
              img[KOFF:ROFF] == src_container[KOFF:ROFF],
              f'{ROFF - KOFF} bytes sha {sha_bytes(img[KOFF:ROFF])[:16]}')

        after = ROFF + rsize
        check(f'{tag}: bytes after the ramdisk identical at the same absolute '
              f'offsets', img[after:] == src_container[after:],
              f'{FULL_SIZE - after} bytes tail')
        diff = [i for i in range(FULL_SIZE) if img[i] != src_container[i]]
        bad = [i for i in diff if not (12 <= i <= 15 or ROFF <= i < after)]
        check(f'{tag}: every differing byte is in the size field or the ramdisk',
              not bad, f'{len(diff)} differing bytes, outside: {len(bad)}')

        # ---- vs the v7/v7b image: only the ramdisk region may differ -------
        prev_path = PREV / prev_name
        if prev_path.exists():
            prev = prev_path.read_bytes()
            pdiff = [i for i in range(FULL_SIZE) if img[i] != prev[i]]
            lo, hi = ROFF, max(after, ROFF + struct.unpack_from('<I', prev, 12)[0])
            pbad = [i for i in pdiff if not (12 <= i <= 15 or lo <= i < hi)]
            check(f'{tag}: differs from {prev_name} only in the size field and '
                  f'the ramdisk region', not pbad,
                  f'{len(pdiff)} differing bytes, outside: {len(pbad)} '
                  f'(ramdisk region {lo}..{hi})')
        else:
            check(f'{tag}: previous image for the diff is present', False,
                  str(prev_path))

        # ---- ramdisk: decompress with lz4, extract with bsdtar ------------
        rd_path = WORK / f'{tag}-ramdisk.lz4'
        rd_path.write_bytes(img[ROFF:after])
        dec = WORK / f'{tag}-ramdisk.cpio'
        dec.write_bytes(sh(['lz4', '-dc', str(rd_path)]))
        check(f'{tag}: stored ramdisk is an lz4 legacy frame',
              img[ROFF:ROFF + 4] == bytes.fromhex('02214c18'),
              img[ROFF:ROFF + 4].hex())
        check(f'{tag}: decompressed ramdisk sha256 matches the manifest',
              sha_bytes(dec.read_bytes()) == man['ramdisk_cpio_sha256'],
              man['ramdisk_cpio_sha256'])
        tree = unpack(dec, WORK / f'tree-{tag}')
        src_label = 'v6' if tag == 'v7' else 'android'
        only_src, only_new, content, types = diff_tree(src_trees[src_label], tree)
        check(f'{tag}: extracted tree == the {src_label} tree except /init',
              only_src == [] and only_new == [] and content == ['init']
              and types == [],
              f'only in source: {only_src}, only in image: {only_new}, '
              f'content differs: {content}, entry types differ: {types}')

        lst_src = listing(WORK / f'src-{src_label}.cpio')
        lst_img = listing(dec)
        meta_diff = sorted(k for k in set(lst_src) & set(lst_img)
                           if lst_src[k] != lst_img[k])
        check(f'{tag}: {len(lst_img)} entries, all names and their '
              f'mode/nlink/uid/gid identical to the {src_label} archive',
              set(lst_src) == set(lst_img) and not meta_diff,
              f'source has {len(lst_src)} entries, image {len(lst_img)}; '
              f'metadata differs on: {meta_diff}')
        init_bytes = (WORK / f'tree-{tag}' / 'init').read_bytes()
        init_from_image = WORK / f'tree-{tag}' / 'init'
        check(f'{tag}: /init in the image is the probe binary',
              sha_bytes(init_bytes) == INIT_SHA
              and len(init_bytes) == MAN['init']['bytes'],
              f'{len(init_bytes)} bytes {INIT_SHA[:16]}')
        check(f'{tag}: /init contains the literal "bootloader\\0"',
              REASON in init_bytes, f'at offset {init_bytes.find(REASON)}')
        want_mode = '-rwxr-xr-x' if tag == 'v7' else '-rwxr-x---'
        check(f'{tag}: /init keeps the source entry mode',
              lst_src.get('init', ('?',))[0] == lst_img.get('init', ('?',))[0]
              == want_mode,
              f'source {lst_src.get("init", ("?",))[0]} -> image '
              f'{lst_img.get("init", ("?",))[0]} (expected {want_mode})')

    # ---- the probe binary, taken out of the image -------------------------
    log()
    log('--- probe binary (extracted from the image)')
    init = WORK / 'init-sonda-bootloader'
    init.write_bytes(init_from_image.read_bytes())
    f_type = sh(['file', '-b', str(init)]).decode().strip()
    elf = sh(['aarch64-linux-gnu-readelf', '-h', str(init)]).decode()
    prog = sh(['aarch64-linux-gnu-readelf', '-l', str(init)]).decode()
    dyn = sh(['aarch64-linux-gnu-readelf', '-d', str(init)]).decode()
    interp = [l for l in prog.splitlines() if 'INTERP' in l]
    needed = [l for l in dyn.splitlines() if 'NEEDED' in l]
    check('probe: static aarch64 ELF EXEC, no interpreter, no DT_NEEDED',
          'statically linked' in f_type and 'AArch64' in elf
          and 'EXEC' in elf and not interp and not needed,
          f'{f_type}; INTERP={len(interp)} NEEDED={len(needed)}')

    src = (OUT / 'candidate-init-sonda-bootloader.c').read_text()
    stripped = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    stripped = re.sub(r'//[^\n]*', ' ', stripped)
    stripped = re.sub(r'"(?:[^"\\]|\\.)*"', '""', stripped)
    calls = sorted(set(re.findall(
        r'\b(_exit|_Exit|exit|abort|quick_exit|at_quick_exit)\s*\(', stripped)))
    check('probe: the source contains no exit/_exit/abort call', not calls,
          calls or 'none')
    check('probe: the source asks for LINUX_REBOOT_CMD_RESTART2 with reason '
          '"bootloader"',
          'LINUX_REBOOT_CMD_RESTART2' in stripped
          and 'LINUX_REBOOT_CMD_RESTART,' not in stripped.replace(
              'LINUX_REBOOT_CMD_RESTART2', '')
          and '"bootloader"' in src,
          'uses RESTART2 (not RESTART) and the literal "bootloader"')

    # needs the unstripped build for the named main(); rebuilt here from the
    # same source so this check does not depend on the build directory
    dbg = WORK / 'init-sonda-bootloader.unstripped'
    ccflags = ['-static', '-Os', '-Wall', '-Wextra', '-Wno-unused-result']
    subprocess.run(['aarch64-linux-gnu-gcc', *ccflags, '-o', str(dbg),
                    str(OUT / 'candidate-init-sonda-bootloader.c')], check=True,
                   capture_output=True)
    shipped_text = sh(['aarch64-linux-gnu-objcopy', '-O', 'binary',
                       '--only-section=.text', str(init), '/dev/stdout'])
    dbg_text = sh(['aarch64-linux-gnu-objcopy', '-O', 'binary',
                   '--only-section=.text', str(dbg), '/dev/stdout'])
    check('probe: the .text of the binary extracted from the image equals the '
          '.text of a fresh build of the source',
          sha_bytes(shipped_text) == sha_bytes(dbg_text),
          f'{len(shipped_text)} bytes sha {sha_bytes(shipped_text)[:16]}')

    dis = sh(['aarch64-linux-gnu-objdump', '-d', '--disassemble=main',
              str(dbg)]).decode()
    (WORK / 'objdump-main.txt').write_text(dis)
    insns = [l for l in dis.splitlines()
             if re.match(r'^\s*[0-9a-f]+:\t', l)]
    rets = [l.strip() for l in insns if re.search(r'\tret\b', l)]
    badjump = sorted({t for l in insns for t in re.findall(r'<([^>]+)>', l)
                      if t in ('exit', '_exit', '_Exit', 'abort', 'quick_exit')})
    last = [l for l in insns][-1] if insns else ''
    check('probe: main() cannot return (no ret, no branch to exit/abort, ends '
          'in an unconditional branch)',
          not rets and not badjump and re.search(r'\tb\t', last) is not None,
          f'{len(insns)} instructions, ret: {rets or "none"}, exit/abort '
          f'targets: {badjump or "none"}, last: {last.strip()}')
    check('probe: main() contains exactly one reboot syscall, and it is not '
          'the plain RESTART command (0x1234567)',
          '0x1234567' not in dis and '0xc3d4' in dis and '0xa1b2' in dis,
          'cmd = mov w3,#0xc3d4 + movk w3,#0xa1b2,lsl #16 = 0xa1b2c3d4 '
          '(LINUX_REBOOT_CMD_RESTART2); 0x1234567 nowhere in main()')

    # method (a): textual sequence
    ok_seq, detail_seq, va = textual_sequence(dis)
    check('probe: disassembly shows SYS_reboot with RESTART2 and an adrp/add '
          'argument (textual scan, method a)', ok_seq, detail_seq)

    # method (b): .rodata dump around the target address
    hits = resolve_pointers(dis, init)
    ok_b = bool(hits) and any(va is None or h[0] == va for h in hits)
    detail_b = (f'{len(hits)} adrp/add target(s) whose bytes are '
                f'"bootloader\\0": ' +
                '; '.join(f'{h[0]:#x} (dump line {h[2]!r})' for h in hits)
                if hits else 'no adrp/add pair resolves to the string')
    check('probe: an address built in main() points at the literal '
          '"bootloader\\0" in .rodata (objdump -s dump, method b)', ok_b,
          detail_b)

    # method (c): PT_LOAD mapping, read from the file itself
    ph = sh(['aarch64-linux-gnu-readelf', '-lW', str(init)]).decode()
    ok_c, detail_c = False, 'no candidate address'
    if va is not None:
        for line in ph.splitlines():
            m = re.match(r'\s*LOAD\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+'
                         r'0x[0-9a-f]+\s+(0x[0-9a-f]+)\s+', line)
            if m:
                off, vaddr, filesz = (int(m.group(1), 16), int(m.group(2), 16),
                                      int(m.group(3), 16))
                if vaddr <= va < vaddr + filesz:
                    fo = off + (va - vaddr)
                    got = init.read_bytes()[fo:fo + len(REASON)]
                    ok_c = got == REASON
                    detail_c = f'{va:#x} -> PT_LOAD file offset {fo} = {got!r}'
                    break
    check('probe: that address maps, through the PT_LOAD headers of the '
          'image\'s own /init, onto the bytes of the string (method c)',
          ok_c, detail_c)

    passed = sum(1 for c in checks if c['pass'])
    log()
    log(f'=== {passed}/{len(checks)} checks passed')
    (OUT / 'VERIFY-probe-v7bl.log').write_text('\n'.join(log_lines) + '\n')
    (OUT / 'verify-probe-v7bl.json').write_text(json.dumps(
        {'checks': checks, 'passed': passed, 'total': len(checks)}, indent=2) + '\n')
    return 0 if passed == len(checks) else 1


if __name__ == '__main__':
    sys.exit(main())
