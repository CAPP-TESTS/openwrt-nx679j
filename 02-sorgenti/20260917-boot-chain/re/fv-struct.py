#!/usr/bin/env python3
"""Struttura di una FV EDK2 + stringhe con offset."""
import struct, sys, re

def guid_str(b):
    d1,d2,d3 = struct.unpack_from('<IHH', b, 0)
    return f'{d1:08x}-{d2:04x}-{d3:04x}-{b[8:10].hex()}-{b[10:16].hex()}'

SECT = {0x01:'COMPRESSION',0x02:'GUID_DEFINED',0x03:'DISPOSABLE',0x10:'PE32',0x11:'PIC',
        0x12:'TE',0x13:'DXE_DEPEX',0x14:'VERSION',0x15:'USER_INTERFACE',0x16:'COMPAT16',
        0x17:'FV_IMAGE',0x18:'FREEFORM',0x19:'RAW',0x1b:'PEI_DEPEX'}
FTYPE = {0x01:'SEC',0x02:'PEI_CORE',0x03:'DXE_CORE',0x04:'PEIM',0x05:'DRIVER',0x06:'COMBINED_PEIM_DRIVER',
         0x07:'APPLICATION',0x08:'SMM',0x09:'FIRMWARE_VOLUME_IMAGE',0x0a:'COMBINED_SMM_DXE',0x0b:'SMM_CORE',
         0x0c:'MM_STANDALONE',0xf0:'PAD'}

def walk_sections(d, s, e, depth, log, out=None):
    off=s
    while off+4<=e:
        size=d[off]|(d[off+1]<<8)|(d[off+2]<<16)
        large=bool(d[off+3]&0x80); typ=d[off+3]&0x7f; hdr=4
        if large:
            if off+8>e: break
            size=struct.unpack_from('<I',d,off+4)[0]; hdr=8
        if size<hdr or off+size>e: break
        log.append(f'{"  "*depth}sez {SECT.get(typ,hex(typ))} @{off:#x} len={size}')
        if typ==0x02 and size>=hdr+20:
            body=off+hdr; g=d[body:body+16]
            doff,attr=struct.unpack_from('<HH',d,body+16)
            log.append(f'{"  "*depth}   GUID_DEFINED {guid_str(g)} dataOffset={doff} attr={attr:#06x}')
        off=off+size

def walk_ffs(d, start, end, log):
    off=start
    while off+24<=end:
        if d[off:off+16]==b'\xff'*16:
            size=d[off+20]|(d[off+21]<<8)|(d[off+22]<<16)
            log.append(f'FFS @{off:#x} PAD/erased size={size}')
            off=(off+max(size,8)+7)&~7
            if size==0: break
            continue
        name=d[off:off+16]; ic=struct.unpack_from('<H',d,off+16)[0]
        typ=d[off+18]
        if ic==0xaaaa or (d[off+16]==0xaa and d[off+17]==0xaa):
            size=struct.unpack_from('<I',d,off+20)[0]; hdr=24
        else:
            size=d[off+20]|(d[off+21]<<8)|(d[off+22]<<16); hdr=24
        if size<hdr or off+size>end:
            log.append(f'FFS @{off:#x} STOP (guid={guid_str(name)} type={typ:#x} size={size} ic={ic:#06x})')
            break
        log.append(f'FFS @{off:#x} type={FTYPE.get(typ,hex(typ))} size={size} guid={guid_str(name)} ic={ic:#06x}')
        walk_sections(d, off+hdr, min(off+size,end), 1, log)
        off=(off+size+7)&~7

def dump(path):
    d=open(path,'rb').read()
    log=[f'### {path} len={len(d)}']
    flen=struct.unpack_from('<Q',d,32)[0]; hlen=struct.unpack_from('<H',d,48)[0]
    log.append(f'FV FvLength={flen} HeaderLength={hlen} BlockMap={d[56:72].hex()}')
    log.append(f'primi 0x80 byte: {d[:0x80].hex()}')
    walk_ffs(d, hlen, min(flen,len(d)), log)
    return log

out=[]
for p in sys.argv[1:]:
    out+=dump(p)+['']
print('\n'.join(out))
