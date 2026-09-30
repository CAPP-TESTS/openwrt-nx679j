#!/bin/sh
set -eu

WORKDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOTFS="$WORKDIR/../../rootfs"
BUILD_DIR=${BUILD_DIR:-/home/user/nx679j-kernel-test.IYMJNo}
KERNEL="$BUILD_DIR/arch/arm64/boot/Image"
DTB="$BUILD_DIR/arch/arm64/boot/dts/qcom/sm8450-nx679j.dtb"
BOOT_STOCK="$WORKDIR/boot-stock"
VENDOR_BOOT_STOCK="$WORKDIR/vendor-boot-stock"
OUTPUT="$WORKDIR/artifacts"
if [ "${SKIP_KERNEL_BUILD:-0}" != 1 ]; then
	BUILD_DIR="$BUILD_DIR" "$WORKDIR/build-headless-kernel.sh"
fi

for path in "$ROOTFS" "$KERNEL" "$DTB" "$BOOT_STOCK/ramdisk" \
	"$VENDOR_BOOT_STOCK/dtb" "$VENDOR_BOOT_STOCK/bootconfig" \
	"$VENDOR_BOOT_STOCK/vendor_ramdisk00"; do
	[ -e "$path" ] || {
		printf 'missing required input: %s\n' "$path" >&2
		exit 1
	}
done

mkdir -p "$OUTPUT"

RAMDISK="$OUTPUT/openwrt-headless-ramdisk.cpio.gz"
BOOT_IMAGE="$OUTPUT/boot-nx679j-headless-openwrt.img"
VENDOR_BOOT_IMAGE="$OUTPUT/vendor_boot-nx679j-headless-openwrt.img"

rm -f "$RAMDISK" "$BOOT_IMAGE" "$VENDOR_BOOT_IMAGE" \
	"$OUTPUT/SHA256SUMS"

(
	cd "$ROOTFS"
	LC_ALL=C find . -print0 | LC_ALL=C sort -z |
		cpio --null --create --format=newc --owner=0:0 --reproducible
) | gzip -9n > "$RAMDISK"

mkbootimg \
	--header_version 4 \
	--os_version 12.0.0 \
	--os_patch_level 2022-02 \
	--kernel "$KERNEL" \
	--ramdisk "$RAMDISK" \
	--cmdline 'console=ttyMSM0,115200n8 console=tty0 loglevel=8 ignore_loglevel initcall_debug fbcon=map:0 panic=10 g_ncm.dev_addr=xx:xx:xx:xx:xx:xx g_ncm.host_addr=xx:xx:xx:xx:xx:xx' \
	--output "$BOOT_IMAGE"

mkbootimg \
	--header_version 4 \
	--pagesize 0x00001000 \
	--base 0x00000000 \
	--kernel_offset 0x00008000 \
	--ramdisk_offset 0x01000000 \
	--tags_offset 0x00000100 \
	--dtb_offset 0x0000000001f00000 \
	--vendor_cmdline 'video=vfb:640x400,bpp=32,memsize=3072000 qcom-dload-mode.download_mode=1 bootconfig' \
	--board '' \
	--dtb "$DTB" \
	--vendor_bootconfig "$VENDOR_BOOT_STOCK/bootconfig" \
	--ramdisk_type 1 \
	--ramdisk_name '' \
	--vendor_ramdisk_fragment "$VENDOR_BOOT_STOCK/vendor_ramdisk00" \
	--vendor_boot "$VENDOR_BOOT_IMAGE"

sha256sum "$BOOT_IMAGE" "$VENDOR_BOOT_IMAGE" "$RAMDISK" > "$OUTPUT/SHA256SUMS"
