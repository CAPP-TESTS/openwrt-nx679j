#!/bin/sh
# Offline compilation and regular-file tests only.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CROSS=${CROSS_CC:-aarch64-linux-gnu-gcc}
HOST=${HOST_CC:-cc}
for name in kprobe-write-fixture ipa-trace-capture; do
  "$CROSS" -static -Os -Wall -Wextra -Werror \
    "$ROOT/qrtr/$name.c" -o "$ROOT/qrtr/$name"
  "$HOST" -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    "$ROOT/qrtr/$name.c" -o "$ROOT/qrtr/test-$name"
done
sh -n "$ROOT/ipa-trace-smoke.sh"
sh -n "$ROOT/ipa-status-trace.sh"
python3 "$ROOT/test-ipa-trace-capture.py" "$ROOT/qrtr/test-ipa-trace-capture"
python3 "$ROOT/test-ipa-status-decode.py"
sha256sum "$ROOT/qrtr/ipa-trace-capture" "$ROOT/ipa-status-trace.sh"
