#!/bin/sh
set -eu

WORKDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
KERNEL_SRC=${KERNEL_SRC:-/home/user/linux-6.12}
BUILD_DIR=${BUILD_DIR:-/home/user/nx679j-kernel-test.IYMJNo}
CROSS_COMPILE=${CROSS_COMPILE:-aarch64-linux-gnu-}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
FRAGMENT="$WORKDIR/nx679j-headless.config"

for path in "$KERNEL_SRC/Makefile" "$FRAGMENT"; do
	[ -e "$path" ] || {
		printf 'missing required input: %s\n' "$path" >&2
		exit 1
	}
done

mkdir -p "$BUILD_DIR"

make -C "$KERNEL_SRC" O="$BUILD_DIR" ARCH=arm64 \
	CROSS_COMPILE="$CROSS_COMPILE" defconfig

"$KERNEL_SRC/scripts/kconfig/merge_config.sh" -m -O "$BUILD_DIR" \
	"$BUILD_DIR/.config" "$FRAGMENT"

make -C "$KERNEL_SRC" O="$BUILD_DIR" ARCH=arm64 \
	CROSS_COMPILE="$CROSS_COMPILE" olddefconfig

make -C "$KERNEL_SRC" O="$BUILD_DIR" ARCH=arm64 \
	CROSS_COMPILE="$CROSS_COMPILE" -j"$JOBS" \
	Image qcom/sm8450-nx679j.dtb
