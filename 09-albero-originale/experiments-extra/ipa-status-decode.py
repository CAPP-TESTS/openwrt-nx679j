#!/usr/bin/env python3
"""Decode only observed entry records; no hardware access.

Stock ipam full parser: .text+0xc30b8, LAN return+0x58e18,
WAN return+0x5a818. Live DT enum22 selects the audited V5 32-byte layout.
Unknown callers and opcode values remain explicit, not guessed.
"""
import argparse
import json
from pathlib import Path
import re

FIELDS = ("pointer", "caller", "raw0", "raw1", "raw2", "raw3")
PATTERN = re.compile(r"\b(" + "|".join(FIELDS) + r")=(0x[0-9a-fA-F]+)\b")
CALLERS = {0x58E18: "LAN", 0x5A818: "WAN"}


def decode(line, text_base):
    if "parse:" not in line:
        return None
    pairs = PATTERN.findall(line)
    if len(pairs) != len(FIELDS) or {k for k, _ in pairs} != set(FIELDS):
        raise ValueError("incomplete or duplicate raw32 fields")
    values = {key: int(value, 16) for key, value in pairs}
    raw = b"".join(values[f"raw{i}"].to_bytes(8, "little") for i in range(4))
    offset = values["caller"] - text_base
    timestamp = re.search(r"(\d+\.\d+):\s+parse:", line)
    return {
        "timestamp": timestamp.group(1) if timestamp else None,
        "path": CALLERS.get(offset, "UNKNOWN"),
        "caller_offset": hex(offset),
        "pointer": hex(values["pointer"]),
        "raw32": raw.hex(),
        "opcode": raw[0],
        "mask": int.from_bytes(raw[2:4], "little"),
        "packet_length": int.from_bytes(raw[4:6], "little"),
        "source": raw[6],
        "destination": raw[30],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--text-base", required=True, type=lambda x: int(x, 0))
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()
    records = []
    for number, line in enumerate(args.trace.read_text().splitlines(), 1):
        try:
            record = decode(line, args.text_base)
        except (ValueError, OverflowError) as exc:
            raise SystemExit(f"{args.trace}:{number}: {exc}") from exc
        if record is not None:
            record["line"] = number
            records.append(record)
            print(json.dumps(record, sort_keys=True))
    print(json.dumps({
        "summary": {
            "records": len(records),
            "paths": {name: sum(r["path"] == name for r in records)
                      for name in ("LAN", "WAN", "UNKNOWN")},
            "opcode_zero": sum(r["opcode"] == 0 for r in records),
        }
    }, sort_keys=True))


if __name__ == "__main__":
    main()
