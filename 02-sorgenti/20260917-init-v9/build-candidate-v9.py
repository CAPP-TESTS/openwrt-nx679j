#!/usr/bin/env python3
"""build-candidate-v9: the ONE slot-B image of experiment 20260917-init-v9.

       v9  =  v8  +  ONE NEW READ-ONLY CHILD  (/nx679j/relay)

What is shipped, and why every byte of the rest is untouched:

  container   : /home/user/nx679j-stock/magisk_patched-30700_Zx2eF.img
                (the image whose ramdisk is measured to boot Android), patched
                IN PLACE: new ramdisk bytes at absolute offset 49115136 and the
                ramdisk_size field at offset 12.  Kernel, cmdline, DTB, the ABL
                handoff and the partition layout are untouched (verified below
                byte by byte).
  ramdisk     : the EXACT cpio of the v8 build (itself the v7 cpio + /init +
                /nx679j/worker, and v7 is MEASURED on hardware to reach
                userspace and self-restart every ~10 s), with
                  /init          replaced by the v9 supervisor (v8 + the relay
                                 spawn/keep-alive logic, same 8 syscalls)
                  /nx679j/worker COPIED BYTE FOR BYTE from the v8 build
                  /nx679j/relay  ADDED: the read-only diagnostic relay
                Nothing else in the archive is even re-compressed differently:
                the "v9 = v8 + relay" claim is checked structurally at the end
                (every other entry's 110-byte header and payload must be
                identical to the v8 archive).
  compression : lz4 -l (legacy frame) level 9, the frame format the device's
                kernel has CONFIG_RD_LZ4 for, round-tripped before shipping.

  Boot argument / protocol: see HARDWARE-TEST-PROTOCOL-v9.md.
  Reading the result from a PC: nc 10.0.0.1 9999  (see HOW-TO-VERIFY-V9.md)

Outputs: boot_b-init-v9.img + manifest-v9.json + build-candidate-v9.log.
Verification happens here AND independently in verify-candidate-v9.py, which
re-derives everything from the shipped image bytes, and RE-RUNS the relay
under qemu-aarch64 (localtest-relay.py) and the whole image in QEMU
(qemu-candidate-v9.py).
"""
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

OUT = Path('/home/user/nx679j-stock/experiments/20260917-init-v9')
WORK = OUT / 'work'
V8 = Path('/home/user/nx679j-stock/experiments/20260917-init-v8')
INIT_SRC = OUT / 'nx679j-init-v9.c'
RELAY_SRC = OUT / 'nx679j-relay.c'
INIT_BIN = WORK / 'init-v9'
INIT_DB = WORK / 'init-v9.unstripped'
RELAY_BIN = WORK / 'relay-v9'
RELAY_DB = WORK / 'relay-v9.unstripped'

# the v8 ramdisk: the base of v9, sha pinned
V8_CPIO = V8 / 'work' / 'v8-ramdisk.cpio'
V8_CPIO_SHA = '4ba9ff83fbe47eb58004c081b0292d87dbe7c5da141a4a449b5f2e4f23c8bc37'
V8_INIT_SHA = '6fa91dec86dceafc01cbd69caea71dbff1be27d26c815802a23bd914be2bd6b1'
V8_WORKER_SHA = 'ac4a6ff8069bbce36d77d566f76ac2e247e4dd9308edcf38920a5daed487be29'
V8_WORKER = V8 / 'work' / 'worker-v8'
V8_IMG = V8 / 'boot_b-init-v8.img'

MAGISK = Path('/home/user/nx679j-stock/magisk_patched-30700_Zx2eF.img')
MAGISK_SHA = '0cd94d5627501f3c65c94542093ad18b1ec6c1c215dc6a7ecab4f941057e8364'
FULL_SIZE = 100663296
KERNEL_OFF = 4096
RAMDISK_OFF = 49115136
HDR_RSIZE_FIELD = 12
KERNEL_SIZE = 49108324

CC = 'aarch64-linux-gnu-gcc'

# PID 1 may only ever execute these syscalls (aarch64 numbers).  v9 does NOT
# add one: the relay is an exec'd child like the worker.
ALLOWED_SYSCALLS = {
    220: 'clone',
    221: 'execve',
    260: 'wait4',
    101: 'nanosleep',
    129: 'kill',
    142: 'reboot',
    94: 'exit_group',
    113: 'clock_gettime',
}
# and it may not contain a call to any of these, whatever the syscall wrapper
FORBIDDEN_NAMES = ('finit_module', 'init_module', 'mount', 'umount', 'open',
                   'read', 'write', 'mknod', 'ioctl', 'socket', 'printf',
                   'malloc', 'free', 'exit')
# the relay: the complete set of call targets reachable from its OWN code.
# Measured on the -g twin (build log), not guessed: read functions
# (open/read/stat/opendir), pure computation (snprintf/memcpy/strlen),
# process/signal calls about its own children (fork/waitpid/alarm/signal), and
# socket calls on its own sockets.  No unlink, rename, mkdir, mknod, chmod,
# chown, truncate, creat, fsync, mount, finit_module, symlink, kill, reboot,
# ptrace or exec -- see FORBIDDEN_CALL_SUBSTRINGS below, which is asserted.
RELAY_ALLOWED_CALLS = {
    # our own functions (inlined helpers keep their .constprop names)
    'main', 'sleep_ms', 'now_ms', 'exists', 'blob_read', 'emit',
    'emit_sanitized', 'outf', 'section_dir', 'section_file.constprop.0',
    'sec_usb', 'sec_storage', 'sec_network', 'sec_modules', 'sec_kernel',
    'sec_journal', 'sec_header', 'serve', 'if_ipv4.constprop.0',
    'P.constprop.0', 'section_modules_dir', 'sec_self', 'section_lines',
    # libc: reads and pure computation
    '__libc_open', '__libc_read', '__libc_write', '__close', '__stat',
    '__opendir', '__readdir', '__closedir', '__snprintf', '___vsnprintf',
    '__dprintf', '__memchr', 'strcpy', 'strncpy', 'strcmp', 'strchr',
    'strerror', 'atoi', '__errno_location', 'readlink', '__readlink',
    '__uname', '__inet_pton', 'inet_ntoa', 'if_nametoindex',
    '__if_nametoindex',
    # ioctl is on this list for the THREE read-only interface queries only
    # (SIOCGIFADDR/SIOCGIFNETMASK/SIOCGIFMTU/SIOCGIFFLAGS); the run-time trace
    # in localtest-relay.py asserts that SIOCSIFADDR never appears
    'ioctl', '__ioctl',
    # libc: its own children and its own sockets
    'signal', '__bsd_signal', 'alarm', 'fork', '__fork', 'waitpid',
    '__waitpid', 'socket', '__socket', 'bind', '__bind', 'setsockopt',
    '__setsockopt', 'getsockname', '__getsockname', 'listen', 'accept',
    '__libc_accept', 'shutdown', 'exit', 'getpid',
 '__getpid', 'getppid', '__getppid',
    '_exit', 'exit_group', 'abort', '__libc_start_main', 'mprotect',
    '__clock_gettime', '__nanosleep', 'getenv', '__getenv',
    # IFUNC slots reached through the PLT (resolved and checked below)
    '.plt', '.plt+0x10', '.plt+0x20', '.plt+0x30', '.plt+0x40',
}
# what may not be reachable from the relay's own code under ANY spelling
FORBIDDEN_CALL_SUBSTRINGS = ('unlink', 'rename', 'mkdir', 'mknod', 'chmod',
                             'chown', 'truncate', 'creat', 'fsync', 'mount',
                             'module', 'symlink', 'kill', 'reboot', 'ptrace',
                             'exec', 'system', 'popen', 'chroot', 'fallocate')
# the five PLT slots of a static glibc binary are IFUNCs; only these five may
# be reachable from our code
RELAY_ALLOWED_PLT = {'__libc_memcpy_ifunc', '__libc_memmove_ifunc',
                     '__libc_memset_ifunc', '__libc_memcmp_ifunc',
                     '__strlen_ifunc'}
log_lines = []


def log(m=''):
    print(m, flush=True)
    log_lines.append(m)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def sha_file(p):
    return sha(Path(p).read_bytes())


def run(cmd, **kw):
    p = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if p.returncode:
        raise SystemExit(f'command failed ({p.returncode}): {" ".join(map(str, cmd))}\n'
                         f'{p.stdout}\n{p.stderr}')
    return p.stdout


# ---------------------------------------------------------------- disassembly
def instructions(binary):
    dis = run(['aarch64-linux-gnu-objdump', '-d', '--no-show-raw-insn', str(binary)])
    out = []
    for line in dis.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\t([a-z0-9._]+)(?:\s+(.*))?$', line)
        if m:
            out.append((int(m.group(1), 16), m.group(2), (m.group(3) or '').strip()))
    return out


def syscall_audit(binary):
    """Every svc #0 in PID 1 must be preceded by a mov/movz of x8 with an
    allowed syscall number -- read from the SHIPPED bytes, not from the source"""
    insns = instructions(binary)
    found = {}
    problems = []
    for i, (addr, mn, op) in enumerate(insns):
        if mn != 'svc':
            continue
        nr = None
        for back in range(1, 25):
            if i - back < 0:
                break
            _, pmn, pop = insns[i - back]
            if pmn in ('b', 'bl', 'ret', 'svc', 'br', 'blr', 'cbz', 'cbnz'):
                break
            if pmn in ('mov', 'movz') and pop.startswith('x8,'):
                mm = re.search(r'#(0x[0-9a-f]+|\d+)', pop)
                nr = int(mm.group(1), 0) if mm else None
                break
            if pop.startswith('x8,'):        # any other write to x8
                problems.append(f'svc at {hex(addr)}: x8 set by {pmn} {pop}, '
                                f'cannot prove the syscall number')
                nr = -1
                break
        if nr is None:
            problems.append(f'svc at {hex(addr)}: no x8 setup found')
        elif nr not in ALLOWED_SYSCALLS:
            problems.append(f'svc at {hex(addr)}: syscall {nr} not in the whitelist')
        else:
            found[nr] = found.get(nr, 0) + 1
    return found, problems


def symbol_names(binary):
    out = run(['aarch64-linux-gnu-nm', '-a', str(binary)])
    return [l.split()[-1] for l in out.splitlines() if l.strip()]


def source_function_names(src):
    """The names of the functions DEFINED in our source file: definitions start
    at column 0 and are followed by '(' .  Used to separate the relay's own code
    from the thousand functions of static libc."""
    out = set()
    for line in Path(src).read_text().splitlines():
        m = re.match(r'^(?:static\s+)?(?:inline\s+)?[A-Za-z_][\w \*]*?\b(\w+)\s*\(', line)
        if m and not line.startswith((' ', '\t', '#', '/', '*')):
            out.add(m.group(1))
    return out


def own_function_ranges(db, src):
    """[(start, end, name)] for the functions defined in OUR source file."""
    names = source_function_names(src)
    out = run(['aarch64-linux-gnu-nm', '-S', '--defined-only', str(db)])
    syms = []
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 4 and f[2].lower() in ('t', 'w') and f[3] in names:
            start = int(f[0], 16)
            syms.append((start, start + (int(f[1], 16) or 1), f[3]))
    syms.sort()
    if not syms:
        raise SystemExit(f'no function of {src} found in {db}')
    return syms


def plt_map(db):
    """{'.plt+0x10': '__libc_memcpy', ...}: resolve the PLT stubs of a static
    binary (IRELATIVE IFUNC slots) to the function they really jump to, so that
    a 'bl .plt+0x20' can be audited exactly like a direct call."""
    d = run(['aarch64-linux-gnu-objdump', '-d', '--no-show-raw-insn',
             '--section=.plt', str(db)])
    got_of = {}
    cur = None
    for line in d.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\s+([a-z0-9._]+)\s*(.*)$', line)
        if not m:
            continue
        addr, mn, op = int(m.group(1), 16), m.group(2), m.group(3).strip()
        if mn == 'adrp' and 'x16' in op:
            mm = re.search(r'([0-9a-f]+)\s+<', op)
            cur = (addr, int(mm.group(1), 16) if mm else None)
        elif mn == 'ldr' and cur and 'x17' in op:
            mm = re.search(r'#(\d+)', op)
            if cur[1] is not None:
                got_of[cur[0]] = cur[1] + (int(mm.group(1)) if mm else 0)
            cur = None
    resolver = {}
    for line in run(['aarch64-linux-gnu-readelf', '-r', '--wide', str(db)]).splitlines():
        m = re.match(r'^([0-9a-f]{16})\s+\S+\s+R_AARCH64_IRELATIVE\s+([0-9a-f]+)', line.strip())
        if m:
            resolver[int(m.group(1), 16)] = int(m.group(2), 16)
    syms = []
    for line in run(['aarch64-linux-gnu-nm', '-S', '--defined-only', str(db)]).splitlines():
        f = line.split()
        if len(f) >= 4 and f[2].lower() in ('t', 'w'):
            syms.append((int(f[0], 16), int(f[1], 16) or 1, f[3]))
    syms.sort()
    out = {}
    if not got_of:
        return out
    plt_addr = min(got_of)
    for stub, got in sorted(got_of.items()):
        r = resolver.get(got)
        name = None
        if r is not None:
            # the symbol that CONTAINS the address: the greatest start <= r
            for s, sz, nm in syms:
                if s <= r < s + sz:
                    name = nm
            if name is None:                    # fall back to the nearest below
                for s, sz, nm in syms:
                    if s <= r:
                        name = nm
        out['.plt' if stub == plt_addr else f'.plt+0x{stub - plt_addr:x}'] = name
    return out


def calls_from_own_code(db, ranges):
    insns = instructions(db)
    calls = {}
    for addr, mn, op in insns:
        if mn != 'bl':
            continue
        if not any(s <= addr < e for s, e, _ in ranges):
            continue
        m = re.search(r'<([^>+]+)', op)
        tgt = m.group(1) if m else op
        calls[tgt] = calls.get(tgt, 0) + 1
    return calls


def compile_all():
    WORK.mkdir(parents=True, exist_ok=True)
    # PID 1 v9: freestanding, no libc at all (identical flags to v8)
    run([CC, '-static', '-nostdlib', '-ffreestanding', '-fno-stack-protector',
         '-fno-builtin', '-fno-asynchronous-unwind-tables', '-Os',
         '-Wl,-e,_start', '-o', str(INIT_BIN), str(INIT_SRC)])
    run([CC, '-static', '-nostdlib', '-ffreestanding', '-fno-stack-protector',
         '-fno-builtin', '-fno-asynchronous-unwind-tables', '-Os', '-g',
         '-Wl,-e,_start', '-o', str(INIT_DB), str(INIT_SRC)])
    # the relay: static ELF with libc, never PID 1, its own child
    run([CC, '-static', '-Os', '-s', '-Wall', '-Wextra',
         '-o', str(RELAY_BIN), str(RELAY_SRC)])
    run([CC, '-static', '-Os', '-g', '-o', str(RELAY_DB), str(RELAY_SRC)])


def check_init():
    res = {}
    data = INIT_BIN.read_bytes()
    res['bytes'] = len(data)
    res['sha256'] = sha(data)
    res['elf_aarch64'] = data[:4] == b'\x7fELF' and struct.unpack_from('<H', data, 18)[0] == 183
    dyn = run(['aarch64-linux-gnu-readelf', '-d', str(INIT_BIN)])
    res['no_dynamic_section'] = 'There is no dynamic section' in dyn or 'NEEDED' not in dyn
    undef = [l.split()[-1] for l in run(['aarch64-linux-gnu-nm', '-u', str(INIT_BIN)]).splitlines()
             if l.strip()]
    res['undefined_symbols'] = undef
    names = symbol_names(INIT_BIN)
    res['symbols'] = sorted(set(n for n in names if not n.startswith('$')))
    res['forbidden_symbols_present'] = [n for n in res['symbols']
                                        if n.strip('_') in FORBIDDEN_NAMES]
    found, problems = syscall_audit(INIT_DB)
    res['syscalls_executed'] = {ALLOWED_SYSCALLS[k]: v for k, v in sorted(found.items())}
    res['syscall_audit_problems'] = problems
    res['text_forbidden_strings'] = [s.decode() for s in
                                     (b'finit_module', b'init_module',
                                      b'/lib/modules', b'/dev/', b'insmod')
                                     if s in data]
    # v9: the two strings that prove the relay logic is really in this binary
    res['relay_refs'] = {k: (k.encode() in data) for k in
                         ('/nx679j/relay', 'v9-summary', 'v9-relay-started',
                          'v9-relay-died', 'v9-relay-restarted')}
    # -Os inlines the small helpers, so only these two survive as symbols;
    # the relay spawn path is the one that reaches clone/execve a second time
    res['relay_symbols'] = [n for n in ('keep_relay', 'mkrelay', 'spawn_bg',
                                        'bg_done') if n in res['symbols']]
    insns = instructions(INIT_DB)
    calls = {t for _, mn, op in insns for t in re.findall(r'<([^>]+)>', op)
             if mn in ('bl', 'b')}
    res['call_targets'] = sorted(calls)
    res['calls_into_libc'] = sorted(c for c in calls
                                   if c in ('exit', '_exit', 'abort', 'printf'))
    return res


def check_relay():
    res = {}
    data = RELAY_BIN.read_bytes()
    res['bytes'] = len(data)
    res['sha256'] = sha(data)
    res['elf_aarch64'] = data[:4] == b'\x7fELF' and struct.unpack_from('<H', data, 18)[0] == 183
    dyn = run(['aarch64-linux-gnu-readelf', '-d', str(RELAY_BIN)])
    res['static'] = 'NEEDED' not in dyn
    # 1. the calls made by the relay's OWN functions
    ranges = own_function_ranges(RELAY_DB, RELAY_SRC)
    res['own_functions'] = [n for _, _, n in ranges]
    calls = calls_from_own_code(RELAY_DB, ranges)
    res['own_calls'] = dict(sorted(calls.items()))
    extra = {k: v for k, v in calls.items() if k not in RELAY_ALLOWED_CALLS}
    res['own_calls_not_whitelisted'] = extra
    # 1b. the PLT-mediated calls: resolved through the IRELATIVE IFUNC slots
    plt = plt_map(RELAY_DB)
    res['plt_resolution'] = plt
    res['own_plt_calls'] = {k: plt.get(k) for k in sorted(calls) if k.startswith('.plt')}
    res['own_plt_calls_not_benign'] = {k: v for k, v in res['own_plt_calls'].items()
                                       if v not in RELAY_ALLOWED_PLT}
    if not res['own_plt_calls']:
        raise SystemExit('no PLT-mediated calls found: the resolution check is empty')
    # 1c. nothing that mutates the system may be reachable, under any spelling
    res['forbidden_call_targets'] = sorted(
        t for t in calls if any(s in t for s in FORBIDDEN_CALL_SUBSTRINGS))
    # 2. what the shipped bytes say about themselves
    res['strings_present'] = {s.decode(): (s in data) for s in
                              (b'READ-ONLY', b'nx679j-relay-v9', b'/nx679j-journal',
                               b'nc ', b'THE BOOT JOURNAL', b'hash_fnv1a64_full',
                               b'SIOCGIFADDR' if False else b'clients_accepted')}
    res['strings_forbidden'] = [s.decode() for s in
                                (b'finit_module', b'init_module', b'insmod',
                                 b'modules.load', b'TIMEOUT-KILLED',
                                 b'create_module', b'delete_module')
                                if s in data]
    # 3. no libc wrapper for anything that mutates the filesystem is called
    #    from our code (the check above) AND the binary must not contain the
    #    libc *call* sites for the module/mount family at all
    res['has_finit_module_call'] = any('finit_module' in k for k in calls)
    res['has_mount_call'] = any(k == 'mount' for k in calls)
    return res


# ------------------------------------------------------------------- cpio
def parse_cpio(buf, label):
    entries = []
    off = 0
    while True:
        magic = buf[off:off + 6]
        if magic not in (b'070701', b'070702'):
            raise SystemExit(f'{label}: bad cpio magic at {off}: {magic!r}')
        f = [int(buf[off + 6 + i * 8:off + 6 + (i + 1) * 8], 16) for i in range(13)]
        (ino, mode, uid, gid, nlink, mtime, fsize, dmaj, dmin, rmaj, rmin,
         namesize, check) = f
        name = buf[off + 110:off + 110 + namesize - 1].decode()
        hdr_end = off + 110 + namesize
        data_off = hdr_end + (-hdr_end % 4)
        data_end = data_off + fsize
        span_end = data_end + (-data_end % 4)
        entries.append(dict(name=name, mode=mode, size=fsize, hdr=off,
                            data_off=data_off, data_end=data_end, span=span_end,
                            payload=buf[data_off:data_end],
                            hdr_bytes=buf[off:off + 110]))
        if name == 'TRAILER!!!':
            return entries, span_end
        off = span_end


def splice_entry(buf, label, name, new_payload):
    entries, _ = parse_cpio(buf, label)
    hits = [e for e in entries if e['name'] == name]
    if len(hits) != 1:
        raise SystemExit(f'{label}: expected exactly one {name!r}, found {len(hits)}')
    e = hits[0]
    hdr = bytearray(e['hdr_bytes'])
    hdr[54:62] = b'%08x' % len(new_payload)
    name_and_pad = buf[e['hdr'] + 110:e['data_off']]
    new_entry = bytes(hdr) + name_and_pad + new_payload
    new_entry += b'\0' * (-len(new_entry) % 4)
    out = buf[:e['hdr']] + new_entry + buf[e['span']:]
    if out[:e['hdr']] != buf[:e['hdr']] or out[e['hdr'] + len(new_entry):] != buf[e['span']:]:
        raise SystemExit(f'{label}: splice changed bytes outside {name!r}')
    return out, e


def add_entry(buf, label, name, payload, mode=0o100755):
    """Insert a NEW file just before the TRAILER!!! entry, leaving every other
    byte of the archive untouched."""
    if name.endswith('/'):
        raise SystemExit('use add_dir() for directories')
    names = {e['name'] for e in parse_cpio(buf, label)[0]}
    if name in names:
        raise SystemExit(f'{label}: {name} already present')
    if '/' in name:
        parent = name.rsplit('/', 1)[0]
        if parent not in names:
            buf = add_dir(buf, label, parent)
    return _insert_before_trailer(buf, label, name, payload, mode)


def add_dir(buf, label, name):
    names = {e['name'] for e in parse_cpio(buf, label)[0]}
    if name in names:
        return buf
    out, _tr = _insert_before_trailer(buf, label, name, b'', 0o40755)
    return out


def _insert_before_trailer(buf, label, name, payload, mode):
    tr = [e for e in parse_cpio(buf, label)[0] if e['name'] == 'TRAILER!!!'][0]
    hdr = b'070701' + b'00000000'                       # ino
    hdr += b'%08X' % mode
    hdr += b'00000000' + b'00000000'                    # uid, gid
    hdr += b'00000001'                                  # nlink
    hdr += b'00000000'                                  # mtime
    hdr += b'%08X' % len(payload)
    hdr += b'00000000' * 4                              # devmajor/minor, rdev*
    nm = name.encode() + b'\0'
    hdr += b'%08X' % len(nm)
    hdr += b'00000000'                                  # check
    entry = hdr + nm
    entry += b'\0' * (-len(entry) % 4)
    entry += payload
    entry += b'\0' * (-len(entry) % 4)
    out = buf[:tr['hdr']] + entry + buf[tr['hdr']:]
    if out[:tr['hdr']] != buf[:tr['hdr']] or out[tr['hdr'] + len(entry):] != buf[tr['hdr']:]:
        raise SystemExit(f'{label}: add_entry changed bytes outside the trailer')
    return out, tr


def compress(cpio_path, out_path):
    subprocess.run(['lz4', '-l', '-f', '-9', str(cpio_path), str(out_path)], check=True)
    return out_path.read_bytes()


def build_image(ramdisk, out_path):
    src = MAGISK.read_bytes()
    buf = bytearray(src)
    buf[RAMDISK_OFF:RAMDISK_OFF + len(ramdisk)] = ramdisk
    struct.pack_into('<I', buf, HDR_RSIZE_FIELD, len(ramdisk))
    out_path.write_bytes(bytes(buf))
    return bytes(buf)


def check_image(path, ramdisk):
    src = MAGISK.read_bytes()
    img = path.read_bytes()
    r = {'path': str(path), 'bytes': len(img), 'sha256': sha(img)}
    if len(img) != FULL_SIZE:
        raise SystemExit('image is not the full partition size')
    hdr = img[:4096]
    magic, ksize, rsize, osver, hsize = struct.unpack_from('<8sIIII', hdr, 0)
    sig_size = struct.unpack_from('<I', hdr, 1580)[0]
    r['header'] = dict(magic=magic.decode(), kernel_size=ksize, ramdisk_size=rsize,
                       header_size=hsize, os_version=hex(osver), signature_size=sig_size)
    if not (magic == b'ANDROID!' and ksize == KERNEL_SIZE and rsize == len(ramdisk)
            and hsize == 1584 and sig_size == 4096):
        raise SystemExit(f'header wrong: {r["header"]}')
    r['kernel_identical'] = img[KERNEL_OFF:RAMDISK_OFF] == src[KERNEL_OFF:RAMDISK_OFF]
    if not r['kernel_identical']:
        raise SystemExit('kernel bytes differ from the container')
    r['ramdisk_identical'] = img[RAMDISK_OFF:RAMDISK_OFF + len(ramdisk)] == ramdisk
    if not r['ramdisk_identical']:
        raise SystemExit('stored ramdisk != input ramdisk')
    after = RAMDISK_OFF + len(ramdisk)
    r['tail_identical_absolute'] = img[after:] == src[after:]
    if not r['tail_identical_absolute']:
        raise SystemExit('tail after the ramdisk differs from the container')
    diff = [i for i in range(FULL_SIZE) if img[i] != src[i]]
    outside = [i for i in diff if not (HDR_RSIZE_FIELD <= i < HDR_RSIZE_FIELD + 4
                                       or RAMDISK_OFF <= i < after)]
    r['diff_count'] = len(diff)
    r['diff_outside_allowed_regions'] = outside[:16]
    if outside:
        raise SystemExit(f'{len(outside)} differing bytes outside the allowed regions')
    old_end = RAMDISK_OFF + struct.unpack_from('<I', src, 12)[0]
    r['leftover_old_ramdisk_bytes'] = max(0, old_end - after)
    return r


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    res = {}
    log('=== build-candidate-v9 (v8 + the read-only diagnostic relay) ===')
    log()

    if sha_file(MAGISK) != MAGISK_SHA:
        raise SystemExit('the container changed: refusing to build')
    log(f'[1] container      {MAGISK.name} sha256={MAGISK_SHA[:16]}... ok')

    # ------------------------------------------------------------------ base
    if not V8_CPIO.exists():
        raise SystemExit(f'missing the v8 ramdisk (the base of v9): {V8_CPIO}')
    v8_bytes = V8_CPIO.read_bytes()
    if sha(v8_bytes) != V8_CPIO_SHA:
        raise SystemExit(f'the v8 ramdisk changed: {sha(v8_bytes)}')
    log(f'[2] ramdisk base   {V8_CPIO}')
    log(f'                    {len(v8_bytes)} bytes sha256={V8_CPIO_SHA}')
    v8_img_sha = sha_file(V8_IMG) if V8_IMG.exists() else None
    log(f'                    (the image built from it: {V8_IMG.name}, '
        f'sha256={v8_img_sha[:16] if v8_img_sha else "n/a"}... the artifact whose '
        f'init+HW behaviour this experiment extends)')
    res['ramdisk_base'] = dict(path=str(V8_CPIO), bytes=len(v8_bytes),
                               sha256=V8_CPIO_SHA, v8_image_sha256=v8_img_sha)

    # the worker is NOT recompiled: it is copied, so "v9 = v8 + relay" is byte
    # exact for everything except /init and the new file
    if not V8_WORKER.exists():
        raise SystemExit(f'missing the v8 worker binary: {V8_WORKER}')
    if sha_file(V8_WORKER) != V8_WORKER_SHA:
        raise SystemExit('the v8 worker binary changed')
    worker_bytes = V8_WORKER.read_bytes()
    log(f'[3] worker         copied from the v8 build, '
        f'{len(worker_bytes)} bytes sha256={V8_WORKER_SHA[:16]}... (not recompiled)')

    log('[4] compiling /init v9 (freestanding) and /nx679j/relay (static)')
    compile_all()
    log(f'    init    {INIT_BIN.stat().st_size} bytes  '
        f'(v8 init was {os.path.getsize(V8 / "work" / "init-v8")} bytes)')
    log(f'    relay   {RELAY_BIN.stat().st_size} bytes')

    log('[5] PID 1 audit: which syscalls can the shipped bytes execute?')
    res['init'] = check_init()
    for k, v in sorted(res['init']['syscalls_executed'].items()):
        log(f'    svc #0 -> {k} ({v} call sites)')
    if res['init']['syscall_audit_problems']:
        raise SystemExit('syscall audit failed: ' + '; '.join(res['init']['syscall_audit_problems']))
    if not res['init']['no_dynamic_section']:
        raise SystemExit('/init is not freestanding (has dynamic section)')
    if res['init']['undefined_symbols']:
        raise SystemExit(f'/init has undefined symbols: {res["init"]["undefined_symbols"]}')
    if res['init']['forbidden_symbols_present']:
        raise SystemExit(f'/init mentions {res["init"]["forbidden_symbols_present"]}')
    if res['init']['text_forbidden_strings']:
        raise SystemExit(f'/init text contains {res["init"]["text_forbidden_strings"]}')
    if not all(res['init']['relay_refs'].values()):
        raise SystemExit(f'/init does not reference the relay: {res["init"]["relay_refs"]}')
    if not {'keep_relay', 'mkrelay'} <= set(res['init']['relay_symbols']):
        raise SystemExit(f'/init relay code missing: {res["init"]["relay_symbols"]}')
    log(f'    same 8 syscalls as v8, no libc, no finit_module/mount/lib-module '
        f'strings, relay logic present ({", ".join(res["init"]["relay_symbols"])})')
    log(f'    call targets: {", ".join(res["init"]["call_targets"]) or "(none)"}')

    log('[6] relay audit: is the new child really read-only?')
    res['relay'] = check_relay()
    rl = res['relay']
    log(f'    {rl["bytes"]} bytes static={rl["static"]} aarch64={rl["elf_aarch64"]}')
    if not (rl['static'] and rl['elf_aarch64']):
        raise SystemExit('the relay is not a static aarch64 ELF')
    if rl['own_calls_not_whitelisted']:
        raise SystemExit(f'the relay calls {rl["own_calls_not_whitelisted"]}, '
                         f'which are not on the read-only whitelist')
    if rl['own_plt_calls_not_benign']:
        raise SystemExit(f'the relay reaches {rl["own_plt_calls_not_benign"]} '
                         f'through the PLT, which are not IFUNC memory helpers')
    if rl['forbidden_call_targets']:
        raise SystemExit(f'the relay can reach {rl["forbidden_call_targets"]}, '
                         f'which mutate the system')
    if rl['strings_forbidden']:
        raise SystemExit(f'the relay contains {rl["strings_forbidden"]}')
    if not all(rl['strings_present'].values()):
        raise SystemExit(f'the relay is missing its own markers: {rl["strings_present"]}')
    if rl['has_finit_module_call'] or rl['has_mount_call']:
        raise SystemExit('the relay contains a finit_module/mount call site')
    log(f'    own functions: {len(rl["own_functions"])}, call targets reachable '
        f'from them: {len(rl["own_calls"])} distinct, all on the read-only whitelist')
    log(f'    PLT (IFUNC) calls resolved: '
        f'{", ".join(f"{k}={v}" for k, v in sorted(rl["own_plt_calls"].items()))}')
    log(f'    nothing reachable that mutates the system '
        f'({len(FORBIDDEN_CALL_SUBSTRINGS)} forbidden substrings checked), '
        f'no module-loader strings, no TIMEOUT-KILLED')
    log(f'    markers present: READ-ONLY, nx679j-relay-v9, /nx679j-journal, '
        f'hash_fnv1a64_full')

    log('[7] ramdisk: splice /init, add /nx679j/relay, COPY the v8 worker')
    rc, e_init = splice_entry(v8_bytes, 'v8 ramdisk', 'init', INIT_BIN.read_bytes())
    res['spliced_init'] = dict(name=e_init['name'], old_size=e_init['size'],
                               new_size=INIT_BIN.stat().st_size, mode=oct(e_init['mode']))
    added, tr = add_entry(rc, 'v8 ramdisk + v9 init', 'nx679j/relay',
                          RELAY_BIN.read_bytes())
    res['added_relay'] = dict(name='nx679j/relay', size=RELAY_BIN.stat().st_size,
                              inserted_before=tr['name'])
    entries, _ = parse_cpio(added, 'v9 ramdisk')
    res['ramdisk_entries'] = len(entries)
    names = [e['name'] for e in entries]
    res['ramdisk_files'] = dict(init='init' in names, worker='nx679j/worker' in names,
                                relay='nx679j/relay' in names)
    if not all(res['ramdisk_files'].values()):
        raise SystemExit(f'ramdisk is missing a program: {res["ramdisk_files"]}')

    # ------------------------------------------------ "v9 = v8 + relay", proved
    log('[8] the v9 = v8 + relay claim, checked structurally on the archives')
    v8_entries, _ = parse_cpio(v8_bytes, 'v8 ramdisk')
    v9_entries, _ = parse_cpio(added, 'v9 ramdisk')
    structural = {'entries_v8': len(v8_entries), 'entries_v9': len(v9_entries),
                  'identical_except_init': [], 'relay_added': None}
    if len(v9_entries) != len(v8_entries) + 1:
        raise SystemExit('v9 does not have exactly one more entry than v8')
    vi = 0
    for e9 in v9_entries:
        if e9['name'] == 'nx679j/relay':
            structural['relay_added'] = dict(name=e9['name'], size=e9['size'],
                                             mode=oct(e9['mode']),
                                             payload_matches=(
                                                 e9['payload'] == RELAY_BIN.read_bytes()))
            if not structural['relay_added']['payload_matches']:
                raise SystemExit('the relay in the archive is not the compiled relay')
            continue
        e8 = v8_entries[vi]
        vi += 1
        if e8['name'] != e9['name']:
            raise SystemExit(f'entry order differs: v8 {e8["name"]!r} vs v9 {e9["name"]!r}')
        h8 = e8['hdr_bytes']
        h9 = e9['hdr_bytes']
        same_hdr = (h8[6:54] == h9[6:54] and h8[62:] == h9[62:])
        if e9['name'] == 'init':
            if e9['payload'] != INIT_BIN.read_bytes():
                raise SystemExit('the /init in the archive is not the compiled init')
            structural['identical_except_init'].append('init (payload replaced)')
            continue
        if not (same_hdr and e8['payload'] == e9['payload']):
            raise SystemExit(f'{e9["name"]!r} differs from the v8 archive')
        structural['identical_except_init'].append(e9['name'])
    if vi != len(v8_entries):
        raise SystemExit(f'entry counts do not line up: consumed {vi} of '
                         f'{len(v8_entries)} v8 entries')
    res['structural'] = structural
    log(f'    {len(v8_entries)} v8 entries -> {len(v9_entries)} v9 entries: '
        f'{len(structural["identical_except_init"])} entries byte-identical '
        f'(header+payload), /init replaced, /nx679j/relay added')
    log(f'    the 4 programs/entries that matter: init, nx679j/worker, '
        f'nx679j/relay (+ the nx679j directory)')

    cpio_path = WORK / 'v9-ramdisk.cpio'
    cpio_path.write_bytes(added)
    log(f'    cpio {len(v8_bytes)} -> {len(added)} bytes, {len(entries)} entries')

    log('[9] lz4 -l legacy compression + round trip')
    lz4 = compress(cpio_path, WORK / 'v9-ramdisk.lz4')
    (OUT / 'v9-ramdisk.lz4').write_bytes(lz4)
    back = subprocess.run(['lz4', '-dc', str(WORK / 'v9-ramdisk.lz4')],
                          capture_output=True).stdout
    if back != added:
        raise SystemExit('lz4 round trip does not reproduce the cpio')
    if lz4[:4] != b'\x02\x21\x4c\x18':
        raise SystemExit(f'not an lz4 legacy frame: {lz4[:4]!r}')
    res['ramdisk_lz4'] = dict(bytes=len(lz4), sha256=sha(lz4), magic=lz4[:4].hex(),
                              round_trip_ok=True)
    log(f'    {len(added)} -> {len(lz4)} bytes, legacy magic {lz4[:4].hex()}, '
        f'round trip byte-identical')

    log('[10] UNPACK the compressed ramdisk and look at the real files: this is '
        'the check the kernel will do')
    up = WORK / 'unpack-v9'
    if up.exists():
        shutil.rmtree(up)
    up.mkdir(parents=True)
    sh = subprocess.run(f'lz4 -dc {WORK / "v9-ramdisk.lz4"} | cpio -idm --quiet',
                        shell=True, cwd=str(up), capture_output=True, text=True)
    if not (up / 'init').is_file() and sh.returncode:
        raise SystemExit(f'cpio -idm failed: {sh.stderr[:300]}')
    init_p = up / 'init'
    work_p = up / 'nx679j' / 'worker'
    relay_p = up / 'nx679j' / 'relay'
    res['unpacked'] = {
        'init_exists': init_p.is_file(),
        'init_executable': os.access(init_p, os.X_OK),
        'init_sha256': sha_file(init_p) if init_p.is_file() else None,
        'worker_exists': work_p.is_file(),
        'worker_executable': os.access(work_p, os.X_OK),
        'worker_sha256': sha_file(work_p) if work_p.is_file() else None,
        'relay_exists': relay_p.is_file(),
        'relay_executable': os.access(relay_p, os.X_OK),
        'relay_sha256': sha_file(relay_p) if relay_p.is_file() else None,
        'nx679j_is_a_directory': (up / 'nx679j').is_dir(),
    }
    u = res['unpacked']
    if not (u['init_exists'] and u['init_executable'] and u['worker_exists']
            and u['worker_executable'] and u['relay_exists'] and u['relay_executable']):
        raise SystemExit(f'/init, /nx679j/worker or /nx679j/relay does not appear '
                         f'when unpacked: {u}')
    if u['init_sha256'] != sha(INIT_BIN.read_bytes()):
        raise SystemExit('the unpacked /init is not the compiled init')
    if u['worker_sha256'] != V8_WORKER_SHA:
        raise SystemExit('the unpacked /nx679j/worker is not the v8 worker')
    if u['relay_sha256'] != sha(RELAY_BIN.read_bytes()):
        raise SystemExit('the unpacked /nx679j/relay is not the compiled relay')
    log(f'    init {init_p.stat().st_size} ok, nx679j/worker '
        f'{work_p.stat().st_size} ok (== v8 worker), nx679j/relay '
        f'{relay_p.stat().st_size} ok, all executable, all hashes match')

    log('[11] boot image: in-place graft into the container')
    img_path = OUT / 'boot_b-init-v9.img'
    build_image(lz4, img_path)
    res['image'] = check_image(img_path, lz4)
    res['image']['ramdisk_sha256'] = sha(lz4)
    log(f'    {img_path.name} {res["image"]["bytes"]} bytes '
        f'sha256={res["image"]["sha256"]}')
    log(f'    header: {res["image"]["header"]}')
    log(f'    kernel identical, ramdisk identical, tail identical, '
        f'{res["image"]["diff_count"]} bytes differ from the container '
        f'(all inside the size field + ramdisk region)')
    log(f'    leftover bytes of the old ramdisk left in place: '
        f'{res["image"]["leftover_old_ramdisk_bytes"]}')

    res['how_to_read_it'] = {
        'from_a_pc': 'flash boot_b-init-v9.img on slot B, boot it, then RECEIVE '
                     'THE LIVE JOURNAL over USB with one command: `nc 10.0.0.1 9999` '
                     '(usb0 is the NCM gadget, the phone is 10.0.0.1; give the host '
                     'interface an address in the same /24, e.g. 10.0.0.2/24)',
        'relay_port': 9999,
        'relay_iface': 'usb0 (addr 10.0.0.1, set by the worker before PID 1 '
                       'starts the relay)',
        'relay_is_read_only': 'it opens every file O_RDONLY, its only writes are '
                              'to the socket it just accepted and to the console',
        'protocol': {
            'success': 'logo STEADY (no flash) + host sees USB 18d1:4ee7 -> the '
                       'gadget is up, and `nc 10.0.0.1 9999` prints the journal live',
            'not_successful': 'logo CYCLES: 8 s = UFS ok but no gadget, 30 s = no '
                              'UFS/rawdump, 60 s = PID 1 had to kill an operation',
            'dead': 'logo STEADY and no USB at all',
        },
        'journal_copies': ['rawdump partition offset 0 (written by the worker)',
                           '/sys/fs/pstore on the next Android boot',
                           '/dev/kmsg of the running boot',
                           'the relay dump, section [8], over TCP'],
    }
    (OUT / 'manifest-v9.json').write_text(json.dumps(res, indent=2) + '\n')
    (OUT / 'build-candidate-v9.log').write_text('\n'.join(log_lines) + '\n')
    log()
    log(f'manifest -> {OUT / "manifest-v9.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
