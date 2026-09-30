#!/usr/bin/env python3
"""Independent re-verification of the shipped v7 / v7b images.

Deliberately uses DIFFERENT tools than build-probe-v7.py: the ramdisks are
unpacked with GNU cpio and the trees are compared with coreutils hashes, so a
bug in the builder's own cpio parser cannot hide a wrong file in the images.

Checks per image:
  1. length == 100663296, i.e. the whole boot_b partition;
  2. boot header v4: kernel_size, ramdisk_size == the stored ramdisk, header
     size 1584, signature_size 4096;
  3. the kernel area is byte-identical to the Magisk container that ABL booted
     from slot B in the control run;
  4. everything after the new ramdisk is byte-identical to the container at the
     SAME absolute offsets (the graft signature measured on probe6);
  5. every byte that differs from the container sits either in the ramdisk_size
     header field or inside the new ramdisk;
  6. the stored ramdisk decompresses (lz4 -d, legacy frame) to a cpio archive
     whose extracted tree equals the source tree EXCEPT /init, which must be the
     probe binary (sha256 + mode);
  7. the probe binary is a static aarch64 ELF with no PT_INTERP, no DT_NEEDED.
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
           '/probe-v7-sonda')
BASE = OUT.parent
WORK = OUT / 'verify-scratch'
MAN = json.loads((OUT / 'manifest.json').read_text())
MAGISK = Path(MAN['container']['path'])
FULL_SIZE = MAN['container']['full_size']
ROFF = MAN['container']['ramdisk_offset']
KOFF = MAN['container']['kernel_offset']
KSIZE = MAN['container']['kernel_size']
INIT_SHA = MAN['init']['sha256']
log_lines, checks = [], []


def log(msg=''):
    print(msg)
    log_lines.append(str(msg))


def check(name, ok, detail=''):
    checks.append({'check': name, 'pass': bool(ok), 'detail': str(detail)})
    log(f'[{"PASS" if ok else "FAIL"}] {name}: {detail}')
    return bool(ok)


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


def sha_bytes(b):
    return hashlib.sha256(b).hexdigest()


def sh(cmd, cwd=None, inp=None):
    r = subprocess.run(cmd, cwd=cwd, input=inp, capture_output=True)
    if r.returncode != 0:
        raise SystemExit(f'command failed: {" ".join(cmd)}\n{r.stderr.decode()}')
    return r.stdout


def unpack(cpio_path, dest):
    """Extract a newc archive with bsdtar into a clean directory.

    bsdtar (libarchive) applies directory permissions AFTER filling them, which
    GNU cpio does not: the Android ramdisk ships .backup with mode 0000, so
    `cpio -idm` creates it empty and then cannot write its 3 files.
    Device nodes need CAP_MKNOD: libarchive reports them and continues, so those
    errors are tolerated here -- their metadata is compared separately through
    `cpio -itv` (see listing()).
    """
    if dest.exists():
        # a previous run may have left a mode-0000 .backup behind
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
    # .backup ships mode 0000: make the tree readable again so it can be walked.
    # File modes/uid/gid are verified separately with `cpio -itv` (listing()), so
    # this normalisation cannot hide a metadata difference.
    sh(['chmod', '-R', 'u+rwX', '.'], cwd=str(dest))
    files, meta = {}, {}
    for p in sorted(dest.rglob('*')):
        rel = p.relative_to(dest).as_posix()
        if p.is_symlink():
            meta[rel] = f'symlink {p.readlink()}'
        elif p.is_dir():
            meta[rel] = 'dir'
        else:
            meta[rel] = 'file'
            files[rel] = sha_bytes(p.read_bytes())
    fm = sh(['find', '.', '-printf', '%y %P\\n'], cwd=str(dest))
    types = sorted(l for l in fm.decode().splitlines())
    return {'files': files, 'kinds': meta, 'types': types,
            'node_errors': [l for l in msgs if "Can't create" in l]}


def listing(cpio_path):
    """Name -> (mode, nlink, uid, gid) from GNU cpio's own verbose listing.

    This is the check that also covers the device nodes (which cannot be
    extracted without root) and the uid/gid/mode of every entry.
    """
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
    """Return (only_in_a, only_in_b, content_diff, mode_diff)."""
    fa, fb = a['files'], b['files']
    only_a = sorted(set(fa) - set(fb))
    only_b = sorted(set(fb) - set(fa))
    content = sorted(k for k in set(fa) & set(fb) if fa[k] != fb[k])
    types = sorted(set(a['types']) ^ set(b['types']))
    return only_a, only_b, content, types


def main():
    log('=== verify-probe-v7: independent re-check with GNU cpio ===')
    src_container = MAGISK.read_bytes()
    check('container is the measured Magisk image',
          sha_bytes(src_container) == MAN['container']['sha256'],
          f'{len(src_container)} bytes {MAN["container"]["sha256"][:16]}')
    v6_trees_arch = BASE / 'candidate-minimal-gadget-v6/work/ramdisk.cpio.gz'
    android_arch = OUT / 'inputs/magisk-android-ramdisk.cpio'
    WORK.mkdir(exist_ok=True)

    src_trees = {}
    for label, arch, conv in (('v6', v6_trees_arch, True),
                              ('android', android_arch, False)):
        raw = Path(arch).read_bytes()
        if conv:
            import gzip
            raw = gzip.decompress(raw)
        tmp = WORK / f'src-{label}.cpio'
        tmp.write_bytes(raw)
        src_trees[label] = unpack(tmp, WORK / f'tree-src-{label}')
        log(f'[i] source tree {label}: {len(src_trees[label]["files"])} files, '
            f'{len(src_trees[label]["kinds"])} entries')

    for tag in ('v7', 'v7b'):
        man = MAN['images'][tag]
        img_path = Path(man['path'])
        img = img_path.read_bytes()
        log()
        log(f'--- {tag}: {img_path}')

        check(f'{tag}: sha256 matches the manifest', sha_bytes(img) == man['sha256'],
              man['sha256'])
        check(f'{tag}: length is the full partition', len(img) == FULL_SIZE,
              f'{len(img)} bytes')
        ksize, rsize = struct.unpack_from('<II', img, 8)
        hdr_ok = (img[:8] == b'ANDROID!' and ksize == KSIZE
                  and rsize == man['ramdisk_bytes'] == len(img[ROFF:ROFF + rsize])
                  and struct.unpack_from('<I', img, 20)[0] == 1584
                  and struct.unpack_from('<I', img, 40)[0] == 4
                  and struct.unpack_from('<I', img, 1580)[0] == 4096)
        check(f'{tag}: header v4 consistent (kernel/ramdisk/header/signature size)',
              hdr_ok, f'kernel_size={ksize} ramdisk_size={rsize} '
                      f'header_size=1584 header_version=4 signature_size=4096')
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

        # ---- ramdisk: decompress with lz4 and extract with cpio ----
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
        only_src, only_new, content, types = diff_tree(
            src_trees[src_label], tree)
        check(f'{tag}: extracted tree == the {src_label} tree except /init',
              only_src == [] and only_new == [] and content == ['init']
              and types == [],
              f'only in source: {only_src}, only in image: {only_new}, '
              f'content differs: {content}, entry types differ: {types}')

        # cpio's own verbose listing: covers the device nodes too, and the
        # mode/nlink/uid/gid of every entry (mknod cannot run unprivileged).
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
        check(f'{tag}: /init in the image is the probe binary',
              sha_bytes(init_bytes) == INIT_SHA and len(init_bytes) == MAN['init']['bytes'],
              f'{len(init_bytes)} bytes {INIT_SHA[:16]}')
        want_mode = '-rwxr-xr-x' if tag == 'v7' else '-rwxr-x---'
        m_src = lst_src.get('init', ('?',))[0]
        m_img = lst_img.get('init', ('?',))[0]
        check(f'{tag}: /init keeps the source entry mode',
              m_src == m_img == want_mode,
              f'source {m_src} -> image {m_img} (expected {want_mode})')

    # ---- the probe binary itself ----
    log()
    log('--- probe binary')
    init = WORK / 'init-sonda'
    init.write_bytes((WORK / 'tree-v7' / 'init').read_bytes())
    f_type = sh(['file', '-b', str(init)]).decode().strip()
    elf = sh(['aarch64-linux-gnu-readelf', '-h', str(init)]).decode()
    prog = sh(['aarch64-linux-gnu-readelf', '-l', str(init)]).decode()
    dyn = sh(['aarch64-linux-gnu-readelf', '-d', str(init)]).decode()
    interp = [l for l in prog.splitlines() if 'INTERP' in l]
    needed = [l for l in dyn.splitlines() if 'NEEDED' in l]
    check('probe: static aarch64 ELF, no interpreter, no DT_NEEDED',
          'statically linked' in f_type and 'AArch64' in elf
          and 'EXEC' in elf and not interp and not needed,
          f'{f_type}; INTERP={len(interp)} NEEDED={len(needed)}')
    src = (OUT / 'candidate-init-sonda.c').read_text()
    stripped = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    stripped = re.sub(r'//[^\n]*', ' ', stripped)
    stripped = re.sub(r'"(?:[^"\\]|\\.)*"', '""', stripped)
    calls = sorted(set(re.findall(r'\b(_exit|_Exit|exit|abort|quick_exit|at_quick_exit)\s*\(',
                                  stripped)))
    check('probe: the source contains no exit/_exit/abort call', not calls,
          calls or 'none')
    obj = sh(['aarch64-linux-gnu-objdump', '-d',
              '--disassemble=main', str(OUT / 'work/init-sonda.unstripped')]).decode()
    cannot_return, detail = main_cannot_return(obj)
    check('probe: main() cannot return (no return instruction, no branch to '
          'exit/abort, ends in an unconditional branch)', cannot_return, detail)

    passed = sum(1 for c in checks if c['pass'])
    log()
    log(f'=== {passed}/{len(checks)} checks passed')
    (OUT / 'VERIFY-probe-v7.log').write_text('\n'.join(log_lines) + '\n')
    (OUT / 'verify-probe-v7.json').write_text(json.dumps(
        {'checks': checks, 'passed': passed, 'total': len(checks)}, indent=2) + '\n')
    return 0 if passed == len(checks) else 1


if __name__ == '__main__':
    sys.exit(main())
