#!/usr/bin/env python3
"""Cerca la tabella dei comandi fastboot: array di puntatori a stringhe nome.
VA = blob_off - 0xb0."""
import struct
B = "out/abl_a.img@1000_f1048_gd1060_fvi_4.bin"
PE = 0xb0
d = open(B, 'rb').read()

names = {
    "flash:": 0x6ddec, "erase:": 0x6ddf3, "set_active": 0x6ddfa,
    "flashing get_unlock_ability": 0x6de05, "flashing unlock": 0x6de21,
    "flashing lock": 0x6de31, "oem enable-charger-screen": 0x6de3f,
    "oem disable-charger-screen": 0x6de59, "oem off-mode-charge": 0x6de74,
    "oem select-display-panel": 0x6de88, "oem device-info": 0x6dea1,
    "continue": 0x6deb1, "rb-recovery": 0x6deba, "rb-fastboot": 0x6deca,
    "rb-bootloader": 0x6deda, "getvar:": 0x6deec, "download:": 0x6def4,
    "snapshot-update-status": 0x6df4b, "oem nubia_unlock": 0x6edb6,
    "flashing avb_custom_key": 0x6e169, "getvar:partition-type": 0x6ddb1,
    "unknown command": 0x6dddc, "set_active2 ": 0x6d3f7,
}
print("--- occorrenze dei puntatori (little-endian QWORD) ---")
for n, fo in sorted(names.items(), key=lambda kv: kv[1]):
    va = fo - PE
    pat = struct.pack('<Q', va)
    hits = []
    s = 0
    while True:
        i = d.find(pat, s)
        if i < 0:
            break
        hits.append(i)
        s = i + 1
    print(f'{n:30s} VA={va:#08x} -> {[hex(h) for h in hits]}')
