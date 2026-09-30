#!/bin/sh
# nx679j-mm-standard-boot.sh — NX679J: bring up the STANDARD modemmanager
# interface once the vendor chain has completed its datapath setup.
#
# Why: chain.sh (S95, background) sets up the IPA datapath and starts a WDS
# session on the EMBEDDED data port; ModemManager can only connect after the
# chain is done AND that chain-owned WDS session has been stopped (the modem
# allows a single WDS session on the embedded port; MM creates its own).
# This script runs detached at boot:
#   1. polls /tmp/chain.log for DONE (max ~8 min, dynamic)
#   2. stops the chain-owned wds-session (pid from /tmp/wds-session.pid)
#   3. waits for the netdev rmnet_ipa0 + ModemManager to see the modem
#   4. runs ifup modem (standard proto modemmanager)
#   5. logs the result to /tmp/mm-standard-boot.log via logger
#
# Safe to re-run; if the iface is already up it does nothing.

LOG=/tmp/mm-standard-boot.log
say() { echo "[$(cut -d. -f1 /proc/uptime)] $*" >> $LOG; logger -s -t mm-standard-boot "$*"; }

# v101 ANNULLATA (era un peggioramento): la seconda istanza NON e' spreco, e' di fatto il RETRY
# che fa vedere il modem a MM (probe #1 inutile). Il modem saliva a 171s invece di ~142s.
# Prossimo passo (v102): retry INTENZIONALE del restart MM dentro una sola istanza.

# already up?
if ubus call network.interface.modem status 2>/dev/null | grep -q '"up": true'; then
	say "iface modem gia' su, nulla da fare"
	exit 0
fi

# 1) wait for chain DONE
for i in $(seq 1 480); do
	grep -q "^\[.*\] DONE" /tmp/chain.log 2>/dev/null && break
	sleep 1
done
say "chain: $(grep -c '^\[.*\] DONE' /tmp/chain.log 2>/dev/null) done-marker(s)"

# 2) v119 ESPERIMENTO: NON fermiare piu' la wds-session della catena.
# MISURATO su 3 boot consecutivi (23/09): dati a 64-65 s con la catena, poi `ping=KO` a 165 s in
# 2 boot su 3. Causa: QUI si uccideva la sessione WDS della catena per liberare lo slot del port
# embedded a ModemManager — ma il bearer di MM arriva a 130-330 s (variabile) e nel frattempo NON
# c'e' connettivita'. Ora la sessione resta NOSTRA: MM continua a servire l'oggetto modem
# (registrazione/stato) per la pagina Cellular di LuCI; se il suo connect fallisce perche' lo slot
# e' occupato, non importa — i dati li porta la catena e `wan_early` resta su.
# (Blocco originale conservato come documentazione:)
#   W=$(cat /tmp/wds-session.pid 2>/dev/null)
#   if [ -n "$W" ] && kill -0 "$W" 2>/dev/null; then kill "$W"; say "wds-session ... fermato"; fi
say "v119: wds-session della catena LASCIATA VIVA (lo slot dati resta nostro; MM solo per la pagina modem)"
sleep 3

# 3) wait for rmnet_ipa0 (chain creates it)
for i in $(seq 1 120); do
	[ -e /sys/class/net/rmnet_ipa0 ] && break
	sleep 1
done

# inject the kernel event so MM probes the modem (kobject uevents are never
# delivered on this image for runtime netdevs; the watcher does the same)
if [ -e /sys/class/net/rmnet_ipa0 ]; then
	ACTION=add DEVPATH=/devices/virtual/net/rmnet_ipa0 INTERFACE=rmnet_ipa0 \
		sh /etc/hotplug.d/net/25-modemmanager-net 2>/dev/null
	say "evento rmnet_ipa0 iniettato"
fi

# v128: RIPRISTINO del restart MM (v124 lo aveva tolto per isolare il bug L3, poi
# attribuito a proto 'none' su wan_early e risolto in v125: quel vincolo non serve piu').
# FATTO MISURATO sul boot 91fe5ca5 (v127, senza restart): MM entra in thrash
# (324x "cleaning up port") e non crea MAI l'oggetto modem -> `mmcli -L -J` restituisce
# {"modem-list":[]} -> la pagina Cellular di LuCI resta vuota (REFRESHING senza dati).
# MM resta PASSIVO sul dato: nessun `ifup modem`, nessun Simple.Connect. Il bearer resta
# della catena (proto nx679j su wan_early); MM serve solo l'oggetto modem alla pagina Cellular.
for p in $(ps w | grep -E "[M]odemManager" | awk '{print $1}'); do
	kill -9 "$p" 2>/dev/null || true
done
sleep 10
/etc/init.d/modemmanager start >/dev/null 2>&1
say "v128: MM restart forte eseguito, attendo il modem"
MODEM_OK=0
for i in $(seq 1 120); do
  if [ "$(mmcli -L 2>/dev/null | grep -c 'Modem/')" -ge 1 ]; then MODEM_OK=1; break; fi
  sleep 1
done
if [ "$MODEM_OK" = 1 ]; then say "v128: modem osservato (MM passivo, nessun connect)"; else say "WARN v128: modem non osservato"; fi

# v128: l'L3 resta della catena — `ifup modem` NON va eseguito (creerebbe un secondo owner).
say "v128: ifup modem NON eseguito"
# 5) result: measure external chain state without waiting for an interface we deliberately do not bring up.
ADDR=$(ip -o -4 addr show dev rmnet_data0 2>/dev/null | grep -c inet)
ROUTE=$(ip -4 route show default dev rmnet_data0 2>/dev/null | grep -c '^default')
PING=KO
ping -c 1 -W 2 1.1.1.1 >/dev/null 2>&1 && PING=OK
say "v124: datapath addr=$ADDR default=$ROUTE ping=$PING"

