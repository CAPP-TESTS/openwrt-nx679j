================================================================================
 dtbo-replace.py  —  versione corretta (5 ottobre 2026)
 Script dell'Appendice A della guida; usato al passo 5.1 (dtbo_b, slot B).

 COSA FA E COSA NON FA
 --------------------------------------------------------------------------
 Reinserisce un overlay .dtb GIA' modificato in una voce dell'immagine DTBO.
 Non modifica il contenuto degli overlay. La correzione del pannello si fa
 prima, con dtc: nel .dts della voce si rinomina l'etichetta nel nodo
 __fixups__ (dsi_r66451_amoled_cmd -> dsi_nubia_r6130_amoled_cmd_dphy).

 CORREZIONI RISPETTO ALLA VERSIONE PRECEDENTE
 --------------------------------------------------------------------------
 1. Controlli espliciti al posto di 'assert': con "python3 -O" gli assert
    vengono eliminati e la versione precedente avrebbe scritto senza alcun
    controllo.
 2. Validazione del nuovo .dtb: oltre al magic, il campo totalsize dell'FDT
    deve coincidere con la dimensione del file (un .dtb troncato veniva
    accettato) e la versione FDT deve essere >= 16.
 3. Validazione dell'immagine di partenza: header_size e dt_entries_offset
    coerenti, ogni voce dentro total_size e che inizi con un FDT.
 4. La zona di destinazione deve essere tutta a zero; se in coda alla
    partizione c'e' un footer AVB ("AVBf") lo script si ferma. Prima scriveva
    senza guardare cosa sovrascriveva.
 5. Rifiuta di scrivere sopra il file di ingresso (l'originale va conservato).
 6. Dopo la scrittura rilegge il file e verifica: header (tranne total_size),
    le altre voci e i loro contenuti identici all'originale; la voce scelta
    contiene esattamente il nuovo overlay; id/rev/custom invariati.
 7. Docstring corretta: non suggerisce piu' che lo script faccia il fix.

 VERIFICHE ESEGUITE
 --------------------------------------------------------------------------
 - Formato confrontato con dtbo-parse.py del progetto e con i valori reali
   del dtbo NX679J documentati nel repository (02-sorgenti/20260917-boot-chain):
   44 voci da 32 byte, dt_entries_offset 32, total_size 9.447.224, offset
   delle voci non allineati (es. 110915): l'allineamento non e' richiesto.
 - Hash descriptor AVB di dtbo: e' in vbmeta, non in un footer della
   partizione (BOOT-CHAIN-NX679J.md §6.5). Il padding dopo total_size e' zero.
 - Test su un DTBO sintetico con la stessa forma (44 voci, entry size 32,
   offset non allineati, 24 MiB), overlay compilati con dtc -@.
   Catena completa: dtbo-parse.py -> dtc -> rinomina in __fixups__ -> dtc ->
   dtbo-replace.py -> dtbo-parse.py. Risultato identico con e senza -O.
 - Con fdtoverlay su un DTB base di prova: prima della modifica il pannello
   punta al nodo r66451, dopo punta al nodo r6130.
 - Ogni controllo di sicurezza provato: argomenti, uscita = ingresso, .dtb
   non FDT, .dtb troncato, magic DTBO errato, indice fuori intervallo, indice
   esadecimale (0x23 = 35), footer AVB, zona di destinazione non a zero,
   spazio insufficiente.

 LIMITI
 --------------------------------------------------------------------------
 - Ricostruzione: il repository contiene solo il lato lettura (dtbo-parse.py),
   non lo script di scrittura degli autori.
 - Non provato sul dtbo reale del telefono (blob vendor, non nel repository).
 - Dopo la modifica l'hash di dtbo non corrisponde piu' a quello in vbmeta:
   va bene solo con bootloader SBLOCCATO. Prima di un eventuale relock,
   dtbo_b va riportato all'originale.
 - Verificare sempre con dtbo-parse.py e con le due letture a freddo
   dopo la scrittura su dtbo_b.

 Uso: salvare tutto a partire dalla riga "#!/usr/bin/env python3" in un file
 dtbo-replace.py, poi:
   python3 dtbo-replace.py dtbo_b-orig.img 35 e35-mod.dtb dtbo_b-mod.img
================================================================================

#!/usr/bin/env python3
"""dtbo-replace.py -- reinserisce UNA voce (overlay FDT) gia' preparata dentro
un'immagine DTBO (formato dt_table AOSP, header e voci big-endian), lasciando
invariato tutto il resto: header (tranne total_size), le altre voci e il loro
contenuto, la dimensione della partizione.

NON modifica il contenuto degli overlay: non cerca e non rinomina nodi o
etichette. La modifica del pannello si fa PRIMA, con dtc, sul file .dts della
voce (rinomina dell'etichetta nel nodo __fixups__). Questo script esegue solo
l'ultimo passo: rimettere il .dtb modificato nella voce scelta.

uso:
    python3 dtbo-replace.py  dtbo_b-orig.img  35  entry35-mod.dtb  dtbo_b-mod.img

Strategia (append-only): il nuovo blob viene SEMPRE scritto in coda ai dati
esistenti, dentro il padding a zero della partizione, allineato a 4 byte; si
aggiornano solo size/offset della voce scelta e il campo total_size. Il vecchio
blob resta come byte morti non piu' referenziati. Cosi' non si scrive mai sopra
un'altra voce, anche se piu' voci condividono lo stesso offset.

Tutti i controlli sono espliciti (non 'assert': con python3 -O gli assert
verrebbero eliminati). Alla fine il file scritto viene riletto e verificato.
"""
import hashlib
import os
import struct
import sys

DTBO_MAGIC = 0xd7b7ab1e
FDT_MAGIC = 0xd00dfeed
AVB_FOOTER_MAGIC = b'AVBf'


def die(msg):
    sys.exit('ERRORE: ' + msg)


def entries(buf, cnt, eoff, esz):
    return [struct.unpack_from('>4I', buf, eoff + i * esz) for i in range(cnt)]


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    src, idx_s, newp, out = sys.argv[1:5]
    try:
        idx = int(idx_s, 0)
    except ValueError:
        die('indice non valido: %r' % idx_s)
    if os.path.abspath(src) == os.path.abspath(out):
        die("l'uscita non puo' coincidere con l'ingresso: conserva l'originale")

    d = bytearray(open(src, 'rb').read())
    new = open(newp, 'rb').read()

    # --- header dt_table: 8 x uint32 big-endian ---
    if len(d) < 32:
        die('file troppo corto per un DTBO')
    magic, total, hsz, esz, cnt, eoff, psz, ver = struct.unpack_from('>8I', d, 0)
    if magic != DTBO_MAGIC:
        die("non e' un DTBO: magic 0x%08x" % magic)
    if hsz < 32 or eoff < hsz:
        die('header_size/dt_entries_offset incoerenti: %d / %d' % (hsz, eoff))
    if esz < 16:
        die('dt_entry_size inatteso: %d' % esz)
    if not 0 <= idx < cnt:
        die('indice %d fuori intervallo (0..%d)' % (idx, cnt - 1))
    if eoff + cnt * esz > total:
        die('tabella delle voci oltre total_size')
    if total > len(d):
        die('total_size (%d) oltre il file (%d)' % (total, len(d)))

    # --- coerenza dell'immagine di partenza: ogni voce dentro total_size ---
    ents = entries(d, cnt, eoff, esz)
    for i, (sz, off, _, _) in enumerate(ents):
        if off < eoff + cnt * esz or off + sz > total:
            die('voce %d fuori dai dati (off=%d size=%d total=%d)' % (i, off, sz, total))
        if d[off:off + 4] != struct.pack('>I', FDT_MAGIC):
            die('voce %d non inizia con un FDT' % i)

    # --- validazione del nuovo overlay ---
    if len(new) < 40 or struct.unpack_from('>I', new, 0)[0] != FDT_MAGIC:
        die("la nuova voce non e' un FDT (magic d00dfeed)")
    fdt_total, = struct.unpack_from('>I', new, 4)
    fdt_ver, = struct.unpack_from('>I', new, 20)
    if fdt_total != len(new):
        die("FDT incoerente: totalsize=%d ma il file e' di %d byte (troncato?)"
            % (fdt_total, len(new)))
    if fdt_ver < 16:
        die('versione FDT inattesa: %d' % fdt_ver)

    # --- non toccare un eventuale footer AVB in coda alla partizione ---
    if d[-64:-60] == AVB_FOOTER_MAGIC:
        die('trovato un footer AVB in coda alla partizione: caso non gestito, '
            'nessuna scrittura')

    ent_off = eoff + idx * esz
    old_size, old_off = ents[idx][0], ents[idx][1]
    shared = [i for i, e in enumerate(ents) if i != idx and e[1] == old_off]

    # --- nuovo blob in coda ai dati reali, allineato a 4 byte ---
    new_off = (total + 3) & ~3
    end = new_off + len(new)
    if end > len(d):
        die('spazio insufficiente: servono %d byte oltre total_size, ne restano %d'
            % (end - total, len(d) - total))
    if any(d[total:end]):
        die("la zona di destinazione (0x%x..0x%x) non e' tutta a zero: "
            'contiene dati sconosciuti, nessuna scrittura' % (total, end))

    d[new_off:end] = new
    struct.pack_into('>I', d, 4, end)                       # total_size
    struct.pack_into('>2I', d, ent_off, len(new), new_off)  # size, offset voce idx

    with open(out, 'wb') as f:
        f.write(d)

    # --- verifica: rilettura del file scritto e confronto con l'originale ---
    r = open(out, 'rb').read()
    orig = open(src, 'rb').read()
    if len(r) != len(orig):
        die('verifica: dimensione cambiata')
    rh = struct.unpack_from('>8I', r, 0)
    oh = struct.unpack_from('>8I', orig, 0)
    if rh[0] != oh[0] or rh[2:] != oh[2:] or rh[1] != end:
        die('verifica: header inatteso')
    rents = entries(r, cnt, eoff, esz)
    for i in range(cnt):
        if i == idx:
            continue
        a, b = eoff + i * esz, eoff + (i + 1) * esz
        if r[a:b] != orig[a:b]:
            die('verifica: voce %d modificata' % i)
        sz, off = rents[i][0], rents[i][1]
        if r[off:off + sz] != orig[off:off + sz]:
            die('verifica: contenuto della voce %d modificato' % i)
    sz, off = rents[idx][0], rents[idx][1]
    if (sz, off) != (len(new), new_off) or r[off:off + sz] != new:
        die('verifica: la voce %d non contiene il nuovo overlay' % idx)
    a, b = eoff + idx * esz + 8, eoff + (idx + 1) * esz
    if r[a:b] != orig[a:b]:
        die('verifica: id/rev/custom della voce %d modificati' % idx)

    print('voce %d: %d -> %d byte, spostata @0x%x (prima @0x%x)%s'
          % (idx, old_size, len(new), new_off, old_off,
             '; voci con lo stesso offset lasciate intatte: %s' % shared
             if shared else ''))
    print('total_size %d -> %d; file %d byte (invariato)' % (total, end, len(r)))
    print('verifica OK: header e le altre %d voci invariati' % (cnt - 1))
    print('sha256 %s' % hashlib.sha256(r).hexdigest())


if __name__ == '__main__':
    main()
