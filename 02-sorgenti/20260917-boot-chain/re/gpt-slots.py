#!/usr/bin/env python3
"""Legge le entry GPT di sde (read-only, dump fresco) e decodifica gli attributi
slot secondo il layout ricavato dal codice ABL:
  bit48-49 Priority, bit50 Active, bit51-53 Retry, bit54 Success, bit55 Unbootable.
Uso: gpt-slots.py <file entries> <block_size>"""
import struct, sys

path = sys.argv[1]
blocksize = int(sys.argv[2]) if len(sys.argv) > 2 else 4096
d = open(path, 'rb').read()
n = len(d) // 128
print('entry GPT lette: %d (blocco %d B)' % (n, blocksize))
rows = []
for i in range(n):
    e = d[i * 128:(i + 1) * 128]
    if e[:16] == b'\x00' * 16:
        continue
    first_lba, last_lba, attr = struct.unpack_from('<QQQ', e, 32)
    name = e[56:128].decode('utf-16-le').split('\x00')[0]
    rows.append((i, name, first_lba, last_lba, attr))

print('partizioni con attributi non nulli (slot metadata):')
print('%-3s %-20s %-14s %-16s %-6s %-6s %-6s %-7s %-9s' % ('#', 'nome', 'lba0..lba1', 'attr', 'prio', 'act', 'retry', 'succ', 'unboot'))
for i, name, f, l, a in rows:
    if a == 0 or name in ('', 'gpt'):
        continue
    prio = (a >> 48) & 0x3
    act = (a >> 50) & 1
    retry = (a >> 51) & 0x7
    succ = (a >> 54) & 1
    unb = (a >> 55) & 1
    other = a & ~((0x3 << 48) | (1 << 50) | (0x7 << 51) | (1 << 54) | (1 << 55))
    print('%-3d %-20s %-14s 0x%016x %-6d %-6d %-6d %-7d %-9d altri_bit=0x%x' %
          (i, name, '%d..%d' % (f, l), a, prio, act, retry, succ, unb, other))
print()
print('conteggio partizioni totali nella tabella: %d' % len(rows))
