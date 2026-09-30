#!/usr/bin/env python3
import base64, struct
raw = base64.b64decode(open("/home/user/nx679j-stock/experiments/20260917-boot-chain/live/gpt_sde_all.txt").read().strip())
print("byte letti:", len(raw), "-> entry da 128 B:", len(raw) // 128)
def dec(a):
    prio = (a >> 48) % 4
    succ = (a >> 50) % 2
    tries = (a >> 51) % 8
    unb = (a >> 54) % 2
    b55 = (a >> 55) % 2
    b60 = (a >> 60) % 2
    b0 = a % 2
    return "prio=%d succ=%d tries=%d b54=%d b55=%d b60=%d b0=%d" % (prio, succ, tries, unb, b55, b60, b0)
for i in range(len(raw) // 128):
    e = raw[i * 128:(i + 1) * 128]
    if e[:16] == b"\x00" * 16:
        continue
    name = e[56:128].decode("utf-16-le", "ignore").split("\x00")[0]
    first, last, attr = struct.unpack("<QQQ", e[32:56])
    print("%3d %-18s lb=%-8d last=%-8d byte=%-11d attr=0x%016x  %s" % (
        i + 1, name, first, last, (last - first + 1) * 4096, attr, dec(attr)))
