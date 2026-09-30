#!/usr/bin/env python3
"""Resolve module load order from real ELF symbols, not guesswork.

Reads the stock vendor .ko set, builds provider->symbol and
module->undefined maps with readelf, then topologically sorts the
transitive closure of the USB chain.
"""
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

BASE = Path('/home/user/nx679j-stock/experiments/20260916-122926-native-baseline')
VENDOR_RD = BASE / 'unpacked-current/vendor_boot_a/vendor_ramdisk00'
OUT = BASE / 'module-order.json'

TARGETS = ['dwc3-msm.ko', 'phy-msm-snps-hs.ko', 'phy-msm-ssusb-qmp.ko',
           'phy-msm-snps-eusb2.ko', 'ucsi_glink.ko', 'pmic_glink.ko',
           'altmode-glink.ko', 'fsa4480-i2c.ko',
           'ssusb-redriver-nb7vpq904m.ko']

raw = subprocess.run(['lz4', '-dc', str(VENDOR_RD)], check=True,
                     stdout=subprocess.PIPE).stdout

mods = {}
off = 0
while off < len(raw):
    if raw[off:off + 6] not in (b'070701', b'070702'):
        break
    f = [int(raw[off + 6 + i * 8:off + 14 + i * 8], 16) for i in range(13)]
    size, ns = f[6], f[11]
    name = raw[off + 110:off + 110 + ns - 1].decode()
    start = (off + 110 + ns + 3) & ~3
    if name == 'TRAILER!!!':
        break
    if name.endswith('.ko'):
        mods[name.split('/')[-1]] = raw[start:start + size]
    off = (start + size + 3) & ~3
print(f'{len(mods)} modules')

provides, needs = {}, {}
with tempfile.TemporaryDirectory() as td:
    tmp = Path(td) / 'm.ko'
    for base, blob in mods.items():
        tmp.write_bytes(blob)
        out = subprocess.run(['readelf', '-sW', str(tmp)],
                             capture_output=True, text=True).stdout
        exp, und = set(), set()
        for line in out.splitlines():
            p = line.split()
            if len(p) < 8 or not p[0].endswith(':'):
                continue
            ndx, sym = p[6], p[7].split('@')[0]
            if not sym or sym.startswith('$') or sym.startswith('.'):
                continue
            if ndx == 'UND':
                und.add(sym)
            elif p[3] in ('FUNC', 'OBJECT') and p[4] == 'GLOBAL':
                exp.add(sym)
        provides[base] = exp
        needs[base] = und

sym_owner = {}
for base, syms in provides.items():
    for s in syms:
        sym_owner.setdefault(s, base)

resolved, order, unresolved = set(), [], {}


def visit(mod, stack=()):
    if mod in resolved or mod in stack:
        return
    for sym in sorted(needs.get(mod, ())):
        owner = sym_owner.get(sym)
        if owner and owner != mod:
            visit(owner, stack + (mod,))
        elif owner is None:
            unresolved.setdefault(mod, []).append(sym)
    resolved.add(mod)
    order.append(mod)


for t in TARGETS:
    if t not in mods:
        sys.exit(f'{t} absent from vendor ramdisk')
    visit(t)

# Symbols with no provider are expected: they come from the kernel itself.
report = {
    'order': order,
    'count': len(order),
    'targets': TARGETS,
    'kernel_provided_symbols': {m: len(v) for m, v in sorted(unresolved.items())},
}
OUT.write_text(json.dumps(report, indent=2) + '\n')
print(f'load order ({len(order)} modules):')
for i, m in enumerate(order, 1):
    print(f'  {i:3d}. {m}')
print(f'-> {OUT}')
