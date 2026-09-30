#!/usr/bin/env python3
"""Trova xref AArch64 (ADRP+ADD) verso indirizzi/stringhe in un'immagine PE
caricata come blob lineare (VA == offset file, come in questo ABL).
Uso: xref.py <blob> 0xADDR [0xADDR ...]"""
import sys, struct

def xrefs(d, target):
    out=[]
    n=len(d)//4
    for i in range(n-1):
        w=struct.unpack_from('<I',d,i*4)[0]
        if (w & 0x9F000000)!=0x90000000: continue
        rd=w & 0x1f
        imm=((w>>29)&3) | (((w>>5)&0x7ffff)<<2)
        if imm & (1<<20): imm-=1<<21
        page=((i*4) & ~0xFFF) + (imm<<12)
        for j in range(1,5):
            w2=struct.unpack_from('<I',d,(i+j)*4)[0]
            if (w2 & 0xFF800000)==0x91000000 and (w2&0x1f)==rd:
                imm12=(w2>>10)&0xFFF; sh=(w2>>22)&3
                addr=page+(imm12<<(12*sh))
                if addr==target:
                    out.append((i*4,(i+j)*4))
    return out

d=open(sys.argv[1],'rb').read()
for a in sys.argv[2:]:
    t=int(a,16)
    xs=xrefs(d,t)
    print(f'== {a}')
    for x in xs:
        print(f'   xref @ {hex(x[0])} (add @ {hex(x[1])})')
        # context: 64 bytes before, 96 after
        s=max(0,x[0]-64); e=min(len(d),x[1]+96)
        for k in range(s,e,4):
            mark=' <==' if k in x else ''
            print(f'      {k:#08x}: {struct.unpack_from("<I",d,k)[0]:08x}{mark}')
    if not xs: print('   nessuna xref ADRP+ADD trovata')
