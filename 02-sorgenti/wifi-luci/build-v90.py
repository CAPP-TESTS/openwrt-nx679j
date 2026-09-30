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

# Snapshot dei documenti a OGNI build: protezione da scritture sbagliate
# (incidente del 25/09/2026 con RIPRESA.md). Non dipende dalla mia disciplina.
subprocess.run(['sh', str(SES / 'doc-backup.sh')], check=False)

INIT9 = pathlib.Path('/home/user/nx679j-stock/experiments/20260917-init-v9')
W = SES / 'build-v64'
OVL = W / 'overlay'
KV = '5.10.66-android12-9-00005-gf6e6376090be-ab8060604'
IMG = SES / 'boot_b-live.img'
OUT = SES / 'boot_b-v90-mmwd.img'
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
for _f in ['nx679j-display-late.sh', 'nx679j-cell.sh', 'nx679j-modules.sh']:
    _dst = OVL / 'owrt/usr/lib/nx679j/modem' / _f
    _dst.parent.mkdir(parents=True, exist_ok=True)
    _sh.copy(str(SES / _f), str(_dst))
    _dst.chmod(0o755)
_p = subprocess.run(['sh', '-n', str(_s94d)], capture_output=True)
assert _p.returncode == 0, _p.stderr.decode()
_p2 = subprocess.run(['sh', '-n', str(OVL / 'owrt/usr/lib/nx679j/modem/nx679j-display-late.sh')], capture_output=True)
assert _p2.returncode == 0, _p2.stderr.decode()
_lt = (OVL / 'owrt/usr/lib/nx679j/modem/nx679j-display-late.sh').read_text()
assert 'X=${DISPLAY_LATE_X:-70}' in _lt, 'gate display non a 70s'
assert 'DL-RETRY' in _lt, 'fix v107 del marker display non applicato'
assert 'DL-FIRED' in _lt
# --- qmicli COMPLETO (collection full) ---
# Il qmicli di OpenWrt usa la collection "basic": ha 11 comandi NAS e NON conosce
# --nas-get-lte-cphy-ca-info (carrier aggregation). Qui lo sostituiamo con quello
# costruito dall'SDK con collection "full" (stessa versione 1.36, stesso SONAME
# libqmi-glib.so.5: qmi-network, che tiene su la connessione, non se ne accorge).
for _src, _dst in [('qmicli-full', 'owrt/usr/bin/qmicli'),
                   ('libqmi-glib.so.5.11.0', 'owrt/usr/lib/libqmi-glib.so.5.11.0')]:
    _s = SES / _src
    assert _s.exists(), 'manca %s' % _s
    _d = OVL / _dst
    _d.parent.mkdir(parents=True, exist_ok=True)
    _sh.copy(str(_s), str(_d))
    _d.chmod(0o755)
_lnk = OVL / 'owrt/usr/lib/libqmi-glib.so.5'
if _lnk.is_symlink() or _lnk.exists():
    _lnk.unlink()
_lnk.symlink_to('libqmi-glib.so.5.11.0')
# Moduli vendor minimali: catena PON (tasto/volume) + catena glink (alimentazione, Type-C).
_kd = OVL / 'owrt/usr/lib/nx679j/modem/kmod'
_kd.mkdir(parents=True, exist_ok=True)
for _k in sorted((SES / 'kernmods').glob('*.ko')):
    _sh.copy(str(_k), str(_kd / _k.name))
print('moduli vendor installati:', len(list(_kd.glob('*.ko'))))
# Firmware del touch (goodix): senza questi file il driver non puo' aggiornare
# il controller, che cade in modalita' ROM ("rom_pid:BERLIN") e resta senza
# touch. Presi dalla partizione vendor stock (letti con debugfs, sola lettura).
# Firmware del touch: si installa DOPO il persist (vedi sotto), non qui:
# i tar di persistenza contengono il rootfs completo e cancellano tutto.

print('qmicli full + libqmi installati nell immagine')
print('S94 + launcher iniettati e validati (ATTENZIONE: i tar di persistenza li sovrascrivono, vedi sotto)')

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

# --- Firmware del touch (goodix) DOPO il persist ---
# Due lezioni sul campo (25/09), entrambe costate ore:
#  1) request_firmware() gira in un kthread del KERNEL: cerca in /lib/firmware del
#     ROOT VERO (il ramdisk), NON dentro la chroot -> serve OVL/lib/firmware;
#  2) va copiato DOPO i tar di persistenza, che contengono il rootfs intero e
#     sovrascriverebbero i file (l'installazione anticipata li faceva sparire).
# Senza questi file il controller resta in modalita' ROM ("rom_pid:BERLIN") e il
# touch non funziona. Presi dalla partizione vendor stock (debugfs, sola lettura).
for _base in ('lib/firmware', 'owrt/lib/firmware'):
    _fwd = OVL / _base
    _fwd.mkdir(parents=True, exist_ok=True)
    for _f in sorted((SES / 'goodix-fw').glob('*.bin')):
        _sh.copy(str(_f), str(_fwd / _f.name))
print('firmware touch installato DOPO persist (root+chroot):',
      len(list((OVL / 'lib/firmware').glob('goodix*.bin'))), 'file')

psrc = SES / 'nx679j-proto-v124.sh'
_ldst = OVL / 'owrt/www/luci-static/resources/protocol/nx679j.js'
_ldst.parent.mkdir(parents=True, exist_ok=True)
_lsrc = SES / 'luci-proto-nx679j.js'
_ldst.write_text(_lsrc.read_text())
_ldst.chmod(0o644)
if "network.registerProtocol('nx679j'" not in _ldst.read_text():
    raise RuntimeError('v126 LuCI nx679j handler missing registration')

pdst = OVL / 'owrt/lib/netifd/proto/nx679j.sh'
pdst.write_text(psrc.read_text())
pdst.chmod(0o755)
p = subprocess.run(['sh', '-n', str(pdst)], capture_output=True)
if p.returncode != 0:
    raise RuntimeError('v125 nx679j proto syntax: ' + p.stderr.decode())
_pt = psrc.read_text()
for _forbidden in ('ip addr add', 'ip addr replace', 'ip route add', 'ip route replace', 'ifup ', 'ifdown '):
    if _forbidden in _pt:
        raise RuntimeError('v125 proto non-read-only: ' + _forbidden)
if 'proto_init_update "$ifname" 1 1' not in _pt:
    raise RuntimeError('v125 address-external flag absent')
if 'proto_add_ipv4_address' not in _pt:
    raise RuntimeError('v129 L3 address reporting absent')
# v129: address-external protegge SOLO gli indirizzi; una route riportata diventa di netifd
# e viene cancellata a ifdown/reload (netifd non ha un flag "route-external").
if 'proto_add_ipv4_route' in _pt:
    raise RuntimeError('v129: il proto riporta una route -> netifd la possiederebbe')

# v130: strato di stato Modem X65 (rpcd + ACL + menu + vista) e supervisor del link.
# Iniettati DOPO i tar di persistenza, come il proto e la regola udev.
_lm = [
    (SES / 'luci-nx679j-modem.rpcd',         OVL / 'owrt/usr/libexec/rpcd/luci.nx679j-modem', 0o755),
    (SES / 'luci-app-nx679j-modem.acl.json', OVL / 'owrt/usr/share/rpcd/acl.d/luci-app-nx679j-modem.json', 0o644),
    (SES / 'luci-app-nx679j-modem.menu.json',OVL / 'owrt/usr/share/luci/menu.d/luci-app-nx679j-modem.json', 0o644),
    (SES / 'luci-app-nx679j-modem.status.js',OVL / 'owrt/www/luci-static/resources/view/nx679j-modem/status.js', 0o644),
    (SES / 'nx679j-link-watch.sh',           OVL / 'owrt/usr/lib/nx679j/modem/nx679j-link-watch.sh', 0o755),
]
for _s, _d, _m in _lm:
    if not _s.exists():
        raise RuntimeError('v130: sorgente assente: ' + str(_s))
    _d.parent.mkdir(parents=True, exist_ok=True)
    _sh3.copy(str(_s), str(_d))
    _d.chmod(_m)
print('v130: strato di stato + link-watch iniettati dopo il persist')

_u80_src = SES / 'mm-final/80-mm-nx679j.rules'
_sh3.copy2(_u80_src, _u80)
_u80.chmod(0o644)
if _u80.read_bytes() != _u80_src.read_bytes():
    raise RuntimeError('v124: regola udev overlay diversa dal sorgente')
if 'KERNEL=="rmnet_ipa0", ENV{ID_MM_PHYSDEV_UID}="qcom-soc"' not in _u80.read_text():
    raise RuntimeError('v124: qcom-soc rmnet_ipa0 rule absent')
if 'KERNEL=="rmnet_ipa0", ENV{ID_MM_PORT_IGNORE}' in _u80.read_text():
    raise RuntimeError('v124: stale rmnet_ipa0 ignore rule')
print('v124: qcom-soc rmnet_ipa0 policy injected after PERSIST-INSIDE')

# --- v88: iniezione DOPO i tar di persistenza (i tar contengono la versione v87 del launcher)
_k3 = SES / 'nx679j-kiosk3'
assert _k3.exists(), 'binario nx679j-kiosk3 assente: compilarlo sul host'
assert _k3.stat().st_size > 150000, 'kiosk3 sospetto: %d B' % _k3.stat().st_size
_k3d = OVL / 'owrt/usr/lib/nx679j/modem/nx679j-kiosk3'
_k3d.parent.mkdir(parents=True, exist_ok=True)
_sh.copy(str(_k3), str(_k3d))
_k3d.chmod(0o755)
_lt_src = SES / 'nx679j-display-late.sh'
_lt_dst = OVL / 'owrt/usr/lib/nx679j/modem/nx679j-display-late.sh'
_sh.copy(str(_lt_src), str(_lt_dst))
_lt_dst.chmod(0o755)
assert 'nx679j-kiosk3' in _lt_dst.read_text(), 'launcher in OVL non invoca kiosk3'
assert (OVL / 'owrt/usr/lib/nx679j/modem/nx679j-drmtest').exists(), 'drmtest (fallback) assente in OVL'
print('v88: kiosk3 (%d B) + launcher kiosk3-first iniettati DOPO il persist-inside' % _k3d.stat().st_size)

# --- v135: UI interattiva sul display (nx679j-ui + nx679j-ui-fetch). Come kiosk3, va
# iniettata DOPO i tar di persistenza; il launcher la preferisce e kiosk3 resta il fallback.
_ui = SES / 'nx679j-ui'
assert _ui.exists(), 'binario nx679j-ui assente: compilarlo con la toolchain SDK'
assert _ui.stat().st_size > 150000, 'nx679j-ui sospetto: %d B' % _ui.stat().st_size
_uid = OVL / 'owrt/usr/lib/nx679j/modem/nx679j-ui'
_uid.parent.mkdir(parents=True, exist_ok=True)
_sh.copy(str(_ui), str(_uid))
_uid.chmod(0o755)
_uif = SES / 'nx679j-ui-fetch.sh'
assert _uif.exists(), 'nx679j-ui-fetch.sh assente'
_uifd = OVL / 'owrt/usr/lib/nx679j/modem/nx679j-ui-fetch.sh'
_sh.copy(str(_uif), str(_uifd))
_uifd.chmod(0o755)
# Raccoglitore della superficie di controllo (servizi, wifi, log, processi, rotte,
# lease, mount, impostazioni): senza di lui la UI mostra solo il modem.
_gth = SES / 'nx679j-ui-gather.sh'
assert _gth.exists(), 'nx679j-ui-gather.sh assente'
_gthd = OVL / 'owrt/usr/lib/nx679j/modem/nx679j-ui-gather.sh'
_sh.copy(str(_gth), str(_gthd))
_gthd.chmod(0o755)
print('v142: gatherer iniettato (superficie completa tipo LuCI)')
assert 'nx679j-ui' in _lt_dst.read_text(), 'launcher in OVL non invoca nx679j-ui'
assert 'nx679j-kiosk3' in _lt_dst.read_text(), 'launcher in OVL ha perso il fallback kiosk3'
print('v135: nx679j-ui (%d B) + fetcher iniettati; launcher UI-first con fallback kiosk3' % _uid.stat().st_size)

# Diagnostica: campionatore per il monitoraggio dal host. Misura SEPARATAMENTE
# release fence (commit accettato) e retire fence (frame ritirato dal pannello):
# le due possono divergere e la sola release non distingue un pannello fermo.
_smp = SES / 'nx679j-ui-sample.sh'
if _smp.exists():
    _smpd = OVL / 'owrt/usr/lib/nx679j/modem/nx679j-ui-sample.sh'
    _sh.copy(str(_smp), str(_smpd)); _smpd.chmod(0o755)
    print('v142: nx679j-ui-sample.sh iniettato (diagnostica fence/pannello)')

# --- v136: uinput-touch, per iniettare un tocco sintetico e verificare da remoto
# il percorso tocco->hit-test->azione senza dita (la UI scansiona piu' event*).
_ut = SES / 'uinput-touch'
assert _ut.exists(), 'uinput-touch assente: compilarlo con la toolchain SDK'
_utd = OVL / 'owrt/usr/lib/nx679j/modem/uinput-touch'
_sh.copy(str(_ut), str(_utd))
_utd.chmod(0o755)
assert 'mknod /dev/uinput' in _lt_dst.read_text(), 'launcher: manca la creazione del nodo uinput'
print('v136: uinput-touch (%d B) iniettato; nodo /dev/uinput creato dal launcher' % _utd.stat().st_size)

# v123: verify the only boot-services owner is the watchdog; no direct standard launcher.
_bs_txt = (SES / 'nx679j-boot-services.sh').read_text()
assert 'v123: mm-standard-boot delegato al solo mm-watchdog' in _bs_txt, 'v123: dedup owner assente'
assert 'rcS: mm-standard-boot avviato' not in _bs_txt, 'v123: launcher diretto ancora presente'

_wd = SES / 'nx679j-mm-watchdog.sh'
assert _wd.exists(), 'nx679j-mm-watchdog.sh assente'
_wdd = OVL / 'owrt/usr/lib/nx679j/modem/nx679j-mm-watchdog.sh'
_wdd.parent.mkdir(parents=True, exist_ok=True)
_sh.copy(str(_wd), str(_wdd))
_wdd.chmod(0o755)
_bsd = OVL / 'owrt/etc/nx679j-boot-services.sh'
_sh.copy(str(SES / 'nx679j-boot-services.sh'), str(_bsd))
_bsd.chmod(0o755)
assert 'mm-watchdog' in _bsd.read_text(), 'boot-services non lancia il watchdog'
print('v90: watchdog modem iniettato + boot-services aggiornato')

# --- v91: FIX bug 'log' non definito alla riga 60 di mm-standard-boot (moriva PRIMA del MM restart)
_ms = SES / 'nx679j-mm-standard-boot.sh'
assert _ms.exists(), 'nx679j-mm-standard-boot.sh assente in SES'
_txt = _ms.read_text()
# v128: the MM restart is intentional (MM must create the modem object that LuCI's Cellular
# page reads); the bearer stays chain-owned, so `ifup modem` must not appear in active lines.
assert 'v128: MM restart forte eseguito' in _txt, 'v128: MM restart marker absent'
assert 'v128: ifup modem NON eseguito' in _txt, 'v128: no-ifup marker absent'
_active_lines = [line.split('#', 1)[0].strip() for line in _txt.splitlines()]
# `say "..."` e' un messaggio di log: contiene la stringa del marker, non un comando.
_active_cmds = [l for l in _active_lines if l and not l.startswith('say "')]
if any('ifup modem' in l for l in _active_cmds):
    raise RuntimeError('v128: ifup modem must not run (L3 stays chain-owned)')

_msd = OVL / 'owrt/usr/lib/nx679j/modem/nx679j-mm-standard-boot.sh'
_msd.parent.mkdir(parents=True, exist_ok=True)
_sh.copy(str(_ms), str(_msd))
_msd.chmod(0o755)
print('v91: mm-standard-boot corretto (log->say) e iniettato dopo il persist')

# --- v92: bootstrap con attese DINAMICHE (poll DMS, cap = timer vecchi) invece di sleep 86/15
_ob = SES / 'nx679j-observed-bootstrap.sh'
assert _ob.exists(), 'nx679j-observed-bootstrap.sh assente in SES'
# v92 ANNULLATA: la condizione DMS e' un falso positivo (service=4096 visibile prima che il
# modem sia pronto -> bootstrap rc=1 ready=0 -> nessun modem). Ripristinati i timer originali.
_ot = _ob.read_text()
assert 'qmi_ready()' in _ot, 'patch v93 dinamica non applicata'
assert 'MSS_PROTECTIVE_STOP' in _ot, 'sequenza stop/restart persa (non deve cambiare)'
assert 'first + 86' not in _ot, 'timer fisso 86s ancora presente'
_obd = OVL / 'owrt/usr/lib/nx679j/modem/openwrt-observed-bootstrap.sh'
_obd.parent.mkdir(parents=True, exist_ok=True)
_sh.copy(str(_ob), str(_obd))
_obd.chmod(0o755)
print('v92: observed-bootstrap dinamico iniettato dopo il persist')

# --- v99: chain.sh con attesa del remoteproc MSS (non si arrende piu' con STOP bootstrap)
_ch = SES / 'chain.sh'
assert _ch.exists(), 'chain.sh assente in SES'
_ct = _ch.read_text()
assert 'attesa MSS' in _ct, 'patch attesa MSS non applicata a chain.sh'
_chd = OVL / 'owrt/usr/lib/nx679j/modem/chain.sh'
_chd.parent.mkdir(parents=True, exist_ok=True)
_sh.copy(str(_ch), str(_chd))
_chd.chmod(0o755)
print('v99: chain.sh con attesa MSS iniettato dopo il persist')

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
# Limite alzato da 45 a 60 MB: il qmicli "collection full" + libqmi aggiungono
# ~6 MB al ramdisk (il qmicli di OpenWrt ha 11 comandi NAS, quello full 17, e
# serve per la carrier aggregation). boot_b e' 96 MB: 45-50 MB ci stanno
# comodamente. Il controllo resta, per scoprire ramdisk palesemente sbagliati.
assert 10_000_000 < len(newram) < 60_000_000, f'ramdisk sospetto: {len(newram)}'
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
assert (scratch / 'owrt/usr/lib/nx679j/modem/nx679j-kiosk3').exists(), 'kiosk3 assente dal ramdisk'
assert (scratch / 'owrt/lib/netifd/proto/nx679j.sh').exists(), 'v125 proto nx679j assente'
assert (scratch / 'owrt/www/luci-static/resources/protocol/nx679j.js').exists(), 'v126 LuCI frontend absent'
_final_js = (scratch / 'owrt/www/luci-static/resources/protocol/nx679j.js').read_text()
assert "network.registerProtocol('nx679j'" in _final_js, 'v126 LuCI handler not registered'
assert _final_js == (SES / 'luci-proto-nx679j.js').read_text(), 'v126 LuCI frontend payload mismatch'
_final_proto = (scratch / 'owrt/lib/netifd/proto/nx679j.sh').read_text()
assert 'proto_init_update "$ifname" 1 1' in _final_proto
assert 'ip addr replace' not in _final_proto and 'ip route replace' not in _final_proto
assert 'nx679j.proto=nx679j' in (scratch / 'owrt/usr/lib/nx679j/modem/chain.sh').read_text() or \
       'wan_early.proto=nx679j' in (scratch / 'owrt/usr/lib/nx679j/modem/chain.sh').read_text()

# v127: il hold delle sessioni QMI deve essere 86400 s (non 3600) in TUTTI i launcher
# del payload finale. Fail-closed: se un launcher torna a 3600 la connettivita' muore a ~1 h.
_hold_files = [
    'owrt/usr/lib/nx679j/modem/chain.sh',
    'owrt/usr/lib/nx679j/modem/openwrt-data-staged.sh',
    'owrt/usr/lib/nx679j/modem/openwrt-data-observed.sh',
    'owrt/usr/lib/nx679j/modem/nx679j-modem-prepare.sh',
]
_hold_seen = 0
for _hf in _hold_files:
    _p = scratch / _hf
    if not _p.exists():
        raise RuntimeError('v127: launcher assente dal payload: ' + _hf)
    for _line in _p.read_text().splitlines():
        if 'qmi-qrtr-observed' in _line and '-session' in _line:
            _hold_seen += 1
            if ' 86400 ' not in _line:
                raise RuntimeError('v127: hold != 86400 in %s: %s' % (_hf, _line.strip()))
if _hold_seen < 3:
    raise RuntimeError('v127: trovate solo %d righe di lancio sessione' % _hold_seen)
print('v127: hold=86400 su %d righe di lancio sessione nel payload finale' % _hold_seen)

assert 'nx679j-kiosk3' in (scratch / 'owrt/usr/lib/nx679j/modem/nx679j-display-late.sh').read_text()

# v130: payload finale dello strato di stato e del supervisor (fail-closed).
for _p, _mk in [
    ('owrt/usr/libexec/rpcd/luci.nx679j-modem', 'luci.nx679j-modem'),
    ('owrt/usr/share/rpcd/acl.d/luci-app-nx679j-modem.json', 'getStatus'),
    ('owrt/usr/share/luci/menu.d/luci-app-nx679j-modem.json', 'nx679j-modem/status'),
    ('owrt/www/luci-static/resources/view/nx679j-modem/status.js', 'callStatus'),
    ('owrt/usr/lib/nx679j/modem/nx679j-link-watch.sh', 'sess_alive'),
]:
    _q = scratch / _p
    if not _q.exists() or _mk not in _q.read_text():
        raise RuntimeError('v130: payload incompleto o marker assente: ' + _p)
print('v130: payload dello strato di stato verificato (5 file)')
print('VERIFICA v88: kiosk3 presente nel ramdisk e invocato dal launcher')
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
