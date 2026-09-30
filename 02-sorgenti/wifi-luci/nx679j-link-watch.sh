#!/bin/sh
# nx679j-link-watch — liveness + rinnovo del bearer QMI (modello mwan3track, adattato al QRTR).
# Livello 1 (ogni TICK, costo radio ~0): la sessione e' ancora viva?
# Livello 2 (ogni 5 tick o su sospetto): ICMP LEGATO all'interfaccia = verita' di piano utente.
# Su fallimento: rinnovo (stop+start in un processo) e riapplicazione L3, poi ifup wan_early.
#
# Perche' serve: nessun componente upstream ricrea da se' un bearer WDS; e su QRTR il CID
# non attraversa i processi, quindi il rinnovo deve stare in un unico processo (il nostro).
IFACE=rmnet_data0
REQ=/tmp/nx679j-link-renew.request
LOG=/tmp/link-watch.log
TICK=30
FAILS=0
say() { echo "[$(cut -d. -f1 /proc/uptime)] $*" >> "$LOG"; }

# la sessione QMI e' viva? (dpm + wds)
sess_alive() {
  pidof qmi-qrtr-observed >/dev/null 2>&1
}

# Rinnovo: ferma le sessioni vecchie, riavvia dpm+wds, riapplica l'L3 dalla NUOVA sessione.
# (Meccanismo identico a chain.sh/openwrt-cellular-staged.sh; verificato sul device.)
renew() {
  say "RINNOVO: avvio"
  for p in $(pidof qmi-qrtr-observed 2>/dev/null); do kill "$p" 2>/dev/null; done
  sleep 2
  rm -f /tmp/wds-session.pid /tmp/dpm-session.pid
  : > /tmp/dpm-session.log
  /tmp/qmi-qrtr-observed dpm-session 4 1 2 23 86400 >> /tmp/dpm-session.log 2>&1 &
  echo $! > /tmp/dpm-session.pid
  i=0; while [ $i -lt 20 ] && ! grep -q 'OPENED endpoint=4:1' /tmp/dpm-session.log; do sleep 1; i=$((i+1)); done
  : > /tmp/wds-session.log
  /tmp/qmi-qrtr-observed wds-session internet.it 4 1 1 86400 >> /tmp/wds-session.log 2>&1 &
  echo $! > /tmp/wds-session.pid
  # v133: attesa ROBUSTA. Con la sola riga HOLDING si poteva leggere contenuto VECCHIO
  # e abortire il rinnovo -> device senza L3 (difetto osservato il 24/09 alle 20:45).
  # Servono SIA il blocco impostazioni SIA HOLDING.
  i=0
  while [ $i -lt 90 ]; do
    grep -q '^  IPv4 addr: ' /tmp/wds-session.log && grep -q HOLDING /tmp/wds-session.log && break
    sleep 1; i=$((i+1))
  done
  a=$(awk '/^  IPv4 addr: /{print $3}' /tmp/wds-session.log | tail -1)
  m=$(awk '/^  IPv4 netmask: /{print $3}' /tmp/wds-session.log | tail -1)
  old=$(ip -4 -o addr show dev "$IFACE" 2>/dev/null | awk '{print $4}' | head -1)
  [ -n "$a" ] || { say "RINNOVO: nessun indirizzo nella nuova sessione"; return 1; }
  p=$(printf '%s' "$m" | awk -F. '{n=0;for(i=1;i<=4;i++){v=$i+0;while(v){n+=v%2;v=int(v/2)}}print n}')
  ip -4 addr add "$a/$p" dev "$IFACE" 2>/dev/null
  g=$(awk '/^  IPv4 gateway: /{print $3}' /tmp/wds-session.log | tail -1)
  [ -n "$g" ] && ip -4 route replace default via "$g" dev "$IFACE" 2>/dev/null
  [ -n "$old" ] && [ "$old" != "$a/$p" ] && ip -4 addr del "$old" dev "$IFACE" 2>/dev/null
  ifup wan_early >/dev/null 2>&1
  say "RINNOVO: L3 $a/$p via $g (vecchio $old rimosso)"
}

# Attesa che la catena abbia finito: durante il bring-up NON si contano guasti
# (altrimenti il supervisor rinnoverebbe contro la catena stessa).
w=0
while [ $w -lt 240 ] && ! grep -q "] DONE" /tmp/chain.log 2>/dev/null; do
  sleep 2; w=$((w+1))
done
say "catena: done-marker=$(grep -c '] DONE' /tmp/chain.log 2>/dev/null) (attesa $((w*2))s)"

# Ciclo principale. Hysteresis: servono 2 evidenze di guasto prima di rinnovare.
#
# Cadenza dei probe (misurata il 2026-09-24 su un'interruzione reale: ~3 minuti
# offline). Prima: ping ogni 5 tick = 150 s, quindi due evidenze = fino a 300 s
# solo per ACCORGERSI del guasto, piu' il rinnovo (45-90 s). Ora: ping ogni 2
# tick (60 s) e, alla prima evidenza, una conferma a 5 s — cioe' il secondo
# probe scatta subito invece di attendere un altro ciclo intero. Caso peggiore
# di rilevamento: ~65 s (era ~300). Il costo in piu' c'e' solo quando qualcosa
# e' gia' sospetto: a link sano i probe sono uno ogni 60 s.
N=0
echo "up $(cut -d. -f1 /proc/uptime) fails=0" > /tmp/link-health
say "link-watch avviato (probe ICMP ogni $((TICK*2))s, conferma a 5s)"
while :; do
  if [ -f "$REQ" ]; then
    rm -f "$REQ"
    say "richiesta di rinnovo dal controllo (LuCI/ubus)"
    # v134: stessa regola del percorso automatico — su fallimento non azzerare i guasti,
    # così il tentativo successivo parte al giro dopo.
    if renew; then FAILS=0; else FAILS=1; say "RINNOVO (da controllo): fallito, ritento al prossimo giro"; fi
  elif ! sess_alive; then
    FAILS=$((FAILS+1)); say "sessione QMI assente (fails=$FAILS)"
  else
    N=$((N+1))
    if [ $((N % 2)) -eq 0 ]; then
      if ping -I "$IFACE" -c1 -W3 8.8.8.8 >/dev/null 2>&1; then
        FAILS=0
      else
        FAILS=$((FAILS+1)); say "ICMP legato a $IFACE fallito (fails=$FAILS)"
        # Prima evidenza: si conferma subito. Restano due evidenze (la politica non
        # cambia), ma non si aspetta un secondo ciclo intero per averle.
        if [ "$FAILS" -eq 1 ]; then
          sleep 5
          if ping -I "$IFACE" -c1 -W3 8.8.8.8 >/dev/null 2>&1; then
            FAILS=0; say "ICMP: recuperato da solo (nessun rinnovo)"
          else
            FAILS=$((FAILS+1)); say "ICMP: confermato guasto (fails=$FAILS)"
          fi
        fi
      fi
    fi
  fi
  if [ "$FAILS" -ge 2 ]; then
    # v133: se il rinnovo fallisce NON azzerare i guasti: si ritenta al giro successivo
    # invece di attendere due cicli nuovi (il device resterebbe offline piu' a lungo).
    if renew; then FAILS=0; else FAILS=1; say "RINNOVO: fallito, ritento al prossimo giro"; fi
  fi
  if [ "$FAILS" -eq 0 ]; then st=up; else st=down; fi
  echo "$st $(cut -d. -f1 /proc/uptime) fails=$FAILS" > /tmp/link-health
  sleep $TICK
done
