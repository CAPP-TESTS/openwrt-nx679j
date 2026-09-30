#!/usr/bin/env python3
"""Offline tests only: regular scratch files, never a block device or modem."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

binary = Path(sys.argv[1]).resolve()
root = Path(tempfile.mkdtemp(prefix="ipa-trace-capture-test-"))
slot_size = 32768
offset = (32 + 380 + 15) * slot_size
end = offset + slot_size
history_size = slot_size - 1024


def setup(name, payload=b""):
    folder = root / name
    folder.mkdir()
    out = folder / "out"
    out.mkdir()
    raw = folder / "raw"
    with raw.open("wb") as f:
        f.seek(offset - 32)
        f.write(b"B" * 32)
        f.write(b"X" * slot_size)
        f.write(b"A" * 32)
    source = folder / "input"
    source.write_bytes(payload)
    return source, raw, out


def command(source, raw, out, seconds="2", test=True):
    args = [str(binary), str(source), str(raw), str(out), seconds, "fixture"]
    return args + (["--test-regular"] if test else [])


def check_guards(raw):
    with raw.open("rb") as f:
        f.seek(offset - 32)
        assert f.read(32) == b"B" * 32
        f.seek(end)
        assert f.read(32) == b"A" * 32
    assert raw.stat().st_size == end + 32


payload = b"".join(f"synthetic record {i:08d}\n".encode() for i in range(7000))
source, raw, out = setup("rolling", payload)
r = subprocess.run(command(source, raw, out), capture_output=True, timeout=10)
assert r.returncode == 0, r.stderr
assert (out / "trace").read_bytes() == payload
assert r.stdout.startswith(payload)
assert (out / "ready").is_file()
with raw.open("rb") as f:
    f.seek(offset)
    block = f.read(slot_size).rstrip(b"\0")
header, retained = block.split(b"\n", 1)
assert b"slot=395 checkpoint=15" in header
assert f"total={len(payload)} ".encode() in header
assert retained == payload[-history_size:]
check_guards(raw)

for name, duration, test, expected in [
    ("short-duration", "0", True, 2),
    ("long-duration", "181", True, 2),
    ("bad-duration", "no", True, 2),
    ("reject-regular", "1", False, 1),
]:
    source, raw, out = setup(name, b"not captured\n")
    before = raw.read_bytes()
    r = subprocess.run(command(source, raw, out, duration, test),
                       capture_output=True, timeout=3)
    assert r.returncode == expected, (name, r.returncode, r.stderr)
    assert raw.read_bytes() == before

source, raw, out = setup("exclusive", b"must not overwrite\n")
(out / "trace").write_bytes(b"preserve")
before = raw.read_bytes()
r = subprocess.run(command(source, raw, out), capture_output=True, timeout=3)
assert r.returncode == 1 and (out / "trace").read_bytes() == b"preserve"
assert raw.read_bytes() == before

for name in ("stop-token", "deadline", "signal"):
    source, raw, out = setup(name)
    fifo = source.with_name("fifo")
    os.mkfifo(fifo)
    fd = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
    start = time.monotonic()
    p = subprocess.Popen(command(fifo, raw, out, "1"),
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        while not (out / "ready").exists():
            assert p.poll() is None, p.communicate()
            assert time.monotonic() - start < 3
            time.sleep(0.01)
        os.write(fd, b"bounded synthetic line\n")
        while (out / "trace").stat().st_size == 0:
            assert time.monotonic() - start < 3
            time.sleep(0.01)
        if name == "stop-token":
            (out / "stop").touch()
        elif name == "signal":
            p.send_signal(signal.SIGTERM)
        stdout, stderr = p.communicate(timeout=3)
        assert p.returncode == 0, stderr
        assert (out / "trace").read_bytes() == b"bounded synthetic line\n"
        if name == "deadline":
            assert time.monotonic() - start >= 0.9
        check_guards(raw)
    finally:
        os.close(fd)
        if p.poll() is None:
            p.kill()
            p.wait()

r = subprocess.run([str(binary)], capture_output=True, timeout=3)
assert r.returncode == 2, r.stderr
print("PASS rolling history, exact slot boundaries, guards, exclusive files, "
      "stop token, signal and deadline; artifacts:", root)
