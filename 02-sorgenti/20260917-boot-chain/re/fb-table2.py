#!/usr/bin/env python3
"""Decodifica la tabella dei comandi fastboot dell'ABL (array di
struct { const char *name; <handler> } da 16 byte) e risolve i nomi."""
import struct
B = "out/abl_a.img@1000_f1048_gd1060_fvi_4.bin"
PE = 0xb0
d = open(B, 'rb').read()

start = 0x6f560
end = 0x6f720

def cstr(off):
    if off < 0 or off >= len(d):
        return None
    e = d.find(b'\x00', off)
    if e < 0:
        return None
    s = d[off:e]
    if not s or any(c < 0x20 or c > 0x7e for c in s):
        return None
    return s.decode()

print(f'tabella da blob {start:#x} a {end:#x} (VA {start-PE:#x})')
for o in range(start, end, 16):
    p_name, p_h = struct.unpack_from('<QQ', d, o)
    nm = cstr(p_name + PE) if 0x1000 <= p_name < 0x72000 else None
    if p_name == 0 and p_h == 0:
        print(f'  {o:#08x} (VA {o-PE:#08x}) = terminatore')
        continue
    kind = 'testo(.text/.rodata)' if 0x1000 <= p_h < 0x72000 else 'ALTRO'
    print(f'  {o:#08x} (VA {o-PE:#08x})  name_ptr={p_name:#010x} "{nm}"  handler={p_h:#010x} [{kind}]')
