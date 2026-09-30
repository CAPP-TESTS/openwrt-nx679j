#!/bin/sh
# v91: ASPETTA il remoteproc MSS (si registra tardivamente) e POI (ri)lancia la sequenza modem standard.
LOG=/tmp/mm-wd.log
say(){ echo "$(date +%H:%M:%S 2>/dev/null||echo t) $*" >>$LOG; }
D=4080000.remoteproc-mss
has(){ grep -ql mss /sys/class/remoteproc/*/name 2>/dev/null; }
say "attendo il rproc MSS (uptime $(cut -d. -f1 /proc/uptime))"
i=0
while [ $i -lt 280 ]; do
  has && { say "MSS disponibile a uptime $(cut -d. -f1 /proc/uptime)s"; break; }
  # v103: se il remoteproc MSS non c'e' dopo 30s, tenta il rebind SUBITO (unica cura in-place
  # nota). Prima si aspettavano 560s prima di provarci: un boot intero senza modem.
  if [ $i -eq 15 ] || [ $i -eq 45 ] || [ $i -eq 75 ]; then
    P=$(readlink -f /sys/bus/platform/devices/$D/driver 2>/dev/null)
    if [ -d "$P" ]; then
      say "v103: MSS assente a uptime $(cut -d. -f1 /proc/uptime)s -> rebind"
      echo $D > $P/unbind 2>>$LOG; sleep 2; echo $D > $P/bind 2>>$LOG
    else
      say "v103: driver MSS non trovato (rebind impossibile)"
    fi
  fi
  sleep 2; i=$((i+1))
done
# v95: catena SUBITO appena l'MSS e' pronto (~37s) invece di aspettare il wrapper wifi (~83s).
# Guardia anti-boot-loop: se due boot consecutivi tentano l'early e il modem non arriva,
# si torna automaticamente alla modalita' prudente (attesa del wrapper).
MK=/proc/1/root/dev/rd
RAW=$(dd if=$MK bs=32768 skip=412 count=1 2>/dev/null | head -c 24)
case "$RAW" in
  *MM-EARLY*) N=${RAW##*MM-EARLY }; N=${N%% *}; N=$((N+1));;
  *) N=1;;
esac
if [ "$N" -ge 3 ]; then
  say "v95: $((N-1)) tentativi early senza modem -> PRUDENTE (attendo il wrapper)"
  while [ ! -f /tmp/wifi-wrapper.done ]; do sleep 2; done
else
  printf '%-23s' "MM-EARLY $N" > /tmp/mk412
  dd if=/tmp/mk412 of=$MK bs=32768 seek=412 conv=sync,notrunc 2>/dev/null
  say "v95: EARLY tentativo $N (catena subito, uptime $(cut -d. -f1 /proc/uptime))"
  # v97: replica lo STAGING di /etc/init.d/nx679j-modem (symlink toolkit -> /tmp, ~85s) e poi
  # avvia la catena in anticipo. Senza lo staging la catena a 37s non ha gli helper.
  if [ ! -f /tmp/chain-early.started ]; then
    : > /tmp/chain-early.started
    mkdir -p /tmp /var/run/nx679j /rfs/readwrite/ota_firewall /var/lib/tqftpserv
    for f in /usr/lib/nx679j/modem/*; do b=${f##*/}; [ -e "/tmp/$b" ] || ln -s "$f" "/tmp/$b" 2>/dev/null; done
    say "v97: staging in /tmp fatto ($(ls /tmp/*.sh 2>/dev/null | wc -l) symlink), avvio catena"
    setsid /tmp/chain.sh >>/tmp/chain-early.log 2>&1 &
  fi
fi
if has; then
  say "MSS presente a uptime $(cut -d. -f1 /proc/uptime)s"
else
  say "MSS assente dopo ${i}0s: rebind"
  P=$(readlink -f /sys/bus/platform/devices/$D/driver 2>/dev/null)
  [ -d "$P" ] && { echo $D > $P/unbind 2>>$LOG; sleep 5; echo $D > $P/bind 2>>$LOG; sleep 20; }
  has || say "MSS ANCORA ASSENTE (la catena fallira')"
fi
# v130: ModemManager rimosso — niente mm-standard-boot.
# L'L3 lo porta la catena (proto nx679j su wan_early) e lo stato lo serve luci.nx679j-modem.
say "v130: mm-standard-boot NON invocato (ModemManager dismesso)"
say "fine: sessione=$(pidof qmi-qrtr-observed 2>/dev/null | wc -w) iface=$(ip -o -4 addr show 2>/dev/null | grep -cE 'qmapmux|rmnet_data')"
# v130: il segnale di salute e' la sessione QMI, non un oggetto di ModemManager.
if [ "$(pidof qmi-qrtr-observed 2>/dev/null | wc -w)" -ge 1 ]; then
  printf '%-23s' "LINK-OK" > /tmp/mk412
  dd if=/tmp/mk412 of=/proc/1/root/dev/rd bs=32768 seek=412 conv=sync,notrunc 2>/dev/null
  say "v130: sessione QMI attiva -> contatore early azzerato"
fi