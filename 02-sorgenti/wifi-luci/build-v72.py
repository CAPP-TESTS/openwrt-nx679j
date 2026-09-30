#!/usr/bin/env python3
"""
build-v71.py — NX679J boot_b v71: v70 + ubus-wait pre-rcS (servizi al boot FIX) + display+touchpaint in S94 + regola MM aggiornata.

v59 = v9.cpio + new-uid0.cpio + overlay v60 (da owrt-live15.tar.gz):
  - TUTTO v58 PIU':
  - /lib/netifd/proto/nx679j.sh FIXATO: add_protocol (non add_proto!) + lettura IP
    senza 'ip -o' (busybox non lo supporta) + auto-riparazione IP/route dal
    /tmp/wds-session.log (il churn netifd flusha addr/route: il proto li ricrea).
  - network: lan_wifi device='phy0-ap0' diretto (fix deterministico attach; via
    wireless-bind il v58 perdeva l'attach e dnsmasq non emetteva il dhcp-range).
  - dhcp: dnsmasq con list interface 'phy0-ap0' + 'usb0' (bind per netdev).
  - wrapper v59: niente flap wifi, attesa IP + fallback diretto, uhttpd SENZA
    commit nel fallback, wan-share con [ -f ].
  - switch.sh (base v58-live): export PATH (fix radice netifd proto handlers).
  - password root impostata (shadow nel tar).
Contenitore dalle basi di sempre (boot_b-live.img), stessa lunghezza 100663296.
"""
import hashlib
import pathlib
import shutil
import struct
import subprocess

SES = pathlib.Path('/home/user/nx679j-stock/experiments/20260920-wifi-luci')
INIT9 = pathlib.Path('/home/user/nx679j-stock/experiments/20260917-init-v9')
W = SES / 'build-v64'
OVL = W / 'overlay'
KV = '5.10.66-android12-9-00005-gf6e6376090be-ab8060604'
IMG = SES / 'boot_b-live.img'
OUT = SES / 'boot_b-v72-wifi.img'
P = 4096


def ru(x):
    return (x + P - 1) // P * P


if W.exists():
    import subprocess as _sp; _sp.run(['chmod','-R','u+w',str(OVL)], capture_output=True)
    shutil.rmtree(W)
OVL.mkdir(parents=True)

# --- A) owrt/ dal tar live
subprocess.run(['tar', '-xzf', str(SES / 'owrt-live15.tar.gz'), '-C', str(OVL),
                '--transform', 's|^\\./|owrt/|'], check=True)
n_owrt = sum(1 for _ in (OVL / 'owrt').rglob('*'))
print('owrt estratti:', n_owrt)
assert (OVL / 'owrt/usr/sbin/wpad').exists()
assert (OVL / f'owrt/lib/modules/{KV}/qca_cld3_qca6490.ko').exists()
assert (OVL / 'owrt/etc/config/wireless').exists()
assert (OVL / 'owrt/sbin/ujail.off').exists(), 'ujail.off assente'

# proto nx679j FIXATO
proto = OVL / 'owrt/lib/netifd/proto/nx679j.sh'
assert proto.exists(), 'proto nx679j assente'
pt = proto.read_text()
# NB: /sbin/ujail nel layer base NON e' cancellabile con cpio add-only:
# il wrapper v61 lo rinomina a ogni boot (vedi assert 'ujail.off' in wr).
assert 'add_protocol nx679j' in pt, 'proto: add_protocol mancante (regressione file!)'
assert 'add_proto ' not in pt.replace('add_protocol', ''), 'proto: add_proto residuo'
assert 'dotted2prefix' in pt, 'proto: auto-riparazione mancante'
assert '-o addr' not in pt, 'proto: usa ip -o (busybox non lo supporta)'
proto.chmod(0o755)

# iptables alpine + wan-share
assert (OVL / 'owrt/usr/sbin/xtables-legacy-multi').exists(), 'iptables assente'
assert (OVL / 'owrt/usr/lib/xtables/libxt_MASQUERADE.so').exists(), 'extensions assenti'
ws = OVL / 'owrt/etc/nx679j-wan-share.sh'
assert ws.exists(), 'wan-share assente'
ws.chmod(0o755)
assert (OVL / 'owrt/lib/libc.musl-aarch64.so.1').is_symlink() or \
       (OVL / 'owrt/lib/libc.musl-aarch64.so.1').exists(), 'symlink musl assente'

# config: network + dhcp
net = (OVL / 'owrt/etc/config/network').read_text()
assert "proto 'nx679j'" in net, 'network.modem.proto != nx679j'
assert "device 'phy0-ap0'" in net, 'lan_wifi device != phy0-ap0 (fix attach!)'
assert 'usb0' not in net
dh = (OVL / 'owrt/etc/config/dhcp').read_text()
assert 'lan_wifi' in dh and '151.5.216.30' in dh, 'dnsmasq DNS non configurato'
assert 'phy0-ap0' in dh, 'dnsmasq non bindato a phy0-ap0'

# --- B) wrapper servizi in-chroot v61 + fix v61 nei file live
wr = (SES / 'v61-wifi-services.sh').read_text()
dst = OVL / 'owrt/etc/nx679j-wifi-services.sh'
dst.write_text(wr)
dst.chmod(0o755)
assert 'nx679j-wan-share' in wr
assert 'ujail.off' in wr, 'wrapper v61: rename ujail mancante!'
assert 'ubus call network.interface.lan_wifi up' in wr, 'belt ubus mancante'
assert 'uci commit' not in wr, 'wrapper: commit uci nel fallback!'
p = subprocess.run(['sh', '-n', str(dst)], capture_output=True)
assert p.returncode == 0, p.stderr.decode()

# wan-share v61 (sostituisce quello catturato)
wsrc = (SES / 'nx679j-wan-share-v61.sh').read_text()
wdst = OVL / 'owrt/etc/nx679j-wan-share.sh'
wdst.write_text(wsrc)
wdst.chmod(0o755)
assert '-s $LAN' in wsrc, 'wan-share v61: manca -s LAN'
p = subprocess.run(['sh', '-n', str(wdst)], capture_output=True)
assert p.returncode == 0, p.stderr.decode()

# proto v61 (validazioni + gw riletto dal kernel)
psrc = (SES / 'nx679j-proto-v61.sh').read_text()
pdst = OVL / 'owrt/lib/netifd/proto/nx679j.sh'
pdst.write_text(psrc)
pdst.chmod(0o755)
assert 'wds-session.pid' in psrc, 'proto v61: freshness mancante'
p = subprocess.run(['sh', '-n', str(pdst)], capture_output=True)
assert p.returncode == 0, p.stderr.decode()

# hotplug per il modem (retrigger su comparsa rmnet_data0)
hp = SES / '10-nx679j-modem'
hpdst = OVL / 'owrt/etc/hotplug.d/net/10-nx679j-modem'
hpdst.parent.mkdir(parents=True, exist_ok=True)
hpdst.write_text(hp.read_text())
hpdst.chmod(0o755)

# --- C) switch.sh (base gia' con PATH + blocco v57)
sw = (SES / 'switch-v58-live.sh').read_text()
assert 'export PATH=/usr/sbin:/usr/bin:/sbin:/bin' in sw, 'PATH fix assente'
assert 'v57 wifi' in sw, 'blocco v57 assente nella base'
(OVL / 'nx679j').mkdir()
(OVL / 'nx679j/switch.sh').write_text(sw)
(OVL / 'nx679j/switch.sh').chmod(0o755)
p = subprocess.run(['sh', '-n', str(OVL / 'nx679j/switch.sh')], capture_output=True)
assert p.returncode == 0, p.stderr.decode()
print('switch.sh: syntax OK')

# --- C2) boot-services v2 (selettore diagnostico)
_bs = SES / 'nx679j-boot-services.sh'
_bd = OVL / 'owrt/etc/nx679j-boot-services.sh'
_bd.write_text(_bs.read_text())
_bd.chmod(0o755)
_p = subprocess.run(['sh', '-n', str(_bd)], capture_output=True)
assert _p.returncode == 0, _p.stderr.decode()
print('boot-services.sh: syntax OK')

# --- cpio + uid0
out = subprocess.run('find . | LC_ALL=C sort | cpio -o -H newc --quiet', shell=True, cwd=OVL,
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
print(f'v64.cpio {len(data)} B, {tz} uid azzerati')
(W / 'v64-uid0.cpio').write_bytes(bytes(data))

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
assert 10_000_000 < len(newram) < 45_000_000, f'ramdisk sospetto: {len(newram)}'
assert 40_000_000 < ks < 60_000_000, f'kernel size sospetto: {ks}'
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
assert 'nx679j-wifi-services.sh' in swf, 'switch.sh non invoca il wrapper!'
wrf = (scratch / 'owrt/etc/nx679j-wifi-services.sh').read_text()
assert 'nx679j-wan-share' in wrf and 'ujail.off' in wrf
assert (scratch / 'owrt/etc/hotplug.d/net/10-nx679j-modem').exists()
wsf = (scratch / 'owrt/etc/nx679j-wan-share.sh').read_text()
assert '-s $LAN' in wsf
ptf = (scratch / 'owrt/lib/netifd/proto/nx679j.sh').read_text()
assert 'add_protocol nx679j' in ptf and 'dotted2prefix' in ptf
assert (scratch / 'owrt/usr/sbin/xtables-legacy-multi').exists()
assert (scratch / 'owrt/etc/nx679j-wan-share.sh').exists()
netf = (scratch / 'owrt/etc/config/network').read_text()
assert "device 'phy0-ap0'" in netf and "proto 'nx679j'" in netf
qca = scratch / 'owrt/lib/modules' / KV / 'qca_cld3_qca6490.ko'
assert hashlib.md5(qca.read_bytes()).hexdigest() == '977053f2eec388e65f7a4584b06ca32e'
print('VERIFICA PASS ->', OUT)
