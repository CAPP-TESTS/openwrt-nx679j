#!/system/bin/sh
# Native Android airplane-mode cycle; no manual QMI setters or remoteproc writes.
# Budgets: native restore watchdog 20s; strace hard limit 180s; observe 120s.
set -eu
umask 077
BASE=${1:?absolute tool directory required}
case "$BASE" in /*) ;; *) exit 2;; esac
LABEL=${2:?new capture directory name required}
case "$LABEL" in ""|*[!a-zA-Z0-9_-]*) exit 2;; esac
OUT="$BASE/$LABEL"
export PATH=/system/bin:/system/xbin:$PATH
test "$(id -u)" = 0
test "$(getprop ro.boot.slot_suffix)" = _a
test "$(settings get global airplane_mode_on)" = 0
test "$(settings get global mobile_data)" = 1
test "$(cat /sys/class/remoteproc/remoteproc4/name)" = 4080000.remoteproc-mss
test "$(cat /sys/class/remoteproc/remoteproc4/state)" = running
test -x "$BASE/strace-static"
test -x "$BASE/qmi-qrtr"
mkdir "$OUT"
trace_pid=
trace_wrapper=
logger_pid=
restore_pid=
targets=
cleanup() {
  code=$?
  trap - EXIT INT TERM
  set +e
  if [ "$(settings get global airplane_mode_on)" != 0 ]; then
    timeout -k 2 5 cmd connectivity airplane-mode disable
  fi
  if [ -n "$trace_pid" ]; then
    kill -INT "$trace_pid" 2>/dev/null
    for unused in 1 2 3 4 5; do
      state=$(awk '/^State:/ {print $2}' "/proc/$trace_pid/status" 2>/dev/null)
      case "$state" in ""|Z) break;; esac
      sleep 1
    done
    state=$(awk '/^State:/ {print $2}' "/proc/$trace_pid/status" 2>/dev/null)
    case "$state" in ""|Z) ;; *) kill -KILL "$trace_pid"; code=1;; esac
  fi
  [ -z "$trace_wrapper" ] || wait "$trace_wrapper"
  [ -z "$logger_pid" ] || { kill -TERM "$logger_pid"; wait "$logger_pid"; }
  # The separate native restore watchdog finishes before the 120s observation.
  # On an early error it remains alive until its 20s deadline.
  for p in $targets; do
    if [ ! -r "/proc/$p/status" ]; then
      printf 'TARGET_EXITED pid=%s\n' "$p"
      code=1
      continue
    fi
    for task in /proc/"$p"/task/*; do
      tp=$(awk '/^TracerPid:/ {print $2}' "$task/status" 2>/dev/null)
      if [ -n "$tp" ] && [ "$tp" != 0 ]; then
        printf 'ERROR_STILL_TRACED task=%s tracer=%s\n' "$task" "$tp"
        code=1
      fi
    done
    printf 'DETACHED pid=%s\n' "$p"
  done
  plane=$(settings get global airplane_mode_on)
  [ "$plane" = 0 ] || code=1
  printf 'CLEANUP_COMPLETE uptime=%s airplane=%s exit=%s\n' \
    "$(cat /proc/uptime)" "$plane" "$code"
  exit "$code"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
stamp() {
  printf 'EVENT %s uptime=%s epoch=%s\n' "$1" "$(cat /proc/uptime)" "$(date +%s)"
}
sample() {
  {
    stamp "$1"
    timeout -k 1 5 "$BASE/qmi-qrtr" raw 2 0001002d000000 || true
    printf 'MSS=%s airplane=%s\n' \
      "$(cat /sys/class/remoteproc/remoteproc4/state)" \
      "$(settings get global airplane_mode_on)"
  } >> "$OUT/samples.log"
}

set --
for name in qcrilNrd netmgrd qmipriod qti; do
  pids=$(pidof "$name")
  test -n "$pids"
  count=$(printf '%s\n' "$pids" | wc -w)
  if [ "$name" = qcrilNrd ]; then test "$count" = 2; else test "$count" = 1; fi
  for p in $pids; do
    test "$(awk '/^TracerPid:/ {print $2}' "/proc/$p/status")" = 0
    printf 'TARGET %s %s %s\n' "$name" "$p" "$(readlink "/proc/$p/exe")" |
      tee -a "$OUT/targets.log"
    ls -l "/proc/$p/fd" > "$OUT/fds-before-$p.log"
    targets="$targets $p"
    set -- "$@" -p "$p"
  done
done
printf '%s\n' "$$" > "$OUT/controller.pid"
cat /proc/sys/kernel/random/boot_id > "$OUT/boot-id"
timeout -k 1 5 "$BASE/qmi-qrtr" list > "$OUT/services-before.log"
sample baseline
grep -q 'TLV 0x01 len=1  u8=0' "$OUT/samples.log"
sh "$BASE/android-crashlog.sh" "$OUT" > "$OUT/raw-logger.log" 2>&1 &
logger_pid=$!
for unused in 1 2 3 4 5 6 7 8 9 10; do
  if [ -s "$OUT/raw-slot-number" ]; then break; fi
  if [ ! -r "/proc/$logger_pid/status" ]; then
    printf 'ERROR: rawdump logger exited before first slot\n'
    exit 1
  fi
  sleep 1
done
if [ ! -s "$OUT/raw-slot-number" ]; then
  printf 'ERROR: first rawdump slot not ready within 10 seconds\n'
  exit 1
fi
kill -0 "$logger_pid"
test "$(awk '/^State:/ {print $2}' "/proc/$logger_pid/status")" != Z
stamp rawdump_logger_ready

# Timeout targets only its child tracer, never the existing vendor processes.
# No strace --kill-on-exit. The shell exec preserves the PID recorded here.
timeout -s INT -k 5 180 sh -c \
  'printf "%s\n" "$$" > "$1"; shift; exec "$@"' sh "$OUT/strace.pid" \
  "$BASE/strace-static" -f -I 2 -ttt -T -yy -xx -s 8192 \
  -e trace=%network,read,write,readv,writev,ioctl,openat,close,dup,dup3,fcntl,clone,clone3 \
  -o "$OUT/syscalls.log" "$@" > "$OUT/strace-control.log" 2>&1 &
trace_wrapper=$!
sleep 2
read -r trace_pid < "$OUT/strace.pid"
test "$(readlink "/proc/$trace_pid/exe")" = "$BASE/strace-static"
for p in $targets; do
  for task in /proc/"$p"/task/*; do
    tp=$(awk '/^TracerPid:/ {print $2}' "$task/status")
    test "$tp" = "$trace_pid"
  done
done
stamp all_targets_attached | tee -a "$OUT/samples.log"

# Runs independently so an observer failure cannot leave airplane mode enabled.
sh -c '
  sleep 20
  if [ "$(settings get global airplane_mode_on)" != 0 ]; then
    timeout -k 2 5 cmd connectivity airplane-mode disable
    printf "WATCHDOG_RESTORED uptime=%s\n" "$(cat /proc/uptime)"
  fi
' > "$OUT/restore-watchdog.log" 2>&1 &
restore_pid=$!
stamp native_airplane_enable | tee -a "$OUT/samples.log"
timeout -k 2 5 cmd connectivity airplane-mode enable
sleep 5
sample airplane_enabled
stamp native_airplane_disable | tee -a "$OUT/samples.log"
timeout -k 2 5 cmd connectivity airplane-mode disable

start=$(awk '{print int($1)}' /proc/uptime)
while [ "$(( $(awk '{print int($1)}' /proc/uptime) - start ))" -lt 120 ]; do
  kill -0 "$trace_wrapper"
  test -r "/proc/$trace_pid/status"
  test "$(awk '/^State:/ {print $2}' "/proc/$trace_pid/status")" != Z
  kill -0 "$logger_pid"
  test "$(awk '/^State:/ {print $2}' "/proc/$logger_pid/status")" != Z
  sample recovery
  sleep 3
done
wait "$restore_pid"
timeout -k 1 5 "$BASE/qmi-qrtr" list > "$OUT/services-after.log"
"$BASE/rmnet-inspect" > "$OUT/links-after.log"
ip addr show > "$OUT/addresses-after.log"
ip -4 route show table all > "$OUT/routes-after.log"
dmesg > "$OUT/dmesg-after.log"
for p in $targets; do
  ls -l "/proc/$p/fd" > "$OUT/fds-after-$p.log"
done
stamp capture_complete
