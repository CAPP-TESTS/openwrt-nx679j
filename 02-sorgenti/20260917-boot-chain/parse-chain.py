#!/usr/bin/env python3
"""Analisi offline, sola lettura, degli stadi della catena di boot NX679J.
Nessun accesso al device: usa solo i readback locali.
"""
import hashlib, json, os, struct, subprocess, sys

BASE = "/home/user/nx679j-stock/experiments/20260916-122926-native-baseline"
RB = os.path.join(BASE, "current-readback")
RT = os.path.join(BASE, "runtime-v2")
DIAG = os.path.join(BASE, "diag-gpt-bootchain-20260917-034017")

def h(path, n=None):
    hh = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            hh.update(b)
            if n is not None:
                n -= len(b)
                if n <= 0:
                    break
    return hh.hexdigest()

def sz(path):
    return os.path.getsize(path)

def hdr(t):
    print("\n" + "=" * 78)
    print("## " + t)
    print("=" * 78)

# ---------------------------------------------------------------- 1. GPT sde
hdr("1. GPT di sde: decodifica dei bit di attributo (metadati A/B)")
ents = open(os.path.join(RT, "sde-primary-entries.bin"), "rb").read()
print("file: runtime-v2/sde-primary-entries.bin  dim=%d  sha256=%s" % (len(ents), hashlib.sha256(ents).hexdigest()))
print("geometria sde: %s settori da 4096" % open(os.path.join(RT, "sde-geometry.txt")).read().split())
ATTR_NAMES = {48: "priority bit0", 49: "priority bit1", 50: "successful", 54: "unbootable"}
def decode_attr(v):
    prio = (v >> 48) & 0x3
    succ = (v >> 50) & 0x1
    tries = (v >> 51) & 0x7
    unb = (v >> 54) & 0x1
    rest = [b for b in range(0, 64) if (v >> b) & 1 and not (48 <= b <= 56)]
    return dict(raw=hex(v), priority=prio, successful=succ, tries_remaining=tries,
                unbootable=unb, altri_bit=rest)
rows = []
for i in range(32):
    e = ents[i * 128:(i + 1) * 128]
    if e[:16] == b"\x00" * 16:
        continue
    first, last = struct.unpack_from("<QQ", e, 32)
    attr = struct.unpack_from("<Q", e, 48)[0]
    name = e[56:128].decode("utf-16-le").split("\x00")[0]
    rows.append((i, name, first, last, attr))
for i, name, first, last, attr in rows:
    d = decode_attr(attr)
    if attr == 0:
        continue
    print("  entry %2d  %-22s lba %8d..%-8d  attr=%s  prio=%d succ=%d tries=%d unbootable=%d  altri_bit=%s"
          % (i, name, first, last, d["raw"], d["priority"], d["successful"], d["tries_remaining"], d["unbootable"], d["altri_bit"]))
print("\nPartizioni con attributi NON nulli (tutte):")
for i, name, first, last, attr in rows:
    print("    %-24s %s" % (name, hex(attr)))

# ---------------------------------------------------------------- 2. boot_a
hdr("2. boot_a.img — header v4 completo")
boot_a_path = os.path.join(RB, "boot_a.img")
d = open(boot_a_path, "rb").read(4096)
print("file: %s  dim=%d  sha256=%s" % (boot_a_path, sz(boot_a_path), h(boot_a_path)))
magic = d[:8]
ksize, rsize, osver, hsize = struct.unpack_from("<IIII", d, 8)
reserved = struct.unpack_from("<IIII", d, 24)
hversion = struct.unpack_from("<I", d, 40)[0]
cmdline = d[44:44 + 1536]
sigsize = struct.unpack_from("<I", d, 1580)[0]
print("magic=%r kernel_size=%d ramdisk_size=%d os_version=0x%08x header_size=%d" % (magic, ksize, rsize, osver, hsize))
print("reserved=%s header_version=%d signature_size=%d" % (reserved, hversion, sigsize))
print("cmdline grezza (bytes 44..1579) = %r" % cmdline.split(b"\x00")[0])
nz = sum(1 for b in cmdline if b)
print("byte non-zero nella cmdline: %d / 1536" % nz)
print("layout atteso: kernel @%d (0x%x) len %d | ramdisk @%d len %d | signature @%d len %d | fine dati @%d"
      % (4096, 4096, ksize, 4096 + ksize, rsize, 4096 + ksize + rsize, sigsize, 4096 + ksize + rsize + sigsize))

# sha dei blocchi
kpath = os.path.join(BASE, "unpacked-current", "boot_a", "kernel")
rpath = os.path.join(BASE, "unpacked-current", "boot_a", "ramdisk")
spath = os.path.join(BASE, "unpacked-current", "boot_a", "boot_signature")
for p, lbl in ((kpath, "kernel"), (rpath, "ramdisk"), (spath, "boot_signature")):
    if os.path.exists(p):
        print("  %-14s %10d B  sha256=%s" % (lbl, sz(p), h(p)))

# ---------------------------------------------------------------- 3. vendor_boot_a
hdr("3. vendor_boot_a.img — header v4")
vpath = os.path.join(RB, "vendor_boot_a.img")
d = open(vpath, "rb").read(4096)
print("file: %s dim=%d sha256=%s" % (vpath, sz(vpath), h(vpath)))
print("magic=%r header_version=%d page_size=%d" % (d[:8], struct.unpack_from("<I", d, 8)[0], struct.unpack_from("<I", d, 12)[0]))
kaddr, raddr, vrsize = struct.unpack_from("<III", d, 16)
vcmd = d[28:28 + 2048]
print("kernel_addr=0x%x ramdisk_addr=0x%x vendor_ramdisk_size=%d" % (kaddr, raddr, vrsize))
print("vendor cmdline = %r" % vcmd.split(b"\x00")[0])
tags, name, hsize, dtbsize = struct.unpack_from("<I", d, 2076)[0], d[2080:2096], struct.unpack_from("<I", d, 2096)[0], struct.unpack_from("<I", d, 2100)[0]
dtbaddr = struct.unpack_from("<Q", d, 2104)[0]
print("tags_addr=0x%x name=%r header_size=%d dtb_size=%d dtb_addr=0x%x" % (tags, name.split(b"\x00")[0], hsize, dtbsize, dtbaddr))
vtabsize, vtabnum, vtabsz, bcsize = struct.unpack_from("<IIII", d, 2112)
print("vendor_ramdisk_table_size=%d table_entry_num=%d entry_size=%d bootconfig_size=%d" % (vtabsize, vtabnum, vtabsz, bcsize))
for slot in ("a", "b"):
    p = os.path.join(BASE, "unpacked-current", "vendor_boot_%s" % slot, "vendor_ramdisk00")
    p2 = os.path.join(BASE, "unpacked-current", "vendor_boot_%s" % slot, "dtb")
    p3 = os.path.join(BASE, "unpacked-current", "vendor_boot_%s" % slot, "bootconfig")
    print("  vendor_boot_%s: vendor_ramdisk00 %d B sha=%s" % (slot, sz(p), h(p)))
    print("  vendor_boot_%s: dtb              %d B sha=%s" % (slot, sz(p2), h(p2)))
    print("  vendor_boot_%s: bootconfig       %d B sha=%s" % (slot, sz(p3), h(p3)))
print("\nbootconfig (testo, vendor_boot_a):")
print(open(os.path.join(BASE, "unpacked-current", "vendor_boot_a", "bootconfig")).read())

# ---------------------------------------------------------------- 4. dtbo
hdr("4. dtbo_a.img / dtbo_b.img — tabella DTBO")
for slot in ("a", "b"):
    p = os.path.join(RB, "dtbo_%s.img" % slot)
    with open(p, "rb") as f:
        head = f.read(64)
    magic, total, hsize, entry_size, entry_count, entries_off, page_size, version = struct.unpack_from(">IIIIIII I", head + b"\x00" * (36 - len(head)))[:8] if False else struct.unpack_from(">8I", head[:32])
    print("\ndtbo_%s.img dim=%d sha256=%s" % (slot, sz(p), h(p)))
    print("  magic=0x%08x total_size=%d header_size=%d dt_entry_size=%d dt_entry_count=%d dt_entries_offset=%d page_size=%d version=%d"
          % (magic, total, hsize, entry_size, entry_count, entries_off, page_size, version))
    with open(p, "rb") as f:
        f.seek(entries_off)
        tbl = f.read(entry_size * entry_count)
    nz = 0
    for i in range(entry_count):
        e = tbl[i * entry_size:(i + 1) * entry_size]
        dt_size, dt_off, id_, rev, cust = struct.unpack_from(">IIIII", e, 0)
        if dt_size or dt_off or id_:
            nz += 1
            if i < 6 or i > entry_count - 3:
                print("  entry %2d: dt_size=%-8d dt_offset=%-9d id=0x%08x rev=0x%08x custom=0x%08x" % (i, dt_size, dt_off, id_, rev, cust))
    print("  voci con contenuto non nullo: %d / %d" % (nz, entry_count))

# ---------------------------------------------------------------- 5. vbmeta
hdr("5. vbmeta_a.img / vbmeta_b.img — header AVB")
for slot in ("a", "b"):
    p = os.path.join(RB, "vbmeta_%s.img" % slot)
    d = open(p, "rb").read(256)
    print("\nvbmeta_%s.img dim=%d sha256=%s" % (slot, sz(p), h(p)))
    print("  magic=%r" % d[:4])
    (ver_maj, ver_min, auth_size, aux_size, alg, rollback, flags, rel_off, rel_size) = struct.unpack_from(">IIQQIIIIQ", d, 4)
    print("  required_libavb_version=%d.%d" % (ver_maj, ver_min))
    print("  auth_block_size=%d aux_block_size=%d algorithm_type=%d rollback_index=%d" % (auth_size, aux_size, alg, rollback))
    print("  flags=0x%08x -> HASHTREE_DISABLED=%d VERIFICATION_DISABLED=%d" % (flags, (flags >> 0) & 1, (flags >> 1) & 1))
    print("  rollback_index_location=%d release_string@%d len=%d" % (rollback, rel_off, rel_size))
    rs = d[rel_off:rel_off + rel_size] if rel_size < 200 else b"(oltre i 256 B letti)"
    print("  release_string=%r" % rs)
    if alg:
        print("  public_key_metadata_size=%d" % struct.unpack_from(">I", d, 20 + 4 + 4 + 8 + 8 + 4 + 4 + 4)[0])

# ---------------------------------------------------------------- 6. misc / BCB
hdr("6. misc.img — primo KiB (BCB atteso: 'bootonce-bootloader')")
p = os.path.join(RB, "misc.img")
d = open(p, "rb").read(4096)
print("file sha256=%s" % h(p))
print("primi 64 B = %r" % d[:64])
print("offset 0..31 come stringa = %r" % d[:32].split(b"\x00")[0])
print("byte non-zero nei primi 4096: %d" % sum(1 for b in d if b))
p7 = os.path.join(BASE, "probe-v7-devstate", "misc-now.bin")
d7 = open(p7, "rb").read(4096)
print("probe-v7-devstate/misc-now.bin (12:13 del 17/9) primi 32 B = %r" % d7[:32].split(b"\x00")[0])

# ---------------------------------------------------------------- 7. xbl/abl
hdr("7. xbl_a/b e abl_a/b — confronto")
for n in ("xbl_a.img", "xbl_b.img", "abl_a.img", "abl_b.img"):
    p = os.path.join(DIAG, n)
    print("  %-12s %9d B sha256=%s" % (n, sz(p), h(p)))
for pair in (("xbl_a.img", "xbl_b.img"), ("abl_a.img", "abl_b.img")):
    a = open(os.path.join(DIAG, pair[0]), "rb").read()
    b = open(os.path.join(DIAG, pair[1]), "rb").read()
    diff = sum(1 for x, y in zip(a, b) if x != y)
    print("  %s vs %s: lunghezze %d/%d, byte diversi=%d" % (pair[0], pair[1], len(a), len(b), diff))

# ---------------------------------------------------------------- 8. config.gz
hdr("8. /proc/config.gz del kernel in esecuzione (runtime-v2/config.gz)")
import gzip
cfg = gzip.open(os.path.join(RT, "config.gz"), "rt", errors="replace").read().splitlines()
print("righe=%d sha256(config.gz)=%s" % (len(cfg), h(os.path.join(RT, "config.gz"))))
keys = ["CONFIG_RD_GZIP", "CONFIG_RD_LZ4", "CONFIG_RD_ZSTD", "CONFIG_RD_XZ", "CONFIG_RD_LZO",
        "CONFIG_BLK_DEV_INITRD", "CONFIG_INITRAMFS_SOURCE", "CONFIG_BINFMT_SCRIPT", "CONFIG_BINFMT_ELF",
        "CONFIG_PANIC_TIMEOUT", "CONFIG_PANIC_ON_OOPS", "CONFIG_KALLSYMS", "CONFIG_DEVTMPFS",
        "CONFIG_MODULES", "CONFIG_MODULE_FORCE_LOAD", "CONFIG_USB_CONFIGFS", "CONFIG_USB_F_NCM",
        "CONFIG_USB_F_RNDIS", "CONFIG_USB_F_ACM", "CONFIG_SCSI_UFS_QCOM", "CONFIG_PHY_QCOM_QMP_UFS",
        "CONFIG_PHY_QCOM_USB_SNPS_FEMTO_V2", "CONFIG_USB_DWC3", "CONFIG_USB_DWC3_MSM", "CONFIG_PSTORE",
        "CONFIG_PSTORE_RAM", "CONFIG_PSTORE_CONSOLE", "CONFIG_PSTORE_PMSG", "CONFIG_ANDROID_BINDERFS",
        "CONFIG_ANDROID", "CONFIG_EXT4_FS", "CONFIG_F2FS_FS", "CONFIG_OVERLAY_FS", "CONFIG_TMPFS",
        "CONFIG_SQUASHFS", "CONFIG_USB_GADGET", "CONFIG_LOCALVERSION", "CONFIG_DEBUG_INFO_BTF"]
kv = {}
for line in cfg:
    if "=" in line and not line.startswith("#"):
        k, v = line.split("=", 1)
        kv[k] = v
for k in keys:
    print("  %-38s = %s" % (k, kv.get(k, "(non presente)")))
print("\n  riga CONFIG_INITRAMFS*:")
for line in cfg:
    if "INITRAMFS" in line:
        print("    " + line)
print("\n  righe MODULE_SIG / VERIFY:")
for line in cfg:
    if "MODULE_SIG" in line or "MODULE_COMPRESS" in line:
        print("    " + line)
print("\n  righe RANDOMIZE / KASLR:")
for line in cfg:
    if "RANDOMIZE" in line:
        print("    " + line)
print("\n  version string del kernel (uname -r da adb-uname.txt):")
print("   " + open(os.path.join(BASE, "adb-uname.txt")).read().strip())
print("   stringa versione in Image (unpacked-current/boot_a/kernel):")
k = open(os.path.join(BASE, "unpacked-current", "boot_a", "kernel"), "rb").read(1 << 20)
i = k.find(b"Linux version ")
print("   %r" % k[i:i + 160])
