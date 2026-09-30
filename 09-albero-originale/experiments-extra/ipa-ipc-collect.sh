#!/bin/sh
# One reader per shared IPC cursor. These log files read RAM, not MMIO.
set -eu
test "$(cat /proc/sys/kernel/random/boot_id)" = "$(cat /tmp/observed-bootstrap.boot-id)"
phase=${1:-ingress}
case "$phase" in
  ingress) base=/tmp/ipa-ipc-collector; finished=/tmp/ipa-ingress.finished ;;
  network|ping)
    base=/tmp/ipa-$phase-collector
    finished=/tmp/cellular-ping.done
    if [ "$phase" = network ]; then
      predecessor=/tmp/ipa-ipc-collector
    else
      predecessor=/tmp/ipa-network-collector
    fi
    grep -q '^IPC_COLLECTOR_FINISHED ' "$predecessor.log"
    read -r previous < "$predecessor.pid"
    case "$previous" in ''|*[!0-9]*) exit 1;; esac
    if [ -r "/proc/$previous/status" ]; then
      test "$(awk '/^State:/ {print $2}' "/proc/$previous/status")" = Z
    fi
    ;;
  *) exit 2 ;;
esac
mkdir "$base.once"
# IPC cursors are shared: never overlap ingress and network collectors.
mkdir /tmp/ipa-ipc-reader.lock
trap 'rmdir /tmp/ipa-ipc-reader.lock' EXIT
printf '%s\n' "$$" > "$base.pid"
for name in ipa ipa_low gsi; do
  test -r "/sys/kernel/debug/ipc_logging/$name/log"
done
deadline=$(( $(awk '{print int($1)}' /proc/uptime) + 180 ))
: > "$base.ready"
while [ "$(awk '{print int($1)}' /proc/uptime)" -lt "$deadline" ]; do
  stamp=$(cat /proc/uptime)
  for name in ipa ipa_low gsi; do
    {
      printf 'IPC=%s UPTIME=%s\n' "$name" "$stamp"
      cat "/sys/kernel/debug/ipc_logging/$name/log"
    } >> "/tmp/ipa-ipc-$name.log"
  done
  {
    printf 'IRQ_UPTIME=%s\n' "$stamp"
    cat /proc/interrupts
    if [ -s /tmp/ipa-ingress.pid ]; then
      read -r pid < /tmp/ipa-ingress.pid
      case "$pid" in ''|*[!0-9]*) exit 1;; esac
      if [ -r "/proc/$pid/status" ]; then
        printf 'INGRESS_PID=%s\n' "$pid"
        cat "/proc/$pid/status" "/proc/$pid/wchan" "/proc/$pid/stack" || :
      fi
    fi
  } >> /tmp/ipa-ipc-state.log
  test ! -e "$finished" || break
  test ! -e "$base.stop" || break
  sleep 1
done
printf 'IPC_COLLECTOR_FINISHED uptime=%s phase=%s\n' "$(cat /proc/uptime)" "$phase"
