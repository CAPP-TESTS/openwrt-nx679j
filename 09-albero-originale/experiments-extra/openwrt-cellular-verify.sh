#!/bin/sh
# Consume only the current WDS client's returned IPv4 settings.
set -eu
iface=rmnet_data0
active_pid() {
  test -r "/proc/$1/status" || return 1
  test "$(awk '/^State:/ {print $2}' "/proc/$1/status")" != Z
}
valid_ipv4() {
  awk -v text="$1" 'BEGIN {
    if (split(text,a,".") != 4) exit 1;
    for (i=1;i<=4;i++) if (a[i] !~ /^[0-9]+$/ || a[i]+0>255) exit 1;
  }'
}
test "$(cat /proc/sys/kernel/random/boot_id)" = "$(cat /tmp/observed-bootstrap.boot-id)"
grep -q '^DATA_BEARER_READY ' /tmp/data-observed.log
for f in /tmp/dpm-session.pid /tmp/wds-session.pid /tmp/crashlog-dump.pid; do
  read -r pid < "$f"
  active_pid "$pid"
done
addr=$(awk '/^  IPv4 addr: / {print $3}' /tmp/wds-session.log)
gateway=$(awk '/^  IPv4 gateway: / {print $3}' /tmp/wds-session.log)
mask=$(awk '/^  IPv4 netmask: / {print $3}' /tmp/wds-session.log)
mtu=$(awk '/^  MTU: / {print $2}' /tmp/wds-session.log)
valid_ipv4 "$addr"
valid_ipv4 "$gateway"
valid_ipv4 "$mask"
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
ip -4 route show default > /tmp/cellular-default-before.log
test ! -s /tmp/cellular-default-before.log
ip -4 addr show dev "$iface" > /tmp/cellular-address-before.log
if grep -q 'inet ' /tmp/cellular-address-before.log; then exit 1; fi
mkdir /tmp/cellular-verify.once
printf 'CONFIGURE uptime=%s interface=%s address=%s/%s gateway=%s mtu=%s\n' \
  "$(cat /proc/uptime)" "$iface" "$addr" "$prefix" "$gateway" "$mtu"
ip link set "$iface" mtu "$mtu" up
ip -4 addr add "$addr/$prefix" dev "$iface"
ip -4 route add default via "$gateway" dev "$iface"
ip -4 addr show dev "$iface"
ip -4 route show
ip -4 route get 8.8.8.8 > /tmp/cellular-route-get.log
cat /tmp/cellular-route-get.log
grep -Fq "via $gateway dev $iface" /tmp/cellular-route-get.log
grep -Fq "src $addr" /tmp/cellular-route-get.log
for counter in rx_packets tx_packets rx_bytes tx_bytes; do
  printf 'BEFORE %s=%s\n' "$counter" "$(cat "/sys/class/net/$iface/statistics/$counter")"
done
result=0
ping -c 3 -W 2 8.8.8.8 > /tmp/cellular-ping.log 2>&1 || result=$?
cat /tmp/cellular-ping.log
for counter in rx_packets tx_packets rx_bytes tx_bytes; do
  printf 'AFTER %s=%s\n' "$counter" "$(cat "/sys/class/net/$iface/statistics/$counter")"
done
test "$result" = 0
grep -Eq '^3 packets transmitted, 3( packets)? received, 0% packet loss' /tmp/cellular-ping.log
printf 'CELLULAR_CRITERIA_PASS uptime=%s\n' "$(cat /proc/uptime)"
