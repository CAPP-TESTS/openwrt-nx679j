#!/usr/bin/env python3
"""
build-v58.py — NX679J boot_b v58 "wifi+luci+wan-share persistenti".

v58 = v9.cpio + new-uid0.cpio + overlay v58:
  A) owrt/** = albero rootfs LIVE catturato (owrt-live4.tar.gz): include
     - pacchetti wifi (iw/wpad/wifi-scripts) + config persistenti
     - fix ujail.off
     - /lib/netifd/proto/nx679j.sh  (proto LuCI "adopt" della catena modem)
     - /usr/sbin/xtables-legacy-multi + /usr/lib/xtables/* (iptables Alpine musl)
     - /etc/nx679j-wan-share.sh (NAT+forwarding SIM->LAN, idempotente)
     - dnsmasq con server/DNS del bearer
  B) wrapper /etc/nx679j-wifi-services.sh v58 (avvio servizi PATH-safe + wan-share)
  C) switch.sh = switch-v58-live.sh (base con export PATH=/usr/sbin:... + blocco v57)
     NIENTE nuovo blocco: il wrapper e' l'unica cosa che cambia.
Contenitore dalle basi di sempre (boot_b-live.img), stessa lunghezza 100663296.
"""
import hashlib
import pathlib
import shutil
import struct
import subprocess

SES = pathlib.Path('/home/user/nx679j-stock/experiments/20260920-wifi-luci')
INIT9 = pathlib.Path('/home/user/nx679j-stock/experiments/20260917-init-v9')
W = SES / 'build-v58'
OVL = W / 'overlay'
KV = '5.10.66-android12-9-00005-gf6e6376090be-ab8060604'
IMG = SES / 'boot_b-live.img'
OUT = SES / 'boot_b-v58-wifi.img'
P = 4096


def ru(x):
    return (x + P - 1) // P * P


if W.exists():
    shutil.rmtree(W)
OVL.mkdir(parents=True)

# --- A) owrt/ dal tar live
subprocess.run(['tar', '-xzf', str(SES / 'owrt-live4.tar.gz'), '-C', str(OVL),
                '--transform', 's|^\\./|owrt/|'], check=True)
n_owrt = sum(1 for _ in (OVL / 'owrt').rglob('*'))
print('owrt estratti:', n_owrt)
assert (OVL / 'owrt/usr/sbin/wpad').exists()
assert (OVL / f'owrt/lib/modules/{KV}/qca_cld3_qca6490.ko').exists()
assert (OVL / 'owrt/etc/config/wireless').exists()
# fix jail persistito:
assert (OVL / 'owrt/sbin/ujail.off').exists(), 'ujail.off assente'
# proto nx679j + iptables + wan-share:
assert (OVL / 'owrt/lib/netifd/proto/nx679j.sh').exists(), 'proto nx679j assente'
assert (OVL / 'owrt/usr/sbin/xtables-legacy-multi').exists(), 'iptables assente'
assert (OVL / 'owrt/usr/lib/xtables/libxt_MASQUERADE.so').exists(), 'extensions assenti'
assert (OVL / 'owrt/etc/nx679j-wan-share.sh').exists(), 'wan-share assente'
net = (OVL / 'owrt/etc/config/network').read_text()
assert "proto 'nx679j'" in net, 'network.modem.proto != nx679j'
assert 'usb0' not in net
dhcp = (OVL / 'owrt/etc/config/dhcp').read_text()
assert 'lan_wifi' in dhcp and '151.5.216.30' in dhcp, 'dnsmasq DNS non configurato'

# --- B) wrapper servizi in-chroot v58
wr = (SES / 'v58-wifi-services.sh').read_text()
dst = OVL / 'owrt/etc/nx679j-wifi-services.sh'
dst.write_text(wr)
dst.chmod(0o755)
assert 'nx679j-wan-share' in wr
p = subprocess.run(['sh', '-n', str(dst)], capture_output=True)
assert p.returncode == 0, p.stderr.decode()
assert 'v57 wifi' in wr or 'v58' in wr

# --- C) switch.sh (base gia' con PATH + blocco v57)
sw = (SES / 'switch-v58-live.sh').read_text()
assert 'export PATH=/usr/sbin:/usr/bin:/sbin:/bin' in sw, 'PATH fix assente'
assert 'v57 wifi' in sw, 'blocco v57 assente nella base'
assert 'v58' not in sw.split('export PATH')[0]
(OVL / 'nx679j').mkdir()
(OVL / 'nx679j/switch.sh').write_text(sw)
(OVL / 'nx679j/switch.sh').chmod(0o755)
p = subprocess.run(['sh', '-n', str(OVL / 'nx679j/switch.sh')], capture_output=True)
assert p.returncode == 0, p.stderr.decode()
print('switch.sh v58: syntax OK')

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
print(f'v58.cpio {len(data)} B, {tz} uid azzerati')
(W / 'v58-uid0.cpio').write_bytes(bytes(data))

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
assert 'export PATH=/usr/sbin:/usr/bin:/sbin:/bin' in swf
assert 'v57 wifi' in swf
wrf = (scratch / 'owrt/etc/nx679j-wifi-services.sh').read_text()
assert 'nx679j-wan-share' in wrf
assert (scratch / 'owrt/lib/netifd/proto/nx679j.sh').exists()
assert (scratch / 'owrt/usr/sbin/xtables-legacy-multi').exists()
assert (scratch / 'owrt/etc/nx679j-wan-share.sh').exists()
qca = scratch / 'owrt/lib/modules' / KV / 'qca_cld3_qca6490.ko'
assert hashlib.md5(qca.read_bytes()).hexdigest() == '977053f2eec388e65f7a4584b06ca32e'
print('VERIFICA PASS ->', OUT)
