#!/system/bin/sh
# Validate the exact timeout/exec and syscall decoder options on our fixture.
set -eu
BASE=${1:?tool directory required}
mkdir "$BASE/trace-bounded-smoke.once"
"$BASE/trace-fixture" > "$BASE/bounded-fixture.log" 2>&1 &
fixture=$!
code=0
timeout -s INT -k 2 4 sh -c \
  'printf "%s\n" "$$" > "$1"; shift; exec "$@"' sh "$BASE/bounded-strace.pid" \
  "$BASE/strace-static" -f -I 2 -ttt -T -yy -xx -s 8192 \
  -e trace=%network,read,write,readv,writev,ioctl,openat,close,dup,dup3,fcntl,clone,clone3 \
  -o "$BASE/bounded-syscalls.log" -p "$fixture" \
  > "$BASE/bounded-strace-control.log" 2>&1 || code=$?
printf 'TIMEOUT_EXIT=%s\n' "$code"
grep '^TracerPid:[[:space:]]*0$' "/proc/$fixture/status"
wait "$fixture"
grep -q '^FIXTURE_PASS$' "$BASE/bounded-fixture.log"
grep -q 'sendmsg(' "$BASE/bounded-syscalls.log"
grep -q 'recvmsg(' "$BASE/bounded-syscalls.log"
printf 'PASS: bounded tracer, full decoder options, detached live fixture\n'
