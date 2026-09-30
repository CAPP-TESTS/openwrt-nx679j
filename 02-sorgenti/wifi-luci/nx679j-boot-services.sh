#!/bin/sh
# nx679j-boot-services v3: attende ubus E il completamento del wifi-wrapper,
# poi lancia l'rcS. Motivo: l'rcS avviato durante il bring-up wifi (primi ~90s)
# fa crashare il SoC (reset hardware). Testato: rcS tardivo = stabile.
# v94: il waiter modem parte SUBITO (il suo polling e' gratis); la catena pesante resta
# comunque dopo /tmp/wifi-wrapper.done, che il waiter stesso attende prima di eseguirla.
if [ -x /usr/lib/nx679j/modem/nx679j-mm-watchdog.sh ] && [ ! -f /tmp/mm-waiter.pid ]; then
  setsid /usr/lib/nx679j/modem/nx679j-mm-watchdog.sh </dev/null >/dev/null 2>&1 &
  echo $$ > /tmp/mm-waiter.pid
  logger -s -t procd "boot-services: waiter modem avviato subito (v94)"
fi

n=0
while [ "$n" -lt 150 ]; do
  ubus call system board >/dev/null 2>&1 && break
  sleep 2
  n=$((n+1))
done
logger -s -t procd "ubus pronto dopo $((n*2))s"
# v104 ESPERIMENTO: rcS il prima possibile, senza aspettare il wrapper wifi (era fino a 300s).
# Guardia anti-boot-loop in slot 413: RC-EARLY n (n>=3 => torna prudente), RC-OK => resettato.
MK=/proc/1/root/dev/rd
RAW=$(dd if=$MK bs=32768 skip=413 count=1 2>/dev/null | head -c 24)
case "$RAW" in
  *RC-EARLY*) RN=${RAW##*RC-EARLY }; RN=${RN%% *}; RN=$((RN+1));;
  *) RN=1;;
esac
if [ "$RN" -ge 3 ]; then
  MAXW=150
  logger -s -t procd "v104: $((RN-1)) tentativi rcS-anticipato falliti -> attesa PIENA del wrapper"
else
  MAXW=${WIFI_MAX:-18}
  printf '%-23s' "RC-EARLY $RN" > /tmp/mk413
  dd if=/tmp/mk413 of=$MK bs=32768 seek=413 conv=sync,notrunc 2>/dev/null
  logger -s -t procd "v104: rcS ANTICIPATO (attesa wrapper max $((MAXW*2))s, tentativo $RN)"
fi
m=0
while [ "$m" -lt "$MAXW" ]; do
  if [ -f /tmp/wifi-wrapper.done ]; then break; fi
  sleep 2
  m=$((m+1))
done
if [ -f /tmp/wifi-wrapper.done ]; then
  logger -s -t procd "wifi-wrapper completato dopo $((m*2))s extra"
else
  logger -s -t procd "wifi-wrapper timeout ($((m*2))s extra), procedo"
fi
logger -s -t procd "margine sicurezza +10s prima dell'rcS (v94: era 60s; la finestra wifi e' gia' chiusa dal wrapper)"
sleep 10

# --- v130: ModemManager DISMESSO. Provato sul device: tenta il reset dell'interfaccia dati su
# rmnet_ipa0, va in thrash sul nodo QRTR e produce una pagina falsa (SIM 2 inesistente, 179 s).
# La rootfs e' volatile: si neutralizza ad OGNI boot, prima che rcS possa avviarlo.
# I file restano nel payload base, ma nulla li attiva (revert = togliere questo blocco).
for b in /usr/sbin/ModemManager /usr/sbin/ModemManager-monitor /usr/sbin/ModemManager-wrapper /usr/bin/mmcli; do
  [ -e "$b" ] && mv "$b" "$b.disabled" 2>/dev/null
done
/etc/init.d/modemmanager stop >/dev/null 2>&1
/etc/init.d/mm-netlink-watch stop >/dev/null 2>&1
# la voce UCI 'modem' era solo il contenitore di MM (ed e' quella che mostrava "Carrier: Absent")
uci -q delete network.modem && uci -q commit network
# e la sua pagina/menu: il nostro lo sostituisce (stesso titolo, ordine 30)
rm -f /usr/share/luci/menu.d/luci-proto-modemmanager.json \
      /www/luci-static/resources/view/modemmanager/status.js 2>/dev/null
logger -s -t procd "v130: ModemManager neutralizzato (binari .disabled, iface 'modem' rimossa)"
if [ -f /tmp/rcs-select ]; then
  logger -s -t procd "rcS: selezione presente"
  while read s; do
    if [ -x "/etc/rc.d/$s" ]; then
      "/etc/rc.d/$s" boot
    fi
  done < /tmp/rcs-select
  logger -s -t procd "rcS: selezione completata"
else
  if [ -f /tmp/rcs-full ]; then
    logger -s -t procd "rcS: avvio completo (richiesto)"
    /etc/init.d/rcS S boot
  else
    logger -s -t procd "rcS: default sicuro (chain+MM+dbus+uhttpd)"
    for s in S50uhttpd S60dbus S95nx679j-modem S94nx679j-display-touch; do
      if [ -x "/etc/rc.d/$s" ]; then
        "/etc/rc.d/$s" boot
      fi
    done
    logger -s -t procd "rcS: default completato (touch/display on-demand)"
# --- launcher RITARDATO touch+display (decoupled dalla rcS; dopo la finestra instabile)
if [ ! -f /tmp/no-display-late ] && [ -x /usr/lib/nx679j/modem/nx679j-display-late.sh ]; then
  setsid /usr/lib/nx679j/modem/nx679j-display-late.sh </dev/null >/dev/null 2>&1 &
  logger -s -t procd "rcS: launcher display-late avviato (touch+display a uptime ~3000)"
fi
  fi
fi

# v123: mm-watchdog e' l'unico owner della sequenza modem standard.
# Il watchdog e' gia' avviato in testa, conserva MSS recovery/staging/retry e lancia
# mm-standard-boot dopo il proprio gate. Un secondo lancio qui duplicava restart MM.
logger -s -t procd "v123: mm-standard-boot delegato al solo mm-watchdog"

# --- v90: watchdog modem (MSS rproc assente al boot -> rebind, senza reboot)
if [ -x /usr/lib/nx679j/modem/nx679j-mm-watchdog.sh ] && [ ! -f /tmp/mm-waiter.pid ]; then
  setsid /usr/lib/nx679j/modem/nx679j-mm-watchdog.sh </dev/null >/dev/null 2>&1 &
  logger -s -t procd "rcS: mm-watchdog avviato (v90)"
fi

# --- v130: supervisor del link: sorveglia il bearer (processo sessione + ICMP legato
# all'interfaccia), rinnova la sessione QMI, riapplica l'L3 e aggiorna la vista netifd.
# Sostituisce la funzione di reconnect che prestava ModemManager (connection.d/10-report-down).
if [ -x /usr/lib/nx679j/modem/nx679j-link-watch.sh ] && [ ! -f /tmp/link-watch.pid ]; then
  setsid /usr/lib/nx679j/modem/nx679j-link-watch.sh </dev/null >>/tmp/link-watch.log 2>&1 &
  echo $! > /tmp/link-watch.pid
  logger -s -t procd "v130: link-watch avviato (sorveglianza + rinnovo)"
fi

# v110: fix del bug LuCI "ReferenceError: View is not defined" (presente in 23.05/24.10/25.12,
# corretto upstream SOLO su master dal 2026-07-20, commit b27f781d13fc). E' il bug che fa
# fallire la pagina Interface (e quindi falsa cio' che si vede in LuCI, es. "carrier absent").
# La rootfs e' volatile -> si applica ad OGNI boot, come gli altri fix.
# v115: launcher display lanciato PRESTO, in parallelo — non piu' in serie dopo i servizi modem.
# MISURATO: come S94 partiva a ~94 s (rcS esegue S50..S71 prima) -> kiosk a ~127 s. Da qui parte
# a ~36 s e la gate interna (70 s) resta la protezione: NON si anticipa il momento in cui il
# display si accende, si elimina solo l'attesa della coda di rcS.
if [ -x /usr/lib/nx679j/modem/nx679j-display-late.sh ] && [ ! -f /tmp/no-display ]; then
  setsid /usr/lib/nx679j/modem/nx679j-display-late.sh </dev/null >/dev/null 2>&1 &
  logger -s -t procd "v115: launcher display avviato in anticipo (parallelo a rcS)"
fi

U=/www/luci-static/resources/ui.js
if [ -f "$U" ] && grep -q 'instanceof View' "$U" 2>/dev/null; then
  sed -i "s/!(view instanceof View)/typeof view?.render !== 'function'/" "$U" 2>/dev/null \
    && logger -s -t procd "v110: LuCI ui.js corretto (View is not defined)"
fi
rm -f /tmp/luci-indexcache* 2>/dev/null

# v104: se siamo arrivati qui, rcS (e quindi il SoC) ha retto: marca RC-OK e azzera il contatore
printf '%-23s' "RC-OK" > /tmp/mk413
dd if=/tmp/mk413 of=/proc/1/root/dev/rd bs=32768 seek=413 conv=sync,notrunc 2>/dev/null
logger -s -t procd "v104: boot-services completato senza reset -> RC-OK"
