#!/bin/bash
# nxstate.sh — stato del NX679J in <3s: FASTBOOT | ANDROID | OPENWRT | OFFLINE
# uso: ./nxstate.sh                 -> stampa lo stato
#      ./nxstate.sh wait ANDROID 180 -> attende lo stato (timeout sec)
KEY=/home/user/.ssh/nx679j_key
SSHOPT=(-i "$KEY" -o IdentitiesOnly=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=3 -o BatchMode=yes -o LogLevel=ERROR)

check_state() {
  # 1. fastboot (istantaneo)
  if timeout 5 fastboot devices 2>/dev/null | grep -qE '[0-9a-f]{6,}'; then echo FASTBOOT; return; fi
  # 2. adb (istantaneo; matches 'SERIAL	device')
  if timeout 5 adb devices 2>/dev/null | grep -qE '^[0-9A-Za-z]{6,}[[:space:]]+device$'; then echo ANDROID; return; fi
  # 3. ssh OpenWrt (breve)
  if timeout 6 ssh "${SSHOPT[@]}" root@10.0.0.1 'true' >/dev/null 2>&1; then echo OPENWRT; return; fi
  # 4. adb unauthorized/offline?
  if timeout 5 adb devices 2>/dev/null | grep -qE '[[:space:]](offline|unauthorized)$'; then echo ANDROID-ADB-ISSUE; return; fi
  echo OFFLINE
}

if [ "$1" = "wait" ]; then
  TARGET="$2"; TMO="${3:-180}"; T0=$(date +%s)
  while :; do
    S=$(check_state)
    [ "$S" = "$TARGET" ] && { echo "$TARGET"; exit 0; }
    [ $(( $(date +%s) - T0 )) -ge "$TMO" ] && { echo "TIMEOUT($S)"; exit 1; }
    sleep 2
  done
else
  check_state
fi
