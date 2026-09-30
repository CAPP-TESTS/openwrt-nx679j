#!/usr/bin/env python3
"""dt_table (dtbo): header big-endian, 32 B per voce. Dump del FDT scelto."""
import struct, sys


def parse_dtbo(path):
    d = open(path, 'rb').read()
    f = struct.unpack_from('>8I', d, 0)
    magic, total, hsz, esz, cnt, eoff, psz, ver = f
    print('magic=0x%08x total_size=%d header_size=%d dt_entry_size=%d' % (magic, total, hsz, esz))
    print('dt_entry_count=%d dt_entries_offset=%d page_size=%d version=%d' % (cnt, eoff, psz, ver))
    ents = []
    for i in range(cnt):
        sz, off, id_, rev = struct.unpack_from('>4I', d, eoff + i * esz)
        ents.append((i, sz, off, id_, rev))
    return d, ents, hsz


def fdt_walk(blob):
    res = {}
    if blob[:4] != b'\xd0\x0d\xfe\xed':
        return res
    off_struct, off_strings = struct.unpack_from('>II', blob, 8)
    pos = off_struct
    cur = []
    while pos + 8 <= len(blob):
        tag, ln = struct.unpack_from('>II', blob, pos)
        if tag == 1:
            cur.append(blob[pos + 8:pos + ln].split(b'\x00')[0].decode('utf8', 'replace'))
        elif tag == 2:
            if cur:
                cur.pop()
        elif tag == 3:
            nlen = (ln + 3) // 4 * 4
            name_off = struct.unpack_from('>I', blob, pos + 4)[0]
            if pos + 8 + nlen + 4 > len(blob):
                break
            plen = struct.unpack_from('>I', blob, pos + 8 + nlen)[0]
            end = blob.index(b'\x00', off_strings + name_off)
            name = blob[off_strings + name_off:end].decode('utf8', 'replace')
            res['/'.join(cur) + ':' + name] = blob[pos + 8 + nlen + 4:pos + 8 + nlen + 4 + plen]
            pos += 8 + nlen + 4 + ((plen + 3) // 4 * 4)
            continue
        elif tag in (4, 9):
            break
        pos += 8 + ((ln + 3) // 4 * 4)
    return res


if __name__ == '__main__':
    p = sys.argv[1]
    idxs = [int(x, 0) for x in sys.argv[2:]] or []
    d, ents, hsz = parse_dtbo(p)
    print('--- prime 8 voci (i,size,off,id,rev) ---')
    for e in ents[:8]:
        print('   ', e)
    for idx in idxs:
        i, sz, off, id_, rev = ents[idx]
        blob = d[off:off + sz]
        print('--- voce %d size=%d off=0x%x id=0x%x rev=0x%x magic=%s len=%d' %
              (idx, sz, off, id_, rev, blob[:4].hex(), len(blob)))
        props = fdt_walk(blob)
        for k in sorted(props):
            if k.endswith(('model', 'compatible', 'qcom,board-id', 'qcom,msm-id')):
                v = props[k]
                if k.endswith(('model', 'compatible')):
                    print('   %-50s = %r' % (k, v.replace(b'\x00', b'|').decode('utf8', 'replace')))
                else:
                    print('   %-50s = %s' % (k, [hex(c) for c in
                          [int.from_bytes(v[x:x + 4], 'big') for x in range(0, len(v) - len(v) % 4, 4)]]))
        open('work/dtbo_entry%d.dtb' % idx, 'wb').write(blob)
        print('   dump -> work/dtbo_entry%d.dtb (%d B)' % (idx, len(blob)))
