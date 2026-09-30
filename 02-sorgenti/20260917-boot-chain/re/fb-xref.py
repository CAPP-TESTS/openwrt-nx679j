#!/usr/bin/env python3
import re, bisect
lines = open('logs/abl_text.asm').read().splitlines()
addr = []
for i, l in enumerate(lines):
    m = re.match(r'\s+([0-9a-f]+):', l)
    if m:
        addr.append(int(m.group(1), 16))
# trova adrp verso 0x6f000, poi add con imm nel range tabella
for i, l in enumerate(lines):
    if re.search(r'adrp\s+x\d+, 0x6f000', l):
        for j in range(i + 1, min(i + 6, len(lines))):
            m = re.search(r'add\s+(x\d+), x\d+, #(0x[0-9a-f]+)', lines[j])
            if m:
                imm = int(m.group(2), 16)
                if 0x400 <= imm <= 0x700:
                    print('REF @', lines[i].split(':')[0].strip(), '->', m.group(1), hex(0x6f000 + imm))
                    for k in range(max(0, j - 6), min(j + 14, len(lines))):
                        print('   ', lines[k].strip())
                    print()
                break
