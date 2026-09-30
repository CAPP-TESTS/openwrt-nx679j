#!/usr/bin/env python3
"""Analisi stadio 2: dtb container, dtbo applicato, ramdisk (magiskinit + moduli)."""
import hashlib, io, os, struct, subprocess, sys

BASE = "/home/user/nx679j-stock/experiments/20260916-122926-native-baseline"
RB = os.path.join(BASE, "current-readback")
UC = os.path.join(BASE, "unpacked-current")
WORK = "/home/user/nx679j-stock/experiments/20260917-boot-chain/work"
os.makedirs(WORK, exist_ok=True)

def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()

def hdr(t):
    print("\n" + "=" * 78 + "\n## " + t + "\n" + "=" * 78)

# ------------------------------------------------ A. DTB container in vendor_boot
hdr("A. vendor_boot dtb: e' un contenitore multi-DTB?")
p = os.path.join(UC, "vendor_boot_a", "dtb")
d = open(p, "rb").read(64)
print("primi 64 byte: %s" % d[:32].hex())
magic = struct.unpack_from(">I", d, 0)[0]
print("magic = 0x%08x" % magic)
if magic == 0xd7b7ab1e:
    (total, hsize, esz, ecnt, eoff, pgsz, ver) = struct.unpack_from(">IIIIIII", d, 4)
    print("  total_size=%d header_size=%d entry_size=%d entry_count=%d entries_offset=%d page_size=%d version=%d"
          % (total, hsize, esz, ecnt, eoff, pgsz, ver))
    with open(p, "rb") as f:
        f.seek(eoff)
        tbl = f.read(esz * ecnt)
    sizes = []
    for i in range(ecnt):
        e = tbl[i * esz:(i + 1) * esz]
        dt_size, dt_off, id_, rev, cust = struct.unpack_from(">IIIII", e, 0)
        sizes.append((i, dt_size, dt_off, id_, rev, cust))
    for i, s, o, id_, rev, cust in sizes:
        print("  entry %2d: size=%-8d off=%-9d id=0x%08x rev=0x%08x custom=0x%08x" % (i, s, o, id_, rev, cust))
    # estrai l'entry 5 (ro.boot.dtb_idx=5)
    idx = 5
    i, s, o, id_, rev, cust = sizes[idx]
    with open(p, "rb") as f:
        f.seek(o)
        blob = f.read(s)
    out = os.path.join(WORK, "dtb-vendor-idx%d.dtb" % idx)
    open(out, "wb").write(blob)
    print("  estratto entry %d -> %s (%d B) sha256=%s" % (idx, out, len(blob), hashlib.sha256(blob).hexdigest()))
else:
    blob = open(p, "rb").read()
    out = os.path.join(WORK, "dtb-vendor-single.dtb")
    open(out, "wb").write(blob)
    print("  singolo FDT -> %s sha256=%s" % (out, hashlib.sha256(blob).hexdigest()))

# ------------------------------------------------ B. bootargs dentro i DTB
hdr("B. chosen/bootargs dei DTB disponibili")
def decompile(src, dst):
    r = subprocess.run(["dtc", "-I", "dtb", "-O", "dts", "-o", dst, src],
                       capture_output=True, text=True)
    return r.returncode, r.stderr[:400]

out5 = os.path.join(WORK, "dtb-vendor-idx5.dtb")
rc, err = decompile(out5, os.path.join(WORK, "dtb-vendor-idx5.dts"))
print("dtc su %s -> rc=%d %s" % (out5, rc, err.replace("\n", " ")[:200] if rc else ""))
if rc == 0:
    txt = open(os.path.join(WORK, "dtb-vendor-idx5.dts")).read()
    import re
    for m in re.finditer(r'(model|compatible|bootargs)\s*=\s*"([^"]{0,400})"', txt[:200000]):
        print("   %-12s = %s" % (m.group(1), m.group(2)[:300]))

# dtbo entry
hdr("C. dtbo_a: entry 35 (ro.boot.dtbo_idx=35) e entry 0")
p = os.path.join(RB, "dtbo_a.img")
with open(p, "rb") as f:
    head = f.read(32)
magic, total, hsize, esz, ecnt, eoff, pgsz, ver = struct.unpack_from(">8I", head)
with open(p, "rb") as f:
    f.seek(eoff)
    tbl = f.read(esz * ecnt)
entries = []
for i in range(ecnt):
    e = tbl[i * esz:(i + 1) * esz]
    dt_size, dt_off, id_, rev, cust = struct.unpack_from(">IIIII", e, 0)
    entries.append((i, dt_size, dt_off, id_, rev, cust))
tot = sum(s for _, s, _, _, _, _ in entries)
print("somma dt_size = %d ; total_size dichiarato = %d ; delta = %d" % (tot, total, total - tot))
for idx in (35, 0, 5, 43):
    i, s, o, id_, rev, cust = entries[idx]
    with open(p, "rb") as f:
        f.seek(o)
        blob = f.read(s)
    out = os.path.join(WORK, "dtbo-entry%02d.dtb" % idx)
    open(out, "wb").write(blob)
    rc, err = decompile(out, os.path.join(WORK, "dtbo-entry%02d.dts" % idx))
    print("\n  entry %2d: dt_size=%-8d dt_offset=%-9d  -> %s sha256=%s  (dtc rc=%d)"
          % (idx, s, o, out, hashlib.sha256(blob).hexdigest(), rc))
    if rc == 0:
        txt = open(os.path.join(WORK, "dtbo-entry%02d.dts" % idx)).read()
        import re
        for m in re.finditer(r'(model|qcom,board-id|qcom,msm-id|bootargs)\s*=\s*("([^"]{0,300})"|<[^>]{0,200}>|\[[^\]]{0,200}\])', txt[:400000]):
            print("     %-16s = %s" % (m.group(1), m.group(2)[:250].replace("\n", " ")))

# ------------------------------------------------ D. ramdisk boot_a
hdr("D. boot_a ramdisk: contenuto (magiskinit)")
raw = open(os.path.join(UC, "boot_a", "ramdisk"), "rb").read()
print("compresso: %d B sha256=%s magic=%s" % (len(raw), hashlib.sha256(raw).hexdigest(), raw[:4].hex()))
if raw[:4] == b"\x02\x21\x4c\x18":
    r = subprocess.run(["lz4", "-l", "-d", "-c", os.path.join(UC, "boot_a", "ramdisk")], capture_output=True)
    u = r.stdout
    print("lz4 legacy: rc=%d, %d B" % (r.returncode, len(u)))
else:
    import gzip
    u = gzip.decompress(raw)
print("decompresso: %d B sha256=%s" % (len(u), hashlib.sha256(u).hexdigest()))
open(os.path.join(WORK, "magisk-ramdisk.cpio"), "wb").write(u)
r = subprocess.run(["cpio", "-itv", "--quiet"], input=u, capture_output=True)
lines = r.stdout.decode(errors="replace").strip().split("\n")
print("voci cpio: %d" % len(lines))
for l in lines:
    print("  " + l)

# /init = magiskinit
hdr("E. /init del boot ramdisk = magiskinit?")
r = subprocess.run(["bsdtar", "-xOf", os.path.join(WORK, "magisk-ramdisk.cpio"), "init"], capture_output=True)
if r.returncode != 0:
    r = subprocess.run(["cpio", "-i", "--to-stdout", "init"], input=u, capture_output=True)
mi = r.stdout
open(os.path.join(WORK, "magiskinit"), "wb").write(mi)
print("/init: %d B sha256=%s" % (len(mi), hashlib.sha256(mi).hexdigest()))
print("primi 20 byte: %s" % mi[:20].hex())
r = subprocess.run(["file", os.path.join(WORK, "magiskinit")], capture_output=True, text=True)
print(r.stdout.strip())
r = subprocess.run(["readelf", "-h", os.path.join(WORK, "magiskinit")], capture_output=True, text=True)
print("\n".join(l for l in r.stdout.splitlines() if any(k in l for k in ("Class", "Type:", "Machine", "Entry"))))
r = subprocess.run(["strings", "-n", "6", os.path.join(WORK, "magiskinit")], capture_output=True, text=True)
s = r.stdout.splitlines()
print("stringhe totali (len>=6): %d" % len(s))
keys = ["magisk", "Magisk", "switch_root", "first_stage", "fstab", "modules.load", "modprobe",
        "sepolicy", "overlay.d", ".backup", "config", "init.rc", "MAGISK_VER", "27.", "30.",
        "selinux", "setenforce", "tmpfs", "/proc", "system/bin/init", "dmesg"]
for k in keys:
    hits = [x for x in s if k in x]
    if hits:
        print("  -- %-18s %d occorrenze; esempi: %s" % (k, len(hits), hits[:6]))

# ------------------------------------------------ F. vendor ramdisk
hdr("F. vendor_boot vendor_ramdisk00: moduli e liste")
vraw = open(os.path.join(UC, "vendor_boot_a", "vendor_ramdisk00"), "rb").read()
print("compresso: %d B magic=%s" % (len(vraw), vraw[:4].hex()))
r = subprocess.run(["lz4", "-l", "-d", "-c", os.path.join(UC, "vendor_boot_a", "vendor_ramdisk00")], capture_output=True)
vu = r.stdout
print("decompresso: %d B sha256=%s" % (len(vu), hashlib.sha256(vu).hexdigest()))
open(os.path.join(WORK, "vendor-ramdisk.cpio"), "wb").write(vu)
r = subprocess.run(["cpio", "-it", "--quiet"], input=vu, capture_output=True)
names = r.stdout.decode(errors="replace").strip().split("\n")
print("voci cpio: %d" % len(names))
import collections
top = collections.Counter()
kos = []
for n in names:
    parts = n.split("/")
    top[parts[0] if len(parts) == 1 else "/".join(parts[:2])] += 1
    if n.endswith(".ko"):
        kos.append(n)
for k, v in sorted(top.items()):
    print("  %-30s %d" % (k, v))
print("file .ko: %d" % len(kos))
ml = subprocess.run(["cpio", "-i", "--to-stdout", "lib/modules/modules.load"], input=vu, capture_output=True).stdout.decode()
mlr = subprocess.run(["cpio", "-i", "--to-stdout", "lib/modules/modules.load.recovery"], input=vu, capture_output=True).stdout.decode()
print("\nmodules.load: %d righe" % len([x for x in ml.splitlines() if x.strip()]))
print("  sha256=%s" % hashlib.sha256(ml.encode()).hexdigest())
print("  prime 10: %s" % ml.splitlines()[:10])
print("modules.load.recovery: %d righe" % len([x for x in mlr.splitlines() if x.strip()]))
print("  sha256=%s" % hashlib.sha256(mlr.encode()).hexdigest())
# rischi
hdr("G. fstab nel vendor ramdisk e moduli pericolosi")
for name in ("first_stage_ramdisk/fstab.qcom",):
    rr = subprocess.run(["cpio", "-i", "--to-stdout", name], input=vu, capture_output=True)
    print("\n--- %s (%d B) ---" % (name, len(rr.stdout)))
    print(rr.stdout.decode(errors="replace"))
DANGER = ["cnss2", "icnss2", "cdsp-loader", "cdsprm", "mhi_", "atmel_mxt_ts", "aw9620x", "fsa4480",
          "qcom_q6v5", "qrtr", "wlan", "ipa", "glink", "smem", "socinfo", "boot_stats"]
for d in DANGER:
    m = [x for x in ml.splitlines() if d in x]
    mr = [x for x in mlr.splitlines() if d in x]
    if m or mr:
        print("  %-14s in modules.load: %-40s in modules.load.recovery: %s" % (d, m or "-", mr or "-"))
