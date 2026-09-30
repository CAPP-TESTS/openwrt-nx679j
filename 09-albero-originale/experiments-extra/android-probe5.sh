#!/system/bin/sh
echo "== RFS/qcom devices in /dev:"
ls -la /dev/ | grep -iE 'qcom|rmt|rfs|smem'
echo "== remoteprocs:"
for r in /sys/class/remoteproc/remoteproc*/; do echo "$r $(cat $r/name 2>/dev/null) $(cat $r/state 2>/dev/null)"; done
echo "== init services tftp/rmt/rmtfs/diag/qrtr:"
getprop | grep -E 'init.svc' | grep -iE 'tftp|rmt|diag|qrtr|qti|dataqti'
echo "== rmt_storage strings (paths):"
grep -a -o -E '/(dev|data|vendor|mnt)[a-zA-Z0-9_/.-]*' /vendor/bin/rmt_storage 2>/dev/null | sort -u | head -25
echo "== tftp_server strings (paths):"
grep -a -o -E '/(dev|data|vendor|mnt)[a-zA-Z0-9_/.-]*' /vendor/bin/tftp_server 2>/dev/null | sort -u | head -25
echo "== current svc count:"
/data/local/tmp/qmi-qrtr list 2>/dev/null | grep -c service=
echo "== tftp/rmt procs:"
ps -A -o PID,USER,NAME | grep -iE 'tftp|rmt'
echo END
