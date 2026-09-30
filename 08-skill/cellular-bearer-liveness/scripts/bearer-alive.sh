#!/bin/sh
# Read-only liveness probe for a QMI/RMNET cellular bearer.
#
#   ssh -i ~/.ssh/<key> root@<device> 'sh -s' < scripts/bearer-alive.sh
#
# Changes NOTHING: no ifup/ifdown, no address or route modification.
#
# Why: after the packet-data session ends, Linux keeps the IPv4 address and the
# default route, so `ubus status`, LuCI and `ip addr` keep reporting a healthy
# link. This probe distinguishes "bearer alive" from "stale L3" (false green).
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin

IF=${IF:-rmnet_data0}
LOG=${LOG:-/tmp/wds-session.log}
PROBE=${PROBE:-qmi-qrtr}

printf 'uptime_s=%s\n' "$(cut -d. -f1 /proc/uptime 2>/dev/null)"
printf 'boot=%s\n' "$(cut -c1-8 /proc/sys/kernel/random/boot_id 2>/dev/null)"

# 1) Liveness of the process holding the QMI session.
#    State 'Z' (zombie) means the session is over, whatever the L3 says.
alive=0
for p in /proc/[0-9]*; do
  [ -r "$p/cmdline" ] || continue
  cmd=$(tr '\0' ' ' < "$p/cmdline" 2>/dev/null)
  case "$cmd" in
    *"$PROBE"*)
      st=$(awk '{print $3}' "$p/stat" 2>/dev/null)
      printf 'proc pid=%s state=%s cmd=%s\n' "$(basename "$p")" "$st" "$cmd"
      case "$st" in Z) ;; *) alive=$((alive + 1)) ;; esac
      ;;
  esac
done
printf 'session_proc_alive=%s\n' "$alive"

# 2) Kernel L3. Present is NOT proof of connectivity (report it anyway: it is
#    what makes a false green possible).
printf 'kernel_addr=%s\n' "$(ip -4 -o addr show dev "$IF" 2>/dev/null | awk '{print $4}' | tr '\n' ' ')"
printf 'kernel_default=%s\n' "$(ip -4 route show default 2>/dev/null | tr '\n' ' ')"

# 3) Last session: granted hold, whether a Stop was issued, and the settings.
printf 'session_hold=%s\n' "$(grep -a -o 'HOLDING seconds=[0-9]*' "$LOG" 2>/dev/null | tail -1)"
printf 'session_stop_seen=%s\n' "$(grep -a -c 'stopping handle' "$LOG" 2>/dev/null)"
printf 'session_addr=%s\n' "$(awk '/^  IPv4 addr: /{print $3}' "$LOG" 2>/dev/null | tail -1)"
printf 'session_gw=%s\n' "$(awk '/^  IPv4 gateway: /{print $3}' "$LOG" 2>/dev/null | tail -1)"

# 4) Real reachability: the only direct evidence.
if ping -c 4 -W 3 1.1.1.1 >/dev/null 2>&1; then
  echo 'VERDICT=PING_OK'
else
  echo 'VERDICT=PING_KO'
  echo '  if kernel_addr/kernel_default are present, the L3 is STALE (false green in status UIs).'
  echo '  Renewal is two moves: re-establish the session AND re-apply the L3 from the NEW settings.'
fi
