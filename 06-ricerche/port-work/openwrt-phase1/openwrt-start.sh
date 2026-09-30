#!/system/bin/sh
set -eu

ROOT=/data/local/openwrt-rootfs
LOG=/data/local/tmp/openwrt-start.log

log() {
	echo "[$(date +%F_%T)] $*" >> "$LOG"
}

set_prop() {
	/system/bin/setprop "$1" "$2"
}

is_mounted() {
	grep -q " $1 " /proc/mounts
}

ensure_dir() {
	[ -d "$1" ] || mkdir -p "$1"
}

mount_fs() {
	TYPE="$1"
	SRC="$2"
	DST="$3"
	ensure_dir "$DST"
	if ! is_mounted "$DST"; then
		mount -t "$TYPE" "$SRC" "$DST"
		log "mounted $TYPE $SRC -> $DST"
	fi
}

run_chroot() {
	chroot "$ROOT" /bin/busybox sh -c "$1"
}

bind_mount() {
	SRC="$1"
	DST="$2"
	ensure_dir "$DST"
	if ! is_mounted "$DST"; then
		mount --bind "$SRC" "$DST"
		log "bind $SRC -> $DST"
	fi
}

: > "$LOG"
log "starting OpenWrt chroot bootstrap"
set_prop openwrt.phase2.module.health startup_enter
set_prop openwrt.phase2.module.openwrt starting
set_prop openwrt.phase2.net.state init
set_prop openwrt.phase2.net.reason none

ensure_dir "$ROOT"
[ -x "$ROOT/bin/sh" ] || {
	log "missing $ROOT/bin/sh"
	set_prop openwrt.phase2.module.openwrt failed_missing_rootfs
	set_prop openwrt.phase2.module.health startup_failed_missing_rootfs
	exit 1
}

mount_fs proc proc "$ROOT/proc"
mount_fs sysfs sysfs "$ROOT/sys"
bind_mount /dev "$ROOT/dev"
mount_fs devpts devpts "$ROOT/dev/pts"
mount_fs tmpfs tmpfs "$ROOT/tmp"
mount_fs tmpfs tmpfs "$ROOT/run"

ensure_dir "$ROOT/var/run"
ensure_dir "$ROOT/var/lock"
ensure_dir "$ROOT/var/run/ubus"

cat > "$ROOT/tmp/resolv.conf" <<'EOF'
nameserver 1.1.1.1
nameserver 8.8.8.8
EOF
run_chroot '/bin/busybox date > /tmp/chroot_date; /bin/busybox cat /etc/openwrt_release > /tmp/openwrt_release'
[ -s "$ROOT/tmp/openwrt_release" ] || {
	log "chroot sanity failed: missing /tmp/openwrt_release"
	set_prop openwrt.phase2.module.openwrt failed_chroot_sanity
	set_prop openwrt.phase2.module.health startup_failed_chroot_sanity
	exit 1
}
log "chroot sanity passed"
set_prop openwrt.phase2.module.openwrt chroot_ready
set_prop openwrt.phase2.module.health chroot_ready

run_chroot 'export LD_LIBRARY_PATH=/lib:/usr/lib; /bin/busybox mkdir -p /var/run/ubus; /sbin/ubusd -s /var/run/ubus/ubus.sock >/tmp/ubusd.log 2>&1 &'
run_chroot 'export LD_LIBRARY_PATH=/lib:/usr/lib; /sbin/logd -S 64 >/tmp/logd.log 2>&1 &'
run_chroot 'export LD_LIBRARY_PATH=/lib:/usr/lib; /sbin/rpcd -s /var/run/ubus/ubus.sock >/tmp/rpcd.log 2>&1 &'
run_chroot 'export LD_LIBRARY_PATH=/lib:/usr/lib; /usr/sbin/dropbear -R -E -p 127.0.0.1:2222 >/tmp/dropbear.log 2>&1 &' || true
if ! run_chroot 'for n in 1 2 3 4 5; do [ -S /var/run/ubus/ubus.sock ] && exit 0; /bin/busybox sleep 1; done; exit 1'; then
	log "ubus socket wait timed out"
fi
HOTSPOT_IF=$(ip -4 -brief addr | awk '/^wlan[0-9]+/ && $3 ~ /^192[.]168[.]/ {print $1; exit}')
HOTSPOT_IP=
if [ -n "$HOTSPOT_IF" ]; then
	HOTSPOT_CIDR=$(ip -4 -brief addr show dev "$HOTSPOT_IF" | awk 'NR==1{print $3}')
	HOTSPOT_IP=${HOTSPOT_CIDR%/*}
fi
if [ -n "$HOTSPOT_IP" ] && run_chroot '[ -S /var/run/ubus/ubus.sock ]'; then
	run_chroot 'export LD_LIBRARY_PATH=/lib:/usr/lib; /bin/busybox killall uhttpd >/dev/null 2>&1 || true'
	run_chroot "export LD_LIBRARY_PATH=/lib:/usr/lib; /usr/sbin/uhttpd -f -R -h /www -r OpenWrt -x /cgi-bin -u /ubus -U /var/run/ubus/ubus.sock -t 60 -T 30 -k 20 -A 1 -n 3 -N 100 -p $HOTSPOT_IP:80 >/tmp/uhttpd.log 2>&1 &"
	log "uhttpd listening on $HOTSPOT_IP:80 ($HOTSPOT_IF)"
else
	log "hotspot LAN or ubus socket not ready; LuCI skipped"
fi
if [ -x /data/local/openwrt-router-up.sh ]; then
	if /data/local/openwrt-router-up.sh >> "$LOG" 2>&1; then
		log "router-up success"
		set_prop openwrt.phase2.module.health router_up_ok
	else
		ROUTER_RC=$?
		log "router-up failed (non-fatal) rc=$ROUTER_RC"
		set_prop openwrt.phase2.module.health "router_up_fail_rc_$ROUTER_RC"
	fi
	log "router-up invoked"
else
	log "router-up script missing; skipped"
	set_prop openwrt.phase2.net.state missing_router_script
	set_prop openwrt.phase2.net.reason missing_router_script
fi

log "done"
set_prop openwrt.phase2.module.openwrt done
set_prop openwrt.phase2.module.health done
