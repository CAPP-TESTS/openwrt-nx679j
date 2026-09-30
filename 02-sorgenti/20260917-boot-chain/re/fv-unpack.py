#!/usr/bin/env python3
"""Parse EDK2 firmware volumes (UEFI FV/FFS/section tree) inside Qualcomm bootloader
dumps, decompress GUID_DEFINED (LZMA / Tiano) sections recursively, and dump the
decompressed payloads. Read-only, works on local copies."""
import os, sys, struct, lzma, zlib, json

GUID_LZMA   = bytes.fromhex('93fd219e729c154c8c4be77f1db2d792')  # 9E21FD93-9C72-4C15-8C4B-E77F1DB2D792
GUID_TIANO  = bytes.fromhex('ad8012a31e48b64195e8127f4c984779')  # A31280AD-481E-41B6-95E8-127F4C984779
GUID_CRCM32 = bytes.fromhex('2d7a4a3f8ab64e4f9c85d6b3a1f8d0f1')  # not used, placeholder
GUID_BROTLI = bytes.fromhex('3d532050496d6e489c66b9d1a6b74f57')
GUID_GZIP   = bytes.fromhex('1d5c1b5e' + '00'*12)

def guid_str(b):
    d1,d2,d3 = struct.unpack_from('<IHH', b, 0)
    return f'{d1:08x}-{d2:04x}-{d3:04x}-{b[8:10].hex()}-{b[10:16].hex()}'

def ent(b):
    from collections import Counter
    import math
    if not b: return 0.0
    c=Counter(b); n=len(b)
    return -sum((v/n)*math.log2(v/n) for v in c.values())

def lzma_decompress(data):
    """EDK2 LZMA sections on this device carry a full 13-byte .lzma header
    (props=0x5d dictsize uncompressed-size); fall back to raw framings."""
    for skip in (0, 4, 8):
        blob = data[skip:]
        try:
            out = lzma.decompress(blob, format=lzma.FORMAT_ALONE)
            if out:
                return out
        except Exception:
            pass
        try:
            d = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE)
            out = d.decompress(blob)
            if len(out) > 4096:
                return out
        except Exception:
            pass
    return None

def find_fvs(data, magic=b'_FVH'):
    out=[]
    i=data.find(magic)
    while i!=-1:
        start=i-40           # signature sits at offset 40 in the FV header
        if start>=0:
            flen=struct.unpack_from('<Q',data,start+32)[0]
            hlen=struct.unpack_from('<H',data,start+48)[0]
            if 0x40<=hlen<=0x100 and 0<flen<=len(data)-start and (flen%512)==0:
                out.append((start,flen,hlen))
        i=data.find(magic,i+1)
    return out

def Stats(): return {'files':0,'sections':0,'decompressed':0,'failed':0,'leaf':0}

def walk_sections(data, start, end, depth, path, outdir, log, res, sectname=''):
    """Walk FFS section stream in data[start:end]; dump leaf payloads."""
    off=start
    while off+4 <= end:
        size = data[off] | (data[off+1]<<8) | (data[off+2]<<16)
        typ  = data[off+3] & 0x7f
        large= bool(data[off+3] & 0x80)
        hdr=4
        if large:
            if off+8>end: break
            size = struct.unpack_from('<I', data, off+4)[0]
            hdr=8
        if size < hdr or off+size > end:
            break
        res['sections']+=1
        body_off = off+hdr
        body_end = off+size
        nm = {0x01:'COMPRESSION',0x02:'GUID_DEFINED',0x03:'DISPOSABLE',0x10:'PE32',
              0x11:'PIC',0x12:'TE',0x13:'DXE_DEPEX',0x14:'VERSION',0x15:'USER_INTERFACE',
              0x16:'COMPAT16',0x17:'FV_IMAGE',0x18:'FREEFORM',0x19:'RAW'}.get(typ, f'TYPE_{typ:#x}')
        line=f'{"  "*depth}[sect {nm} off={off:#x} size={size} ({size} B) large={large}]'
        log.append(line)
        if typ==0x02 and size>=hdr+20:
            g = data[body_off:body_off+16]
            d_off, d_attr = struct.unpack_from('<HH', data, body_off+16)
            gs = guid_str(g)
            payload = data[off+d_off : body_end]   # DataOffset e' relativo all'inizio della sezione
            log.append(f'{"  "*depth}    GUID_DEFINED {gs} dataOffset={d_off} attr={d_attr:#06x} payload={len(payload)} B entropy={ent(payload):.2f}')
            dec=None
            if payload[:3]==b'\x1f\x8b\x08':
                try:
                    dec=zlib.decompressobj(31).decompress(payload)
                except Exception:
                    dec=None
            if dec is None:
                dec=lzma_decompress(payload)
            if dec:
                res['decompressed']+=1
                log.append(f'{"  "*depth}    -> decompresso {len(dec)} B')
                fn=os.path.join(outdir, (path+f'_gd_{gs[:8]}_{off:x}').replace('/','_') + '.bin')
                open(fn,'wb').write(dec)
                walk_sections(dec, 0, len(dec), depth+2, path+f'_gd{off:x}', outdir, log, res)
            else:
                res['failed']+=1
                log.append(f'{"  "*depth}    -> DECOMPRESSIONE FALLITA (GUID {gs})')
        elif typ==0x01 and size>hdr+4:
            # EFI standard (Tiano) compression: header = uncompressed len (4) + compressed len (4)
            res['failed']+=1
            log.append(f'{"  "*depth}    Tiano/EFI standard compression: non decodificato')
        elif typ==0x17 and size>hdr:
            res['leaf']+=1
            inner=data[body_off:body_end]
            fn=os.path.join(outdir, (path+f'_fvi_{off:x}').replace('/','_')+'.bin')
            open(fn,'wb').write(inner)
            walk_fv(inner, 0, fn+'.fv', outdir, log, res, depth+1)
        else:
            res['leaf']+=1
            fn=os.path.join(outdir, (path+f'_{nm}_{off:x}').replace('/','_')+'.bin')
            open(fn,'wb').write(data[body_off:body_end])
        off = body_end
    return

def walk_ffs(data, fv_start, fv_len, hlen, outdir, log, res, tag):
    ffs_start=fv_start+hlen
    off=ffs_start
    end=fv_start+fv_len
    while off+24<=end:
        if data[off:off+16]==b'\xff'*16:
            # pad / erased: advance by its declared size instead of stopping
            psz=data[off+20]|(data[off+21]<<8)|(data[off+22]<<16)
            if psz<24:
                psz=8
            log.append(f'[FFS {tag} off={off:#x} PAD/erased size={psz}]')
            off=(off+psz+7)&~7
            continue
        name=data[off:off+16]
        ic=struct.unpack_from('<H',data,off+16)[0]
        if ic==0xaaaa:  # FFS3 / large file
            size=struct.unpack_from('<I',data,off+20)[0]
            hdr=24
            ftype=data[off+18]
        else:
            size=data[off+20]|(data[off+21]<<8)|(data[off+22]<<16)
            hdr=24
            ftype=data[off+18]
        if size<hdr or off+size>end:
            break
        res['files']+=1
        log.append(f'[FFS {tag} off={off:#x} guid={guid_str(name)} type={ftype:#04x} size={size}]')
        walk_sections(data, off+hdr, off+size, 1, f'{tag}_f{off:x}', outdir, log, res)
        # align to 8
        off = (off+size+7)&~7
    return

def walk_fv(data, start, tag, outdir, log, res, depth=0):
    flen=struct.unpack_from('<Q',data,start+32)[0]
    hlen=struct.unpack_from('<H',data,start+48)[0]
    log.append(f'[FV {tag} off={start:#x} len={flen} ({flen} B) headerlen={hlen}]')
    walk_ffs(data, start, flen, hlen, outdir, log, res, tag)

def main():
    outdir=sys.argv[1]; os.makedirs(outdir, exist_ok=True)
    log=[]; res=Stats()
    for path in sys.argv[2:]:
        data=open(path,'rb').read()
        tag=os.path.basename(path)
        fvs=find_fvs(data)
        log.append(f'### {path} ({len(data)} B) — firmware volume trovati: {[(hex(s),l,h) for s,l,h in fvs]}')
        for (s,l,h) in fvs:
            walk_fv(data, s, f'{tag}@{s:x}', outdir, log, res)
    open(os.path.join(outdir,'..','logs','fv-walk.log'),'w').write('\n'.join(log))
    print('\n'.join(log[:200]))
    print('...')
    print('\n'.join(log[-40:]))
    print('STATS', res)

main()
