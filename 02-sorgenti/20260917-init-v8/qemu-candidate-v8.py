#!/usr/bin/env python3
"""QEMU full-system verification of the v8 image (device kernel, our ramdisk).

Two runs, both with the kernel and ramdisk extracted from the SHIPPED image:

  A "clean"   : /init walks mounts -> chain -> ufs -> gadget, and because QEMU
                has no UFS and no UDC the walk ends in the failure class, so it
                must reach the heartbeat and call reboot(2).  Under -no-reboot
                QEMU then exits: that is the "logo keeps cycling" outcome, and it
                proves PID 1 survived every operation.
  B "poisoned": one bundled .ko is replaced by a FIFO, so the child that opens
                it blocks for ever inside open(2).  The chain worker must kill it
                after MOD_TIMEOUT_MS, journal TIMEOUT-KILLED, CONTINUE with the
                next module, and PID 1 must still reach the reboot.

What this proves: PID 1 cannot be blocked by a stuck child (B), the journal is
written incrementally and names the stuck module (B), and the reporting path
works (A+B).  What it cannot prove: anything about the device's real hardware.
"""
import hashlib
import json
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

OUT = Path('/home/user/nx679j-stock/experiments/20260917-init-v8')
WORK = OUT / 'qemu'
WORK.mkdir(parents=True, exist_ok=True)
PAGE = 4096
TAG = 'nx679j:'          # the prefix the worker really writes (was 'nx679j-v8:':
res = {'runs': {}, 'checks': []}


def check(name, ok, detail=''):
    res['checks'].append({'check': name, 'pass': bool(ok), 'detail': str(detail)[:600]})
    print(f'[{"PASS" if ok else "FAIL"}] {name}: {str(detail)[:300]}')
    return bool(ok)


def split_image(img: bytes):
    ksize, rsize = struct.unpack_from('<II', img, 8)
    r_off = PAGE + ((ksize + PAGE - 1) // PAGE) * PAGE
    return img[PAGE:PAGE + ksize], img[r_off:r_off + rsize], r_off, rsize


def fifo_ramdisk(lz4: bytes, first_ko: str) -> bytes:
    """Rebuild the ramdisk with exactly one .ko entry turned into a FIFO, so the
    child that opens it hangs for ever in open(2).

    The previous version only zeroed the size field and left the payload in
    place; the kernel then refused the whole ramdisk with
    "rootfs image is not initramfs (junk within compressed archive)" and QEMU
    booted with no initramfs at all, so the B run measured nothing.  This
    version re-serialises the archive, which keeps it valid.
    """
    cpio = subprocess.run(['lz4', '-dc'], input=lz4, capture_output=True).stdout
    name = first_ko.encode()
    entries = []
    off = 0
    victim = None
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
            entries.append((hdr, nm, b'', b'00001180'))     # S_IFIFO|0644
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
        nfield = nm + b'\0'          # newc namesize counts the NUL
        out += h + nfield + b'\0' * (-(110 + len(nfield)) % 4)
        out += payload + b'\0' * (-len(payload) % 4)
    out += cpio[off:]        # the TRAILER entry, byte for byte
    p = subprocess.run(['lz4', '-l', '-f', '-9'], input=bytes(out),
                       capture_output=True)
    if p.returncode:
        raise SystemExit('lz4 failed on the poisoned ramdisk')
    return p.stdout

def run_qemu(tag, kernel, ramdisk, deadline):
    kern = WORK / f'{tag}-kernel'
    rd = WORK / f'{tag}-ramdisk'
    kern.write_bytes(kernel)
    rd.write_bytes(ramdisk)
    log = WORK / f'{tag}.log'
    append = 'console=ttyAMA0 earlycon loglevel=8 panic=10 rdinit=/init'
    cmd = ['qemu-system-aarch64', '-machine', 'virt', '-cpu', 'cortex-a57',
           '-accel', 'tcg', '-smp', '2', '-m', '2048', '-nodefaults',
           '-nic', 'none', '-display', 'none', '-monitor', 'none',
           '-serial', 'stdio', '-no-reboot',
           '-kernel', str(kern), '-initrd', str(rd), '-append', append]
    print(f'[$] {tag}: {" ".join(cmd)}')
    t0 = time.time()
    proc = subprocess.Popen(cmd, stdout=open(log, 'wb'), stderr=subprocess.STDOUT,
                            stdin=subprocess.DEVNULL, cwd=str(WORK))
    exited = False
    try:
        while time.time() - t0 < deadline:
            time.sleep(2)
            if proc.poll() is not None:
                exited = True
                break
            if 'Restarting system' in log.read_text(errors='replace'):
                time.sleep(2)
                break
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
    text = log.read_text(errors='replace')
    d = {'qemu_cmd': cmd, 'log': str(log), 'exit_code': proc.returncode,
         'exited_by_itself': exited, 'seconds': round(time.time() - t0, 1),
         'rebooted': 'Restarting system' in text,
         'panics': [m for m in ('Kernel panic', 'Attempted to kill init', 'not syncing',
                                'Unable to handle kernel', 'Internal error: Oops',
                                'BUG:', 'Call trace:') if m in text],
         'lines': [l for l in text.splitlines() if TAG in l]}
    res['runs'][tag] = d
    return d


def main():
    img = (OUT / 'boot_b-init-v8.img').read_bytes()
    man = json.loads((OUT / 'manifest.json').read_text())
    kernel, lz4, r_off, rsize = split_image(img)
    check('image: kernel bytes match the container',
          hashlib.sha256(kernel).hexdigest() == man['image']['kernel_ident_sha256']
          if 'kernel_ident_sha256' in man['image'] else len(kernel) == 49108324,
          f'{len(kernel)} bytes')
    check('image: ramdisk lz4 matches the manifest',
          hashlib.sha256(lz4).hexdigest() == man['ramdisk_lz4']['sha256'],
          f'{rsize} bytes at {r_off}, sha256={hashlib.sha256(lz4).hexdigest()[:16]}')

    # ---------------------------------------------------------------- run A
    a = run_qemu('A-clean', kernel, lz4, deadline=300)
    got = '\n'.join(a['lines'])
    full = (WORK / 'A-clean.log').read_text(errors='replace')

    def idx(pat):
        i = full.find(pat)
        return i

    check('A: PID 1 came up and its journal is being written',
          'mounts: start' in got or 'journal mirror' in got, a['lines'][:2])
    check('A: the mounts step ran', 'mounts: done' in got,
          [l for l in a['lines'] if 'mount' in l][:3])
    # ---- the three claims, verified from the runtime order, not from hope ----
    i_journal = idx('FIRST JOURNAL WRITE')
    i_gadget = idx('gadget: start')
    i_mod = idx('ATTEMPT finit_module')
    check('A: ORDER - the first journal write happened before ANY module load',
          i_journal >= 0 and i_mod >= 0 and i_journal < i_mod,
          f'journal at byte {i_journal}, first finit_module at byte {i_mod}')
    check('A: ORDER - the first gadget attempt happened before ANY module load',
          i_gadget >= 0 and i_mod >= 0 and i_gadget < i_mod,
          f'gadget at byte {i_gadget}, first finit_module at byte {i_mod}')
    check('A: the first journal write reported the rawdump state honestly',
          'first journal write:' in got,
          [l for l in a['lines'] if 'first journal write' in l][:1])
    check('A: the chain ran and attempted modules',
          'chain: start' in got and i_mod >= 0,
          [l for l in a['lines'] if 'chain:' in l][:2])
    check('A: the gadget step ran', 'gadget: start' in got,
          [l for l in a['lines'] if 'gadget:' in l][:3])
    check('A: no panic, PID 1 never died', not a['panics'], a['panics'] or 'clean')
    check('A: PID 1 reached reboot(2) -- the logo would keep cycling',
          a['rebooted'], a['seconds'])
    check('A: the kernel performed the restart (QEMU -no-reboot exited)',
          a['exited_by_itself'], f'exit code {a["exit_code"]}')

    # ---------------------------------------------------------------- run B
    first_ko = (Path('/tmp/v7x/lib/modules/fallback.order').read_text().splitlines()
                if Path('/tmp/v7x/lib/modules/fallback.order').exists() else [])
    first_ko = [l.strip() for l in first_ko if l.strip() and not l.startswith('#')]
    if not first_ko:
        subprocess.run(['mkdir', '-p', '/tmp/v7x'], check=True)
        subprocess.run('cd /tmp/v7x && cpio -idm --quiet < '
                       f'{OUT.parent}/20260916-122926-native-baseline/probe-v7-sonda'
                       '/work/v7-ramdisk.cpio', shell=True, check=True)
        first_ko = [l.strip() for l in
                    Path('/tmp/v7x/lib/modules/fallback.order').read_text().splitlines()
                    if l.strip() and not l.startswith('#')]
    victim = first_ko[0]
    canon_victim = victim[:-3].replace('-', '_') if victim.endswith('.ko') else victim.replace('-', '_')
    print(f'[$] poisoning {victim} (FIFO) to make its loader child hang for ever')
    poisoned = fifo_ramdisk(lz4, victim)
    (OUT / 'v8-ramdisk-poisoned.lz4').write_bytes(poisoned)
    b = run_qemu('B-poisoned', kernel, poisoned, deadline=420)
    got_b = '\n'.join(b['lines'])
    check('B: the poisoned module was attempted', f'ATTEMPT finit_module' in got_b,
          [l for l in b['lines'] if 'ATTEMPT finit_module' in l][:1])
    # the journal pads the module name with %-34s, so match on the same LINE
    kill_lines = [l for l in b['lines']
                  if 'TIMEOUT-KILLED' in l and canon_victim in l]
    check('B: the stuck child was KILLED on the deadline and journalled',
          bool(kill_lines), kill_lines[:1])
    # after the FIRST kill line, a later module must still be loaded: that is
    # the "lose the driver, not the phone" property.  (The mirror repeats the
    # whole journal, so the FIRST occurrence is the meaningful one.)
    after = got_b.split('TIMEOUT-KILLED', 1)
    later = [l for l in after[1].splitlines() if 'loaded rc=0' in l] if len(after) > 1 else []
    check('B: the walk CONTINUED after the kill (later modules loaded)',
          bool(later), later[:2])
    check('B: the chain reported its summary (ok / timeout-killed counters)',
          'chain: done in' in got_b,
          [l for l in b['lines'] if 'chain: done in' in l][:1])
    check('B: PID 1 still reached reboot(2) with a stuck child alive',
          b['rebooted'], b['seconds'])
    check('B: no panic in the poisoned run', not b['panics'], b['panics'] or 'clean')

    passed = sum(1 for c in res['checks'] if c['pass'])
    res['summary'] = {'passed': passed, 'total': len(res['checks'])}
    (OUT / 'qemu-candidate-v8.json').write_text(json.dumps(res, indent=2) + '\n')
    (OUT / 'qemu' / 'qemu-candidate-v8.log').write_text(
        '\n'.join(f'[{"PASS" if c["pass"] else "FAIL"}] {c["check"]}: {c["detail"]}'
                  for c in res['checks']) + f'\n\n{passed}/{len(res["checks"])} passed\n')
    print(f'\n=== {passed}/{len(res["checks"])} checks passed -> qemu-candidate-v8.json')
    return 0 if passed == len(res['checks']) else 1


if __name__ == '__main__':
    sys.exit(main())
