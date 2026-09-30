#!/usr/bin/env python3
"""Serie temporale degli attributi GPT dei boot slot: tre snapshot reali."""
import struct, os
BASE = "/home/user/nx679j-stock/experiments/20260916-122926-native-baseline"
snap = [
    ("16/09 12:45 runtime-v2/sde-primary-entries.bin", BASE + "/runtime-v2/sde-primary-entries.bin", 0),
    ("17/09 12:13 probe-v7-devstate/gpt-sde-now.bin", BASE + "/probe-v7-devstate/gpt-sde-now.bin", 0x2000),
    ("17/09 ~20:00 live/gpt_sde_all.txt (base64)", "/home/user/nx679j-stock/experiments/20260917-boot-chain/live/gpt_sde_all.txt", None),
]
import base64
def load(path, off):
    if path.endswith(".txt"):
        return base64.b64decode(open(path).read().strip()), 0
    return open(path, "rb").read(), off
for label, path, off in snap:
    data, base = load(path, off)
    print("==", label, "(%d B dal file)" % len(data))
    tbl = data[base:base + 96 * 128] if not path.endswith(".txt") else data
    for i in range(len(tbl) // 128):
        e = tbl[i * 128:(i + 1) * 128]
        if e[:16] == b"\x00" * 16:
            continue
        name = e[56:128].decode("utf-16-le", "ignore").split("\x00")[0]
        if name not in ("boot_a", "boot_b", "vbmeta_a", "vbmeta_b"):
            continue
        a = struct.unpack("<Q", e[48:56])[0]
        print("   %-9s attr=0x%016x prio=%d succ=%d tries=%d b54=%d b55=%d b60=%d" % (
            name, a, (a >> 48) % 4, (a >> 50) % 2, (a >> 51) % 8, (a >> 54) % 2, (a >> 55) % 2, (a >> 60) % 2))
