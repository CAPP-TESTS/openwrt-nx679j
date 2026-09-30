#!/usr/bin/env python3
"""Parse the qmi-trace output: extract QRTR sendto/recvfrom with payloads."""
import re, sys, collections

REQ = re.compile(r'^(\S+) enter (sendto|recvfrom)\(0x([0-9a-f]+), 0x([0-9a-f]+), 0x([0-9a-f]+), 0x[0-9a-f]+, 0x[0-9a-f]+, 0x[0-9a-f]+\)( bytes=([0-9]+): ((?:[0-9a-f]{2} ?)*))?( *addr: bytes=12: ((?:[0-9a-f]{2} ?)*))?')
EXIT = re.compile(r'^(\S+) exit  (sendto|recvfrom) -> (-?[0-9]+)')
DUMPLINE = re.compile(r'^\s+([0-9a-f ]+)\s*$')

# the dumps can wrap: "bytes=N: xx xx ... \n   yy yy ..."  (continuation lines indented)
def parse(path):
    events = []
    for line in open(path, errors='replace'):
        line = line.rstrip('\n')
        if ' enter sendto(' in line or ' enter recvfrom(' in line:
            head, _, rest = line.partition(')')
            try:
                parts = head.split('(')[1].split(',')
                fd = int(parts[0].strip(), 16)
                ln = int(parts[2].strip(), 16)
            except Exception:
                continue
            pay = ''
            addr = ''
            if 'bytes=' in rest:
                seg = rest.split('bytes=', 1)[1]
                if ' addr: ' in seg:
                    payseg, addrseg = seg.split(' addr: ', 1)
                else:
                    payseg, addrseg = seg, ''
                if ':' in payseg:
                    pay = payseg.split(':', 1)[1].replace(' ', '')
                if 'bytes=12:' in addrseg:
                    addr = addrseg.split('bytes=12:', 1)[1].replace(' ', '')
            ev = {'fd': fd, 'call': 'sendto' if 'sendto' in head else 'recvfrom',
                  'hex': pay, 'addr': addr}
            if len(addr) == 24:
                b = bytes.fromhex(addr)
                ev['node'] = int.from_bytes(b[4:8], 'little')
                ev['port'] = int.from_bytes(b[8:12], 'little')
            events.append(ev)
    return events

def qmi(hexs):
    if len(hexs) < 14:
        return None
    b = bytes.fromhex(hexs)
    typ = b[0]
    txn = int.from_bytes(b[1:3], 'little')
    msgid = int.from_bytes(b[3:5], 'little')
    ln = int.from_bytes(b[5:7], 'little')
    return typ, txn, msgid, ln, len(b)

def main(path):
    evs = parse(path)
    print(f"# {len(evs)} events from {path}")
    pairs = collections.Counter()
    for e in evs:
        if e['call'] == 'sendto' and e.get('port') is not None:
            q = qmi(e['hex'])
            if q:
                pairs[(e['node'], e['port'], q[2])] += 1
    print("## sendto (node,port,msgid) counts:")
    for (n,p,mid), c in sorted(pairs.items(), key=lambda x: -x[1]):
        print(f"   node={n} port={p} msgid=0x{mid:04x} x{c}")
    print()
    print("## full conversation (sendto with payload + qmi decode; responds length):")
    n = 0
    for e in evs:
        if e['call'] != 'sendto':
            continue
        q = qmi(e['hex'])
        if not q:
            continue
        typ, txn, msgid, ln, tot = q
        n += 1
        if n > 400:
            break
        node = e.get('node'); port = e.get('port')
        print(f"[{n:4}] fd={e['fd']:#x} -> node={node} port={port} type={typ} txn={txn} msgid=0x{msgid:04x} len={ln} total={tot} hex={(e['hex'] if tot>14 else '')}")

if __name__ == '__main__':
    main(sys.argv[1])
