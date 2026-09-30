#!/bin/sh
# dmcap: cattura dmesg in loop nel rawdump slot 440 (sopravvive al reset HW).
while true; do
  dmesg | tail -c 30000 > /tmp/dm.buf 2>/dev/null
  dd if=/tmp/dm.buf of=/proc/1/root/dev/rd bs=1024 seek=$((440*32)) conv=fsync 2>/dev/null
  sleep 2
done
