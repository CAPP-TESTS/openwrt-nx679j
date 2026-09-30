#!/usr/bin/env python3
"""
build-v54.py (overlay) — NX679J boot_b v54 "wifi+luci persistente".

Ramdisk attuale (immagine live) = v9.cpio + new-uid0.cpio  (verificato: init e
switch.sh md5 + ramdisk_size combaciano con l'immagine live).
Si aggiunge un TERZO archivio v54.cpio con SOLO i file nuovi/modificati:
nel merge initramfs del kernel le voci con lo stesso path vengono sovrascritte,
quindi v54 vince.  Stesso formato di build-v10.py per il contenitore
(boot img v0, page 4096, header offset 12 = ramdisk_size).
"""
import hashlib
import pathlib
import shutil
import struct
import subprocess
import sys

SES = pathlib.Path('/home/user/nx679j-stock/experiments/20260920-wifi-luci')
INIT9 = pathlib.Path('/home/user/nx679j-stock/experiments/20260917-init-v9')
W = SES / 'build-v54'
OVL = W / 'overlay'
KV = '5.10.66-android12-9-00005-gf6e6376090be-ab8060604'
IMG = SES / 'boot_b-live.img'
OUT = SES / 'boot_b-v54-wifi.img'
P = 4096


def ru(x):
    return (x + P - 1) // P * P


def run(cmd, **kw):
    print('+', cmd if isinstance(cmd, str) else ' '.join(map(str, cmd)))
    return subprocess.run(cmd, shell=isinstance(cmd, str), check=True, **kw)


# ---------------------------------------------------------------- 0. staging overlay
if W.exists():
    shutil.rmtree(W)
OVL.mkdir(parents=True)
(W).mkdir(exist_ok=True)

# --- A) switch.sh + blocco wifi
sw = (SES / 'live-switch.sh').read_text()
block = (SES / 'v54-wifi-block.sh').read_text()
anchor = '    $BB chroot /owrt /sbin/init >> $J 2>&1\n'
assert anchor in sw, 'anchor non trovato'
assert 'v54 wifi' not in sw, 'blocco gia presente'
sw2 = sw.replace(anchor, block + anchor)
(OVL / 'nx679j').mkdir()
(OVL / 'nx679j/switch.sh').write_text(sw2)
(OVL / 'nx679j/switch.sh').chmod(0o755)
p = subprocess.run(['sh', '-n', str(OVL / 'nx679j/switch.sh')], capture_output=True)
assert p.returncode == 0, p.stderr.decode()
print('switch.sh + blocco v54: syntax OK')

# --- B) config
(OVL / 'owrt/etc/config').mkdir(parents=True)
for f in ('network', 'wireless', 'uhttpd'):
    shutil.copy2(SES / 'live-config/etc/config' / f, OVL / 'owrt/etc/config' / f)
    print('config <-', f)
# dhcp con lan_wifi aggiunto
dhcp = (SES / 'live-config/etc/config/dhcp').read_text()
assert "config dhcp 'lan_wifi'" not in dhcp
dhcp += "\nconfig dhcp 'lan_wifi'\n\toption interface 'lan_wifi'\n\toption start '100'\n\toption limit '100'\n\toption leasetime '12h'\n"
(OVL / 'owrt/etc/config/dhcp').write_text(dhcp)
print('dhcp: + sezione lan_wifi')

# --- C) moduli
mdir = OVL / 'owrt/lib/modules' / KV
mdir.mkdir(parents=True)
for m in ['cnss_prealloc.ko', 'cnss_utils.ko', 'cnss_nl.ko', 'wlan_firmware_service.ko',
          'cnss_plat_ipc_qmi_svc.ko', 'qcom_ramdump.ko', 'qrtr-mhi.ko', 'cnss2.ko']:
    shutil.copy2(SES / 'v54-modules' / m, mdir / m)
shutil.copy2('/home/user/nx679j-stock/port-work/vboot-dtb-swap/stock_dump/vendor_modules/qca_cld3_qca6490.ko',
             mdir / 'qca_cld3_qca6490.ko')
print('moduli overlay:', len(list(mdir.iterdir())))

# --- D) regulatory
(OVL / 'owrt/lib/firmware').mkdir(parents=True)
shutil.copy2(SES / 'live-config/lib/firmware/regulatory.db', OVL / 'owrt/lib/firmware/regulatory.db')

# ---------------------------------------------------------------- 1. v54.cpio + uid0
out = subprocess.run('find . | cpio -o -H newc --quiet', shell=True, cwd=OVL,
                     capture_output=True, check=True).stdout
data = bytearray(out)
off = 0
tzero = 0
while off + 110 <= len(data) and data[off:off + 6] == b'070701':
    namesize = int(data[off + 94:off + 102], 16)
    filesize = int(data[off + 54:off + 62], 16)
    if data[off + 22:off + 30] != b'00000000' or data[off + 30:off + 38] != b'00000000':
        tzero += 1
    data[off + 22:off + 30] = b'00000000'
    data[off + 30:off + 38] = b'00000000'
    namepad = namesize + ((4 - ((110 + namesize) % 4)) % 4)
    off = off + 110 + namepad + ((filesize + 3) & ~3)
print(f'v54.cpio {len(data)} B, {tzero} uid azzerati')
(W / 'v54-uid0.cpio').write_bytes(bytes(data))

# ---------------------------------------------------------------- 2. merge + lz4
v9 = (INIT9 / 'v10-work/v9.cpio').read_bytes()
new = (INIT9 / 'v10-work/new-uid0.cpio').read_bytes()
merged = v9 + new + bytes(data)
(W / 'merged.cpio').write_bytes(merged)
print(f'merged cpio: {len(merged)} B (v9 {len(v9)} + new {len(new)} + v54 {len(data)})')
newram = subprocess.run(['lz4', '-l', '-9', '-c'], input=merged,
                        capture_output=True, check=True).stdout
(W / 'new-ramdisk.lz4').write_bytes(newram)
print(f'ramdisk lz4 legacy: {len(newram)} B (prima: 11413547)')

# ---------------------------------------------------------------- 3. contenitore
img = IMG.read_bytes()
assert img[:8] == b'ANDROID!'
ks, rs = struct.unpack_from('<II', img, 8)
print(f'base: kernel_size={ks} ramdisk_size={rs} total={len(img)}')
ram_off = P + ru(ks)
sig = img[ram_off + ru(rs):ram_off + ru(rs) + P]
hdr = bytearray(img[:P])
struct.pack_into('<I', hdr, 12, len(newram))
body = (bytes(hdr) + img[P:P + ru(ks)] + newram
        + b'\0' * (ru(len(newram)) - len(newram)) + sig)
outimg = body + img[len(body):]
print(f'immagine: {len(outimg)} B; stessa lunghezza: {len(outimg) == len(img)}')
assert len(outimg) == len(img), 'lunghezza cambiata!'
OUT.write_bytes(outimg)
print('sha256:', hashlib.sha256(outimg).hexdigest())
print('md5   :', hashlib.md5(outimg).hexdigest())

# ---------------------------------------------------------------- 4. verifica
# 4a. il v54 contiene tutto
ver = subprocess.run(['cpio', '-it', '--quiet'], input=bytes(data), capture_output=True)
files = set(x for x in ver.stdout.decode('latin1').split('\n') if x)
need = ['nx679j/switch.sh', 'owrt/etc/config/wireless', 'owrt/etc/config/network',
        'owrt/etc/config/uhttpd', 'owrt/etc/config/dhcp',
        f'owrt/lib/modules/{KV}/qca_cld3_qca6490.ko', f'owrt/lib/modules/{KV}/cnss2.ko',
        'owrt/lib/firmware/regulatory.db']
for n in need:
    print(f'  v54: {n:60s} {"OK" if n in files else "ASSENTE"}')
assert all(n in files for n in need)

# 4b. estrazione del merged PER ARCHIVI (il kernel concatena, cpio no):
# si estraggono in sequenza v9 -> new -> v54 nello stesso scratch; l'ultimo vince.
scratch = W / 'check'
scratch.mkdir()
for part_data in (v9, new, bytes(data)):
    subprocess.run(['cpio', '-idmu', '--quiet'], cwd=scratch, input=part_data,
                   capture_output=True)  # mknod non permesso: ignorato
swfinal = (scratch / 'nx679j/switch.sh').read_text()
print('switch.sh finale contiene v54 wifi:', 'v54 wifi' in swfinal)
assert 'v54 wifi' in swfinal
init_final = (scratch / 'init').read_bytes()
print('init finale sha256[:8]:', hashlib.sha256(init_final).hexdigest()[:8], '(atteso 658dbaf6... md5)')
import hashlib as _h
print('init md5 == live:', _h.md5(init_final).hexdigest() == '658dbaf67e5687b76bee4a186485c522')
assert _h.md5(init_final).hexdigest() == '658dbaf67e5687b76bee4a186485c522'
wcfg = (scratch / 'owrt/etc/config/wireless').read_text()
print('wireless finale ha NX679J-TEST:', 'NX679J-TEST' in wcfg)
assert 'NX679J-TEST' in wcfg
netcfg = (scratch / 'owrt/etc/config/network').read_text()
print('network finale senza usb0:', 'usb0' not in netcfg)
assert 'usb0' not in netcfg
qca = scratch / 'owrt/lib/modules' / KV / 'qca_cld3_qca6490.ko'
print('qca in immagine md5:', _h.md5(qca.read_bytes()).hexdigest())
print('DONE ->', OUT)
