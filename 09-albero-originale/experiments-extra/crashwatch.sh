#!/bin/bash
# crashwatch.sh - capture phone dmesg + journal into a HOST file until reboot.
# Runs bounded (240 iterations x 3s = 12 min) then exits.
LOG=/home/user/nx679j-stock/experiments/crashwatch.log
SSH="ssh -i /home/user/.ssh/nx679j_key -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 root@10.0.0.1"
remote="echo UP=\$(awk '{print \$1}' /proc/uptime) R3=\$(cat /sys/class/remoteproc/remoteproc3/state 2>/dev/null); dmesg | tail -18; echo JRN; tail -4 /proc/1/root/nx679j-journal 2>/dev/null"
: > "$LOG"
for i in $(seq 1 240); do
  TS=$(date +%H:%M:%S)
  OUT=$($SSH "$remote" 2>&1)
  RC=$?
  { echo "=== [$TS] iter=$i rc=$RC"; echo "$OUT"; } >> "$LOG"
  sleep 3
done
echo "DONE" >> "$LOG"
