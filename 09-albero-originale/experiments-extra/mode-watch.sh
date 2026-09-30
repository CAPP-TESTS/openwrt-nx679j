#!/bin/sh
i=0
while [ $i -lt 18 ]; do
  i=$((i+1))
  sleep 18
  T=$(awk '{print $1}' /proc/uptime)
  M=$(/tmp/qmi-qrtr raw 2 0080012d000000 2>/dev/null | grep -o 'u8=[0-9]*' | head -1)
  S=$(/tmp/qmi-qrtr list 2>/dev/null | grep -c service=)
  echo "uptime=$T mode=$M svc=$S"
  case "$M" in *u8=0*) echo "ONLINE-REACHED at uptime=$T"; break;; esac
done
echo "== tftp tail:"
grep -vE 'del_client|Ignoring' /tmp/tqftpserv.log | tail -6
echo END
