#!/system/bin/sh
echo "== FULL SVC LIST qrtr-lookup:"
/vendor/bin/qrtr-lookup 0 2>&1
echo "== processes:"
ps -A -o PID,USER,NAME | grep -iE 'rmtfs|tftp|pd-mapper|qmuxd|diag|dataqti|qmipriod|netmgrd|qcril|qti|port-bridge|ssgtzd|dpmQmiMgr|ipacm|tftp_server'
echo "== vendor bin related:"
ls /vendor/bin/ | grep -iE 'rmt|tftp|mapper|storage|ssg|diag|qmi|port-bridge'
echo "== props:"
getprop | grep -iE 'rmtfs|tftp|pd.mapper|dataqti|qmuxd'
echo "== block by name (modem/fsg/efs):"
ls /dev/block/by-name/ 2>/dev/null | grep -iE 'modem|fsg|fsc|efs|snv'
echo "== node 1 (AP) services:"
/vendor/bin/qrtr-lookup 0 2>&1 | awk '$4 == 1'
echo END
