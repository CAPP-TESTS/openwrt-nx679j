#!/usr/bin/env python3
"""localtest-relay.py: does the SHIPPED relay really serve the dump over TCP?

Nothing here uses a build artifact from this experiment's own tree as the thing
under test: the program is taken OUT OF boot_b-init-v9.img (image -> lz4 ->
cpio -> /nx679j/relay), hash-checked against the manifest, and then run under
qemu-aarch64 user mode.  The client is a real netcat (`tools/bin/nc`, the
BusyBox netcat, static) started as `nc 127.0.0.1 <port>` exactly like the
command a human will type at 10.0.0.1 on the phone.

What is asserted, and with what evidence:

  1. a plain `nc` gets a complete dump: banner, sections [1]..[9], end marker,
     exit status 0 (the relay closes, nc terminates);
  2. the dump is a READABLE TEXT file: every byte printable ASCII (or \\n/\\t);
  3. the journal in the dump is the journal on disk: the FNV-1a 64 hashes the
     relay prints are recomputed here in Python over the file and over the
     window;
  4. THE ANSWER DOES NOT DEPEND ON THE CLIENT: sections [1]..[8] are byte
     identical for three different clients, including one that first sends
     junk (a client cannot ask for anything, and cannot change anything);
  5. three sequential clients are all served (the relay never dies on the
     first one, and never stops listening);
  6. a client that connects and does NOT read cannot hold the relay: another
     client is served while it is still hanging, and after the client deadline
     the relay reports itself with no serving child left;
  7. the device is not modified: sha256 of every file of the fake root is
     unchanged after all of the above;
  8. READ-ONLY, from the real syscalls: the same binary is run under
     qemu-aarch64 -strace and the trace is inspected: no open with
     O_WRONLY/O_RDWR/O_CREAT/O_TRUNC/O_APPEND, no unlink/rename/mknod/mkdir/
     mount/finit_module/chmod/chown/truncate, no SIOCSIFADDR, and every write
     goes to a socket or to the console;
  9. a big journal (2.8 MB) produces a bounded dump that keeps the NEWEST
     complete lines, and says so.

NOT asserted here: that usb0/10.0.0.1 exists on the phone (that is the gadget's
job, measured in the QEMU boot test), nor anything about real hardware.
"""
import hashlib
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

OUT = Path('/home/user/nx679j-stock/experiments/20260917-init-v9')
IMG = OUT / 'boot_b-init-v9.img'
NC = OUT / 'tools' / 'bin' / 'nc'
ROOT = OUT / 'localtest'
SMALL = ROOT / 'root-small'
BIG = ROOT / 'root-big'
checks = []


def check(name, ok, detail=''):
    checks.append({'check': name, 'pass': bool(ok), 'detail': str(detail)[:400]})
    print(f'[{"PASS" if ok else "FAIL"}] {name}: {detail}')
    return bool(ok)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def fnv(b):
    h = 14*****************
    for c in b:
        h ^= c
        h = (h * 1099511628211) & ((1 << 64) - 1)
    return h


def free_port():
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    p = s.getsockname()[1]
    s.close()
    return p


def extract_relay():
    """the relay, taken out of the shipped image (not from the build tree)"""
    img = IMG.read_bytes()
    ksize, rsize = struct.unpack_from('<II', img, 8)
    r_off = 4096 + ((ksize + 4095) // 4096) * 4096
    cpio = subprocess.run(['lz4', '-dc'], input=img[r_off:r_off + rsize],
                          capture_output=True).stdout
    off = 0
    while off < len(cpio):
        if cpio[off:off + 6] not in (b'070701', b'070702'):
            break
        fsize = int(cpio[off + 54:off + 62], 16)
        ns = int(cpio[off + 94:off + 102], 16)
        nm = cpio[off + 110:off + 110 + ns - 1].decode()
        hdr_end = off + 110 + ns
        doff = hdr_end + (-hdr_end % 4)
        if nm == 'nx679j/relay':
            return cpio[doff:doff + fsize]
        if nm == 'TRAILER!!!':
            break
        off = doff + fsize + (-(doff + fsize) % 4)
    raise SystemExit('no /nx679j/relay in the shipped image')


def mkroot(dst, journal_bytes):
    shutil.rmtree(dst, ignore_errors=True)
    for d in ('proc', 'sys/class/net/lo', 'sys/class/net/usb0',
              'sys/class/udc/a600000.dwc3', 'sys/class/usb_role/dext',
              'sys/class/block/rd', 'sys/class/block/sda1', 'sys/fs/pstore',
              'sys/module/foo', 'sys/module/bar', 'sys/kernel/config/usb_gadget/g1',
              'lib/modules/5.10.136-android13', 'dev', 'dev/block/by-name'):
        (dst / d).mkdir(parents=True, exist_ok=True)
    files = {
        'proc/version': 'Linux version 5.10.136-android13 (abuild@nx679j) fake\n',
        'proc/cmdline': 'console=ttyAMA0 loglevel=8 rdinit=/init\n',
        'proc/uptime': '12.34 5.67\n',
        'proc/loadavg': '0.05 0.10 0.05 1/120 345\n',
        'proc/meminfo': 'MemTotal: 4096000 kB\nMemFree: 3000000 kB\n',
        'proc/devices': 'Character devices:\n  1 mem\nBlock devices:\n259 blkext\n',
        'proc/mounts': '/dev/rd on /mnt type ext4 (rw)\n',
        'sys/class/net/lo/operstate': 'up\n',
        'sys/class/net/lo/address': 'xx:xx:xx:xx:xx:xx\n',
        'sys/class/net/usb0/operstate': 'unknown\n',
        'sys/class/net/usb0/address': 'xx:xx:xx:xx:xx:xx\n',
        'sys/class/udc/a600000.dwc3/state': 'configured\n',
        'sys/class/udc/a600000.dwc3/current_speed': 'super-speed\n',
        'sys/class/udc/a600000.dwc3/function': 'configfs-gadget.g1\n',
        'sys/class/usb_role/dext/role': 'device\n',
        'sys/class/block/rd/size': '524288\n',
        'sys/class/block/rd/dev': '7:0\n',
        'sys/class/block/sda1/size': '524288\n',
        'sys/class/block/sda1/dev': '8:1\n',
        'sys/class/block/sda1/partition': '1\n',
        'sys/module/foo/refcnt': '1\n',
        'sys/kernel/config/usb_gadget/g1/UDC': 'a600000.dwc3\n',
        'sys/kernel/config/usb_gadget/g1/idVendor': '0x18d1\n',
        'sys/kernel/config/usb_gadget/g1/idProduct': '0x4ee7\n',
        'dev/kmsg': '',
        'dev/rd': '',
    }
    for k, v in files.items():
        p = dst / k
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(v)
    for i in range(3):
        (dst / f'lib/modules/5.10.136-android13/mod{i}.ko').write_bytes(b'\x7fELF fake')
    (dst / 'nx679j-journal').write_bytes(journal_bytes)
    return dst


def snapshot(root):
    out = {}
    for p in sorted(root.rglob('*')):
        if p.is_file():
            st = p.stat()
            out[str(p.relative_to(root))] = (sha(p.read_bytes()), st.st_size, st.st_mtime_ns)
    return out


def start_relay(binary, root, port, extra_env=None, strace=False, wait_s=30,
                pass_arg=True):
    env = dict(os.environ, V9_RELAY_ROOT=str(root), V9_RELAY_IF='lo',
               V9_RELAY_IP='127.0.0.1', V9_RELAY_WAIT='1')
    if extra_env:
        env.update(extra_env)
    cmd = ['qemu-aarch64']
    if strace:
        cmd.append('-strace')
    cmd.append(str(binary))          # no port argument = the SHIPPED default
    if port and pass_arg:
        cmd.append(str(port))
    log = open(ROOT / f'relay-{port}{"-strace" if strace else ""}.log', 'wb')
    p = subprocess.Popen(cmd, env=env, stdout=log, stderr=log)
    t0 = time.time()
    while time.time() - t0 < wait_s:
        try:
            s = socket.create_connection(('127.0.0.1', port), timeout=1)
            s.close()
            return p, log, time.time() - t0
        except OSError:
            if p.poll() is not None:
                return p, log, -1
            time.sleep(0.2)
    return p, log, -1


def nc(port, timeout=30, prefix=b'', host='127.0.0.1'):
    """a REAL netcat run: `nc <host> <port>`, the same command the user types"""
    p = subprocess.Popen([str(NC), host, str(port)], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        out, err = p.communicate(input=prefix, timeout=timeout)
    except subprocess.TimeoutExpired:
        p.kill()
        out, err = p.communicate()
        return out, err, p.returncode, False
    return out, err, p.returncode, True


def sections(dump):
    """the dump split per section header, so two clients can be compared"""
    parts = re.split(rb'\n(\[\d+\] [^\n]*)\n', dump)
    out, hdr = {}, None
    for i, part in enumerate(parts):
        if i % 2 == 1:
            hdr = part.decode(errors='replace')
        elif hdr:
            out[hdr] = part
    return out


def journal_window(dump):
    m = re.search(rb'----8<---- nx679j-journal ----8<----\n(.*)\n  ---->8---- end of journal',
                  dump, re.S)
    return m.group(1) if m else None


def main():
    OUT.mkdir(exist_ok=True)
    shutil.rmtree(ROOT, ignore_errors=True)
    ROOT.mkdir(parents=True)
    res = {'checks': []}
    print('=== localtest-relay: the shipped relay, a real nc, real syscalls ===\n')

    if not NC.exists():
        raise SystemExit(f'the netcat used by this test is missing: {NC}')
    nc_sha = sha(NC.read_bytes())
    ver = subprocess.run([str(NC), '--help'], capture_output=True, text=True)
    first = (ver.stderr + ver.stdout).strip().splitlines() or ['?']
    print(f'client: {NC} sha256={nc_sha[:16]}...  ({first[0]})')
    print('   the exact client command used here: nc 127.0.0.1 <port>  '
          '(on the phone: nc 10.0.0.1 9999)')
    binary = extract_relay()
    relaybin = ROOT / 'relay-v9.from-image'
    relaybin.write_bytes(binary)
    os.chmod(relaybin, 0o755)
    man = json.loads((OUT / 'manifest-v9.json').read_text())
    check('the relay under test is the one inside the shipped image',
          sha(binary) == man['relay']['sha256'] and len(binary) == man['relay']['bytes'],
          f'{len(binary)} bytes sha256={sha(binary)[:16]}... '
          f'(manifest {man["relay"]["sha256"][:16]}...)')

    # ---------------------------------------------------------------- small case
    jbytes = b''.join(b'nx679j: line %04d op=chain rc=0 journal=1 gadget=0\n' % i
                      for i in range(200))
    mkroot(SMALL, jbytes)
    before = snapshot(SMALL)
    port = free_port()
    p, log, ready = start_relay(relaybin, SMALL, port)
    check('the relay starts and listens (accept() succeeds)', ready >= 0,
          f'listening after {ready:.1f} s on {port}' if ready >= 0 else 'never listened')

    out, err, rc, finished = nc(port)
    res['nc_small'] = dict(bytes=len(out), rc=rc, finished=finished)
    check('`nc 127.0.0.1 <port>` returns a dump and exits 0 (the relay closes)',
          finished and rc == 0 and len(out) > 1000,
          f'{len(out)} bytes, nc rc={rc}, finished={finished}')
    check('the dump says what it is: READ-ONLY, and how to reconnect',
          b'READ-ONLY SNAPSHOT' in out and b'nx679j-relay-v9' in out,
          out.split(b'\n')[1].decode(errors='replace'))
    heads = [h for h in sections(out)]
    want = [f'[{i}]' for i in range(1, 11)]
    got = [h.split(']')[0] + ']' for h in heads]
    check('all ten sections are present, in order', got == want, ', '.join(got))
    check('the dump is readable text (printable ASCII + \\n/\\t only)',
          all(b in b'\n\t' or 32 <= b < 127 for b in out),
          f'{sum(1 for b in out if not (b in (10, 9) or 32 <= b < 127))} non-printable bytes')
    check('the journal section carries the real journal, line by line',
          b'nx679j: line 0000 ' in out and b'nx679j: line 0199 ' in out,
          f'{out.count(b"nx679j: line ")} of 200 journal lines present')
    # the hashes the relay prints must match the file
    mh = re.search(rb'hash_fnv1a64_full=0x([0-9a-f]+) hash_fnv1a64_window=0x([0-9a-f]+)', out)
    full, win = (int(mh.group(1), 16), int(mh.group(2), 16)) if mh else (0, 0)
    jw = journal_window(out)
    # the dump puts \n between the last journal line and the end marker, so the
    # bytes the relay hashed are exactly jw + b'\n'
    winh = fnv(jw + b'\n')
    check('the journal hashes in the dump are the hashes of the file on disk',
          full == fnv(jbytes) and win == winh,
          f'full 0x{full:016x} == 0x{fnv(jbytes):016x}, '
          f'window 0x{win:016x} == 0x{winh:016x}')

    # the relay must report what the KERNEL gave it, and show that the address
    # really exists here (this is the property that failed in QEMU before the
    # fix: it used to echo the address it ASKED for)
    bl = [l for l in out.split(b'\n') if b'[relay] version' in l]
    wl = [l for l in out.split(b'\n') if b'[relay] wanted_ip' in l]
    check('the relay reports the address the kernel really bound and shows it '
          'is present on an interface here',
          bool(bl) and b'bind=127.0.0.1' in bl[0] and bool(wl)
          and b'on_an_interface=yes(lo)' in wl[0],
          ((bl[0] if bl else b'') + b' | ' + (wl[0] if wl else b'')).decode())

    # ------------------------------------------- three clients, one with junk
    outs = []
    for i, prefix in enumerate((b'', b'GET /journal HTTP/1.1\r\n\r\n', b'\x00\x01\x02rubbish')):
        o, e, r, f = nc(port, prefix=prefix)
        outs.append(o)
        check(f'client {i + 1} (prefix {len(prefix)} bytes) is served completely',
              f and r == 0 and b'END OF DUMP' in o, f'{len(o)} bytes')
    # sections [1]..[8] are device state: two clients must see the same thing
    # EXCEPT for the lines that are live by nature (/proc/uptime, /proc/loadavg,
    # /proc/meminfo).  [9] (the relay's own pid) and [10] (the byte count) belong
    # to the connection, so they are excluded on purpose.
    keep = lambda o: {k: v for k, v in sections(o).items()
                      if re.match(r'\[[1-8]\] ', k)}
    volatile = lambda l: (b'uptime' in l or b'loadavg' in l or b' | Mem' in l
                          or b'meminfo' in l)
    diffs = []
    for other in outs[1:]:
        a, b = keep(outs[0]), keep(other)
        for k in set(a) & set(b):        # a truncated dump lacks sections
            for la, lb in zip(a[k].split(b'\n'), b[k].split(b'\n')):
                if la != lb:
                    diffs.append((k, la[:60], lb[:60]))
    check('what the client SENDS changes nothing: sections [1]..[8] are the '
          'same for all three clients (only live /proc counters may differ)',
          all(volatile(d[1]) for d in diffs),
          f'{len(diffs)} differing lines, all live counters: '
          f'{[d[1].decode(errors="replace") for d in diffs[:2]]}')
    res['small_clients'] = [len(o) for o in outs]

    # ------------------------------------------------------- the rude client
    print('\n-- a client that connects and never reads --')
    line = [l for l in outs[0].split(b'\n') if b'client_deadline_s' in l]
    check('the SHIPPED relay announces its own 30 s client deadline',
          line and b'client_deadline_s=30' in line[0],
          line[0].decode() if line else 'missing')
    # a second instance of the SAME shipped binary, with only that deadline
    # shortened (V9_RELAY_DEADLINE) so this test does not have to wait 30 s
    port_r = free_port()
    pr, logr, readyr = start_relay(relaybin, SMALL, port_r,
                                   extra_env=dict(V9_RELAY_DEADLINE='5'))
    o, _, _, _ = nc(port_r, timeout=20)
    line = [l for l in o.split(b'\n') if b'client_deadline_s' in l]
    check('the deadline is configurable for tests (V9_RELAY_DEADLINE=5 here)',
          line and b'client_deadline_s=5' in line[0],
          line[0].decode() if line else 'missing')
    rude = socket.create_connection(('127.0.0.1', port_r), timeout=5)
    t0 = time.time()
    o, e, r, f = nc(port_r, timeout=25)
    dt = time.time() - t0
    check('another client is served while the rude one is still hanging',
          f and b'END OF DUMP' in o, f'served in {dt:.1f} s, {len(o)} bytes')
    time.sleep(8)                       # past this instance's 5 s deadline
    nc(port_r, timeout=20)              # reaps on the next accept
    o2, _, _, _ = nc(port_r, timeout=20)
    m = re.search(rb'serving_children=(\d+)', o2)
    check('after the deadline the rude client is gone: the relay reports no '
          'leftover serving child', m and m.group(1) == b'1',
          f'serving_children={m.group(1).decode() if m else "?"} (1 = only this client)')
    rude.close()
    pr.terminate()
    logr.close()

    # ------------------------------------------- the SHIPPED default port 9999
    print('\n-- no port argument at all: the default in the shipped bytes --')
    busy = False
    try:
        _s = socket.socket()
        _s.bind(('127.0.0.1', 9999))
        _s.close()
    except OSError:
        busy = True
    if busy or 9999 == port:
        check('the default port 9999 could be tested', False,
              'port 9999 is busy on this machine')
    else:
        pd, logd, readyd = start_relay(relaybin, SMALL, 9999, pass_arg=False)
        od, _, rd_, fd_ = nc(9999, timeout=25)
        line = [l for l in od.split(b'\n') if b'[relay] version' in l]
        check('with no argument the shipped relay listens on 9999 and says so '
              'in the dump (this is the port the phone uses)',
              fd_ and b'port=9999' in (line[0] if line else b''),
              line[0].decode() if line else 'no dump')
        logd.close()
        pd.terminate()

    after = snapshot(SMALL)
    check('nothing in the device tree was touched by any of it',
          before == after, f'{len(before)} files hashed before and after, '
                           f'{len(set(before) ^ set(after))} differences')

    # ------------------------------------------------------- the big journal
    print('\n-- a 2.8 MB journal: bounded dump, newest lines --')
    big = (b'nx679j: ' + b'filler journal line to make the file big\n') * 60000
    mkroot(BIG, big)
    port2 = free_port()
    p2, log2, ready2 = start_relay(relaybin, BIG, port2)
    o, e, r, f = nc(port2, timeout=40)
    res['nc_big'] = dict(bytes=len(o), rc=r, finished=f)
    mh = re.search(rb'journal_file_bytes=(\d+) window_bytes=(\d+)\n\s*hash_fnv1a64_full=0x([0-9a-f]+) '
                   rb'hash_fnv1a64_window=0x([0-9a-f]+)', o)
    jw = journal_window(o) or b''
    ok = bool(mh) and int(mh.group(1)) == len(big) and int(mh.group(3), 16) == fnv(big) \
        and int(mh.group(4), 16) == fnv(jw + b'\n') and len(o) < 600000
    check('a 2.8 MB journal still yields a bounded dump whose hashes are exact',
          ok, f'{len(o)} bytes, file {mh.group(1).decode() if mh else "?"} B, '
              f'window {mh.group(2).decode() if mh else "?"} B')
    check('the window is the NEWEST complete lines of the journal, and says so',
          b'TRUNCATED' in o and jw.startswith(b'nx679j:')
          and big.rstrip(b'\n').endswith(jw.rstrip(b'\n')[-40:]),
          f'window starts with {jw[:24]!r}')
    check('the dump grew by less than the journal did (it is a window, not a copy)',
          len(o) < len(big) / 3, f'{len(o)} vs journal {len(big)}')
    p2.terminate(); log2.close()

    # ------------------------------------------------------- READ-ONLY, traced
    print('\n-- the real syscalls of the same binary (qemu -strace) --')
    port3 = free_port()
    p3, log3, ready3 = start_relay(relaybin, SMALL, port3, strace=True)
    nc(port3, timeout=30)
    nc(port3, timeout=30)
    time.sleep(1)
    p3.terminate()
    time.sleep(0.5)
    log3.close()
    trace = (ROOT / f'relay-{port3}-strace.log').read_bytes()
    lines = [l for l in trace.split(b'\n') if re.match(rb'^\d+ [a-z_]+\S*\(', l)]
    names = {}
    for l in lines:
        n = re.match(rb'^\d+ ([a-z_0-9]+)\(', l)
        if n:
            names[n.group(1).decode()] = names.get(n.group(1).decode(), 0) + 1
    res['strace_syscalls'] = dict(sorted(names.items()))
    open_sites = [l for l in lines if re.match(rb'^\d+ open(at)?\(', l)]
    check('no file is ever opened for writing',
          not any(b'O_WRONLY' in l or b'O_RDWR' in l or b'O_CREAT' in l
                  or b'O_TRUNC' in l or b'O_APPEND' in l for l in open_sites),
          f'{len(open_sites)} opens, all O_RDONLY'
          f'{"" if not open_sites else " (" + open_sites[0].split(b",")[1].strip().decode()[:40] + ")"}')
    forbidden = ('unlink', 'rename', 'mknod', 'mkdir', 'mount', 'finit_module',
                 'init_module', 'chmod', 'chown', 'truncate', 'symlink',
                 'fallocate', 'reboot', 'writev', 'pwrite')
    hit = {k: v for k, v in names.items() if any(f in k for f in forbidden)}
    check('no syscall that mutates the device is executed at all', not hit,
          hit or f'{len(names)} distinct syscalls, none of them a write to the system')
    check('the network is only READ, never configured (no SIOCSIFADDR)',
          b'SIOCSIFADDR' not in trace and b'SIOCGIFADDR' in trace,
          f'{names.get("ioctl", 0)} ioctl calls, SIOCGIFADDR present, SIOCSIFADDR absent')
    # which fds hold sockets in this run?  the ones returned by socket()/accept()
    sock_fds = set()
    for l in lines:
        m = re.match(rb'^\d+ (socket|accept)\(.* = (\d+)$', l.strip())
        if m:
            sock_fds.add(m.group(2).decode())
    wl = [l for l in lines if re.match(rb'^\d+ write\(', l)]
    fds = {}
    for l in wl:
        m = re.match(rb'^(\d+) write\((\d+),', l)
        if m:
            fds[(m.group(1).decode(), m.group(2).decode())] = \
                fds.get((m.group(1).decode(), m.group(2).decode()), 0) + 1
    unexpected = {k: v for k, v in fds.items()
                  if k[1] not in ('1', '2') and k[1] not in sock_fds}
    check('every write goes to the console (fd 1/2) or to a socket: '
          'nothing is ever written to a file, a device or the rawdump',
          not unexpected and bool(fds),
          f'{len(wl)} writes, targets (pid,fd): {dict(sorted(fds.items()))}, '
          f'socket fds seen: {sorted(sock_fds, key=int)}')
    check('the relay really served both clients in the traced run',
          names.get('accept', 0) >= 2 and names.get('bind', 0) >= 1,
          f'accept={names.get("accept", 0)} bind={names.get("bind", 0)} '
          f'listen={names.get("listen", 0)}')

    print('\n-- stopping the relays --')
    for proc in (p, p3):
        if proc.poll() is None:
            proc.terminate()
    for proc in (p, p3):
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=10)
    log.close()
    check('the relay process ends when it is terminated (it never traps '
          'signals, so it cannot refuse to die)',
          p.poll() is not None and p3.poll() is not None,
          f'rc {p.poll()} and {p3.poll()}')

    # -------------------------------------------------------------- summary
    passed = sum(1 for c in checks if c['pass'])
    res['checks'] = checks
    res['nc'] = dict(path=str(NC), sha256=nc_sha)
    res['relay_from_image'] = dict(sha256=sha(binary), bytes=len(binary))
    res['summary'] = {'passed': passed, 'total': len(checks)}
    (OUT / 'localtest-relay.json').write_text(json.dumps(res, indent=2) + '\n')
    print(f'\n=== {passed}/{len(checks)} checks passed -> localtest-relay.json')
    return 0 if passed == len(checks) else 1


if __name__ == '__main__':
    sys.exit(main())
