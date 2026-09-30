#!/bin/sh
# Private, bounded entry tracing only; no modem, pipe, format, or packet writes.
# Stock ipam.ko: wrapper entry .text+0xc30b8, LAN LR+0x58e18, WAN LR+0x5a818.
# Four nofault 8-byte fetches cover the 32-byte IPA5.1 status.
set -eu
phase=${1:?register|ingress|network required}
case "$phase" in register|ingress|network) ;; *) exit 2;; esac
T=/sys/kernel/tracing
I=$T/instances/nxipa_status
G=nxipa_status
OUT=/tmp/ipa-status-$phase
test ! -e "$I"
test ! -e "$T/events/$G"
test -d /sys/module/ipam
if [ "$phase" = register ]; then
  test "$(cat /sys/class/remoteproc/remoteproc3/state)" = offline
else
  test "$(cat /proc/sys/kernel/random/boot_id)" = "$(cat /tmp/observed-bootstrap.boot-id)"
  test "$(cat /tmp/ipa-status-rawdump-preserved)" = "$(cat /proc/sys/kernel/random/boot_id)"
  test -b /proc/1/root/dev/rd
  grep -qx 'PARTNAME=rawdump' /sys/block/sda/sda11/uevent
  test -x /tmp/ipa-trace-capture
  test "$(cat /sys/class/remoteproc/remoteproc3/state)" = running
fi
mkdir "$OUT"
printf '%s\n' "$$" > "$OUT/controller.pid"
created=0
instance=0
sink=
cleanup() {
  result=$?
  trap - EXIT INT TERM HUP
  set +e
  if [ "$instance" = 1 ]; then
    printf '0\n' > "$I/tracing_on"
    if [ -e "$I/events/$G/parse/enable" ]; then
      printf '0\n' > "$I/events/$G/parse/enable"
    fi
  fi
  if [ -n "$sink" ]; then
    kill -TERM "$sink"
    wait "$sink"
  fi
  cat "$T/error_log" > "$OUT/error-log"
  cat "$T/kprobe_profile" > "$OUT/kprobe-profile"
  if [ "$instance" = 1 ]; then
    cat "$I/trace" > "$OUT/trace-remainder"
    for stats in "$I"/per_cpu/cpu*/stats; do
      printf '%s\n' "$stats"
      cat "$stats"
    done > "$OUT/buffer-stats"
  fi
  if [ "$created" = 1 ]; then
    printf -- '-:%s/parse\n' "$G" >> "$T/kprobe_events" || result=1
  fi
  if [ "$instance" = 1 ]; then rmdir "$I" || result=1; fi
  cat /sys/kernel/debug/kprobes/list > "$OUT/kprobes-after"
  test ! -e "$T/events/$G/parse" || result=1
  printf '%s\n' "$result" > "$OUT/result"
  printf 'IPA_TRACE_CLEANUP phase=%s exit=%s uptime=%s\n' \
    "$phase" "$result" "$(cat /proc/uptime)"
  exit "$result"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
cat /proc/sys/kernel/random/boot_id > "$OUT/boot-id"
cat /sys/module/ipam/sections/.text > "$OUT/ipam-text"
awk '$3=="ipahal_pkt_status_parse" && $4=="[ipam]" {print}' \
  /proc/kallsyms > "$OUT/parser-symbol"
test "$(wc -l < "$OUT/parser-symbol")" = 1
cat /sys/kernel/debug/kprobes/list > "$OUT/kprobes-before"
mkdir "$I"
instance=1
printf '0\n' > "$I/tracing_on"
# 64 KiB per CPU is two rawdump slots of trace memory, not an IPA parameter.
printf '64\n' > "$I/buffer_size_kb"
if grep -qw mono "$I/trace_clock"; then printf 'mono\n' > "$I/trace_clock"; fi
cat "$I/trace_clock" > "$OUT/trace-clock"
printf '%s\n' \
  "p:$G/parse ipam:ipahal_pkt_status_parse pointer=%x0:x64 caller=%x30:x64 raw0=+0(%x0):x64 raw1=+8(%x0):x64 raw2=+16(%x0):x64 raw3=+24(%x0):x64" \
  >> "$T/kprobe_events"
created=1
cat "$I/events/$G/parse/format" > "$OUT/event-format"
cat "$T/kprobe_events" > "$OUT/kprobe-events"
cat /sys/kernel/debug/kprobes/list > "$OUT/kprobes-registered"
printf 'IPA_TRACE_REGISTERED phase=%s uptime=%s\n' "$phase" "$(cat /proc/uptime)"
if [ "$phase" = register ]; then exit 0; fi
printf '1\n' > "$I/events/$G/parse/enable"
printf '1\n' > "$I/tracing_on"
/tmp/ipa-trace-capture "$I/trace_pipe" /proc/1/root/dev/rd "$OUT" 180 "$phase" &
sink=$!
printf '%s\n' "$sink" > "$OUT/sink.pid"
result=0
wait "$sink" || result=$?
sink=
exit "$result"
