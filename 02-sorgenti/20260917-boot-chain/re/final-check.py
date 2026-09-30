#!/usr/bin/env python3
"""Verifica finale (corretta): 9 FDT in vendor_boot + voci dtbo con proprieta' di match.
Risolve i nomi delle proprieta' in modo robusto (token -> offset nel blocco stringhe)."""
import struct

VB = '/home/user/nx679j-stock/full_extracted_v311/vendor_boot.img'
DTBO = 'bl/dtbo_a.img'
WANT = ('model', 'compatible', 'qcom,board-id', 'qcom,msm-id', 'qcom,pmic-id')


def strblock_map(blob, off_strings, size_dt_strings):
    sb = blob[off_strings:off_strings + size_dt_strings]
    m = {}
    pos = 0
    while pos < len(sb):
        end = sb.find(b'\x00', pos)
        if end < 0:
            break
        if end > pos:
            m[pos] = sb[pos:end].decode('utf8', 'replace')
        pos = end + 1
    return m


def root_props(blob):
    """proprieta' del nodo radice del FDT."""
    if blob[:4] != b'\xd0\x0d\xfe\xed':
        return {}
    off_struct, off_strings = struct.unpack_from('>II', blob, 8)
    size_dt_strings = struct.unpack_from('>I', blob, 32)[0]
    s = strblock_map(blob, off_strings, size_dt_strings)
    pos = off_struct
    out = {}
    depth = 0
    while pos + 8 <= len(blob):
        tag, ln = struct.unpack_from('>II', blob, pos)
        if tag == 1:
            depth += 1
        elif tag == 2:
            depth -= 1
            if depth < 0:
                break
        elif tag == 3:
            nlen = (ln + 3) // 4 * 4
            no = struct.unpack_from('>I', blob, pos + 4)[0]
            plen = struct.unpack_from('>I', blob, pos + 8 + nlen)[0]
            nm = s.get(no)
            if depth == 1 and nm in WANT:
                out[nm] = blob[pos + 8 + nlen + 4:pos + 8 + nlen + 4 + plen]
            pos += 8 + nlen + 4 + ((plen + 3) // 4 * 4)
            continue
        elif tag in (4, 9):
            break
        pos += 8 + ((ln + 3) // 4 * 4)
    return out


def cells(v):
    return [hex(int.from_bytes(v[x:x + 4], 'big')) for x in range(0, len(v) - len(v) % 4, 4)]


d = open(VB, 'rb').read()
hsz = struct.unpack_from('<I', d, 2096)[0]
dtb_size = struct.unpack_from('<I', d, 2100)[0]
dtb_addr = struct.unpack_from('<Q', d, 2104)[0]
vrs = struct.unpack_from('<I', d, 24)[0]
pg = struct.unpack_from('<I', d, 12)[0]
print('vendor_boot: header_size=%d page=%d vendor_ramdisk_size=%d dtb_size=%d dtb_addr=0x%x' %
      (hsz, pg, vrs, dtb_size, dtb_addr))
off_dtb = ((hsz + pg - 1) // pg) * pg + ((vrs + pg - 1) // pg) * pg
print('area dtb a offset %d (0x%x)' % (off_dtb, off_dtb))
area = d[off_dtb:off_dtb + dtb_size]
off = 0
n = 0
while off + 8 <= len(area) and area[off:off + 4] == b'\xd0\x0d\xfe\xed':
    ts = struct.unpack_from('>I', area, off)[0]
    p = root_props(area[off:off + ts])
    print('#%d off=%-9d size=%-7d model=%s' % (n, off, ts, p.get('model', b'').replace(b'\x00', b'|').decode('utf8', 'replace')))
    print('     compatible=%s' % p.get('compatible', b'').replace(b'\x00', b'|').decode('utf8', 'replace'))
    print('     msm-id=%s board-id=%s' % (cells(p.get('qcom,msm-id', b'')) if 'qcom,msm-id' in p else '-',
                                          cells(p.get('qcom,board-id', b'')) if 'qcom,board-id' in p else '-'))
    off += ts
    n += 1
print('FDT totali:', n)
rest = area[off:]
print('byte dopo ultimo FDT: %d, non-zero: %d' % (len(rest), sum(1 for b in rest if b)))

dd = open(DTBO, 'rb').read()
magic, total, hsz2, esz, cnt, eoff, psz, ver = struct.unpack_from('>8I', dd, 0)
print()
print('dtbo: magic=0x%x voci=%d' % (magic, cnt))
for i in range(cnt):
    sz, o, id_, rev = struct.unpack_from('>4I', dd, eoff + i * esz)
    blob = dd[o:o + sz]
    if blob[:4] != b'\xd0\x0d\xfe\xed':
        continue
    p = root_props(blob)
    if 'qcom,board-id' in p or 'model' in p:
        print('  voce %2d size=%7d model=%-58r board-id=%s' % (
            i, sz, p.get('model', b'').replace(b'\x00', b'|').decode('utf8', 'replace')[:58],
            cells(p['qcom,board-id']) if 'qcom,board-id' in p else '-'))
