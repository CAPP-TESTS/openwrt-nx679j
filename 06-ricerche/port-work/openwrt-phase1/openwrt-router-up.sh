#!/system/bin/sh
set -u

LOG=/data/local/tmp/openwrt-router-up.log
IP=/system/bin/ip
IPT=/system/bin/iptables
WAIT_SECS=${OWRT_WAN_WAIT_SECS:-90}
WAIT_STEP=3
LOCKDIR=/data/local/tmp/openwrt-router-up.lock

log() {
	echo "[$(date +%F_%T)] $*" >> "$LOG"
}

set_net_prop() {
	/system/bin/setprop "openwrt.phase2.net.$1" "$2"
}

fail() {
	REASON="$1"
	log "fail: $REASON"
	set_net_prop state failed
	set_net_prop reason "$REASON"
	exit 1
}

cleanup_lock() {
	rmdir "$LOCKDIR" 2>/dev/null || true
}

ipt_retry() {
	TRY=0
	while [ "$TRY" -lt 5 ]; do
		"$IPT" "$@" && return 0
		TRY=$((TRY + 1))
		sleep 1
	done
	return 1
}

pick_wan_if_raw() {
	CAND=$($IP -4 -o addr show up scope global 2>/dev/null | awk '$2 ~ /^rmnet_data[0-9]+(@[^ ]+)?$/ {print $2; exit}')
	[ -n "$CAND" ] && {
		echo "$CAND"
		return 0
	}

	CAND=$($IP -4 route show table all 2>/dev/null | awk '/^default/ {for (i = 1; i <= NF; i++) if ($i == "dev") {d = $(i+1); if (d ~ /^rmnet_data[0-9]+(@[^ ]+)?$/) {print d; exit}}}')
	[ -n "$CAND" ] && {
		echo "$CAND"
		return 0
	}

	CAND=$($IP -4 route show default 2>/dev/null | awk '/^default/ {for (i = 1; i <= NF; i++) if ($i == "dev") {print $(i+1); exit}}')
	[ -n "$CAND" ] && {
		echo "$CAND"
		return 0
	}

	return 1
}

ensure_chain() {
	TABLE="$1"
	CHAIN="$2"
	ipt_retry -t "$TABLE" -N "$CHAIN" >/dev/null 2>&1 || true
	ipt_retry -t "$TABLE" -F "$CHAIN" || fail "iptables_flush_${TABLE}_${CHAIN}"
}

ensure_jump() {
	TABLE="$1"
	PARENT="$2"
	TARGET="$3"
	if ! ipt_retry -t "$TABLE" -C "$PARENT" -j "$TARGET" >/dev/null 2>&1; then
		ipt_retry -t "$TABLE" -I "$PARENT" 1 -j "$TARGET" || fail "iptables_jump_${TABLE}_${PARENT}_${TARGET}"
	fi
}

ensure_rule() {
	TABLE="$1"
	shift
	if ! ipt_retry -t "$TABLE" -C "$@" >/dev/null 2>&1; then
		ipt_retry -t "$TABLE" -A "$@" || fail "iptables_rule_${TABLE}"
	fi
}

reset_pref_rules() {
	PREF="$1"
	while $IP rule del pref "$PREF" >/dev/null 2>&1; do
		:
	done
}

echo "[$(date +%F_%T)] router-up start" > "$LOG"
if ! mkdir "$LOCKDIR" 2>/dev/null; then
	log "lock busy: another router-up run in progress; skipping"
	exit 0
fi
trap cleanup_lock EXIT INT TERM
set_net_prop state starting
set_net_prop reason none

ELAPSED=0
WAN_IF_RAW=""
while [ "$ELAPSED" -le "$WAIT_SECS" ]; do
	WAN_IF_RAW="$(pick_wan_if_raw || true)"
	[ -n "$WAN_IF_RAW" ] && break
	log "waiting for WAN interface (${ELAPSED}/${WAIT_SECS}s)"
	set_net_prop state waiting_wan
	set_net_prop reason "no_wan_yet_${ELAPSED}s"
	sleep "$WAIT_STEP"
	ELAPSED=$((ELAPSED + WAIT_STEP))
done

[ -n "$WAN_IF_RAW" ] || fail "no_wan_after_${WAIT_SECS}s"

WAN_IF=${WAN_IF_RAW%%@*}
WAN_TABLE=main
if [ -n "$($IP -4 route show table "$WAN_IF" 2>/dev/null)" ]; then
	WAN_TABLE="$WAN_IF"
fi

set_net_prop wan "$WAN_IF"
set_net_prop table "$WAN_TABLE"
log "wan_if=$WAN_IF wan_table=$WAN_TABLE"

echo 1 > /proc/sys/net/ipv4/ip_forward 2>/dev/null || fail "ip_forward_write_failed"
echo 0 > /proc/sys/net/ipv4/conf/all/rp_filter 2>/dev/null || true
echo 0 > "/proc/sys/net/ipv4/conf/$WAN_IF/rp_filter" 2>/dev/null || true

[ -x "$IPT" ] || fail "iptables_missing"

ensure_chain nat OWRT_HYBRID_NAT
ensure_chain filter OWRT_HYBRID_FWD
ensure_jump nat POSTROUTING OWRT_HYBRID_NAT
ensure_jump filter FORWARD OWRT_HYBRID_FWD

ensure_rule nat OWRT_HYBRID_NAT -o "$WAN_IF" -j MASQUERADE
ensure_rule filter OWRT_HYBRID_FWD -o "$WAN_IF" -s 10.0.0.0/8 -j ACCEPT
ensure_rule filter OWRT_HYBRID_FWD -o "$WAN_IF" -s 172.16.0.0/12 -j ACCEPT
ensure_rule filter OWRT_HYBRID_FWD -o "$WAN_IF" -s 192.168.0.0/16 -j ACCEPT
ensure_rule filter OWRT_HYBRID_FWD -i "$WAN_IF" -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT

reset_pref_rules 17500
reset_pref_rules 17501
reset_pref_rules 17502

$IP rule add pref 17500 from 10.0.0.0/8 lookup "$WAN_TABLE" >/dev/null 2>&1 || true
$IP rule add pref 17501 from 172.16.0.0/12 lookup "$WAN_TABLE" >/dev/null 2>&1 || true
$IP rule add pref 17502 from 192.168.0.0/16 lookup "$WAN_TABLE" >/dev/null 2>&1 || true

$IP rule show | grep -q "^17500:.*from 10\\.0\\.0\\.0/8 lookup $WAN_TABLE" || fail ip_rule_add_17500
$IP rule show | grep -q "^17501:.*from 172\\.16\\.0\\.0/12 lookup $WAN_TABLE" || fail ip_rule_add_17501
$IP rule show | grep -q "^17502:.*from 192\\.168\\.0\\.0/16 lookup $WAN_TABLE" || fail ip_rule_add_17502

set_net_prop state applied
set_net_prop reason ok
log "router-up done"
