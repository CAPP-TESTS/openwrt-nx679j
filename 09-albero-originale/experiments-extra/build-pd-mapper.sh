#!/bin/sh
# Upstream pd-mapper 5ecd2fe926aca7abfe40724177f63b942cff3947.
# Stock NX679J maps are plain .jsn; their read-only mount is outside the
# OpenWrt root and reachable through PID 1's root.
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
P="$D/refs/pd-mapper"
Q="$D/refs/qrtr-master"
aarch64-linux-gnu-gcc -static -Os -Wall -Wextra \
	-D_GNU_SOURCE -DNO_LZMA \
	'-DFIRMWARE_BASE="/proc/1/root/lib/firmware/"' \
	-I"$Q/include" \
	"$P/pd-mapper.c" "$P/assoc.c" "$P/json.c" "$P/servreg_loc.c" \
	"$Q/lib/qrtr.c" "$Q/lib/qmi.c" "$Q/lib/logging.c" \
	-o "$D/refs/pd-mapper-static"
