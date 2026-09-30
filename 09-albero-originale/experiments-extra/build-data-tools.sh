#!/bin/sh
# Build and offline validation only: no SSH, modem, or network configuration.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
UAPI=${1:-/home/user/nx679j-kernel/kernel_platform/msm-kernel/include/uapi}
CROSS=${CROSS_CC:-aarch64-linux-gnu-gcc}
HOST=${HOST_CC:-cc}
test -r "$UAPI/linux/msm_rmnet.h"
"$CROSS" -static -Os -Wall -Wextra -Werror \
  "$ROOT/qrtr/qmi-qrtr.c" -o "$ROOT/qrtr/qmi-qrtr-dpm"
"$CROSS" -static -Os -Wall -Wextra -Werror -idirafter "$UAPI" \
  "$ROOT/qrtr/rmnet-config.c" -o "$ROOT/qrtr/rmnet-config"
"$CROSS" -static -Os -Wall -Wextra -Werror \
  "$ROOT/qrtr/rmnet-link.c" -o "$ROOT/qrtr/rmnet-link"
"$HOST" -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  "$ROOT/qrtr/test-qmi-qrtr.c" -o "$ROOT/qrtr/test-qmi-qrtr"
"$HOST" -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -idirafter "$UAPI" "$ROOT/qrtr/test-rmnet-config.c" \
  -o "$ROOT/qrtr/test-rmnet-config"
"$HOST" -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  "$ROOT/qrtr/test-rmnet-link.c" -o "$ROOT/qrtr/test-rmnet-link"
"$ROOT/qrtr/test-qmi-qrtr"
"$ROOT/qrtr/test-rmnet-config"
"$ROOT/qrtr/test-rmnet-link"
for script in dpm-init.sh pipe-init.sh wda-init.sh mux-init.sh crashlog-dump.sh; do
  sh -n "$ROOT/$script"
done
sha256sum "$ROOT/qrtr/qmi-qrtr-dpm" "$ROOT/qrtr/rmnet-config" "$ROOT/qrtr/rmnet-link"
