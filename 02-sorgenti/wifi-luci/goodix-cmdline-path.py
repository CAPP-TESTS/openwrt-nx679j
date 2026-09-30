#!/usr/bin/env python3
"""Aggiunge firmware_class.path=<dir> al cmdline dell'immagine boot Android.

Perche': al probe del driver il kernel cerca i firmware in /lib/firmware del
SISTEMA MONTATO (rootfs ricostruita a ogni boot dallo stock), dove i nostri
goodix_*.bin non ci sono e non possono restare (e' una ramfs). La nostra copia
vive in /owrt/lib/firmware dentro il ramdisk flashato: esiste SEMPRE, dal primo
istante, e il kernel la trova se gliela indichiamo nella command line.

Formato header Android boot v3/v4:
  44 byte fissi (magic 8, kernel_size 4, ramdisk_size 4, os_version 4,
  header_size 4, reserved 16, header_version 4) poi il campo cmdline:
  v3 = 1536 byte, v4 = 4096 byte, terminato da NUL.

Uso: goodix-cmdline-path.py <boot.img> [dir]
"""
import pathlib
import struct
import sys

PARAM = "firmware_class.path=/owrt/lib/firmware"


def main() -> int:
    img = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "boot_b-v90-mmwd.img")
    want = sys.argv[2] if len(sys.argv) > 2 else PARAM
    if not img.exists():
        print(f"immagine non trovata: {img}")
        return 1
    d = bytearray(img.read_bytes())
    if bytes(d[:8]) != b"ANDROID!":
        print("non e' un'immagine Android boot")
        return 1
    hv = struct.unpack("<I", d[40:44])[0]
    if hv < 3:
        print(f"header v{hv}: campo cmdline non a offset 44, non gestito")
        return 1
    clen = 4096 if hv == 4 else 1536
    raw = bytes(d[44:44 + clen])
    old = raw.split(b"\x00", 1)[0].decode("ascii", "replace")
    key = want.split("=", 1)[0]
    if key in old:
        print(f"cmdline: {key} gia presente, nessuna modifica")
        print(f"valore attuale: {[w for w in old.split() if w.startswith(key)]}")
        return 0
    new = (old + " " + want).strip()
    enc = new.encode("ascii")[:clen - 1]
    d[44:44 + clen] = enc + b"\x00" * (clen - len(enc))
    img.write_bytes(bytes(d))
    print(f"cmdline aggiornato (header v{hv}, {clen} byte di campo):")
    print(f"  prima: ...{old[-80:]}")
    print(f"  dopo : ...{enc.decode('ascii')[-80:]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
