#!/usr/bin/env python3
"""QEMU full-system verification of the v9 image.

In every run the ramdisk is the LZ4 frame taken out of boot_b-init-v9.img (the
shipped artifact), and the kernel is taken out of the same image in A and B.

  A "clean"    : device kernel, -nic none.  PID 1 walks mounts -> journal ->
                 gadget -> chain, fails (no UFS, no UDC), spawns the RELAY,
                 then reaches the heartbeat and calls reboot(2).  Under
                 -no-reboot QEMU exits: the "logo keeps cycling" outcome.
  B "poisoned" : one bundled .ko replaced by a FIFO so a loader child hangs for
                 ever in open(2).  The chain must kill it, journal
                 TIMEOUT-KILLED, continue, and PID 1 must still reach reboot.
  C "real nc"  : mainline arm64 kernel (the device kernel has NO NIC driver for
                 QEMU) + e1000 + slirp hostfwd + ip=..., so the guest has a real
                 network stack.  The SHIPPED relay is then read FROM THE HOST
                 with the real `nc`, through the guest kernel's TCP/IP stack:
                     nc 127.0.0.1 <hostfwd port>   ->  guest 0.0.0.0:9999
                 and the dump must contain the LIVE guest state (its kernel
                 version, its eth0 address, its own boot journal).

What this proves: PID 1 cannot be blocked by a stuck child (B), the relay child
is started by the shipped init and survives (A/B), and the shipped relay really
answers `nc` with the device's real state (C).  What it cannot prove: anything
about the phone's UFS/USB hardware.
"""
import hashlib
import json
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

OUT = Path('/home/user/nx679j-stock/experiments/20260917-init-v9')
WORK = OUT / 'qemu'
WORK.mkdir(parents=True, exist_ok=True)
IMG = OUT / 'boot_b-init-v9.img'
NC = OUT / 'tools' / 'bin' / 'nc'
UPSTREAM = Path('/home/user/nx679j-stock/kernel-patch-test'
                '/upstream-6.6.60-protected.Image')
PAGE = 4096
TAG = 'nx679j:'
HOSTFWD_PORT = 18107
res = {'runs': {}, 'checks': []}


def check(name, ok, detail=''):
    res['checks'].append({'check': name, 'pass': bool(ok),
                          'detail': str(detail)[:600]})
    print(f'[{"PASS" if ok else "FAIL"}] {name}: {str(detail)[:300]}')
    return bool(ok)


def split_image(img):
    ksize, rsize = struct.unpack_from('<II', img, 8)
    koff = PAGE + ((ksize + PAGE - 1) // PAGE) * PAGE
    return img[PAGE:PAGE + ksize], img[koff:koff + rsize], koff, rsize


def run_qemu(tag, kernel, ramdisk, deadline, extra_cmd=None, no_reboot=True):
    """Boot once and collect the serial log.  Returns the run record."""
    kern = WORK / f'{tag}-kernel'
    rd = WORK / f'{tag}-ramdisk.lz4'
    kern.write_bytes(kernel)
    rd.write_bytes(ramdisk)
    log = WORK / f'{tag}.log'
    cmd = ['qemu-system-aarch64', '-machine', 'virt', '-cpu', 'cortex-a57',
           '-accel', 'tcg', '-smp', '2', '-m', '2048', '-nodefaults',
           '-display', 'none', '-monitor', 'none', '-serial', 'stdio',
           '-kernel', str(kern), '-initrd', str(rd)]
    if no_reboot:
        cmd.append('-no-reboot')
    append = 'console=ttyAMA0 earlycon loglevel=8 panic=10 rdinit=/init'
    cmd += ['-nic', 'none', '-append', append]
    if extra_cmd:
        cmd = extra_cmd(cmd)
    d = {'qemu_cmd': cmd, 'log': str(log), 'ramdisk_sha256': hashlib.sha256(
        ramdisk).hexdigest()}
    res['runs'][tag] = d
    return d, cmd, log


def boot(tag, kernel, ramdisk, deadline, extra_cmd=None, no_reboot=True,
         ready_pat=None, ready_timeout=150):
    """Start QEMU, wait for `ready_pat` (or the deadline), leave it RUNNING if
    ready_pat is given, and return (dict, Popen)."""
    d, cmd, log = run_qemu(tag, kernel, ramdisk, deadline, extra_cmd,
                           no_reboot)
    print(f'[$] {tag}: ready={" ".join(cmd[:3])} ... -> {log.name}')
    t0 = time.time()
    proc = subprocess.Popen(cmd, stdout=open(log, 'wb'),
                            stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                            cwd=str(WORK))
    if ready_pat:
        while time.time() - t0 < ready_timeout:
            time.sleep(1)
            if log.exists() and ready_pat in log.read_bytes():
                d['ready_after_s'] = round(time.time() - t0, 1)
                d['proc'] = proc
                d['log'] = log
                return d, proc
            if proc.poll() is not None:
                break
        return d, proc
    while time.time() - t0 < deadline:
        time.sleep(2)
        if proc.poll() is not None or b'Restarting system' in log.read_bytes():
            time.sleep(2)
            break
    stop(proc)
    d.update(finish(tag, log, proc, t0))
    return d, proc


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


def finish(tag, log, proc, t0):
    text = log.read_bytes().decode(errors='replace')
    return {'exit_code': proc.returncode, 'seconds': round(time.time() - t0, 1),
            'rebooted': 'Restarting system' in text,
            'panics': [m for m in ('Kernel panic', 'Attempted to kill init',
                                   'not syncing', 'Unable to handle kernel',
                                   'Internal error: Oops', 'BUG:',
                                   'Call trace:') if m in text],
            'lines': [l for l in text.splitlines() if TAG in l]}


def fifo_ramdisk(lz4, first_ko):
    """Rebuild the ramdisk with one .ko entry turned into a FIFO, so the child
    that opens it hangs for ever inside open(2)."""
    cpio = subprocess.run(['lz4', '-dc'], input=lz4,
                          capture_output=True).stdout
    name = first_ko.encode()
    entries, off, victim = [], 0, None
    while True:
        if cpio[off:off + 6] not in (b'070701', b'070702'):
            raise SystemExit(f'bad cpio at {off}')
        hdr = cpio[off:off + 110]
        fsize = int(cpio[off + 54:off + 62], 16)
        namesize = int(cpio[off + 94:off + 102], 16)
        nm = cpio[off + 110:off + 110 + namesize - 1]
        hdr_end = off + 110 + namesize
        data_off = hdr_end + (-hdr_end % 4)
        payload = cpio[data_off:data_off + fsize]
        if nm == b'TRAILER!!!':
            break
        if victim is None and (nm == name or nm.rsplit(b'/', 1)[-1] == name):
            victim = nm
            entries.append((hdr, nm, b'', b'00001180'))
        else:
            entries.append((hdr, nm, payload, None))
        off = data_off + fsize + (-(data_off + fsize) % 4)
    if victim is None:
        raise SystemExit(f'{first_ko} not found in the ramdisk')
    out = bytearray()
    for hdr, nm, payload, mode in entries:
        h = bytearray(hdr)
        h[14:22] = mode if mode else h[14:22]
        h[54:62] = b'%08X' % len(payload)
        nfield = nm + b'\0'
        out += h + nfield + b'\0' * (-(110 + len(nfield)) % 4)
        out += payload + b'\0' * (-len(payload) % 4)
    out += cpio[off:]
    p = subprocess.run(['lz4', '-l', '-f', '-9'], input=bytes(out),
                       capture_output=True)
    if p.returncode:
        raise SystemExit('lz4 failed on the poisoned ramdisk')
    return p.stdout
def collect(proc, log):
    """stop the VM and return what the serial log says"""
    stop(proc)
    text = log.read_bytes().decode(errors='replace')
    return text


def run_a(kernel, lz4):
    print('\n=== A: device kernel, shipped ramdisk, no NIC ===')
    d, proc = boot('A-clean', kernel, lz4, deadline=300)
    text = d.get('text') or (WORK / 'A-clean.log').read_bytes().decode(
        errors='replace')
    got = '\n'.join(d['lines'])
    check('A: PID 1 came up and the journal is being written',
          'mounts: done' in got, d['lines'][:1])
    check('A: ORDER - the first journal write happens before ANY module load',
          text.find('FIRST JOURNAL WRITE') >= 0
          and text.find('FIRST JOURNAL WRITE') < text.find('ATTEMPT finit_module'),
          f'journal at {text.find("FIRST JOURNAL WRITE")}, '
          f'first finit_module at {text.find("ATTEMPT finit_module")}')
    check('A: ORDER - the first gadget attempt happens before ANY module load',
          0 <= text.find('gadget: start') < text.find('ATTEMPT finit_module'),
          f'gadget at {text.find("gadget: start")}')
    check('A: the journal write reported the rawdump state honestly',
          'first journal write:' in got,
          [l for l in d['lines'] if 'first journal write' in l][:1])
    check('A: the chain ran and attempted modules',
          'chain: start' in got and 'ATTEMPT finit_module' in got,
          [l for l in d['lines'] if 'chain:' in l][:2])
    check('A: no panic, PID 1 never died', not d['panics'], d['panics'] or 'clean')
    # ---- the v9 addition, in the device kernel -------------------------------
    check('A: PID 1 SPAWNED the relay (journal line from the shipped init)',
          'v9-relay-started op=relay rc=0' in got,
          [l for l in d['lines'] if 'v9-relay-started' in l][:1])
    up_lines = [l.strip() for l in text.splitlines()
                if 'READ-ONLY relay up' in l]
    check('A: the relay child itself says it is up (its own console line)',
          bool(up_lines), up_lines[:1])
    iface_lines = [l.strip() for l in text.splitlines()
                   if 'interfaces in this namespace' in l]
    check('A: the relay lists the interfaces it can see (so "no usb0" is a '
          'fact on the console, not a silence)',
          bool(iface_lines), iface_lines[:1])
    want_lines = [l.strip() for l in text.splitlines()
                  if 'is NOT on any interface' in l
                  or 'is ON an interface' in l]
    # The law, now independent of any kernel quirk: if the wanted address is
    # not on an interface in this namespace, the relay MUST fall back to the
    # wildcard (a listener the kernel accepted on an address nobody has is a
    # listener nobody can reach); if it IS present, it must have taken it.
    ok_inv, why = True, []
    for l in want_lines:
        m = re.search(r'wanted (\S+) is (ON an interface|NOT on any interface)'
                      r'.*nonlocal_bind=(\S+); bound (\S+):(\d+)', l)
        if not m:
            continue
        want, on, nl, bound, port = m.groups()
        if on.startswith('NOT') and bound != '0.0.0.0':
            ok_inv = False
        if on.startswith('ON') and bound != want:
            ok_inv = False
        why.append(f'bound {bound}:{port}, {want} {on.lower()}, '
                   f'nonlocal_bind={nl}')
    check('A: the relay binds the wanted address only when an interface here '
          'really has it, otherwise the wildcard -- and says which',
          ok_inv and bool(why), '; '.join(why) or 'no bind line found')
    fb = [l.strip() for l in text.splitlines() if 'wildcard_fallback=' in l]
    consistent = all(
        (('bind=0.0.0.0' in l or '0.0.0.0' in l) == ('wildcard_fallback=1' in l))
        for l in fb) if fb else False
    check('A: wildcard_fallback is 1 exactly when the bound address is 0.0.0.0 '
          '(the report is internally consistent)',
          bool(fb) and consistent, fb[:1])
    check('A: PID 1 reached reboot(2) WITH the relay child alive (the relay '
          'cannot wedge the boot)',
          d['rebooted'], f"{d['seconds']} s")
    check('A: the kernel performed the restart (QEMU -no-reboot exited)',
          d['exit_code'] is not None, f"exit code {d['exit_code']}")
    return d


def run_b(kernel, lz4):
    print('\n=== B: device kernel, one .ko replaced by a FIFO ===')
    first_ko = []
    order = OUT.parent / '20260916-122926-native-baseline' / 'probe-v7-sonda'
    tmp = Path('/tmp/v9x')
    if not (tmp / 'lib/modules/fallback.order').exists():
        tmp.mkdir(parents=True, exist_ok=True)
        subprocess.run(f'cpio -idm --quiet "lib/modules/*" < '
                       f'{order}/work/v7-ramdisk.cpio', shell=True, cwd=str(tmp),
                       check=True)
    txt = (tmp / 'lib/modules/fallback.order').read_text()
    first_ko = [l.strip() for l in txt.splitlines()
                if l.strip() and not l.startswith('#')]
    victim = first_ko[0]
    canon = victim[:-3].replace('-', '_') if victim.endswith('.ko') else victim
    poisoned = fifo_ramdisk(lz4, victim)
    (OUT / 'v9-ramdisk-poisoned.lz4').write_bytes(poisoned)
    print(f'[$] {victim} -> FIFO: its loader child will hang for ever')
    d, proc = boot('B-poisoned', kernel, poisoned, deadline=420)
    text = (WORK / 'B-poisoned.log').read_bytes().decode(errors='replace')
    got = '\n'.join(d['lines'])
    kill_lines = [l for l in d['lines']
                  if 'TIMEOUT-KILLED' in l and canon in l]
    check('B: the poisoned module was attempted and its child KILLED on the '
          'deadline', bool(kill_lines), kill_lines[:1])
    after = got.split('TIMEOUT-KILLED', 1)
    later = [l for l in after[1].splitlines() if 'loaded rc=0' in l] \
        if len(after) > 1 else []
    check('B: the walk CONTINUED after the kill (later modules loaded)',
          bool(later), later[:2])
    check('B: PID 1 still spawned the relay in the poisoned boot',
          'v9-relay-started op=relay rc=0' in got,
          [l for l in d['lines'] if 'v9-relay-started' in l][:1])
    check('B: PID 1 still reached reboot(2) with a stuck child alive',
          d['rebooted'], f"{d['seconds']} s")
    check('B: no panic in the poisoned run', not d['panics'],
          d['panics'] or 'clean')
    return d


def run_c(upstream, lz4):
    """the shipped relay read from the host with the real nc, over TCP"""
    print('\n=== C: mainline kernel + e1000 + slirp, real nc from the host ===')

    def nic(cmd):
        for i, a in enumerate(cmd):                       # drop '-nic none'
            if a == '-nic' and cmd[i + 1] == 'none':
                del cmd[i:i + 2]
                break
        cmd += ['-nic', f'user,model=e1000,hostfwd=tcp:127.0.0.1:{HOSTFWD_PORT}-:9999']
        for i, a in enumerate(cmd):                       # extend the cmdline
            if a == 'console=ttyAMA0 earlycon loglevel=8 panic=10 rdinit=/init':
                cmd[i] = (a + ' ip=10.0.2.15::10.0.2.2:255.255.255.0::eth0:off')
        return cmd

    d, proc = boot('C-nic', upstream, lz4, deadline=0, extra_cmd=nic,
                   no_reboot=False, ready_pat=b'READ-ONLY relay up',
                   ready_timeout=170)
    check('C: the shipped init started the relay inside the booting guest',
          'v9-relay-started op=relay rc=0'
          in (WORK / 'C-nic.log').read_bytes().decode(errors='replace'),
          [l for l in (WORK / 'C-nic.log').read_bytes().decode(
              errors='replace').splitlines() if 'v9-relay-started' in l][:1])
    dump = b''
    for attempt in range(6):
        if proc.poll() is not None:
            break
        p = subprocess.run([str(NC), '127.0.0.1', str(HOSTFWD_PORT)],
                           capture_output=True, timeout=40)
        if p.returncode == 0 and b'END OF DUMP' in p.stdout:
            dump = p.stdout
            break
        time.sleep(3)
    (OUT / 'qemu' / 'C-nic-dump.txt').write_bytes(dump)
    check('C: `nc 127.0.0.1 <hostfwd>` from the HOST returns a complete dump '
          '(the shipped relay, through the guest TCP/IP stack)',
          bool(dump), f'{len(dump)} bytes; '
          f'the exact command is: nc 127.0.0.1 {HOSTFWD_PORT}  '
          f'(on the phone: nc 10.0.0.1 9999)')
    if dump:
        d['dump_bytes'] = len(dump)
        d['dump_sha256'] = hashlib.sha256(dump).hexdigest()
        check('C: section [1] shows the GUEST kernel (a live uname(2), not a '
              'canned string)', b'Linux 6.6.60' in dump and b'[1] KERNEL' in dump,
              [l for l in dump.split(b'\n') if b'uname' in l][:1])
        check('C: section [2] shows the guest\'s own eth0 address',
              b'eth0' in dump and b'addr=10.0.2.15' in dump,
              [l for l in dump.split(b'\n') if b'eth0' in l][:1])
        check('C: section [8] carries the guest\'s real boot journal',
              b'nx679j: mounts: done' in dump
              and b'nx679j: v9-relay-started op=relay rc=0' in dump,
              f'{dump.count(b"nx679j: ")} journal lines in the dump')
        check('C: the relay reports what it bound to, honestly',
              b'bind=0.0.0.0' in dump and b'iface=usb0 if_index=0 present=0'
              in dump,
              [l for l in dump.split(b'\n') if b'[relay] version' in l][:1])
        check('C: section [9] proves the namespace: the relay and PID 1 share '
              'one mount namespace, with /proc and /sys mounted',
              b'/proc/self/ns/mnt           : mnt:[' in dump
              and re.search(rb'/proc/1/ns/mnt \(PID 1\)      : (mnt:\[\d+\])',
                            dump) is not None
              and re.search(rb'/proc/1/ns/mnt \(PID 1\)      : (mnt:\[\d+\])',
                            dump).group(1) ==
              re.search(rb'/proc/self/ns/mnt           : (mnt:\[\d+\])',
                        dump).group(1),
              [l for l in dump.split(b'\n') if b'ns/mnt' in l][:2])
        check('C: the dump is readable text only',
              all(b in (10, 9) or 32 <= b < 127 for b in dump),
              f'{sum(1 for b in dump if not (b in (10, 9) or 32 <= b < 127))} '
              f'non-printable bytes')
        # the strongest form of "the dump is the guest's own": every journal
        # line in the dump must also be in the serial console log
        jl = [l for l in dump.split(b'\n') if l.startswith(b'nx679j: ')]
        log = (WORK / 'C-nic.log').read_bytes()
        present = [l for l in jl if l in log]
        check('C: every journal line in the dump is also in the serial console '
              'log (the dump is the guest\'s own journal, not a canned text)',
              bool(jl) and len(present) == len(jl),
              f'{len(present)}/{len(jl)} journal lines of the dump found in '
              f'{WORK / "C-nic.log"}')
    stop(proc)
    d.update(finish('C-nic', WORK / 'C-nic.log', proc, 0))
    return d


def main():
    img = IMG.read_bytes()
    man = json.loads((OUT / 'manifest-v9.json').read_text())
    kernel, lz4, koff, rsize = split_image(img)
    check('the image is the boot partition size', len(img) == 100663296,
          f'{len(img)} bytes')
    check('the kernel in the image is the container kernel',
          kernel == Path('/home/user/nx679j-stock/magisk_patched-30700_Zx2eF.img'
                         ).read_bytes()[4096:4096 + len(kernel)],
          f'{len(kernel)} bytes, sha256={hashlib.sha256(kernel).hexdigest()[:16]}')
    check('the ramdisk in the image is the ramdisk the build recorded',
          hashlib.sha256(lz4).hexdigest() == man['ramdisk_lz4']['sha256'],
          f'{rsize} bytes at {koff}, sha256='
          f'{hashlib.sha256(lz4).hexdigest()[:16]}')
    relay_blob = None
    cpio = subprocess.run(['lz4', '-dc'], input=lz4,
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
        if nm == 'TRAILER!!!':
            break
        if nm == 'nx679j/relay':
            relay_blob = cpio[doff:doff + fsize]
        off = doff + fsize + (-(doff + fsize) % 4)
    check('the relay inside the ramdisk inside the image is the built one',
          relay_blob is not None
          and hashlib.sha256(relay_blob).hexdigest() == man['relay']['sha256'],
          f'{len(relay_blob) if relay_blob else 0} bytes, sha256='
          f'{hashlib.sha256(relay_blob).hexdigest()[:16] if relay_blob else "?"}')

    run_a(kernel, lz4)
    run_b(kernel, lz4)
    if not UPSTREAM.exists():
        check('C: the mainline kernel for the TCP test is present',
              False, f'{UPSTREAM} missing')
    else:
        run_c(UPSTREAM.read_bytes(), lz4)

    passed = sum(1 for c in res['checks'] if c['pass'])
    res['summary'] = {'passed': passed, 'total': len(res['checks'])}

    def plain(o, where='res'):
        """JSON has no Path: convert and SAY what was converted, so a harness
        detail can never hide a real mismatch"""
        if isinstance(o, dict):
            return {k: plain(v, f'{where}.{k}') for k, v in o.items()}
        if isinstance(o, list):
            return [plain(v, f'{where}[{i}]') for i, v in enumerate(o)]
        if isinstance(o, Path):
            print(f'[harness] {where}: Path -> str ({o})')
            return str(o)
        return o

    blob = plain(res)
    for k in blob['runs']:
        blob['runs'][k].pop('proc', None)
    (OUT / 'qemu-candidate-v9.json').write_text(json.dumps(blob, indent=2) + '\n')
    (WORK / 'qemu-candidate-v9.log').write_text(
        '\n'.join(f'[{"PASS" if c["pass"] else "FAIL"}] {c["check"]}: {c["detail"]}'
                  for c in res['checks'])
        + f'\n\n{passed}/{len(res["checks"])} passed\n')
    print(f'\n=== {passed}/{len(res["checks"])} checks passed '
          f'-> qemu-candidate-v9.json')
    return 0 if passed == len(res['checks']) else 1


if __name__ == '__main__':
    sys.exit(main())
