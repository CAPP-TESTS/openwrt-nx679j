#!/usr/bin/env python3
"""
build-v81.py — NX679J boot_b v71: v70 + ubus-wait pre-rcS (servizi al boot FIX) + display+touchpaint in S94 + regola MM aggiornata.

v59 = v9.cpio + new-uid0.cpio + overlay v60 (da owrt-live19.tar.gz):
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
OUT = SES / 'boot_b-v86-persist-inside.img'
P = 4096


def ru(x):
    return (x + P - 1) // P * P


if W.exists():
    import subprocess as _sp; _sp.run(['chmod','-R','u+w',str(W)], capture_output=True)
    import shutil as _sh2
    def _onerr(func, path, exc):
        import os, stat as _st
        try:
            os.chmod(path, _st.S_IWRITE | _st.S_IREAD)
            func(path)
        except Exception:
            pass
    _sh2.rmtree(W, onerror=_onerr)
OVL.mkdir(parents=True)

# --- A) owrt/ dal tar live
subprocess.run(['tar', '-xzf', str(SES / 'owrt-live19.tar.gz'), '-C', str(OVL),
                '--transform', 's|^\\./|owrt/|'], check=True)
n_owrt = sum(1 for _ in (OVL / 'owrt').rglob('*'))
print('owrt estratti:', n_owrt)
assert (OVL / 'owrt/usr/sbin/wpad').exists()
assert (OVL / f'owrt/lib/modules/{KV}/qca_cld3_qca6490.ko').exists()
assert (OVL / 'owrt/etc/config/wireless').exists()
assert (OVL / 'owrt/sbin/ujail.off').exists(), 'ujail.off assente'

# v81: proto STANDARD modemmanager (il proto custom non e' piu' usato dalla config)
proto = OVL / 'owrt/lib/netifd/proto/modemmanager.sh'
assert proto.exists(), 'proto modemmanager assente'
pt = proto.read_text()
assert '[ -n "${device}" ] || device="any"' in pt, 'proto: fix device=any mancante!'
assert pt.count('[ -n "${device}" ] || device="any"') >= 2, 'proto: fix device=any manca nel teardown!'
assert 'add_device' not in pt, 'proto: add_device residuo (causa teardown spurio!)'
assert 'json_get_vars device apn' in pt, 'proto: get_vars device assente (file sbagliato?)'
proto.chmod(0o755)
# v81: il netlink-watch + init.d + rc.d (annuncio netdev a MM senza udev)
_nw = OVL / 'owrt/usr/lib/nx679j/modem/netlink-watch'
assert _nw.exists() and _nw.stat().st_size > 200000, 'netlink-watch assente/piccolo'
_nw.chmod(0o755)
assert (OVL / 'owrt/etc/init.d/mm-netlink-watch').exists(), 'mm-netlink-watch init.d assente'
assert (OVL / 'owrt/etc/rc.d/S71mm-netlink-watch').exists(), 'S71mm-netlink-watch non abilitato'
# v81: ModemManager v5 + libqmi patchata
_mmb = OVL / 'owrt/usr/sbin/ModemManager'
assert _mmb.exists() and _mmb.stat().st_size > 3000000, 'ModemManager assente/piccolo'
assert (OVL / 'owrt/usr/lib/libqmi-glib.so.5.11.0').exists(), 'libqmi patchata assente'
_u80 = OVL / 'owrt/lib/udev/rules.d/80-mm-nx679j.rules'
assert _u80.exists() and 'qmapmux' in _u80.read_text(), 'regola udev qmapmux assente!'

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
assert "proto 'modemmanager'" in net, 'network.modem.proto != modemmanager'
assert "device 'phy0-ap0'" in net, 'lan_wifi device != phy0-ap0 (fix attach!)'
assert 'usb0' not in net
# v81: la config modem deve avere apn + disable_modem 0 e NESSUN device (main_dev netifd!)
_netsec = net.split("config interface 'modem'")[1].split('config ')[0]
assert "option apn 'internet.it'" in _netsec, 'config modem: apn mancante'
assert 'disable_modem' in _netsec, 'config modem: disable_modem assente'
assert "option device" not in _netsec, 'config modem: option device presente (rompe main_dev!)'
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

# --- C3) S94 pulita (no touchpaint) + launcher display ritardato
import shutil as _sh
_s94 = SES / 'nx679j-display-touch.init'
_s94d = OVL / 'owrt/etc/init.d/nx679j-display-touch'
_s94d.write_text(_s94.read_text())
_s94d.chmod(0o755)
for _f in ['nx679j-display-late.sh']:
    _dst = OVL / 'owrt/usr/lib/nx679j/modem' / _f
    _dst.parent.mkdir(parents=True, exist_ok=True)
    _sh.copy(str(SES / _f), str(_dst))
    _dst.chmod(0o755)
_p = subprocess.run(['sh', '-n', str(_s94d)], capture_output=True)
assert _p.returncode == 0, _p.stderr.decode()
_p2 = subprocess.run(['sh', '-n', str(OVL / 'owrt/usr/lib/nx679j/modem/nx679j-display-late.sh')], capture_output=True)
assert _p2.returncode == 0, _p2.stderr.decode()
_lt = (OVL / 'owrt/usr/lib/nx679j/modem/nx679j-display-late.sh').read_text()
assert 'X=${DISPLAY_LATE_X:-900}' in _lt
assert 'DL-FIRED' in _lt
print('S94 + launcher iniettati e validati')

# --- PERSIST-INSIDE: extra tar di persistenza nell'OVL (sovrascrive owrt-live19)
import tarfile as _tf, tempfile as _tmp, os as _os, shutil as _sh3
_td = _tmp.mkdtemp()
for _t in ['etc.tar', 'luci.tar', 'tools.tar']:
    _tp = SES / 'persist-tars' / _t
    assert _tp.exists(), 'persist-tar mancante: %s' % _tp
    with _tf.open(_tp) as _tfx:
        _tfx.extractall(_td)
def _cpy(_s, _d):
    if _os.path.islink(_s):
        if _os.path.lexists(_d):
            if _os.path.isdir(_d) and not _os.path.islink(_d):
                _sh3.rmtree(_d)
            else:
                _os.remove(_d)
        _os.symlink(_os.readlink(_s), _d)
    elif _os.path.isdir(_s):
        _os.makedirs(_d, exist_ok=True)
        for _e in _os.listdir(_s):
            _cpy(_os.path.join(_s, _e), _os.path.join(_d, _e))
    else:
        if _os.path.lexists(_d) and _os.path.isdir(_d):
            _sh3.rmtree(_d)
        _sh3.copy2(_s, _d)
for _item in _os.listdir(_td):
    _cpy(_os.path.join(_td, _item), str(OVL / 'owrt' / _item))
print('PERSIST-INSIDE: tar estratti in OVL/owrt')

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
ptf = (scratch / 'owrt/lib/netifd/proto/modemmanager.sh').read_text()
assert 'json_get_vars device apn' in ptf and '[ -n "${device}" ] || device="any"' in ptf
assert (scratch / 'owrt/usr/lib/nx679j/modem/netlink-watch').exists()
assert (scratch / 'owrt/etc/rc.d/S71mm-netlink-watch').exists()
assert (scratch / 'owrt/usr/sbin/ModemManager').stat().st_size > 3000000
assert (scratch / 'owrt/usr/lib/libqmi-glib.so.5.11.0').exists()
assert (scratch / 'owrt/usr/sbin/xtables-legacy-multi').exists()
assert (scratch / 'owrt/etc/nx679j-wan-share.sh').exists()
netf = (scratch / 'owrt/etc/config/network').read_text()
assert "device 'phy0-ap0'" in netf and "proto 'modemmanager'" in netf
qca = scratch / 'owrt/lib/modules' / KV / 'qca_cld3_qca6490.ko'
assert hashlib.md5(qca.read_bytes()).hexdigest() == '977053f2eec388e65f7a4584b06ca32e'
import glob as _glob
assert _glob.glob(str(scratch / 'owrt/www/luci-static/resources/protocol/modemmanager.js')), 'PERSIST: modemmanager.js assente!'
assert _glob.glob(str(scratch / 'owrt/usr/lib/*/modem/*atom11')), 'PERSIST: atom11 assente!'
assert _glob.glob(str(scratch / 'owrt/etc/config/network')), 'PERSIST: network config assente!'
print('VERIFICA PASS ->', OUT)
