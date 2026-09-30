#!/bin/sh
# Instrumentation test only: no modem, IPA, networking or rawdump writes.
# Linux 5.10 Documentation/trace/kprobetrace.rst documents %REG and +uOFFS.
# AArch64 vfs_write(file=x0, user_buffer=x1, count=x2, pos=x3).
set -eu
T=/sys/kernel/tracing
I=$T/instances/nxipa_smoke3
G=nxipa_smoke3
OUT=/tmp/nxipa-smoke3
test "$(cat /sys/class/remoteproc/remoteproc3/state)" = offline
test -w "$T/kprobe_events"
test ! -e "$I"
test ! -e "$T/events/$G"
test -x /tmp/kprobe-write-fixture
mkdir "$OUT"
created=0
instance=0
stage=prepare
step() {
  stage=$1
  printf 'SMOKE_STAGE %s uptime=%s\n' "$stage" "$(cat /proc/uptime)"
}
cleanup() {
  result=$?
  trap - EXIT INT TERM
  set +e
  printf 'SMOKE_EXIT stage=%s exit=%s\n' "$stage" "$result"
  cat "$T/error_log" > "$OUT/error-log"
  cat "$T/kprobe_events" > "$OUT/kprobe-events"
  cat /sys/kernel/debug/kprobes/list > "$OUT/kprobe-list"
  dmesg > "$OUT/dmesg"
  if [ -e "$I/events/$G/write/filter" ]; then
    cat "$I/events/$G/write/filter" > "$OUT/event-filter"
  fi
  if [ "$instance" = 1 ]; then
    printf '0\n' > "$I/tracing_on"
    if [ -e "$I/events/$G/write/enable" ]; then
      printf '0\n' > "$I/events/$G/write/enable"
    fi
  fi
  if [ "$created" = 1 ]; then
    printf -- '-:%s/write\n' "$G" >> "$T/kprobe_events" || result=1
  fi
  if [ "$instance" = 1 ]; then
    rmdir "$I" || result=1
  fi
  test ! -e "$T/events/$G/write" || result=1
  printf 'SMOKE_CLEANUP exit=%s boot=%s uptime=%s\n' "$result" \
    "$(cat /proc/sys/kernel/random/boot_id)" "$(cat /proc/uptime)"
  exit "$result"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
cat /proc/sys/kernel/random/boot_id > "$OUT/boot-id"
step create_instance
mkdir "$I"
instance=1
step stop_instance
printf '0\n' > "$I/tracing_on"
step filter_pid
printf '%s\n' "$$" > "$I/set_event_pid"
step register_probe
printf '%s\n' \
  "p:$G/write vfs_write count=%x2:u64 caller=%x30:x64 raw0=+u0(%x1):x64 raw1=+u8(%x1):x64 raw2=+u16(%x1):x64 raw3=+u24(%x1):x64" \
  >> "$T/kprobe_events"
created=1
step filter_count
printf 'count == 32\n' > "$I/events/$G/write/filter"
step read_format
cat "$I/events/$G/write/format" > "$OUT/event-format"
step enable_event
printf '1\n' > "$I/events/$G/write/enable"
step start_instance
printf '1\n' > "$I/tracing_on"
# The helper selects its own PID and issues write(2), not libc writev(2).
# Its synthetic marker is NOT modem traffic or a captured hardware value.
/tmp/kprobe-write-fixture "$I/set_event_pid" "$OUT/fixture"
printf '0\n' > "$I/tracing_on"
step read_trace
cat "$I/trace" > "$OUT/trace"
printf '0\n' > "$I/events/$G/write/enable"
step validate
test "$(wc -c < "$OUT/fixture")" = 32
test "$(grep -c 'write: .*count=32 ' "$OUT/trace")" = 1
grep 'write: .*count=32 ' "$OUT/trace"
grep -q '37**************.*66**************.*37**************.*66**************' \
  "$OUT/trace"
test "$(cat /sys/class/remoteproc/remoteproc3/state)" = offline
stage=complete
printf 'SMOKE_PASS raw32_matches_fixture boot=%s uptime=%s\n' \
  "$(cat /proc/sys/kernel/random/boot_id)" "$(cat /proc/uptime)"
