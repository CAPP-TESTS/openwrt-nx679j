#!/bin/sh
# chain.sh — catena modem X65 completa, con log e marker per step.
# Lanciato detached: sh /tmp/chain.sh > /tmp/chain.out 2>&1 &
set -u
L=/tmp/chain.log
say() { echo "[$(cat /proc/uptime | cut -d' ' -f1)] $*" >> "$L"; }
export PATH=/usr/sbin:/usr/bin:/sbin:/bin

say "BEGIN boot=$(cat /proc/sys/kernel/random/boot_id)"

# Se il collegamento e' gia' vivo non rilanciare la catena pesante (che e' per il boot a freddo).
if ip -4 addr show rmnet_data0 2>/dev/null | grep -q "inet " && ping -c 1 -W 3 8.8.8.8 >/dev/null 2>&1; then
  say "healthy: rmnet_data0 con IP e ping ok, catena saltata"
  exit 0
fi

# prerequisiti
touch /tmp/ipa-ingress.done /tmp/data-observed.log /tmp/cellular-staged.log
chmod 755 /tmp/nx679j-modem-prepare.sh /tmp/openwrt-*.sh /tmp/rmnet-config-agg8192
# v108 FIX INTERMITTENZA: risoluzione dell'MSS PER NOME, mai per indice.
# Provato sul device (campagna 23/09): l'ordine di enumerazione dei remoteproc CAMBIA ad ogni boot
# (in un boot MSS=remoteproc2, in un altro MSS=remoteproc3). Il vecchio codice leggeva
# /sys/class/remoteproc/remoteproc3/state FISSO: quando l'MSS era remoteproc2 leggeva lo stato
# dell'ADSP -> "MSS=offline" fasullo, ramo di salto-bootstrap mai preso, bootstrap al momento
# sbagliato. Ecco perche' ~meta' dei boot falliva senza alcun guasto hardware.
mss_path() {
  for r in /sys/class/remoteproc/remoteproc*; do
    case "$(cat "$r/name" 2>/dev/null)" in
      *4080000.remoteproc-mss*) echo "$r"; return ;;
    esac
  done
}
mss_state() { p=$(mss_path); [ -n "$p" ] && cat "$p/state" 2>/dev/null; }
# v109: caricamento firmware IPA come fa Android (`write /dev/ipa 1`).
# Trovato dalla ricerca: su questo kernel `ipam` NON carica ("Unknown symbol gsi_*") e l'IPA resta
# giu'; Android carica il firmware scrivendo su /dev/ipa, che ESISTE anche qui. Senza IPA il
# percorso dati rmnet non ha il suo blocco di fondo.
if [ -e /dev/ipa ]; then echo 1 > /dev/ipa 2>/dev/null; say "v109: /dev/ipa <- 1 (firmware IPA)"; fi

# v116 — LA CATENA NON DEVE RIPARTIRE SE HA GIA' COMPLETATO IN QUESTO BOOT.
# MISURATO (23/09, due boot): la catena viene rieseguita a ~95 s mentre i dati funzionano da 63 s;
# il 2o giro fallisce agli stage `cell address`/`route` (rc=1) e lascia il device con `ping=KO` per
# tutto il resto del boot (`modem=0`, iface mai su). Non era il modem: era il retry che smontava
# cio' che gia' andava. Se il run precedente e' arrivato a DONE, questo esce subito.
# (Se il run precedente e' FALLITO non c'e' "DONE" nel log -> il retry procede, come deve.)
if grep -q "] DONE" /tmp/chain.log 2>/dev/null; then
  say "v116: catena gia' completata in questo boot (DONE nel log) -> esco senza toccare nulla"
  exit 0
fi

# v117 — launcher del display lanciato DA QUI (la catena parte a ~37.8 s).
# v115 lo metteva in FONDO a boot-services, cioe' dopo rcS e i servizi modem: MISURATO che partiva a
# uptime 93 ("armato" alle 00:23:36 con uptime 93), quindi la gate a 70 era di fatto inutile e il
# kiosk restava a ~122 s. Qui parte a 37.8 s e la gate (70 s) e' la sola cosa che decide.
# La guardia di istanza singola dentro il launcher rende innocua la seconda invocazione (S94).
if [ -x /usr/lib/nx679j/modem/nx679j-display-late.sh ] && [ ! -f /tmp/no-display ]; then
  setsid /usr/lib/nx679j/modem/nx679j-display-late.sh </dev/null >/dev/null 2>&1 &
  say "v117: launcher display avviato a uptime $(cut -d. -f1 /proc/uptime 2>/dev/null) (parallelo alla catena)"
fi

say "MSS=$(mss_state) (path=$(mss_path))"

# Bootstrap solo se il modem non e' gia' attivo in questo boot.
# (Il bootstrap originale ha un gate "once per boot": ai riavvii della catena
#  mid-boot fallirebbe con STOP bootstrap. Se MSS e' gia' running saltiamo.)
if [ -d /tmp/observed-bootstrap.once ] && [ "$(mss_state)" = "running" ]; then
  say "bootstrap: MSS gia' running (gate once presente), salto"
else
  # v99/v108: se il remoteproc MSS non e' registrato, ASPETTA. Risoluzione PER NOME (v108).
  w=0
  while [ $w -lt 150 ] && [ -z "$(mss_path)" ]; do sleep 2; w=$((w+1)); done
  say "attesa MSS: $((w*2))s (registrato=$([ -n "$(mss_path)" ] && echo si || echo no))"
  sh /tmp/openwrt-observed-bootstrap.sh > /tmp/observed-bootstrap.log 2>&1
  say "bootstrap rc=$? ready=$(grep -c READY_FOR_OBSERVED_DMS_TEST /tmp/observed-bootstrap.log)"
  [ "$(grep -c READY_FOR_OBSERVED_DMS_TEST /tmp/observed-bootstrap.log)" = "1" ] || { say "STOP bootstrap"; exit 1; }
fi

if [ -d /tmp/dms-observed-openwrt.once ]; then
  say "dms: gia' fatto in questo boot, salto"
else
  sh /tmp/openwrt-dms-observed-check.sh > /tmp/dms-check.log 2>&1
  r=$?
  say "dms rc=$r finished=$(grep -c OBSERVED_CHECK_FINISHED /tmp/dms-check.log)"
  [ "$r" = "0" ] || { say "STOP dms"; exit 1; }
fi

sh /tmp/nx679j-modem-prepare.sh > /tmp/prepare.log 2>&1
say "prepare rc=$?"

# holder DPM manuale (il prepare v2 chiude la sessione; la catena lo richiede vivo)
# v127: hold 3600 -> 86400. Il hold e' un timer del NOSTRO client, non del modem:
# a 3600 s la sessione WDS si chiudeva da sola e la connettivita' moriva dopo ~1 h
# lasciando IPv4+default route stantii nel kernel (falso verde in LuCI). La ricerca
# upstream (libqmi/ModemManager) conferma che una sessione WDS di molte ore e' normale
# e che il servizio WDS non ha timer di sessione.
/tmp/qmi-qrtr-observed dpm-session 4 1 2 23 86400 > /tmp/dpm-session.log 2>&1 &
echo $! > /tmp/dpm-session.pid
i=0; while [ $i -lt 30 ] && ! grep -q "OPENED endpoint=4:1" /tmp/dpm-session.log; do sleep 1; i=$((i+1)); done
say "dpm open=$(grep -c OPENED /tmp/dpm-session.log)"

/tmp/rmnet-config-agg8192 rmnet_ipa0 ingress > /tmp/ingress.log 2>&1
say "ingress rc=$? accepted=$(grep -c 'INGRESS accepted' /tmp/ingress.log)"

for s in egress wda mux wds; do
  /tmp/openwrt-data-staged.sh $s > /tmp/data-$s.log 2>&1
  say "data $s rc=$?"
done
grep -a "DATA_BEARER_READY" /tmp/data-wds.log >> /tmp/data-observed.log
say "bearer_ready=$(grep -c DATA_BEARER_READY /tmp/data-observed.log)"
[ "$(grep -c DATA_BEARER_READY /tmp/data-observed.log)" -ge 1 ] || { say "STOP data (niente bearer)"; exit 1; }

for s in inspect mtu address route route-get stats ping; do
  /tmp/openwrt-cellular-staged.sh $s > /tmp/cell-$s.log 2>&1
  say "cell $s rc=$?"
done
say "criteria=$(grep -c CELLULAR_CRITERIA_PASS /tmp/cell-ping.log)"

# v113 — interfaccia UCI anticipata, ora NEL POSTO GIUSTO (dopo gli stage che configurano IP/route).
# BUG TROVATO DAI TIMESTAMP DEL DEVICE: prima stava PRIMA di questo loop e alle 62.06 s stampava
# "nessun device dati trovato", perche' l'indirizzo e la route li mette lo stage `address`/`route`.
# Risultato: l'interfaccia nasceva solo al 2o passaggio della catena, quando il mux l'aveva gia'
# creato MM — meccanismo giusto, posizione sbagliata.
# proto 'none' (non 'static'): con static netifd diventa PROPRIETARIO dell'L3 e al reload
# successivo (`ifup modem`) fa system_del_address = cancella IP e route della catena: MISURATO
# (`early=1` con `ping=0`), spiegato dalla ricerca. Con none netifd fa solo device_claim.
# Se serve l'IP visibile in LuCI: `address-external` (proto_init_update "$ifname" 1 1).
DE=$(ip -o -4 addr show 2>/dev/null | grep -E "qmapmux|rmnet_data" | head -1 | cut -d" " -f2)
if [ -n "$DE" ]; then
  uci -q set network.wan_early=interface
  uci -q set network.wan_early.proto=nx679j
  uci -q set network.wan_early.device="$DE"
  uci -q set network.wan_early.auto=1
  uci -q set network.wan_early.defaultroute=0
  uci commit network
  ubus call network reload >/dev/null 2>&1
  ifup wan_early >/dev/null 2>&1
  say "v113: interfaccia UCI 'wan_early' (proto none) su $DE — netifd non tocca l'L3"
else
  say "v113: nessun device dati trovato, interfaccia anticipata non creata"
fi
say "DONE"
