#!/usr/bin/env python3
"""
build-v55.py — NX679J boot_b v55 "wifi+luci persistente, incl. pacchetti".

v55 = v9.cpio + new-uid0.cpio (ramdisk live ca60) + overlay v55:
  A) owrt/** = albero rootfs LIVE catturato (con iw, wpad, wifi-scripts,
     iwinfo, wireless-regdb, ucode-mod-*, config network/wireless/uhttpd/dhcp
     incl. interfaccia modem) — tar --one-file-system, 1396 voci
  B) nx679j/switch.sh con blocco v55 (retry + logd)
Contenitore dalle stesse basi di v54 (boot_b-live.img), stessa lunghezza.
"""
import hashlib
import pathlib
import shutil
import struct
import subprocess
import sys

SES = pathlib.Path('/home/user/nx679j-stock/experiments/20260920-wifi-luci')
INIT9 = pathlib.Path('/home/user/nx679j-stock/experiments/20260917-init-v9')
W = SES / 'build-v55'
OVL = W / 'overlay'
KV = '5.10.66-android12-9-00005-gf6e6376090be-ab8060604'
IMG = SES / 'boot_b-live.img'
OUT = SES / 'boot_b-v55-wifi.img'
P = 4096


def ru(x):
    return (x + P - 1) // P * P


if W.exists():
    shutil.rmtree(W)
OVL.mkdir(parents=True)

# --- A) owrt/ dal tar live
subprocess.run(['tar', '-xzf', str(SES / 'owrt-live.tar.gz'), '-C', str(OVL),
                '--transform', 's|^\./|owrt/|'], check=True)
print('owrt estratti:', sum(1 for _ in (OVL / 'owrt').rglob('*')))
assert (OVL / 'owrt/usr/sbin/wpad').exists()
assert (OVL / 'owrt/sbin/wifi').exists()
assert (OVL / f'owrt/lib/modules/{KV}/qca_cld3_qca6490.ko').exists(), 'qca_cld3 assente dal tar (atteso dal boot v54)'
assert (OVL / 'owrt/etc/config/wireless').exists()

# --- B) switch.sh v55
sw = (SES / 'live-switch.sh').read_text()
block = (SES / 'v55-wifi-block.sh').read_text()
anchor = '    $BB chroot /owrt /sbin/init >> $J 2>&1\n'
assert anchor in sw
assert 'v55 wifi' not in sw
sw2 = sw.replace(anchor, block + anchor)
(OVL / 'nx679j').mkdir()
(OVL / 'nx679j/switch.sh').write_text(sw2)
(OVL / 'nx679j/switch.sh').chmod(0o755)
p = subprocess.run(['sh', '-n', str(OVL / 'nx679j/switch.sh')], capture_output=True)
assert p.returncode == 0, p.stderr.decode()
print('switch.sh v55: syntax OK')

# --- cpio + uid0
out = subprocess.run('find . | cpio -o -H newc --quiet', shell=True, cwd=OVL,
                     capture_output=True, check=True).stdout
data = bytearray(out)
off = 0
tz = 0
while off + 110 <= len(data) and data[off:off + 6] == b'070701':
    namesize = int(data[off + 94:off + 102], 16)
    filesize = int(data[off + 54:off + 62], 16)
    if data[off + 22:off + 30] != b'00000000' or data[off + 30:off + 38] != b'00000000':
        tz += 1
    data[off + 22:off + 30] = b'00000000'
    data[off + 30:off + 38] = b'00000000'
    namepad = namesize + ((4 - ((110 + namesize) % 4)) % 4)
    off = off + 110 + namepad + ((filesize + 3) & ~3)
print(f'v55.cpio {len(data)} B, {tz} uid azzerati')
(W / 'v55-uid0.cpio').write_bytes(bytes(data))

# --- merge + lz4
v9 = (INIT9 / 'v10-work/v9.cpio').read_bytes()
new = (INIT9 / 'v10-work/new-uid0.cpio').read_bytes()
merged = v9 + new + bytes(data)
(W / 'merged.cpio').write_bytes(merged)
print(f'merged: {len(merged)} B')
newram = subprocess.run(['lz4', '-l', '-9', '-c'], input=merged,
                        capture_output=True, check=True).stdout
(W / 'new-ramdisk.lz4').write_bytes(newram)
print(f'ramdisk lz4: {len(newram)} B')

# --- contenitore
img = IMG.read_bytes()
assert img[:8] == b'ANDROID!'
ks, rs = struct.unpack_from('<II', img, 8)
ram_off = P + ru(ks)
sig = img[ram_off + ru(rs):ram_off + ru(rs) + P]
hdr = bytearray(img[:P])
struct.pack_into('<I', hdr, 12, len(newram))
body = bytes(hdr) + img[P:P + ru(ks)] + newram + b'\0' * (ru(len(newram)) - len(newram)) + sig
outimg = body + img[len(body):]
assert len(outimg) == len(img), f'lunghezza {len(outimg)} != {len(img)}'
OUT.write_bytes(outimg)
print('sha256:', hashlib.sha256(outimg).hexdigest())
print('md5   :', hashlib.md5(outimg).hexdigest())

# --- verifica per-archivi
scratch = W / 'check'
scratch.mkdir()
for part in (v9, new, bytes(data)):
    subprocess.run(['cpio', '-idmu', '--quiet'], cwd=scratch, input=part, capture_output=True)
swf = (scratch / 'nx679j/switch.sh').read_text()
assert 'v55 wifi' in swf
assert (scratch / 'owrt/usr/sbin/wpad').exists()
assert (scratch / 'owrt/etc/config/wireless').exists()
net = (scratch / 'owrt/etc/config/network').read_text()
assert 'usb0' not in net and 'lan_wifi' in net
assert 'config modem' in net or "network.modem" in net or 'modem' in net
qca = scratch / 'owrt/lib/modules' / KV / 'qca_cld3_qca6490.ko'
print('qca md5:', hashlib.md5(qca.read_bytes()).hexdigest())
assert hashlib.md5(qca.read_bytes()).hexdigest() == '977053f2eec388e65f7a4584b06ca32e'
print('VERIFICA PASS ->', OUT)
