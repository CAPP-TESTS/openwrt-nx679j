#!/bin/sh
# Resume the proven data chain after the separately archived ingress test.
# Run exactly one stage per invocation; archive checkpoints 18/19 before next.
set -eu
stage=${1:?egress|wda|mux|wds required}
case "$stage" in
  egress) previous= ;;
  wda) previous=egress ;;
  mux) previous=wda ;;
  wds) previous=mux ;;
  *) exit 2 ;;
esac
uptime_s() { awk '{print int($1)}' /proc/uptime; }
stamp() { printf '%s uptime=%s\n' "$1" "$(cat /proc/uptime)"; }
active_pid() {
  case "$1" in ''|*[!0-9]*) return 1;; esac
  test -r "/proc/$1/status" || return 1
  test "$(awk '/^State:/ {print $2}' "/proc/$1/status")" != Z
}
run_worker() {
  logfile=$1
  shift
  "$@" > "$logfile" 2>&1 &
  worker=$!
  printf '%s\n' "$worker" > /tmp/data-worker.pid
  wait "$worker"
  cat "$logfile"
}
finish() {
  result=$?
  trap - EXIT
  set +e
  printf 'DATA_STAGE=%s EXIT=%s uptime=%s\n' "$stage" "$result" "$(cat /proc/uptime)"
  dmesg > "/tmp/data-$stage-dmesg.log"
  /bin/sh /tmp/rawdump-checkpoint.sh 19 "after_$stage" /tmp/data-observed.log
  checkpoint=$?
  if [ "$result" = 0 ]; then result=$checkpoint; fi
  if [ "$result" = 0 ]; then
    : > "/tmp/data-$stage.done"
    stamp "DATA_STAGE_COMPLETE=$stage"
  fi
  exit "$result"
}
test "$(cat /proc/sys/kernel/random/boot_id)" = "$(cat /tmp/observed-bootstrap.boot-id)"
test "$(cat /sys/class/remoteproc/remoteproc3/state)" = running
test -f /tmp/ipa-ingress.done
test -z "$previous" || test -f "/tmp/data-$previous.done"
for file in /tmp/dpm-session.pid /tmp/crashlog-dump.pid; do
  read -r pid < "$file"
  active_pid "$pid"
done
test -s /tmp/crashlog-slot-number
test ! -e /tmp/wds-session.pid
mkdir "/tmp/data-$stage.once"
printf '%s\n' "$$" > /tmp/data-stage.pid
stamp "DATA_STAGE_BEGIN=$stage"
/bin/sh /tmp/rawdump-checkpoint.sh 18 "before_$stage" /tmp/data-observed.log
trap finish EXIT
case "$stage" in
  egress)
    run_worker /tmp/ipa-egress.log /tmp/rmnet-config-staged rmnet_ipa0 egress
    grep -qx 'EGRESS accepted' /tmp/ipa-egress.log
    ;;
  wda)
    run_worker /tmp/wda-set.log /tmp/qmi-qrtr-observed wda-qmap 4 1
    ;;
  mux)
    test ! -d /sys/class/net/rmnet_data0
    # PARTE STANDARD (verificata 2026-09-20): iproute2 ip-full al posto del tool custom
    run_worker /tmp/mux-create.log /sbin/ip link add link rmnet_ipa0 name rmnet_data0 type rmnet mux_id 1
    /tmp/rmnet-inspect > /tmp/data-links-created.log
    parent=$(cat /sys/class/net/rmnet_ipa0/ifindex)
    grep -E "^ifindex=[0-9]+ name=rmnet_data0 parent=$parent flags=0x[0-9a-f]+ kind=rmnet mux_id=1$" /tmp/data-links-created.log
    run_worker /tmp/mux-notify.log /tmp/rmnet-config-staged rmnet_ipa0 notify-mux 1 rmnet_data0
    ip link set rmnet_ipa0 up
    ip link set rmnet_data0 up
    ;;
  wds)
    /tmp/qmi-qrtr-observed wds-session internet.it 4 1 1 3600 > /tmp/wds-session.log 2>&1 &
    wds=$!
    printf '%s\n' "$wds" > /tmp/wds-session.pid
    deadline=$(( $(uptime_s) + 140 ))
    while ! grep -q '^\[session\] HOLDING ' /tmp/wds-session.log; do
      active_pid "$wds"
      test "$(uptime_s)" -lt "$deadline"
      sleep 1
    done
    cat /tmp/wds-session.log
    read -r dpm < /tmp/dpm-session.pid
    active_pid "$dpm"
    active_pid "$wds"
    stamp DATA_BEARER_READY
    ;;
esac
