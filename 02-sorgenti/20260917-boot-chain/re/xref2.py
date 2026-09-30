#!/usr/bin/env python3
"""Xref AArch64 (ADRP+ADD) verso stringhe dell'ABL decompresso.
VA = offset_blob - 0xb0: il PE inizia a 0xb0 nel blob e le sezioni hanno
PointerToRawData == VirtualAddress (verificato con readelf-like parsing).
Uso: xref2.py <blob> <nome> <offset_blob_hex> [...]"""
import struct, sys

PE = 0xb0
d = open(sys.argv[1], 'rb').read()

def xrefs(target):
    out = []
    for i in range(0, len(d) - 16, 4):
        w = struct.unpack_from('<I', d, i)[0]
        if (w & 0x9F000000) != 0x90000000:
            continue
        rd = w & 0x1f
        imm = ((w >> 29) & 3) | (((w >> 5) & 0x7ffff) << 2)
        if imm & (1 << 20):
            imm -= 1 << 21
        pc = i - PE
        page = (pc & ~0xFFF) + (imm << 12)
        for j in range(1, 5):
            w2 = struct.unpack_from('<I', d, i + j * 4)[0]
            if (w2 & 0xFF800000) == 0x91000000 and (w2 & 0x1f) == rd:
                imm12 = (w2 >> 10) & 0xFFF
                sh = (w2 >> 22) & 3
                if page + (imm12 << (12 * sh)) == target:
                    out.append((i - PE, (i + j * 4) - PE))
    return out

args = sys.argv[2:]
print(f'### blob={sys.argv[1]} len={len(d)} PE_base=0x{PE:x}')
for k in range(0, len(args), 2):
    name = args[k]
    fo = int(args[k + 1], 16)
    va = fo - PE
    xs = xrefs(va)
    print(f'== {name} blob_off={fo:#x} VA={va:#x} -> xref {[hex(a) for a, b in xs]}')
