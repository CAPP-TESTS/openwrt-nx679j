#!/usr/bin/env python3
"""Inspect newc ramdisk members without extracting paths or executing payloads."""
import hashlib
import json
from pathlib import Path
import re
import struct

BASE = Path(__file__).resolve().parent
OUT = BASE / 'ramdisk-analysis'
OUT.mkdir(exist_ok=True)
ramdisk = BASE / 'unpacked-current/boot_b/ramdisk'
data = ramdisk.read_bytes()
records = []
wanted = {'init', 'sbin/init', 'bin/busybox', 'lib/ld-musl-aarch64.so.1',
          'etc/openwrt_release', 'etc/inittab', 'etc/rc.local'}
off = 0
while off < len(data):
    if data[off:off+6] not in (b'070701', b'070702'):
        raise ValueError(f'Bad newc magic at {off:#x}')
    fields = [int(data[off+6+i*8:off+14+i*8], 16) for i in range(13)]
    ino, mode, uid, gid, nlink, mtime, size, dmaj, dmin, rmaj, rmin, namesize, check = fields
    if namesize <= 0 or off + 110 + namesize > len(data):
        raise ValueError('invalid filename length')
    raw_name = data[off+110:off+110+namesize]
    if raw_name[-1:] != b'\0':
        raise ValueError('unterminated filename')
    name = raw_name[:-1].decode('utf-8', errors='surrogateescape')
    start = (off+110+namesize+3) & ~3
    stop = start+size
    if stop > len(data):
        raise ValueError('member exceeds archive')
    payload = data[start:stop]
    row = dict(name=name, offset=off, data_offset=start, size=size,
               mode=oct(mode), uid=uid, gid=gid, nlink=nlink,
               rdev_major=rmaj, rdev_minor=rmin,
               sha256=hashlib.sha256(payload).hexdigest())
    records.append(row)
    normalized = name[2:] if name.startswith('./') else name
    if normalized in wanted:
        # Flatten explicitly selected names: never follow archive paths.
        dest = OUT / normalized.replace('/', '__')
        dest.write_bytes(payload)
        row['inspection_copy'] = str(dest)
        if payload.startswith(b'\x7fELF'):
            row['elf'] = dict(class_=payload[4], data=payload[5],
                              machine=struct.unpack_from('<H', payload, 18)[0])
        else:
            row['text_preview'] = payload[:12000].decode(errors='replace')
    off = (stop + 3) & ~3
    if name == 'TRAILER!!!':
        while off < len(data) and data[off] == 0:
            off += 1
        if off == len(data):
            break
        if data[off:off+6] not in (b'070701', b'070702'):
            print('UNPARSED_TRAILER', json.dumps({'offset': off,
                  'bytes': len(data)-off, 'prefix_hex': data[off:off+64].hex()}))
            break
        print('ADDITIONAL_NEWC_ARCHIVE', off)
(OUT / 'cpio-index.json').write_text(json.dumps(records, indent=2) + '\n')
print(json.dumps({'archive': str(ramdisk), 'entries': len(records),
                  'selected': [r for r in records if 'inspection_copy' in r],
                  'devices': [r for r in records if r['name'].startswith(('dev/', './dev/'))]}, indent=2))
kernel = (BASE / 'unpacked-current/boot_b/kernel').read_bytes()
versions = [m.group().decode(errors='replace') for m in re.finditer(rb'Linux version [^\x00\n]{1,512}', kernel)]
print('KERNEL_VERSIONS', json.dumps(versions))
(OUT / 'kernel-versions.json').write_text(json.dumps(versions, indent=2) + '\n')
