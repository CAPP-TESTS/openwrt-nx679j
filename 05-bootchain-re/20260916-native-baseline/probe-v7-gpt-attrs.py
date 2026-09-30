#!/usr/bin/env python3
"""Decode the A/B slot metadata that the GPT entry attribute flags can carry.

AOSP bootloader backends can keep priority / tries_remaining / successful_boot
in bits 48..55 of the GPT partition entry attribute flags instead of in misc.
This decodes both candidate encodings for every partition whose name matches
boot_/vendor_boot_/dtbo_ so the live values can be compared with the values
fastboot reports (slot-successful:X, slot-retry-count:X).

usage: probe-v7-gpt-attrs.py <gpt-dump-or-json> [...]
"""
import json
import struct
import sys
from pathlib import Path

SECTOR = 4096


def entries_from_bin(data):
    off = 2 * SECTOR
    n, esz = 32, 128
    out = []
    for i in range(n):
        e = data[off + i * esz:off + (i + 1) * esz]
        if len(e) < 128 or e[:16] == b'\0' * 16:
            continue
        name = e[:72].decode('utf-16-le').split('\0')[0]
        first, last, attrs = struct.unpack_from('<QQQ', e, 32)
        out.append((name, first, last, attrs))
    return out


def entries_from_json(obj):
    out = []
    for lun, tab in (obj.items() if isinstance(obj, dict) else []):
        for p in (tab if isinstance(tab, list) else tab.get('partitions', [])):
            out.append((p.get('name'), p.get('first_lba'), p.get('last_lba'),
                        p.get('attrs', p.get('attributes', 0))))
    return out


def show(p, ents):
    print(f'--- {p}')
    for name, first, last, attrs in ents:
        if name is None:
            continue
        if not any(name.startswith(k) for k in ('boot', 'vendor_boot',
                                                'dtbo', 'recovery', 'vbmeta')):
            continue
        b0 = attrs % 256
        b1 = (attrs >> 8) % 256
        b5 = (attrs >> 40) % 256
        prio = (attrs >> 48) % 16
        tries = (attrs >> 52) % 8
        succ = (attrs >> 55) % 2
        corr = (attrs >> 56) % 2
        print(f'   {name:<16} lba {first}..{last} attrs={attrs:#018x} '
              f'[48:52)priority={prio} [52:55)tries={tries} '
              f'[55]successful={succ} [56]verity_corrupt={corr} '
              f'(low bytes {b0:#04x} {b1:#04x} {b5:#04x})')


for p in sys.argv[1:]:
    path = Path(p)
    if path.suffix == '.json':
        show(p, entries_from_json(json.loads(path.read_text())))
    else:
        show(p, entries_from_bin(path.read_bytes()))
    print()
