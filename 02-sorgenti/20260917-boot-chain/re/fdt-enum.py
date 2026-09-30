#!/usr/bin/env python3
"""Enumerazione dei 9 FDT in vendor_boot (offsets a catena) + dump DTS via dtc se presente."""
import struct, subprocess, shutil, os

VB = '/home/user/nx679j-stock/full_extracted_v311/vendor_boot.img'
d = open(VB, 'rb').read()
hsz = struct.unpack_from('<I', d, 2096)[0]
pg = struct.unpack_from('<I', d, 12)[0]
vrs = struct.unpack_from('<I', d, 24)[0]
dtb_size = struct.unpack_from('<I', d, 2100)[0]
off_dtb = ((hsz + pg - 1) // pg) * pg + ((vrs + pg - 1) // pg) * pg
area = d[off_dtb:off_dtb + dtb_size]
have_dtc = shutil.which('dtc') is not None
print('area dtb offset=%d size=%d dtc=%s' % (off_dtb, dtb_size, have_dtc))
os.makedirs('work/fdt', exist_ok=True)
off = 0
n = 0
while off + 8 <= len(area) and area[off:off + 4] == b'\xd0\x0d\xfe\xed':
    ts = struct.unpack_from('>I', area, off + 4)[0]
    blob = area[off:off + ts]
    fn = 'work/fdt/base%02d.dtb' % n
    open(fn, 'wb').write(blob)
    info = ''
    if have_dtc:
        r = subprocess.run(['dtc', '-I', 'dtb', '-O', 'dts', fn], capture_output=True, text=True)
        dts = r.stdout
        for ln in dts.splitlines():
            if '/ {' in ln or 'model =' in ln or 'compatible =' in ln or 'qcom,msm-id' in ln or 'qcom,board-id' in ln or 'qcom,pmic-id' in ln:
                info += '\n       ' + ln.strip()
        if r.returncode != 0:
            info = '  [dtc errore: %s]' % r.stderr.strip()[:120]
    print('#%d off=%-9d size=%-7d%s' % (n, off, ts, info))
    off += ts
    n += 1
print('FDT totali:', n)
