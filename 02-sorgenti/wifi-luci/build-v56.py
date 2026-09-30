#!/usr/bin/env python3
"""
build-v56.py — NX679J boot_b v56 "wifi+luci persistenti, fix dal boot reale".

v56 = v9.cpio + new-uid0.cpio + overlay v56:
  A) owrt/** = albero rootfs LIVE catturato (owrt-live2.tar.gz): pacchetti
     iw/wpad/wifi-scripts + config + /sbin/ujail.off (fix jail EINVAL)
  B) owrt/etc/nx679j-wifi-services.sh = wrapper in-chroot (PATH + sequenza)
  C) nx679j/switch.sh con blocco v56 (moduli + wrapper)
Contenitore dalle stesse basi (boot_b-live.img), stessa lunghezza 100663296.
"""
import hashlib
import pathlib
import shutil
import struct
import subprocess

SES = pathlib.Path('/home/user/nx679j-stock/experiments/20260920-wifi-luci')
INIT9 = pathlib.Path('/home/user/nx679j-stock/experiments/20260917-init-v9')
W = SES / 'build-v56'
OVL = W / 'overlay'
KV = '5.10.66-android12-9-00005-gf6e6376090be-ab8060604'
IMG = SES / 'boot_b-live.img'
OUT = SES / 'boot_b-v56-wifi.img'
P = 4096


def ru(x):
    return (x + P - 1) // P * P


if W.exists():
    shutil.rmtree(W)
OVL.mkdir(parents=True)

# --- A) owrt/ dal tar live (post-fix jail + config di oggi)
subprocess.run(['tar', '-xzf', str(SES / 'owrt-live2.tar.gz'), '-C', str(OVL),
                '--transform', 's|^\\./|owrt/|'], check=True)
print('owrt estratti:', sum(1 for _ in (OVL / 'owrt').rglob('*')))
assert (OVL / 'owrt/usr/sbin/wpad').exists()
assert (OVL / 'owrt/sbin/wifi').exists()
assert (OVL / f'owrt/lib/modules/{KV}/qca_cld3_qca6490.ko').exists()
assert (OVL / 'owrt/etc/config/wireless').exists()
# fix jail persistito nell'albero:
assert (OVL / 'owrt/sbin/ujail.off').exists(), 'ujail.off assente'
assert not (OVL / 'owrt/sbin/ujail').exists(), 'ujail ancora presente'

# --- B) wrapper servizi in-chroot
wr = (SES / 'v56-wifi-services.sh').read_text()
dst = OVL / 'owrt/etc/nx679j-wifi-services.sh'
dst.write_text(wr)
dst.chmod(0o755)
p = subprocess.run(['sh', '-n', str(dst)], capture_output=True)
assert p.returncode == 0, p.stderr.decode()

# --- C) switch.sh v56
sw = (SES / 'live-switch.sh').read_text()
block = (SES / 'v56-wifi-block.sh').read_text()
anchor = '    $BB chroot /owrt /sbin/init >> $J 2>&1\n'
assert anchor in sw
assert 'v56 wifi' not in sw
sw2 = sw.replace(anchor, block + anchor)
(OVL / 'nx679j').mkdir()
(OVL / 'nx679j/switch.sh').write_text(sw2)
(OVL / 'nx679j/switch.sh').chmod(0o755)
p = subprocess.run(['sh', '-n', str(OVL / 'nx679j/switch.sh')], capture_output=True)
assert p.returncode == 0, p.stderr.decode()
print('switch.sh v56: syntax OK')

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
print(f'v56.cpio {len(data)} B, {tz} uid azzerati')
(W / 'v56-uid0.cpio').write_bytes(bytes(data))

# --- merge + lz4
v9 = (INIT9 / 'v10-work/v9.cpio').read_bytes()
new = (INIT9 / 'v10-work/new-uid0.cpio').read_bytes()
merged = v9 + new + bytes(data)
print(f'merged: {len(merged)} B')
newram = subprocess.run(['lz4', '-l', '-9', '-c'], input=merged,
                        capture_output=True, check=True).stdout
print(f'ramdisk lz4: {len(newram)} B')
(W / 'new-ramdisk.lz4').write_bytes(newram)

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
assert 'v56 wifi' in swf
assert (scratch / 'owrt/etc/nx679j-wifi-services.sh').exists()
assert (scratch / 'owrt/usr/sbin/wpad').exists()
assert (scratch / 'owrt/sbin/ujail.off').exists()
assert (scratch / 'owrt/etc/config/wireless').exists()
net = (scratch / 'owrt/etc/config/network').read_text()
assert 'usb0' not in net and 'lan_wifi' in net
assert 'modem' in net
qca = scratch / 'owrt/lib/modules' / KV / 'qca_cld3_qca6490.ko'
print('qca md5:', hashlib.md5(qca.read_bytes()).hexdigest())
assert hashlib.md5(qca.read_bytes()).hexdigest() == '977053f2eec388e65f7a4584b06ca32e'
print('VERIFICA PASS ->', OUT)
