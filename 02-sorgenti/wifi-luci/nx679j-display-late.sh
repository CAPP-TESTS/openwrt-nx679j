#!/bin/sh
# nx679j-display-late — bring-up RITARDATO di touch+display.
# Chiamato da boot-services, detached, post-rcS.
# RICETTA VERIFICATA (21/9, v76): moduli touch -> attendere il BIND del goodix
# (input nubia compare, ~15-30s) -> gap di sicurezza -> touchpaint.
# ANTI-BOOT-LOOP: marker rawdump slot 410 (DL-FIRED senza DL-CLEAN = crash
# al tentativo precedente -> skip; per riprovare azzerare lo slot 410).
# Env: DISPLAY_LATE_X (soglia uptime, default 900), DISPLAY_LATE_GAP (default 60).
# Catena di moduli vendor (tasto, alimentazione, Type-C): la chroot non ha
# autoload, quindi li carichiamo noi. Vedi references/vendor-modules.md
[ -x /usr/lib/nx679j/modem/nx679j-modules.sh ] && sh /usr/lib/nx679j/modem/nx679j-modules.sh

X=${DISPLAY_LATE_X:-70}
GAP=${DISPLAY_LATE_GAP:-5}
LOG=/tmp/display-late.log
say() { echo "$(date +%H:%M:%S 2>/dev/null || echo t) $*" >> $LOG; tail -c 30000 $LOG 2>/dev/null > /tmp/bl411; dd if=/tmp/bl411 of=/proc/1/root/dev/rd bs=32768 seek=411 conv=sync,notrunc 2>/dev/null; }
mark() { printf '%-63s' "DL-$1" > /tmp/mk410; dd if=/tmp/mk410 of=/proc/1/root/dev/rd bs=32768 seek=410 conv=sync,notrunc 2>/dev/null; }

st=$(dd if=/proc/1/root/dev/rd bs=32768 skip=410 count=1 2>/dev/null | head -c 40)
case "$st" in
  *DL-FIRED*) case "$st" in
      *DL-CLEAN*) : ;;
      *) # v107: NON bloccare piu' il display al primo fallimento. Il marker serve contro il CRASH
         # del launcher, non contro un boot brutto: un solo boot andato male lasciava lo schermo
         # nero anche al boot successivo. Si riprova fino a 3 tentativi, poi ci si arrende.
         case "$st" in
           *DL-RETRY*3|*DL-RETRY*4|*DL-RETRY*5|*DL-RETRY*6|*DL-RETRY*7|*DL-RETRY*8|*DL-RETRY*9)
             say "MARKER: 3+ tentativi falliti -> Skip."; exit 0 ;;
           *DL-RETRY*) N=$(echo "$st" | sed 's/.*DL-RETRY \([0-9]\).*/\1/'); N=$((N+1)) ;;
           *) N=1 ;;
         esac
         say "MARKER DL-FIRED senza CLEAN (tentativo $N): riprovo invece di bloccarmi."
         mark "RETRY $N" ;;
    esac ;;
esac

# v115: guardia di istanza singola. Il launcher ora viene lanciato PRESTO dai boot-services, ma
# S94 lo lancia comunque: senza questa guardia due istanze farebbero partire la sequenza due volte
# (e la seconda, vedendo il marker FIRED, farebbe un retry per la logica v107).
if [ -f /tmp/display-late.pid ] && kill -0 "$(cat /tmp/display-late.pid 2>/dev/null)" 2>/dev/null; then
  say "altra istanza gia' attiva (pid $(cat /tmp/display-late.pid 2>/dev/null)): esco"
  exit 0
fi
echo $$ > /tmp/display-late.pid

say "armato: attendo uptime >= $X"
# v114: polling ogni 2s invece di 15s. MISURATO: con gate 70 il launcher partiva a uptime 94
# (controlli a 79 -> 94) -> ~24s di ritardo che NON erano un'attesa voluta ma la granularita' del
# polling. Costa 8 iterazioni/s in piu', zero rischio.
N=0
while [ $N -lt 3000 ]; do
  U=$(cut -d. -f1 /proc/uptime 2>/dev/null)
  [ -n "$U" ] && [ "$U" -ge "$X" ] && break
  sleep 2; N=$((N+1))
done
say "uptime $(cut -d. -f1 /proc/uptime): parto"
mark "FIRED $(cat /proc/sys/kernel/random/boot_id 2>/dev/null | head -c 8)"

mkdir -p /dev/dri /dev/input
[ -e /dev/dri/card0 ] || mknod /dev/dri/card0 c 226 0
[ -e /dev/dri/renderD128 ] || mknod /dev/dri/renderD128 c 226 128
[ -e /dev/input/event0 ] || mknod /dev/input/event0 c 13 64

# v114: sleep 2 -> 1 per modulo (8s -> 4s). I moduli si caricano in millisecondi: il sleep era
# prudenza, non necessita'. Se un modulo fallisce lo dice comunque il log.
M=/proc/1/root/lib/modules
for m in panel_event_notifier gpi i2c-msm-geni goodix_core; do
  if [ -e "$M/$m.ko" ]; then
    /usr/lib/nx679j/modem/finitmod "$M/$m.ko" 2>>$LOG && say "  $m ok" || say "  $m FAIL"
    sleep 1
  else
    say "  $m assente"
  fi
done

# v114: attese con passo 2s invece di 5s (granularita' fino a 10s -> 4s).
B=0
while [ $B -lt 150 ]; do
  [ "$(grep -c nubia /proc/bus/input/devices 2>/dev/null)" = "1" ] && break
  sleep 2; B=$((B+1))
done
say "bind goodix: $(grep -c nubia /proc/bus/input/devices 2>/dev/null) (dopo $((B*2))s)"
# attesa fine init asincrona (esd on nel dmesg)
E=0
while [ $E -lt 150 ]; do
  dmesg | grep -q "esd on" && break
  sleep 2; E=$((E+1))
done
say "esd: $(dmesg | grep -c \"esd on\") (dopo $((E*2))s)"

# gap di sicurezza (verificato: load->bind->gap->touchpaint = OK)
sleep $GAP

# === LuCI: proto STANDARD modemmanager (2026-09-22) ===
# Dal pacchetto luci-proto-modemmanager (upstream openwrt/luci @128a7812).
# File master in /usr/lib/nx679j/modem/luci-mm (persistente); /www e' in RAM.
L=/usr/lib/nx679j/modem/luci-mm
if [ -d "$L" ]; then
  mkdir -p /www/luci-static/resources/view/modemmanager /usr/share/luci/menu.d /usr/share/rpcd/acl.d
  [ -f /www/luci-static/resources/modemmanager_helper.js ] || cp "$L/modemmanager_helper.js" /www/luci-static/resources/
  [ -f /www/luci-static/resources/protocol/modemmanager.js ] || cp "$L/modemmanager.js" /www/luci-static/resources/protocol/
  [ -f /www/luci-static/resources/view/modemmanager/status.js ] || cp "$L/view/modemmanager/status.js" /www/luci-static/resources/view/modemmanager/
  [ -f /usr/share/luci/menu.d/luci-proto-modemmanager.json ] || cp "$L/menu.json" /usr/share/luci/menu.d/luci-proto-modemmanager.json
  [ -f /usr/share/rpcd/acl.d/luci-proto-modemmanager.json ] || cp "$L/acl.json" /usr/share/rpcd/acl.d/luci-proto-modemmanager.json
  /etc/init.d/rpcd reload 2>/dev/null
  say "LuCI: proto standard modemmanager installato"
fi
# NOTA (2026-09-22): proto custom nx679j RIMOSSO -> si usa il proto STANDARD
# 'modemmanager' (netifd+MM). Vedi ricetta in skill android-boot-chain-recovery.

# === ESD KILL (2026-09-22: LA CHIAVE DEL SUCCESSO) ===
# Senza questo, ogni setcrtc/flip viene seguito da mismatch 0x0A -> esd recovery -> RESET HW.
# In questo build i modi si scrivono SUL file esd_check_mode: esd_sw_sim_success fa
# simulare SEMPRE successo al check -> niente reset -> il setcrtc passa e i pixel si vedono.
mount -t debugfs none /sys/kernel/debug 2>/dev/null
# v89: disarma il panic SDE (sde_dbg panic_on_err=1 -> 0): un timeout display NON riavvia piu' il telefono.
PN=/sys/kernel/debug/dri/0/debug/panic
if [ -w "$PN" ]; then echo 0 > "$PN" 2>/dev/null; say "panic SDE disarmato: $(cat $PN 2>/dev/null)"; else say "nodo panic assente/non scrivibile"; fi
PE=/sys/kernel/debug/qcom,mdss_dsi_r6130_1080_2400_amoled_cmd
if [ -e "$PE/esd_check_mode" ]; then
  echo esd_sw_sim_success > "$PE/esd_check_mode" && say "ESD KILL ok ($PE)"
else
  say "ESD KILL: debugfs panel dir assente ($PE)"
fi

# DISPLAY: DRMTEST con hold lungo (fa setcrtc legacy + pattern; VERIFICATO 22/9:
# 'variante 0 1080x2400x90cmd: OK!' al primo colpo con ESD killato, pixel a schermo).
# Il touch resta attivo (input0): per DISEGNARE usare a mano il touchpaint.
# v88: CLIENT DI BOOT PREFERITO = kiosk3 (flip atomico NONBLOCK continuo).
# Root cause: il kernel live e' pre-fix "PLL prima del parent RCG" => se il display entra in
# idle power collapse (58 ms) il link NON si rianima piu' (RCG CMD_UPDATE non ackato).
# Un client che committa di continuo non lascia mai collassare il link.
# Fallback: drmtest (percorso noto-buono, immagine statica). Disattivabile con /tmp/no-kiosk3.
KIOSK3=/usr/lib/nx679j/modem/nx679j-kiosk3
# v135: UI INTERATTIVA sul display, al posto di kiosk3. Stesse regole vincolanti:
# parte QUI, al boot (nessun takeover a caldo quando il master non c'e'), e
# committa di continuo per non far collassare il link DSI. kiosk3 resta il
# fallback: se la UI muore, la sua catena riprende il display.
UI=/usr/lib/nx679j/modem/nx679j-ui
UIF=/usr/lib/nx679j/modem/nx679j-ui-fetch.sh
# Orologio: senza RTC il sistema parte al 1970 e sysntpd non sempre riesce a
# partire al boot (il DNS non e' ancora pronto) -> l'ora resta sbagliata e si
# vede sul display. Qui siamo dopo la catena, quindi il DNS c'e' di sicuro.
if [ -x /etc/init.d/sysntpd ] && ! pidof ntpd >/dev/null 2>&1; then
  say "orologio: avvio sysntpd (ora $(date '+%Y-%m-%d %H:%M'))"
  /etc/init.d/sysntpd start >/dev/null 2>&1
  sleep 12
  say "orologio: ora $(date '+%Y-%m-%d %H:%M')"
fi
# Nodo uinput: serve a uinput-touch per iniettare un tocco sintetico (verifica da
# remoto del percorso tocco->azione). Non esiste come node di default in /dev.
[ -e /dev/uinput ] || mknod /dev/uinput c 10 223 2>/dev/null
# idle_pc_disable: il link DSI collassa se resta >58 ms senza commit e NON si
# rianima piu' (serve reboot). Con questa proprieta' attiva il pannello sopravvive
# alle pause: verificato con esperimento controllato (stesso kill+20 s: con la
# proprieta' 59.9 fps, senza -> 200 fence in ritardo). Va creata PRIMA della UI.
touch /tmp/ui-idle-pc-disable
if [ -x "$UI" ] && [ ! -f /tmp/no-ui ]; then
  if ! pidof nx679j-ui >/dev/null 2>&1; then
    pidof nx679j-ui-fetch.sh >/dev/null 2>&1 || setsid "$UIF" >>$LOG 2>&1 </dev/null &
    setsid "$UI" >>$LOG 2>&1 </dev/null &
    K=0; while [ $K -lt 20 ] && ! pidof nx679j-ui >/dev/null 2>&1; do sleep 1; K=$((K+1)); done
    if pidof nx679j-ui >/dev/null 2>&1; then
      say "UI ATTIVA (nx679j-ui: stato e comandi sul display)"; mark "CLEAN ui-uptime=$(cut -d. -f1 /proc/uptime)"
    else
      say "nx679j-ui morto -> fallback kiosk3"; mark "CLEAN ui-morto"
    fi
  else
    say "nx679j-ui gia' attivo"; mark "CLEAN ui-gia-attivo"
  fi
fi

if [ -x "$KIOSK3" ] && [ ! -f /tmp/no-kiosk3 ] && ! pidof nx679j-ui >/dev/null 2>&1; then
  if ! pidof nx679j-kiosk3 >/dev/null 2>&1; then
    setsid "$KIOSK3" >>$LOG 2>&1 </dev/null &
    # v118: qui c'era `sleep 20` FISSO — 20 s di attesa pura, misurati fra "panic SDE disarmato"
    # e "DISPLAY ACCESO" (00:28:30 -> 00:28:50). Il kiosk3 parte in ~1 s: si POLLA invece di
    # aspettare a tempo fisso (cap 20 s come prima, ma esce appena il processo e' vivo).
    K=0; while [ $K -lt 20 ] && ! pidof nx679j-kiosk3 >/dev/null 2>&1; do sleep 1; K=$((K+1)); done
    if pidof nx679j-kiosk3 >/dev/null 2>&1; then
      say "DISPLAY ACCESO (kiosk3: flip NONBLOCK continuo)"; mark "CLEAN kiosk3-uptime=$(cut -d. -f1 /proc/uptime)"
    else
      say "kiosk3 morto -> fallback drmtest"; mark "CLEAN kiosk3-morto"
    fi
  else
    say "kiosk3 gia' attivo"; mark "CLEAN kiosk3-gia-attivo"
  fi
fi

if ! pidof nx679j-kiosk3 >/dev/null 2>&1; then
if [ -x /usr/lib/nx679j/modem/nx679j-drmtest ]; then
  if ! pidof nx679j-drmtest >/dev/null 2>&1; then
    setsid /usr/lib/nx679j/modem/nx679j-drmtest 86400 >>$LOG 2>&1 </dev/null &
    sleep 15
    if pidof nx679j-drmtest >/dev/null 2>&1; then
      say "DISPLAY ACCESO (drmtest hold=86400)"; mark "CLEAN uptime=$(cut -d. -f1 /proc/uptime)"
    else
      say "drmtest morto (nessun crash rilevato)"; mark "CLEAN drmtest-morto"
    fi
  else
    say "drmtest gia' attivo"; mark "CLEAN gia-attivo"
  fi
fi
fi
say "fine"
