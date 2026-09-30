#!/bin/sh
# boot-timeline-poll.sh — misura la timeline di boot di un device remoto via ssh.
#
# Uso:  ./boot-timeline-poll.sh [host] [intervallo_s] [max_iterazioni]
#       KEY=/path/chiave ./boot-timeline-poll.sh 10.0.0.1 3 80
#
# Stampa SOLO le transizioni di: uptime, iface_up (ifstatus <proto>), kiosk, modem (mmcli), ping —
# e in coda il PRIMO uptime in cui ciascun campo e' diventato vero (la coda non conta, il fronte si').
#
# Regole incorporate (ognuna pagata con un ciclo di build+flash sprecato):
#  1. ASSERZIONE DEL RESET: se il primo campione valido ha un uptime alto, il device non si e'
#     riavviato e stai misurando il BOOT VECCHIO -> la timeline non viene registrata.
#  2. Parsing con delimitatore esplicito, mai offset fissi: un campo che cambia larghezza
#     (uptime a 3 cifre) sposta tutti gli altri.
#  3. Il criterio di accettazione deve essere quello che vede l'utente (stato dell'interfaccia),
#     non un proxy comodo (ping): qui i campi sono separati apposta.
#
# Adatta REMOTE al tuo device. Il reboot NON lo fa questo script: riavvia tu, poi lancialo.

HOST=${1:-10.0.0.1}
STEP=${2:-3}
MAX=${3:-80}
IFACE_PROTO=${IFACE_PROTO:-modem}
KIOSK_PROC=${KIOSK_PROC:-nx679j-kiosk3}

SSH="ssh -T -q -i ${KEY:-$HOME/.ssh/nx679j_key} -o ConnectTimeout=3 \
     -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@$HOST"

REMOTE='U=$(cut -d. -f1 /proc/uptime)
F=$(ifstatus '"$IFACE_PROTO"' 2>/dev/null | grep -c "\"up\": true")
K=$(pidof '"$KIOSK_PROC"' | wc -w)
M=$(mmcli -L 2>/dev/null | grep -c Modem)
P=$(ping -c1 -W2 8.8.8.8 >/dev/null 2>&1 && echo 1 || echo 0)
printf "%s %s %s %s %s\n" "$U" "$F" "$K" "$M" "$P"'

PREV=""; ARMED=""; IF=""; KI=""; MO=""; PI=""; i=0
while [ "$i" -lt "$MAX" ]; do
  R=$($SSH "$REMOTE" 2>/dev/null)
  if [ -n "$R" ]; then
    set -- $R
    U=${1:-}; F=${2:-}; K=${3:-}; M=${4:-}; P=${5:-}
    if [ -z "$ARMED" ]; then
      # regola 1: primo campione valido solo se l'uptime e' basso (= device appena riavviato)
      if [ "${U:-9999}" -le 60 ] 2>/dev/null; then
        ARMED=1; echo "[armed] boot nuovo, uptime ${U}s"
      else
        echo "[skip] uptime ${U}s -> boot VECCHIO (non riavviato): timeline non registrata"
      fi
    else
      [ "$R" != "$PREV" ] && echo "U=$U iface=$F kiosk=$K modem=$M ping=$P"
      PREV=$R
      [ -z "$IF" ] && [ "$F" = 1 ] && IF=$U
      [ -z "$KI" ] && [ "$K" -ge 1 ] 2>/dev/null && KI=$U
      [ -z "$MO" ] && [ "$M" = 1 ] && MO=$U
      [ -z "$PI" ] && [ "$P" = 1 ] && PI=$U
      case "$R" in *" 1 1 1 1") echo ">>> TUTTO SU (criterio soddisfatto) a U=$U"; break;; esac
    fi
  fi
  i=$((i+1)); sleep "$STEP"
done
echo "== PRIMI (uptime s): iface=${IF:-mai} kiosk=${KI:-mai} modem=${MO:-mai} ping=${PI:-mai} =="
echo "   se 'mai' in piu' di un campo il boot e' FALLITO, non solo lento: caratterizza il fallimento"
