#!/usr/bin/env python3
"""Audit the WORKER binary: which syscall numbers does it execute?
Same basic-block method as the PID-1 audit in build-candidate-v8.py."""
import re
import subprocess
import sys
from pathlib import Path

TARGET = Path(sys.argv[1])
NR = {334: 'finit_module', 105: 'init_module', 40: 'mount', 39: 'umount2',
      160: 'uname', 101: 'nanosleep', 220: 'clone', 221: 'execve', 260: 'wait4',
      129: 'kill', 142: 'reboot', 56: 'openat', 63: 'read', 64: 'write',
      34: 'mknodat', 29: 'ioctl', 198: 'socket', 94: 'exit_group',
      113: 'clock_gettime', 172: 'getpid', 79: 'newfstatat', 80: 'fstat',
      78: 'readlinkat', 276: 'renameat2', 35: 'unlinkat', 38: 'renameat',
      57: 'close', 62: 'lseek', 66: 'writev', 98: 'futex', 226: 'mprotect',
      222: 'mmap', 214: 'brk', 175: 'gettid', 96: 'set_tid_address',
      99: 'set_robust_list', 261: 'prlimit64', 278: 'getrandom',
      166: 'umask', 49: 'bind', 203: 'connect', 206: 'sendto', 211: 'recvfrom',
      41: 'dup', 25: 'fcntl', 67: 'pread64', 68: 'pwrite64', 227: 'msync',
      233: 'madvise', 215: 'munmap', 117: 'ptrace'}

dis = subprocess.run(['aarch64-linux-gnu-objdump', '-d', '--no-show-raw-insn',
                      str(TARGET)], capture_output=True, text=True).stdout
ins = []
for line in dis.splitlines():
    m = re.match(r'^\s*([0-9a-f]+):\t([a-z0-9._]+)(?:\s+(.*))?$', line)
    if m:
        ins.append((int(m.group(1), 16), m.group(2), (m.group(3) or '').strip()))

found = {}
unresolved = []
for i, (addr, mn, op) in enumerate(ins):
    if mn != 'svc':
        continue
    nr = None
    for back in range(1, 25):
        if i - back < 0:
            break
        _, pmn, pop = ins[i - back]
        if pmn in ('b', 'bl', 'ret', 'svc', 'br', 'blr', 'cbz', 'cbnz'):
            break
        if pmn in ('mov', 'movz') and (pop.startswith('x8,') or pop.startswith('w8,')):
            mm = re.search(r'#(0x[0-9a-f]+|\d+)', pop)
            nr = int(mm.group(1), 0) if mm else None
            break
    if nr is None:
        unresolved.append(hex(addr))
    else:
        found[nr] = found.get(nr, 0) + 1

print(f'{TARGET.name}: {len(ins)} instructions, {sum(found.values())} svc sites')
for k in sorted(found):
    print(f'  {k:4d} {NR.get(k, "?"):>16s}  x{found[k]}')
if unresolved:
    print(f'  unresolved svc sites: {unresolved}')
