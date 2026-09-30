#!/usr/bin/env python3
"""
Verifica crittografica indipendente della firma Qualcomm MBNv7 (hash segment)
presente in xbl_a.img / abl_a.img.
Uso: python3 verify_bootloader_signature.py <immagine> <offset_hash_segment>
Esempio: python3 verify_bootloader_signature.py abl_a.img 0x29000
"""
import struct, sys, hashlib
from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec

FN = {  # nome file -> lista di offset dei hash segment (decodificati dai program header)
    "abl": [0x29000],
    "xbl": [0x412F4, 0x1049F4],
}


def certs_in(data, start, size):
    pos, end, out = start, min(start + size, len(data)), []
    while pos < end - 4:
        if data[pos] == 0x30 and data[pos + 1] == 0x82:
            ln = struct.unpack(">H", data[pos + 2:pos + 4])[0]
            try:
                out.append((pos, 4 + ln, x509.load_der_x509_certificate(data[pos:pos + 4 + ln])))
                pos += 4 + ln
                continue
            except Exception:
                pass
        pos += 1
    return out


def check(path, hdr_off):
    d = open(path, "rb").read()
    (_, ver, cm, qm, om, hs, qs, qc, os_, oc) = struct.unpack_from("<10I", d, hdr_off)
    p = hdr_off + 40
    p_qm, p_om, p_ht = p + cm, p + cm + qm, p + cm + qm + om
    p_qs, p_qc = p_ht + hs, p_ht + hs + qs
    p_os, p_oc = p_qc + qc, p_qc + qc + os_
    print(f"\n== {path}  hash segment @0x{hdr_off:x}  (MBN header version {ver})")
    print(f"   common_meta={cm} qti_meta={qm} oem_meta={om} hash_table={hs}B ({hs//48} SHA-384 entries)")
    print(f"   qti_sig={qs} qti_cert_chain={qc} oem_sig={os_} oem_cert_chain={oc}")
    print(f"   hash_table@0x{p_ht:x}  qti_sig@0x{p_qs:x} qti_chain@0x{p_qc:x}  oem_sig@0x{p_os:x} oem_chain@0x{p_oc:x}")
    msg = d[hdr_off:p_ht + hs]          # header + metadata + hash table = byte firmati
    print(f"   messaggio firmato [0x{hdr_off:x}..0x{p_ht+hs:x}) = {len(msg)} byte")
    # verifica hash della tabella contro i segmenti reali
    for which, soff, ssz, coff, csz in (("QTI", p_qs, qs, p_qc, qc), ("OEM", p_os, os_, p_oc, oc)):
        if not ssz:
            print(f"   {which}: nessuna firma presente")
            continue
        cs = certs_in(d, coff, coff + csz)
        sig = d[soff:soff + ssz]
        ok = None
        for sl in (ssz, ssz - 1):
            for off, ln, c in cs[:1]:
                try:
                    c.public_key().verify(sig[:sl], msg, ec.ECDSA(hashes.SHA384()))
                    ok = (off, ln, c)
                    break
                except Exception:
                    pass
            if ok:
                break
        if ok:
            c = ok[2]
            print(f"   {which} SIGNATURE: VALIDA (ECDSA P-384/SHA-384, DER {ssz}B) con la chiave del certificate @0x{ok[0]:x}")
            print(f"        subject : {c.subject.rfc4514_string()}")
            print(f"        issuer  : {c.issuer.rfc4514_string()}")
            print(f"        serial  : {c.serial_number:x}   valid: {c.not_valid_before_utc} .. {c.not_valid_after_utc}")
            print(f"        sha256  : {c.fingerprint(hashes.SHA256()).hex()}")
        else:
            print(f"   {which} SIGNATURE: NON verificata con i candidati testati")
        for off, ln, c in cs:
            print(f"        chain cert @0x{off:x} ({ln}B): {c.subject.rfc4514_string()}")
        # catena (ordine leaf-first: cert[i] firmato da cert[i+1])
        for i in range(len(cs) - 1):
            try:
                cs[i + 1][2].public_key().verify(
                    cs[i][2].signature, cs[i][2].tbs_certificate_bytes,
                    ec.ECDSA(cs[i][2].signature_hash_algorithm))
                print(f"        catena: cert[{i}] verificato con la chiave di cert[{i+1}] -> OK")
            except Exception as e:
                print(f"        catena: cert[{i}] -> cert[{i+1}] : {type(e).__name__}")
        last = cs[-1][2]
        try:
            last.public_key().verify(last.signature, last.tbs_certificate_bytes,
                                     ec.ECDSA(last.signature_hash_algorithm))
            print(f"        catena: cert[{len(cs)-1}] (root) auto-firmato -> OK")
        except Exception as e:
            print(f"        catena: root self-sign: {type(e).__name__}")
    return d


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    check(sys.argv[1], int(sys.argv[2], 0))
