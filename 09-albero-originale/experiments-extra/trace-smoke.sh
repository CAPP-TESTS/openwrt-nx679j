#!/bin/sh
# Only trace our disposable local-socket fixture; never attach vendor daemons.
set -eu
BASE=${1:?absolute tool/log directory required}
case "$BASE" in /*) ;; *) exit 2;; esac
test -x "$BASE/strace-static"
test -x "$BASE/trace-fixture"
mkdir "$BASE/trace-smoke.once"
trace_pid=
fixture_pid=
cleanup() {
  if [ -n "$trace_pid" ]; then
    kill -INT "$trace_pid" 2>/dev/null || true
    wait "$trace_pid" 2>/dev/null || true
  fi
  if [ -n "$fixture_pid" ]; then
    wait "$fixture_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT
"$BASE/strace-static" -V
"$BASE/trace-fixture" > "$BASE/trace-fixture.log" 2>&1 &
fixture_pid=$!
"$BASE/strace-static" -f -I 2 -s 256 \
  -e trace=clone,clone3,sendmsg,recvmsg \
  -o "$BASE/trace-smoke.log" -p "$fixture_pid" \
  > "$BASE/trace-smoke-tracer.log" 2>&1 &
trace_pid=$!
sleep 4
kill -INT "$trace_pid"
wait "$trace_pid" || true
trace_pid=
grep '^TracerPid:[[:space:]]*0$' "/proc/$fixture_pid/status"
wait "$fixture_pid"
fixture_pid=
grep -q '^EXCHANGE_OK$' "$BASE/trace-fixture.log"
grep -q '^FIXTURE_PASS$' "$BASE/trace-fixture.log"
grep -q 'sendmsg(.*trace-probe-v1' "$BASE/trace-smoke.log"
grep -q 'recvmsg(.*trace-probe-v1' "$BASE/trace-smoke.log"
printf 'PASS: attach in sleep, new thread, socket payloads, detach, target survived\n'
