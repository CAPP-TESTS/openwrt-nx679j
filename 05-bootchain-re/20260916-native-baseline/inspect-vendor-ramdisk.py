#!/usr/bin/env python3
"""Index trusted readback via bounded lz4; don't extract archive paths."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

BASE = Path(__file__).resolve().parent
source = BASE / 'unpacked-current/vendor_boot_a/vendor_ramdisk00'
out = BASE / 'vendor-ramdisk-analysis'
out.mkdir(exist_ok=True)
result = subprocess.run(['lz4', '-dc', str(source)], stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE, timeout=30, check=True)
data = result.stdout
off = 0
records = []
while off < len(data):
    assert data[off:off+6] in (b'070701', b'070702'), (off, data[off:off+16])
    fields = [int(data[off+6+i*8:off+14+i*8], 16) for i in range(13)]
    namesize, size = fields[11], fields[6]
    assert 1 <= namesize <= 4096
    name = data[off+110:off+110+namesize-1].decode()
    start = (off+110+namesize+3) & ~3
    assert start+size <= len(data)
    payload = data[start:start+size]
    row = {'name': name, 'offset': start, 'size': size, 'mode': oct(fields[1]),
           'sha256': hashlib.sha256(payload).hexdigest()}
    if name.endswith(('.rc', '.dep', '.load', '.alias', '.softdep', '.json')) or 'modules.load' in name:
        row['text'] = payload.decode(errors='replace')
    if name.endswith('.ko'):
        row['vermagic'] = [m.group()[9:].decode(errors='replace') for m in re.finditer(rb'vermagic=[^\x00]+', payload)]
    records.append(row)
    off = (start+size+3) & ~3
    if name == 'TRAILER!!!':
        assert not any(data[off:]), 'nonzero trailing bytes'
        break
(out / 'index.json').write_text(json.dumps(records, indent=2)+'\n')
print(json.dumps({'expanded_bytes':len(data), 'count':len(records),
    'modules': [r['name'] for r in records if r['name'].endswith('.ko')],
    'load_files': [r for r in records if 'modules.load' in r['name']],
    'sample_vermagic': next((r['vermagic'] for r in records if r.get('vermagic')), [])}, indent=2))
