#!/usr/bin/env python3
"""Decodifica corretta della tabella comandi fastboot dell'ABL.
Layout osservato: entry da 16 byte = { const char *name; handler_va }."""
import struct
B = "out/abl_a.img@1000_f1048_gd1060_fvi_4.bin"
PE = 0xb0
d = open(B, 'rb').read()

def cstr(va):
    off = va + PE
    if not (0 <= off < len(d)):
        return None
    e = d.find(b'\x00', off)
    if e < 0:
        return None
    s = d[off:e]
    if not s or any(c < 0x20 or c > 0x7e for c in s):
        return None
    return s.decode()

start = 0x6f568
print(f'{"entry_blob":>10} {"entry_VA":>10}  {"name_VA":>10}  {"nome":<32} handler_VA')
for k in range(24):
    o = start + 16 * k
    name_va, handler = struct.unpack_from('<QQ', d, o)
    nm = cstr(name_va)
    if nm is None:
        print(f'  ...stop alla entry {k} (o={o:#x}: name_va={name_va:#x} non e\' una stringa)')
        break
    ok = 'OK' if 0x1000 <= handler < 0x72000 else 'FUORI-.text'
    print(f'{o:#10x} {o-PE:#10x}  {name_va:#10x}  {nm:<32} {handler:#x} [{ok}]')
