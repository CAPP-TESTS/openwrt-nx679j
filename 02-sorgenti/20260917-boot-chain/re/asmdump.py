#!/usr/bin/env python3
"""Estrae finestre di disassemblaggio dal file abl_text.asm intorno a VA date,
e cerca istruzioni ubfx/ubfm (estrazione di campi di bit) in un intervallo."""
import re, sys, bisect

lines = open('logs/abl_text.asm').read().splitlines()
addrs, idxs = [], []
for i, l in enumerate(lines):
    m = re.match(r'\s+([0-9a-f]+):', l)
    if m:
        addrs.append(int(m.group(1), 16)); idxs.append(i)

def window(va, before=8, after=40):
    j = bisect.bisect_right(addrs, va) - 1
    if j < 0: j = 0
    return lines[idxs[j] - before: idxs[j] + after]

def ubfx_scan(lo, hi):
    out = []
    for a, i in zip(addrs, idxs):
        if lo <= a <= hi:
            l = lines[i]
            if '\tubfx\t' in l or '\tubfm\t' in l or '\tubfiz\t' in l:
                out.append(l.strip())
    return out

if 'ubfx' in sys.argv:
    lo = int(sys.argv[2], 16); hi = int(sys.argv[3], 16)
    print(f'--- ubfx/ubfm in {lo:#x}..{hi:#x} ---')
    for l in ubfx_scan(lo, hi):
        print('  ', l)
else:
    for a in sys.argv[1:]:
        va = int(a, 16)
        print(f'######## finestra attorno a {va:#x}')
        for l in window(va):
            print(l)
        print()
