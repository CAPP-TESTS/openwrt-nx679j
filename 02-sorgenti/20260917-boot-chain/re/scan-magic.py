#!/usr/bin/env python3
"""Scans bootloader partition dumps for embedded containers / magic numbers.
Read-only analysis of local copies."""
import sys, zlib, math, struct
from collections import Counter

MAGICS = [
    (b'\x7fELF', 'ELF'),
    (b'\x1f\x8b\x08', 'gzip'),
    (b'\x04\x22\x4d\x18', 'lz4-frame'),
    (b'\x02\x21\x4c\x18', 'lz4-legacy'),
    (b'\x28\xb5\x2f\xfd', 'zstd'),
    (b'\xfd7zXZ\x00', 'xz'),
    (b'BZh', 'bzip2'),
    (b'PK\x03\x04', 'zip'),
    (b'\xd0\x0d\xfe\xed', 'FDT'),
    (b'ANDROID!', 'ANDROID!'),
    (b'VNDRBOOT', 'VNDRBOOT'),
    (b'AVB0', 'AVB0'),
    (b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00', 'zeros16'),
    (b'BOOTLDR!', 'BOOTLDR!'),
    (b'\xed\xfe\x0d\xd0', 'FDT-swapped'),
]

def entropy(b):
    if not b: return 0.0
    c = Counter(b)
    n = len(b)
    return -sum((v/n)*math.log2(v/n) for v in c.values())

def scan(path, want, window=1<<16):
    data = open(path,'rb').read()
    print(f"\n### {path}  size={len(data)} sha256(first16)={data[:16].hex()}")
    print(f"    entropia globale = {entropy(data[:1<<20]):.3f} bit/byte (su 1 MiB)")
    for m, name in MAGICS:
        if name == 'zeros16':
            continue
        offs = []
        i = data.find(m)
        while i != -1 and len(offs) < 40:
            offs.append(i)
            i = data.find(m, i+1)
        if offs:
            print(f"    {name:12s} n>={len(offs):3d} primi offset: {[hex(o) for o in offs[:12]]}")
    # non-zero tail after first 0x100000
    # windows entropy map
    print("    mappa entropia (blocchi 256 KiB, max 24 blocchi):")
    line = []
    for off in range(0, min(len(data), 24*(1<<18)), 1<<18):
        e = entropy(data[off:off+(1<<18)])
        line.append(f"{off:#09x}:{e:.2f}")
    print("      " + "  ".join(line))

for p in sys.argv[1:]:
    scan(p, None)
