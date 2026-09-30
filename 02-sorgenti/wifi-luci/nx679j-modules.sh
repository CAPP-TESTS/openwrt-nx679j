#!/bin/sh
# nx679j-modules — carica la catena di moduli vendor che il nostro boot non carica.
# Nella chroot nessuno fa autoload (manca l'helper uevent): senza questi moduli il
# tasto, l'alimentazione e il Type-C restano senza driver e SEMBRANO non supportati.
# Ordine e dipendenze verificati sul device (dmesg del probe PON).
KM=/usr/lib/nx679j/modem/kmod
log() { echo "moduli: $*"; }
c() {
  n=$(echo "$1" | tr '-' '_')
  lsmod 2>/dev/null | grep -q "^$n " && return 0
  [ -f "$KM/$1.ko" ] || { log "manca $1.ko"; return 1; }
  insmod "$KM/$1.ko" 2>/dev/null && log "$1 ok"
}
# Catena PON: crea i figli del PMIC (pwrkey = tasto laterale, resin = volume)
c qcom-pon
c pm8941-pwrkey
c pmic-pon-log
# Catena glink (alimentazione + Type-C): SOLO caricamento, NESSUNA ricarica.
# ATTENZIONE (lezione 2026-09-25): ricaricare pmic_glink a sistema avviato ha
# abbattuto la sessione dati del modem (rmnet_data0 senza indirizzo, ping KO).
# Il caricabatterie non espone comunque /sys/class/power_supply su questo setup,
# quindi la ricarica era inutile E dannosa. Se serve un giorno, si fa a mano.
c qti_battery_charger
c charger-ulog-glink
c ucsi_glink

# Nodi /dev/input per i dispositivi reali: niente udev nella chroot, e i numeri
# di device si LEGGONO dal kernel (mai stimati - lezione del 2026-09-25).
for e in /sys/class/input/event*; do
  [ -e "$e" ] || continue
  n=$(basename "$e"); d=$(cat "$e/dev" 2>/dev/null)
  [ -n "$d" ] || continue
  maj=${d%:*}; min=${d#*:}
  [ -e "/dev/input/$n" ] || mknod "/dev/input/$n" c "$maj" "$min" 2>/dev/null
done
log "input: $(ls /dev/input/ 2>/dev/null | tr '\n' ' ')"
