#!/usr/bin/env python3
"""STATIC check of the v4 module list against the real ramdisk. Read-only.

Question answered: does every module name written in candidate-init-v4.sh
correspond to a real .ko file inside the ramdisk packed in
candidate-stockkernel-v4/boot_b-stockkernel-openwrt-v4.img ?

The name list and the mixed-spelling table are parsed out of the init script
itself (not copied here), and the file list is parsed out of the built image
(not from a staging directory), so the check cannot drift from either artefact.

Resolution order per name, exactly as the shell does it:
  1. literal            <name>.ko
  2. all hyphens        <name with _ -> ->ko
  3. all underscores    <name with - -> _>.ko
  4. mixed-spelling alias from the init's case table
Anything that matches nothing => flagged NOT RESOLVED (the init logs
"insmod <name> RESOLUTION-FAILED").

Usage: verify-modules-v4.py [--image PATH] [--init PATH]
Exit code 0 only if all names resolve and no stray RESOLUTION-FAILED path is
reachable for a name that does exist.
"""
import argparse
import gzip
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

EXP = Path('/home/user/nx679j-stock/experiments/20260916-122926-native-baseline')
OUT = EXP / 'candidate-stockkernel-v4'
PAGE = 4096


def newc_names(raw: bytes):
    off, names = 0, []
    while off < len(raw):
        if raw[off:off + 6] not in (b'070701', b'070702'):
            raise ValueError(f'bad cpio magic at {off}')
        f = [int(raw[off + 6 + i * 8:off + 14 + i * 8], 16) for i in range(13)]
        size, namesize = f[6], f[11]
        name = raw[off + 110:off + 110 + namesize - 1].decode()
        start = (off + 110 + namesize + 3) & ~3
        if name == 'TRAILER!!!':
            return names
        names.append(name)
        off = (start + size + 3) & ~3
    raise ValueError('no TRAILER!!! in cpio')


def parse_init(path: Path):
    text = path.read_text()
    blk = text.split('for ko in ', 1)[1].split('\ndo\n', 1)[0]
    names = blk.replace('\\', ' ').split()
    alias = {}
    for line in text.splitlines():
        m = re.match(r'\s*([A-Za-z0-9_]+)\)\s*cand="\$M/([A-Za-z0-9_.-]+)\.ko"', line)
        if m:
            alias[m.group(1)] = m.group(2) + '.ko'
    return names, alias, text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--image', default=str(OUT / 'boot_b-stockkernel-openwrt-v4.img'))
    ap.add_argument('--init', default=str(EXP / 'candidate-init-v4.sh'))
    a = ap.parse_args()

    img_path, init_path = Path(a.image), Path(a.init)
    if not img_path.exists():
        sys.exit(f'missing image: {img_path}')
    img = img_path.read_bytes()
    k_size, r_size = struct.unpack_from('<II', img, 8)
    r_off = PAGE + ((k_size + PAGE - 1) // PAGE) * PAGE
    ramdisk = img[r_off:r_off + r_size]
    raw = gzip.decompress(ramdisk)
    entries = newc_names(raw)
    mod_dir = next(n.rsplit('/', 1)[0] for n in entries if n.endswith('.ko'))
    files = {n.rsplit('/', 1)[-1] for n in entries if n.endswith('.ko')}

    names, alias, init_text = parse_init(init_path)

    print(f'image   {img_path}')
    print(f'        {len(img)} bytes sha256={hashlib.sha256(img).hexdigest()}')
    print(f'ramdisk sha256={hashlib.sha256(ramdisk).hexdigest()} entries={len(entries)}')
    print(f'init    {init_path}')
    print(f'        sha256={hashlib.sha256(init_path.read_bytes()).hexdigest()}')
    print(f'module dir inside ramdisk: {mod_dir}')
    print(f'names in init list: {len(names)}   mixed-spelling aliases: {alias or "none"}')
    print()
    print(f'{"#":>2}  {"init list name":32s} {"resolved file":34s} spelling')
    print('-' * 92)

    rows, unresolved = [], []
    for i, nm in enumerate(names, 1):
        cands = [('literal', f'{nm}.ko'),
                 ('all-hyphens', nm.replace('_', '-') + '.ko'),
                 ('all-underscores', nm.replace('-', '_') + '.ko')]
        if nm in alias:
            cands.append(('mixed-spelling alias', alias[nm]))
        hit = next(((c, h) for h, c in cands if c in files), None)
        if hit:
            rows.append({'i': i, 'name': nm, 'file': hit[0], 'spelling': hit[1],
                         'present': f'{mod_dir}/{hit[0]}' in entries})
            print(f'{i:2d}  {nm:32s} {hit[0]:34s} {hit[1]}')
        else:
            unresolved.append(nm)
            rows.append({'i': i, 'name': nm, 'file': None, 'spelling': None,
                         'present': False, 'tried': [c for _, c in cands]})
            print(f'{i:2d}  {nm:32s} {"*** NOT RESOLVED ***":34s} '
                  f'tried {", ".join(c for _, c in cands)}')

    # guard: no RESOLUTION-FAILED path may be taken for a name that exists
    reachable = [r['name'] for r in rows if r['file'] is None]
    print()
    print(f'resolved: {len(rows) - len(unresolved)}/{len(rows)}')
    if unresolved:
        print(f'NOT RESOLVED (init would log RESOLUTION-FAILED): {unresolved}')
    else:
        print('NOT RESOLVED: none - no RESOLUTION-FAILED line is reachable for any '
              'name in the v4 list')
    by_spelling = {}
    for r in rows:
        if r['file']:
            by_spelling[r['spelling']] = by_spelling.get(r['spelling'], 0) + 1
    print('resolution mechanism histogram: ' + json.dumps(by_spelling))
    print('mixed-spelling file confirmed: '
          + (f'nvmem_qcom_spmi_sdam -> {alias["nvmem_qcom_spmi_sdam"]} '
             f'(present={alias["nvmem_qcom_spmi_sdam"] in files})'
             if 'nvmem_qcom_spmi_sdam' in alias else 'n/a'))
    print('control: the v3 buggy spellings would have missed '
          f'{sum(1 for r in rows if r["spelling"] != "literal")} of these names '
          f'(they only tried the literal form twice)')

    report = {
        'image': str(img_path), 'image_bytes': len(img),
        'image_sha256': hashlib.sha256(img).hexdigest(),
        'ramdisk_sha256': hashlib.sha256(ramdisk).hexdigest(),
        'ramdisk_entries': len(entries),
        'mod_dir': mod_dir,
        'init': str(init_path),
        'init_sha256': hashlib.sha256(init_path.read_bytes()).hexdigest(),
        'names': len(rows), 'resolved': len(rows) - len(unresolved),
        'unresolved': unresolved,
        'histogram': by_spelling,
        'alias_table': alias,
        'table': rows,
    }
    (OUT / 'module-resolution-static.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'json -> {OUT / "module-resolution-static.json"}')
    sys.exit(1 if unresolved else 0)


if __name__ == '__main__':
    main()
