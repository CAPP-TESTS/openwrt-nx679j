#!/bin/sh
# One operation per invocation; durable before/after records survive a panic.
find_mss_state() {
  for name in /sys/class/remoteproc/remoteproc*/name; do
    if [ -r "$name" ] && [ "$(cat "$name")" = 4080000.remoteproc-mss ]; then
      printf '%s/state\n' "${name%/name}"
      return 0
    fi
  done
  return 1
}
set -eu
stage=${1:?inspect|mtu|address|route|route-get|stats|ping required}
iface=rmnet_data0
case "$stage" in
  inspect) index=0; previous= ;;
  mtu) index=2; previous=inspect ;;
  address) index=4; previous=mtu ;;
  route) index=6; previous=address ;;
  route-get) index=8; previous=route ;;
  stats) index=10; previous=route-get ;;
  ping) index=12; previous=stats ;;
  *) exit 2 ;;
esac
test "$(cat /proc/sys/kernel/random/boot_id)" = "$(cat /tmp/observed-bootstrap.boot-id)"
R=$(find_mss_state)
test "$(cat "$R")" = running
grep -q '^DATA_BEARER_READY ' /tmp/data-observed.log
for f in /tmp/dpm-session.pid /tmp/wds-session.pid /tmp/crashlog-dump.pid; do
  read -r pid < "$f"
  test -r "/proc/$pid/status"
  test "$(awk '/^State:/ {print $2}' "/proc/$pid/status")" != Z
done
test -z "$previous" || test -f "/tmp/cellular-$previous.done"
mkdir "/tmp/cellular-$stage.once"
printf 'STAGE=%s BEGIN uptime=%s\n' "$stage" "$(cat /proc/uptime)"
/bin/sh /tmp/rawdump-checkpoint.sh "$index" "before_$stage" /tmp/cellular-staged.log
addr=$(awk '/^  IPv4 addr: / {print $3}' /tmp/wds-session.log)
gateway=$(awk '/^  IPv4 gateway: / {print $3}' /tmp/wds-session.log)
mask=$(awk '/^  IPv4 netmask: / {print $3}' /tmp/wds-session.log)
mtu=$(awk '/^  MTU: / {print $2}' /tmp/wds-session.log)
for text in "$addr" "$gateway" "$mask"; do
  awk -v text="$text" 'BEGIN {
    if (split(text,a,".") != 4) exit 1;
    for (i=1;i<=4;i++) if (a[i] !~ /^[0-9]+$/ || a[i]+0>255) exit 1;
  }'
done
prefix=$(printf '%s\n' "$mask" | awk -F. '{
  bits=0; zero=0;
  for (i=1;i<=4;i++) for (j=7;j>=0;j--) {
    bit=int($i/(2^j))%2;
    if (bit) { if (zero) exit 1; bits++; } else zero=1;
  }
  if (bits<1) exit 1;
  print bits;
}')
case "$mtu" in ''|*[!0-9]*) exit 1;; esac
test "$mtu" -ge 68
test "$mtu" -le 65535
case "$stage" in
  inspect)
    ip -4 route show default > /tmp/cellular-default-before.log
    test ! -s /tmp/cellular-default-before.log
    ip -4 addr show dev "$iface" > /tmp/cellular-address-before.log
    if grep -q 'inet ' /tmp/cellular-address-before.log; then exit 1; fi
    cat /tmp/cellular-address-before.log
    ;;
  mtu) ip link set "$iface" mtu "$mtu" up ;;
  address) ip -4 addr add "$addr/$prefix" dev "$iface" ;;
  route) ip -4 route add default via "$gateway" dev "$iface" ;;
  route-get)
    ip -4 addr show dev "$iface"
    ip -4 route show
    ip -4 route get 8.8.8.8 > /tmp/cellular-route-get.log
    cat /tmp/cellular-route-get.log
    grep -Fq "via $gateway dev $iface" /tmp/cellular-route-get.log
    grep -Fq "src $addr" /tmp/cellular-route-get.log
    ;;
  stats)
    for counter in rx_packets tx_packets rx_bytes tx_bytes; do
      value=$(cat "/sys/class/net/$iface/statistics/$counter")
      case "$value" in ''|*[!0-9]*) exit 1;; esac
      printf 'BEFORE %s=%s\n' "$counter" "$value"
    done
    ;;
  ping)
    printf 'PING_EXEC uptime=%s\n' "$(cat /proc/uptime)"
    /bin/sh /tmp/rawdump-checkpoint.sh 14 before_ping_exec /tmp/cellular-staged.log
    result=0
    ping -c 3 -W 2 8.8.8.8 > /tmp/cellular-ping.log 2>&1 || result=$?
    cat /tmp/cellular-ping.log
    printf 'PING_EXIT=%s\n' "$result"
    ;;
esac
printf 'STAGE=%s RETURNED uptime=%s\n' "$stage" "$(cat /proc/uptime)"
/bin/sh /tmp/rawdump-checkpoint.sh "$((index + 1))" "after_$stage" /tmp/cellular-staged.log
if [ "$stage" = ping ]; then
  test "$result" = 0
  grep -Eq '^3 packets transmitted, 3( packets)? received, 0% packet loss' /tmp/cellular-ping.log
  printf 'CELLULAR_CRITERIA_PASS uptime=%s\n' "$(cat /proc/uptime)"
fi
: > "/tmp/cellular-$stage.done"
