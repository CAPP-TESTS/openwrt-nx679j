#!/usr/bin/env python3
"""
v10 build: base = la ramdisk v9 (che funziona), + il rootfs OpenWrt sotto /owrt,
+ /nx679j/switch.sh, + l'init modificato che passa la mano a procd.

Nessuna estrazione della ramdisk v9: si CONCATENA il suo cpio con quello dei file
nuovi.  Il kernel unisce gli archivi initramfs e le voci con lo stesso nome
vengono sovrascritte, quindi il nostro /init nuovo vince (stesso meccanismo con
cui il vendor_ramdisk si sovrappone al boot ramdisk, gia' verificato).
"""
import hashlib
import pathlib
import shutil
import struct
import subprocess

EXP = pathlib.Path('/home/user/nx679j-stock/experiments')
V9DIR = EXP / '20260917-init-v9'
ROOT = EXP / 'openwrt-rootfs/root'
V9IMG = V9DIR / 'boot_b-init-v9.img'
OUT = V9DIR / 'boot_b-init-v10.img'
WORK = V9DIR / 'v10-work'
P = 4096


def ru(x):
    return (x + P - 1) // P * P


def run(cmd):
    print('+', cmd if isinstance(cmd, str) else ' '.join(map(str, cmd)))
    return subprocess.run(cmd, shell=isinstance(cmd, str), check=True)


WORK.mkdir(exist_ok=True)

# ---------------------------------------------------------------- 1. compile
run(['aarch64-linux-gnu-gcc', '-static', '-nostdlib', '-ffreestanding',
     '-fno-stack-protector', '-Os', '-s', '-Wall', '-o',
     str(WORK / 'init'), str(V9DIR / 'nx679j-init-v9.c')])
print('init compilato:', (WORK / 'init').stat().st_size, 'B')

# ---------------------------------------------------------------- 2. switch.sh
SWITCH = r'''#!/owrt/bin/busybox sh
# v15 hand-over.  Every step is appended to the journal so the relay can show
# it live, and PID 1 CANNOT die: a dead PID 1 is a kernel panic, which is the
# reboot loop we just observed on hardware.  Only a fully prepared root is
# handed over with exec; anything else leaves the shell alive forever.
BB=/owrt/bin/busybox
J=/nx679j-journal
say() { echo "switch.sh: $*" >> $J 2>/dev/null; }
say "start pid=$$"
$BB mkdir -p /owrt/proc /owrt/sys /owrt/dev /owrt/dev/pts /owrt/tmp /owrt/run
for m in "proc proc /owrt/proc" "sysfs sysfs /owrt/sys" "tmpfs tmpfs /owrt/dev" "tmpfs tmpfs /owrt/tmp" "tmpfs tmpfs /owrt/run" "devpts devpts /owrt/dev/pts"; do
    set -- $m
    if $BB mount -t $1 $2 $3 2>>$J; then say "mount $3 OK"; else say "mount $3 FAILED"; fi
done
$BB mknod -m 600 /owrt/dev/console c 5 1 2>>$J
$BB mknod -m 666 /owrt/dev/null    c 1 3 2>>$J
$BB mknod -m 666 /owrt/dev/zero    c 1 5 2>>$J
$BB mknod -m 666 /owrt/dev/tty     c 5 0 2>>$J
$BB mknod -m 666 /owrt/dev/random  c 1 8 2>>$J
$BB mknod -m 666 /owrt/dev/urandom c 1 9 2>>$J
$BB mknod -m 666 /owrt/dev/ptmx    c 5 2 2>>$J
say "mdev -s"
$BB mdev -s 2>>$J
say "mdev rc=$?; /sbin/init executable: $([ -x /owrt/sbin/init ] && echo YES || echo NO)"
$BB mount --bind /owrt /owrt 2>/dev/null
if [ -x /owrt/sbin/init ]; then
    # v23: i mount DEVONO essere fatti dall'INTERNO del chroot.  Misurato via SSH:
    # dentro /owrt `ps w` era vuoto, "mount: no /proc/mounts", /proc/<pid>/status
    # inesistente, /dev con soli kmsg/null/pts/urandom -> procd non puo' partire.
    CI="$BB chroot /owrt /bin/busybox"
    $CI mount -t proc proc /proc 2>>$J
    $CI mount -t sysfs sysfs /sys 2>>$J
    say "v23: mount interni fatti"
    for n in "666 /dev/null c 1 3" "600 /dev/console c 5 1" "666 /dev/zero c 1 5" "666 /dev/tty c 5 0" "666 /dev/urandom c 1 9" "666 /dev/ptmx c 5 2"; do
        set -- $n; $CI mknod -m $1 $2 $3 $4 $5 2>>$J
    done
    # v24: procd/rc.common esigono /var/lock - senza, OGNI init script muore con
    # "can't create /var/lock/procd_*.lock: nonexistent directory" (MISURATO via
    # SSH) e ubusd/netifd non partono mai.  /var e' symlink a /tmp (tmpfs vuoto).
    $CI mkdir -p /var/lock /var/run /var/log /var/state /var/tmp /tmp/lock /tmp/run /tmp/log
    # v25: PRE-AVVIO ubusd prima di procd.  Misurato: `/sbin/ubusd &` a mano funziona
    # e `ubus list` mostra l'albero standard, ma procd NON lo lancia -> ubusd/netifd
    # non partono mai.  Se ubus esiste gia', procd ha una ragione in meno per fermarsi.
    ( $CI /sbin/ubusd >>$J 2>&1 & ) ; $BB sleep 2
    say "v25: ubusd pre-avviato; ubus list: $( $CI ubus list 2>/dev/null | wc -l ) oggetti"
    # v29: il mount del firmware lo fa lo SCRIPT con busybox (provato a mano: riesce),
    # perche' la syscall raw dall'init non riesce.  DEVE precedere il probe forzato.
    # v30: vfat e le nls sono MODULI: al momento dello script il filesystem non era
    # ancora registrato (il mount falliva senza dire perche').  Li carico con il
    # kmodloader del root esterno (busybox insmod non funziona) e riprovo.
    for m in vfat nls_cp437 nls_iso8859-1; do
        [ -f /lib/modules/$m.ko ] && /sbin/insmod /lib/modules/$m.ko 2>>$J
    done
    for i in 1 2 3 4 5; do
        $BB mount -t vfat /dev/sde6 /vendor/firmware_mnt 2>>$J && break
        $BB sleep 1
    done
    say "v30: image/ -> $(ls /vendor/firmware_mnt/image 2>/dev/null | head -2 | tr '\n' ' ')"
    # v36: i file del modem devono stare in /lib/firmware (il path che il kernel cerca:
    # lo dice il dmesg di Android "Direct firmware load for modem.b11").  Il bind va
    # fatto con busybox: la syscall raw nell'init da' ENOENT (misurato, loggato in v35).
    $BB mount --bind /vendor/firmware_mnt/image /lib/firmware 2>>$J && say "v36: bind /lib/firmware OK" || say "v36: bind FALLITO"
    say "v36: /lib/firmware -> $(ls /lib/firmware 2>/dev/null | head -2 | tr '\n' ' ')"
    # v36: rmmod+insmod di q6v5_pas = NUOVA registrazione di driver = il kernel riprova
    # i device differiti (finche' il timeout non scade).  Col firmware ora al posto
    # giusto, se quello era il pezzo mancante il remoteproc si registra adesso.
    $BB rmmod qcom_q6v5_pas 2>>$J
    [ -x /owrt/sbin/kmodloader ] && /owrt/sbin/kmodloader /proc/1/root/lib/modules/qcom_q6v5_pas.ko 2>>$J
    $BB sleep 3
    say "v36: remoteproc dopo il reload: $(ls /sys/class/remoteproc/ 2>/dev/null | tr '\n' ' ')"
    # v46: gli altri remoteproc in BACKGROUND: il start e' sincrono e il modem
    # puo' metterci secondi: in primo piano bloccherebbe rete e SSH.
    # v47: il probe di q6v5_pas deve FINIRE prima che qualcuno scriva start:
    # scriverlo durante il probe fa fallire il probe e i rproc vengono rilasciati
    # (misurato: v46, "releasing" a 19.251s = esattamente quando partiva lo script).
    # v49: attesa piu' lunga + controllo di stato: avvio ogni rproc solo se esiste
    # ed e' offline (se il probe e' ancora in corso la scrittura lo farebbe fallire).
    ( $BB sleep 30
      for _r in 0 1 2 3 4; do
        [ -e /sys/class/remoteproc/remoteproc$_r/state ] || continue
        [ "$(cat /sys/class/remoteproc/remoteproc$_r/state 2>/dev/null)" = "offline" ] || continue
        echo start > /sys/class/remoteproc/remoteproc$_r/state 2>>$J
        say "v49: rproc$_r -> $(cat /sys/class/remoteproc/remoteproc$_r/state 2>/dev/null)"
      done ) &
    $BB mkdir -p /vendor/firmware_mnt
    if $BB mount -t vfat /dev/sde6 /vendor/firmware_mnt 2>>$J; then say "v29: firmware montato"; else say "v29: mount firmware FALLITO"; fi
    say "v29: contenuto image/: $(ls /vendor/firmware_mnt/image 2>/dev/null | head -3 | tr '\n' ' ')"
    # v28: risveglio i remoteproc differiti.  Il mount del firmware avviene DOPO la
    # catena moduli (vfat e' un modulo), quindi quando q6v5_pas ha fatto il probe il
    # firmware non c'era ancora: il device e' andato in deferred.  Forzare il probe
    # adesso che il firmware e' montato e' cio' che accende il core del modem.
    for d in 4080000.remoteproc-mss 3000000.remoteproc-adsp 32300000.remoteproc-cdsp 2400000.remoteproc-slpi; do
        echo "$d" > /sys/bus/platform/drivers_probe 2>>$J
    done
    say "v28: probe forzato; remoteproc: $(ls /sys/class/remoteproc/ 2>/dev/null | tr '\n' ' ')"
    say "starting procd as a CHILD; its output is redirected into this journal"
    # v22: dropbear DIRETTO, senza procd.  Con SSH dentro l'OpenWrt in esecuzione
    # si vede PERCHE' procd non parte (logread, ubus, ps, init.d status) invece di
    # guardarlo dal di fuori attraverso un rele' read-only.  Chiavi generate qui.
    ( $BB sleep 6
      $BB mkdir -p /owrt/etc/dropbear
      $BB chroot /owrt /usr/bin/dropbearkey -t rsa -f /etc/dropbear/dropbear_rsa_host_key >>$J 2>&1
      say "chiave host pronta rc=$?"
      $BB chroot /owrt /usr/sbin/dropbear -F -E -p 22 >>$J 2>&1
      say "dropbear terminato rc=$?" ) &
    # v21: SONDA DI MISURA.  12 s dopo il via a procd, tabella processi e porte in
    # ascolto finiscono in journal: cosi' si VEDE se dropbear e' partito e se
    # qualcuno ascolta sulla 22, invece di dedurlo dal rifiuto di connessione.
    ( $BB sleep 12; $BB ps >> $J 2>&1; $BB netstat -ltn >> $J 2>&1 ) &
    $BB chroot /owrt /sbin/init >> $J 2>&1
    say "procd returned rc=$? -- PID 1 stays alive (no panic, no reboot loop)"
fi
say "keeping PID 1 alive forever: the phone stays calm and readable"
while :; do $BB sleep 3600; done
'''

# ---------------------------------------------------------------- 3. stage
NF = WORK / 'newfiles'
if NF.exists():
    shutil.rmtree(NF)
(NF / 'nx679j').mkdir(parents=True)
(NF / 'init').write_bytes((WORK / 'init').read_bytes())
(NF / 'init').chmod(0o755)
(NF / 'nx679j/switch.sh').write_text(SWITCH)
(NF / 'nx679j/switch.sh').chmod(0o755)
shutil.copytree(ROOT, NF / 'owrt', symlinks=True)
# v12: un tar estratto da utente NON privilegiato non garantisce i bit di
# esecuzione: /owrt/bin/busybox finiva 0644 e l'execve dava EACCES (verificato
# su hardware, errno=EACCES).  Ripristino i modi ESATTI dichiarati dall'archivio.
import tarfile                                                   # noqa: E402
with tarfile.open(EXP / 'openwrt-rootfs/owrt-aarch64-rootfs.tar.gz') as tf:
    _fixed = 0
    for _m in tf.getmembers():
        if not (_m.isfile() or _m.isdir()):
            continue
        _p = NF / ('owrt/' + _m.name.lstrip('./'))
        if not _p.exists():
            continue
        _want = _m.mode & 0o7777
        if (_p.stat().st_mode & 0o7777) != _want:
            _p.chmod(_want)
            _fixed += 1
    print('modi ripristinati dal tar:', _fixed)
# v11: l'exec di /owrt/bin/busybox avviene PRIMA del chroot, quindi il suo
# interprete dinamico (percorso assoluto /lib/ld-musl-aarch64.so.1) deve essere
# risolvibile NELLA RADICE DELLA RAMDISK.  Senza questi due file l'execve
# fallisce con ENOENT e si resta in mantenimento (verificato su hardware).
(NF / 'lib').mkdir(parents=True, exist_ok=True)
_src = (NF / 'owrt/lib/ld-musl-aarch64.so.1')
_src = _src.resolve() if _src.is_symlink() else _src
(NF / 'lib/libc.so').write_bytes(_src.read_bytes())
(NF / 'lib/libc.so').chmod(0o755)   # l'INTERPRETE deve essere eseguibile:
                                    # altrimenti l'execve del binario -> EACCES
(NF / 'lib/ld-musl-aarch64.so.1').symlink_to('libc.so')
# v20: busybox dichiara DT_NEEDED **libgcc_s.so.1** oltre a libc.so (misurato con
# readelf).  Nella radice della ramdisk c'era solo libc.so: musl non trovava
# /lib/libgcc_s.so.1 e l'execve di busybox (shebang dello script) moriva.
# Va copiata nella radice ESATTAMENTE la libreria aarch64 del rootfs, 0755.
_src2 = (NF / 'owrt/lib/libgcc_s.so.1')
if _src2.exists():
    (NF / 'lib/libgcc_s.so.1').write_bytes(_src2.read_bytes())
    (NF / 'lib/libgcc_s.so.1').chmod(0o755)
    print('libgcc_s.so.1 copiata nella radice:', _src2.stat().st_size, 'B')
# v26: stack modem.  Il worker carica i .ko elencati in /lib/modules/modules.load.
# Copio i moduli dal set vendor estratto (327 .ko) e creo la dir del firmware.
_MD = NF / 'lib/modules'
_MD.mkdir(parents=True, exist_ok=True)
(NF / 'vendor/firmware_mnt').mkdir(parents=True, exist_ok=True)
(NF / 'lib/firmware').mkdir(parents=True, exist_ok=True)
_VR = pathlib.Path('/tmp/vr/lib/modules')
_MOD = ['vfat.ko', 'nls_cp437.ko', 'nls_iso8859-1.ko',
        'mdt_loader.ko', 'qcom_pil_info.ko', 'qcom_sysmon.ko', 'qcom_q6v5.ko',
        'qcom_q6v5_pas.ko', 'qmi_helpers.ko', 'pdr_interface.ko', 'glink_pkt.ko',
        'smem.ko', 'mhi.ko', 'mhi_cntrl_qcom.ko', 'ipa_fmwk.ko', 'usb_f_gsi.ko',
        # v44: LA CAUSA RADICE.  I 9 device soc:qcom,smp2p-* non avevano NESSUN
        # driver: il modulo che li reclama non era in lista.  Senza smp2p non
        # esiste il canale qcom_smem_state che qcom_q6v5 (driver del modem) chiede
        # nel probe -> -EPROBE_DEFER per sempre.  Con smp2p caricato il fornitore
        # diventa pronto e il probe del mss puo' finalmente girare.
        'smp2p.ko', 'smp2p_sleepstate.ko',
        # v48: PERCORSO DATI del modem (come su Android).  Questi sono i moduli
        # che su Android fanno comparire rmnet_data0..3: rmnet + ipa.
        'rmnet_core.ko', 'rmnet_shs.ko', 'rmnet_perf.ko', 'rmnet_offload.ko',
        'rmnet_aps.ko', 'rmnet_sch.ko', 'rmnet_wlan.ko', 'rmnet_perf_tether.ko',
        'rmnet_ctl.ko', 'ipam.ko', 'ipa_clientsm.ko', 'rndisipam.ko',
        'ipanetm.ko', 'gsim.ko']
# v40: TRIGGER.  Servono .ko che il worker NON carica: il loro finit_module e'
# una NUOVA registrazione di driver, e il kernel riprova i device differiti
# (finestra misurabile: 1424 s).  NON vanno in modules.load, apposta.
_TRIG = ['qcom_spss.ko', 'cnss2.ko', 'icnss2.ko']
for _m in _TRIG:
    _s = _VR / _m
    if _s.exists():
        (_MD / _m).write_bytes(_s.read_bytes())
print('v40: trigger disponibili:', [m for m in _TRIG if (_MD / m).exists()])
_n = 0
for _m in _MOD:
    _s = _VR / _m
    if _s.exists():
        (_MD / _m).write_bytes(_s.read_bytes()); _n += 1
_base = (_VR / 'modules.load').read_text().split()
(_MD / 'modules.load').write_text('\n'.join(_base + _MOD) + '\n')
print('v26: .ko modem copiati:', _n, '| modules.load:', len(_base), '+', len(_MOD))
print('loader musl copiato nella radice ramdisk:', _src.stat().st_size, 'B')
(NF / 'owrt/etc/config').mkdir(parents=True, exist_ok=True)
(NF / 'owrt/etc/config/network').write_text(
    "config interface 'loopback'\n"
    "\toption device 'lo'\n\toption proto 'static'\n"
    "\toption ipaddr '127.0.0.1'\n\toption netmask '255.0.0.0'\n\n"
    "config interface 'usb0'\n"
    "\toption device 'usb0'\n\toption proto 'static'\n"
    "\toption ipaddr '10.0.0.1'\n\toption netmask '255.255.255.0'\n"
    "\toption delegate '0'\n")

key = pathlib.Path.home() / '.ssh/nx679j_key'
if not key.exists():
    run(['ssh-keygen', '-t', 'ed25519', '-N', '', '-f', str(key), '-C', 'nx679j-v10'])
(NF / 'owrt/etc/dropbear').mkdir(parents=True, exist_ok=True)
ak = NF / 'owrt/etc/dropbear/authorized_keys'
ak.write_text(pathlib.Path(str(key) + '.pub').read_text().strip() + '\n')
ak.chmod(0o600)
print('chiave ssh:', key, '-> authorized_keys dentro l immagine')

# ---------------------------------------------------------------- 4. cpio nuovi file
run(f'cd {NF} && find . | cpio -o -H newc --quiet > {WORK}/new.cpio')
data = bytearray((WORK / 'new.cpio').read_bytes())
off = 0
count = 0
while off + 110 <= len(data) and data[off:off + 6] == b'070701':
    # newc: i 13 campi sono ASCII ESADECIMALE da 8 caratteri, non interi binari.
    # campo i -> offset 6 + 8*i.   filesize=campo 6, namesize=campo 11,
    # uid=campo 2, gid=campo 3.
    namesize = int(data[off + 94:off + 102], 16)
    filesize = int(data[off + 54:off + 62], 16)
    data[off + 22:off + 30] = b'00000000'              # uid -> 0
    data[off + 30:off + 38] = b'00000000'              # gid -> 0
    count += 1
    # newc: l'header e' 110 byte; il nome (NUL incluso) e' paddato in modo che
    # header+nome sia multiplo di 4.  Padding del nome da solo: SBAGLIATO.
    namepad = namesize + ((4 - ((110 + namesize) % 4)) % 4)
    off = off + 110 + namepad + ((filesize + 3) & ~3)   # dati allineati a 4, non a pagina!
print('voci nel cpio nuovo:', count)
(WORK / 'new-uid0.cpio').write_bytes(bytes(data))

# ---------------------------------------------------------------- 5. concatena con la ramdisk v9
v9 = V9IMG.read_bytes()
ks, rs = struct.unpack_from('<II', v9, 8)
v9ram_off = P + ru(ks)
v9ram = v9[v9ram_off:v9ram_off + rs]
(WORK / 'v9.cpio').write_bytes(
    subprocess.run(['lz4', '-dc'], input=v9ram, capture_output=True, check=True).stdout)
run(f'cat {WORK}/v9.cpio {WORK}/new-uid0.cpio > {WORK}/merged.cpio')
raw = (WORK / 'merged.cpio').read_bytes()
newram = subprocess.run(['lz4', '-l', '-9', '-c'], input=raw,
                        capture_output=True, check=True).stdout
(WORK / 'new-ramdisk.lz4').write_bytes(newram)
print(f'merged cpio {len(raw)} B -> lz4 legacy {len(newram)} B')

# ---------------------------------------------------------------- 6. contenitore
sig_off = v9ram_off + ru(rs)
sig = v9[sig_off:sig_off + 4096]
hdr = bytearray(v9[:P])
struct.pack_into('<I', hdr, 12, len(newram))           # ramdisk_size
body = (bytes(hdr) + v9[P:P + ru(ks)] + newram
        + b'\0' * (ru(len(newram)) - len(newram)) + sig)
out = body + v9[len(body):]
print('immagine:', len(out), 'B (v9:', len(v9), ') stessa lunghezza:', len(out) == len(v9))
OUT.write_bytes(out)
print('sha256 v10:', hashlib.sha256(out).hexdigest())

# ---------------------------------------------------------------- 7. verifica
listing = subprocess.run(['cpio', '-it', '--quiet'], input=raw,
                         capture_output=True).stdout.decode('latin1').split('\n')
need = ['./init', './nx679j/switch.sh', './owrt/sbin/init',
        './owrt/etc/config/network', './owrt/etc/dropbear/authorized_keys',
        './owrt/bin/busybox', './nx679j/worker', './nx679j/relay']
print('--- presenza file chiave nel cpio unito ---')
for n in need:
    print(f'  {n:44s} {"OK" if n in listing else "ASSENTE"}')
print('voci totali:', len([x for x in listing if x]))
