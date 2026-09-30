#!/usr/bin/env python3
"""Extract qcril's QMI requests to the modem from a qmi-trace log into a
playback file: lines of '<group> <service> <hex>' (group = original fd)."""
import re, sys

portmap = {}
for l in open('android-portmap.txt'):
    m = re.search(r'service=(\d+) version=(\d+).*port=(\d+)', l)
    if m:
        portmap[int(m.group(3))] = int(m.group(1))

out = []
seen = set()
for l in open(sys.argv[1], errors='replace'):
    m = re.match(r'^T(\d+) enter sendto\(0x([0-9a-f]+), 0x[0-9a-f]+, 0x([0-9a-f]+), 0x[0-9a-f]+, 0x[0-9a-f]+, 0x[0-9a-f]+\) bytes=([0-9]+): ((?:[0-9a-f]{2} ?)*?)(?: *addr: bytes=12: ((?:[0-9a-f]{2} ?)*))?$', l.rstrip())
    if not m:
        continue
    tid, fd, ln, hexs, addr = m.group(1), m.group(2), int(m.group(3), 16), m.group(5).replace(' ', ''), (m.group(6) or '').replace(' ', '')
    if len(addr) != 24:
        continue
    b = bytes.fromhex(addr)
    node = int.from_bytes(b[4:8], 'little')
    port = int.from_bytes(b[8:12], 'little')
    if node != 0 or port not in portmap:
        continue
    if len(hexs) < 14 or hexs[0:2] != '00':
        continue  # only requests, skip control/other
    if hexs in seen:
        continue
    seen.add(hexs)
    out.append((fd, portmap[port], hexs))

with open('qcril-playback.txt', 'w') as f:
    for fd, svc, hexs in out:
        f.write(f"{fd} {svc} {hexs}\n")
print(f"{len(out)} messages -> qcril-playback.txt")
