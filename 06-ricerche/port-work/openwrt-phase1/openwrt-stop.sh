#!/system/bin/sh
set -eu

ROOT=/data/local/openwrt-rootfs
for name in uhttpd dropbear rpcd ubusd logd; do
	chroot "$ROOT" /bin/busybox sh -c "/bin/busybox killall $name >/dev/null 2>&1 || true"
done

for p in "$ROOT/dev/pts" "$ROOT/dev" "$ROOT/proc" "$ROOT/sys" "$ROOT/tmp" "$ROOT/run"; do
	if grep -q " $p " /proc/mounts; then
		umount -l "$p" || true
	fi
done

exit 0
