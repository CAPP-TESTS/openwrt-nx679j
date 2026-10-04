#!/usr/bin/env python3
"""dtbo-replace.py -- sostituisce UNA voce (fragment) di un'immagine DTBO
(formato dt_table AOSP, header ed entry big-endian) lasciando invariato tutto
il resto: header, le altre voci e il loro contenuto, e la dimensione della
partizione. Pensato per il fix del pannello sulla voce #35 descritto in 5.1.

uso:
    python3 dtbo-replace.py  dtbo_b-orig.img  35  entry35-mod.dtb  dtbo_b-mod.img

Strategia: NON si scrive mai sopra lo spazio di un'altra voce. Il nuovo blob
viene SEMPRE aggiunto in coda ai dati gia' presenti (4-byte aligned), dentro il
padding della partizione; si aggiornano solo size/offset della voce scelta e il
campo total_size dell'header. Il vecchio blob resta come byte morti, non piu'
referenziati. Cosi' il risultato e' indipendente dalla correttezza del campo
size originale e non puo' corrompere le voci vicine, anche quando piu' voci
condividono lo stesso offset (dedup).
"""
import struct, sys, hashlib

if len(sys.argv) != 5:
    sys.exit(__doc__)
src, idx, newp, out = sys.argv[1], int(sys.argv[2], 0), sys.argv[3], sys.argv[4]

d = bytearray(open(src, 'rb').read())
new = open(newp, 'rb').read()

# --- header dt_table (8 x uint32 big-endian) ---
magic, total, hsz, esz, cnt, eoff, psz, ver = struct.unpack_from('>8I', d, 0)
assert magic == 0xd7b7ab1e, "non e' un DTBO: magic 0x%08x" % magic
assert new[:4] == b'\xd0\x0d\xfe\xed', "la nuova voce non e' un FDT (magic d00dfeed)"
assert esz >= 16, "dt_entry_size inatteso: %d" % esz
assert 0 <= idx < cnt, "indice %d fuori intervallo (0..%d)" % (idx, cnt - 1)
assert eoff + cnt * esz <= len(d), "tabella voci oltre il file"
assert total <= len(d), "total_size (%d) oltre il file (%d)" % (total, len(d))

ent_off = eoff + idx * esz
old_size, old_off = struct.unpack_from('>2I', d, ent_off)

# quante altre voci puntano allo stesso dato (dedup): non vanno toccate
shared = [i for i in range(cnt) if i != idx
          and struct.unpack_from('>2I', d, eoff + i * esz)[1] == old_off]

# nuovo blob SEMPRE in coda ai dati reali, allineato a 4 byte, dentro il padding
new_off = (total + 3) & ~3
end = new_off + len(new)
assert end <= len(d), ("spazio insufficiente nella partizione: servono %d byte, "
                       "ne restano %d" % (end - total, len(d) - total))
d[new_off:end] = new
struct.pack_into('>I', d, 4, end)                 # total_size = nuova coda
struct.pack_into('>2I', d, ent_off, len(new), new_off)  # size, offset della voce idx

open(out, 'wb').write(d)
print("voce %d: %d -> %d byte, spostata @0x%x (prima @0x%x)%s"
      % (idx, old_size, len(new), new_off, old_off,
         "; dedup preservato per voci %s" % shared if shared else ""))
print("file %d byte (invariato); total_size %d; sha256 %s"
      % (len(d), end, hashlib.sha256(d).hexdigest()))
