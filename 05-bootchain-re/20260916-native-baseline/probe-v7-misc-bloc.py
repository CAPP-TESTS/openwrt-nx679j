#!/usr/bin/env python3
"""Decode the AOSP bootloader_control (BLOC) struct from misc dumps.

Purpose: determine, from artefacts, whether ABL decrements slot-retry-count
when it boots a slot that is not marked successful.  The struct lives at
offset 2048 of the misc partition.
"""
import hashlib
import struct
import sys
import zlib
from pathlib import Path

MAGIC = 0x424C4F43          # "BLOC"


def dump(p):
    b = Path(p).read_bytes()
    print(f'--- {p} ({len(b)} B) sha256={hashlib.sha256(b).hexdigest()[:16]}')
    print('   cmd    :', b[0:32].split(b'\0')[0])
    print('   status :', b[32:64].split(b'\0')[0])
    off = 2048
    blk = b[off:off + 64]
    print('   @2048  :', blk.hex())
    magic = struct.unpack_from('<I', b, off + 4)[0]
    ver = b[off + 8]
    suffix = b[off:off + 4]
    print(f'   magic={magic:#x} ({struct.pack("<I", magic)!r}) '
          f'slot_suffix={suffix!r} version={ver}')
    if magic != MAGIC:
        print('   !! no BLOC magic at 2048: this misc copy predates/never '
              'held A/B metadata')
        return
    for i in range(4):
        v = b[off + 9 + i]
        print(f'   slot{i}: priority={v % 16} tries_remaining={(v >> 4) % 8} '
              f'successful={(v >> 7) % 2} raw={v:#04x}')
    nb = b[off + 13]
    print(f'   nb_slot={nb % 8} recovery_tries={(nb >> 3) % 8}')
    print('   @2048+16:', b[off + 16:off + 32].hex())
    crc = struct.unpack_from('<I', b, off + 32)[0]
    print(f'   crc32 field={crc:#x} computed(first 32)='
          f'{zlib.crc32(bytes(b[off:off + 32])) % 2**32:#x}')


for p in sys.argv[1:]:
    dump(p)
    print()
