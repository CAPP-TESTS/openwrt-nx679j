#!/usr/bin/env python3
"""Dry test of the GPT decoder embedded in test-probe-v7-slotb.sh.

Runs the same code on a GPT dump captured from the phone today, so the decoder
is proven before the script is ever pointed at the device.
"""
import struct
import sys

b = open(sys.argv[1], 'rb').read()
if b[0x1000:0x1008] != b'EFI PART':
    print('    (GPT non leggibile: ' + str(len(b)) + ' byte)')
    raise SystemExit(1)
n, esz = struct.unpack_from('<II', b, 0x1000 + 80)
tbl = struct.unpack_from('<Q', b, 0x1000 + 72)[0] * 4096
found = 0
for i in range(n):
    e = tbl + i * esz
    if len(b) < e + 128:
        break
    name = b[e + 56:e + 128].decode('utf-16-le', 'ignore').split('\x00')[0]
    if name not in ('boot_a', 'boot_b'):
        continue
    a = struct.unpack_from('<Q', b, e + 48)[0]
    prio, succ, tries = (a >> 48) & 3, (a >> 50) & 1, (a >> 51) & 7
    found += 1
    print(f'    GPT {name}: priority={prio} successful={succ} '
          f'tries_remaining={tries}   (bits 48-49 / 50 / 51-53)')
print(f'    decoded {found}/2 boot entries from {sys.argv[1]}')
