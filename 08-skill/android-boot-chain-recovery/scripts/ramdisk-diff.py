#!/usr/bin/env python3
"""Exhaustively compare the ramdisks of two Android boot images.

usage:
    ramdisk-diff.py IMAGE_A IMAGE_B

IMAGE_A is the reference (the image that boots), IMAGE_B the candidate.
Prints container formats, the header fields that differ, whether the kernel
region is byte-identical, and the complete entry-by-entry cpio inventory,
split into: only in A, only in B, same path with different content, same path
with different metadata. Classify the output by impact on the kernel's boot
path before you spend a flash cycle on any single difference.

Exit status is 0 even when the images differ: the diff is the result.
"""
import gzip
import hashlib
import os
import stat
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

PAGE = 4096
MAGICS = {
    b"\x1f\x8b": "gzip",
    b"\x02\x21\x4c\x18": "LZ4 legacy (0x184C2102)",
    b"\x04\x22\x4d\x18": "LZ4 frame (0x184D2204)",
    b"070701": "raw cpio newc",
    b"070702": "raw cpio newc (crc)",
}


def roundup(x):
    return (x + PAGE - 1) // PAGE * PAGE


def extract(path):
    d = Path(path).read_bytes()
    if d[:8] != b"ANDROID!":
        raise SystemExit("%s: not an Android boot image (magic %r)" % (path, d[:8]))
    kernel_size, ramdisk_size = struct.unpack_from("<II", d, 8)
    os_version, header_size = struct.unpack_from("<II", d, 16)
    header_version = struct.unpack_from("<I", d, 40)[0]
    signature_size = struct.unpack_from("<I", d, 1580)[0]
    k_off = PAGE
    r_off = k_off + roundup(kernel_size)
    s_off = r_off + roundup(ramdisk_size)
    return {
        "path": path,
        "bytes": len(d),
        "header_version": header_version,
        "header_size": header_size,
        "os_version": os_version,
        "kernel": d[k_off:k_off + kernel_size],
        "ramdisk": d[r_off:r_off + ramdisk_size],
        "signature": d[s_off:s_off + signature_size],
        "signature_size": signature_size,
        "cmdline": d[44:1580].split(b"\x00", 1)[0].decode("utf-8", "replace"),
    }


def decompress(blob, label):
    magic = blob[:4]
    name = MAGICS.get(magic) or MAGICS.get(magic[:2]) or "unknown (%s)" % magic.hex()
    if magic[:2] == b"\x1f\x8b":
        return name, gzip.decompress(blob)
    if magic in (b"\x02\x21\x4c\x18", b"\x04\x22\x4d\x18"):
        tmp = tempfile.NamedTemporaryFile(suffix=".lz4", delete=False)
        tmp.write(blob)
        tmp.close()
        out = tmp.name + ".cpio"
        try:
            subprocess.run(["lz4", "-d", "-f", tmp.name, out], check=True,
                           capture_output=True)
        except FileNotFoundError:
            raise SystemExit("lz4 CLI not found: needed for %s ramdisks" % name)
        data = Path(out).read_bytes()
        os.unlink(tmp.name)
        os.unlink(out)
        return name, data
    return name, blob


def parse_cpio(data):
    """Every newc entry, across concatenated archives."""
    entries, off, n = [], 0, len(data)
    while off < n - 6:
        if data[off:off + 6] not in (b"070701", b"070702"):
            off += 4
            continue
        hdr = data[off:off + 110]
        f = [int(hdr[6 + i * 8:14 + i * 8], 16) for i in range(13)]
        ino, mode, uid, gid, nlink, mtime, size, dmaj, dmin, rmaj, rmin, namesz, _ = f
        nm = data[off + 110:off + 110 + namesz - 1].decode("utf-8", "replace")
        body = (off + 110 + namesz + 3) & ~3
        content = data[body:body + size]
        entries.append({
            "name": nm, "mode": mode, "uid": uid, "gid": gid, "size": size,
            "rdev": (rmaj, rmin),
            "sha": hashlib.sha256(content).hexdigest()[:16],
        })
        off = (body + size + 3) & ~3
        if nm == "TRAILER!!!":
            while off < n and data[off] == 0:
                off += 1
            if data[off:off + 6] not in (b"070701", b"070702"):
                break
    return entries


def kind(mode):
    for bit, label in ((stat.S_ISDIR, "dir"), (stat.S_ISLNK, "link"),
                       (stat.S_ISREG, "file"), (stat.S_ISCHR, "chardev"),
                       (stat.S_ISBLK, "blockdev"), (stat.S_ISFIFO, "fifo"),
                       (stat.S_ISSOCK, "socket")):
        if bit(mode):
            return label
    return "?"


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    a, b = extract(sys.argv[1]), extract(sys.argv[2])
    ra_fmt, ra = decompress(a["ramdisk"], "A")
    rb_fmt, rb = decompress(b["ramdisk"], "B")

    print("=== CONTAINER AND HEADER ===")
    print("%-22s A: %-28s B: %s" % ("raw ramdisk bytes", len(a["ramdisk"]), len(b["ramdisk"])))
    print("%-22s A: %-28s B: %s" % ("container", ra_fmt, rb_fmt))
    print("%-22s A: %-28s B: %s" % ("uncompressed", len(ra), len(rb)))
    print("%-22s A: %-28s B: %s" % ("image bytes", a["bytes"], b["bytes"]))
    print("%-22s A: %-28s B: %s" % ("header ver/size", a["header_version"], b["header_version"]))
    print("%-22s A: %-28s B: %s" % ("signature_size", a["signature_size"], b["signature_size"]))
    print("%-22s A: %-28s B: %s" % ("cmdline", repr(a["cmdline"][:60]), repr(b["cmdline"][:60])))
    print("%-22s A: %-28s B: %s" % ("kernel sha256", hashlib.sha256(a["kernel"]).hexdigest()[:16],
                                     hashlib.sha256(b["kernel"]).hexdigest()[:16]))
    print("kernel region byte-identical: %s" % (a["kernel"] == b["kernel"]))

    ea, eb = parse_cpio(ra), parse_cpio(rb)
    da = {e["name"]: e for e in ea}
    db = {e["name"]: e for e in eb}
    only_a = [n for n in da if n not in db]
    only_b = [n for n in db if n not in da]
    both = [n for n in da if n in db]
    diff_c = [n for n in both if da[n]["sha"] != db[n]["sha"] or da[n]["size"] != db[n]["size"]]
    diff_m = [n for n in both
              if (da[n]["mode"], da[n]["uid"], da[n]["gid"], da[n]["rdev"])
              != (db[n]["mode"], db[n]["uid"], db[n]["gid"], db[n]["rdev"])]

    print("\n=== INVENTORY ===")
    print("A: %d entries | B: %d entries | common: %d" % (len(ea), len(eb), len(both)))
    print("only in A: %d | only in B: %d | content differs: %d | metadata differs: %d"
          % (len(only_a), len(only_b), len(diff_c), len(diff_m)))

    print("\n=== CONTENT DIFFERS (the candidates that matter) ===")
    for n in sorted(diff_c):
        print("  %-40s A %s %8d %s   B %s %8d %s"
              % (n, kind(da[n]["mode"]), da[n]["size"], da[n]["sha"],
                 kind(db[n]["mode"]), db[n]["size"], db[n]["sha"]))

    print("\n=== METADATA DIFFERS ===")
    for n in sorted(diff_m):
        print("  %-40s A %s %d:%d   B %s %d:%d"
              % (n, oct(da[n]["mode"]), da[n]["uid"], da[n]["gid"],
                 oct(db[n]["mode"]), db[n]["uid"], db[n]["gid"]))

    print("\n=== ONLY IN A (reference) ===")
    for n in sorted(only_a):
        print("  %-44s %s %d" % (n, kind(da[n]["mode"]), da[n]["size"]))

    print("\n=== ONLY IN B (candidate) ===")
    for n in sorted(only_b):
        print("  %-44s %s %d" % (n, kind(db[n]["mode"]), db[n]["size"]))

    print("\n=== INIT AND ITS EXECUTION CHAIN ===")
    for n in sorted(set(["init", "sbin/init", "bin/sh", "bin/busybox"] + [
            x["name"] for x in ea + eb if x["name"].startswith("dev/")])):
        ea_n, eb_n = da.get(n), db.get(n)
        print("  %-22s A: %-30s B: %s" % (
            n,
            "%s %s %d" % (kind(ea_n["mode"]), oct(ea_n["mode"]), ea_n["size"]) if ea_n else "--- absent ---",
            "%s %s %d" % (kind(eb_n["mode"]), oct(eb_n["mode"]), eb_n["size"]) if eb_n else "--- absent ---"))
    print("\nRemember: container format and metadata are cheap to fix; a difference in")
    print("/init itself, or in what /init loads at runtime, is what usually decides boot.")


if __name__ == "__main__":
    main()
