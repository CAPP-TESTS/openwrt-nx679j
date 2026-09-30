#!/bin/sh
# nx679j-ui-fetch — alimenta la UI sul display con un file piatto key=value.
#
# Perche' un file e non chiamate dirette: la UI deve mantenere i commit DRM
# continui (il pannello collassa se resta >58 ms senza un commit). Ogni
# chiamata ubus/qmicli dura centinaia di ms e bloccherebbe il loop. Cosi'
# invece la UI legge un file (microsecondi) e non si ferma mai.
#
# Scrive atomico (file temporaneo + mv): la UI non vede mai un file a meta'.
#
# Lo stato del modem costa ~1 s (5 chiamate qmicli) e viene rinfrescato un
# giro su tre. Va tenuto in una CACHE separata: se lo scrivessi solo quando
# lo aggiorno, il giro successivo lo cancellerebbe dal file finale.
D=/tmp/ui-data.txt
T=/tmp/ui-data.txt.tmp
MC=/tmp/ui-modem-cache.txt

# JSON compatto -> righe key=value, prefisso dato.
flat() {
  tr ',' '\n' | sed 's/[{}\"]//g; s/\\//g' | while IFS=: read -r k v; do
    k=$(printf '%s' "$k" | tr -d ' \t\r')
    [ -n "$k" ] && printf '%s.%s=%s\n' "$1" "$k" "$(printf '%s' "$v" | sed 's/^[ \t]*//; s/[ \t]*$//')"
  done
}

iface() {
  s=$(ubus call network.interface."$1" status 2>/dev/null | tr -d '\n\t ')
  up=$(printf '%s' "$s" | grep -o '"up":[a-z]*' | head -1 | cut -d: -f2)
  case "$up" in true) up=up ;; false) up=down ;; *) up=? ;; esac
  dev=$(printf '%s' "$s" | sed -n 's/.*"l3_device":"\([^"]*\)".*/\1/p' | head -1)
  ip4=$(printf '%s' "$s" | grep -o '"ipv4-address":\[{"address":"[^"]*","mask":[0-9]*' | head -1 | sed 's/.*address":"//; s/","mask":/\//')
  printf 'if.%s=%s|%s|%s\n' "$1" "$up" "${dev:--}" "${ip4:--}"
}

# Il primo thermal_zone che risponde con un numero plausibile; i vendor
# Qualcomm non leggono tutti allo stesso modo (alcuni danno EINVAL).
temp() {
  for z in /sys/class/thermal/thermal_zone*/temp; do
    t=$(cat "$z" 2>/dev/null) || continue
    case "$t" in ''|*[!0-9]*) continue ;; esac
    [ "$t" -gt 5000 ] && [ "$t" -lt 120000 ] && { echo $((t / 1000)); return; }
  done
}

n=0
while :; do
  if [ $((n % 3)) -eq 0 ]; then
    ubus call luci.nx679j-modem getStatus 2>/dev/null | flat modem \
      | sed 's/^modem.revision=\([^ ]*\).*/modem.revision=\1/' > "$MC.tmp" 2>/dev/null
    [ -s "$MC.tmp" ] && mv "$MC.tmp" "$MC"
  fi
  {
    printf 'sys.uptime=%s\n' "$(cut -d. -f1 /proc/uptime)"
    printf 'sys.load=%s\n' "$(cut -d' ' -f1-3 /proc/loadavg)"
    printf 'sys.mem=%s MB\n' "$(awk '/MemAvailable/{printf "%d", $2/1024}' /proc/meminfo 2>/dev/null)"
    printf 'sys.temp=%s\n' "$(temp)"
    printf 'modem.health=%s\n' "$(cat /tmp/link-health 2>/dev/null | tr -d '\n' | cut -c1-90)"
    printf 'ui.tick=%s\n' "$n"
    iface lan_wifi
    iface wan_early
    # Tutta la superficie di controllo (servizi, interfacce, wifi, log, processi,
    # lease, rotte, mount, impostazioni) — parita' con la GUI web.
    [ -x /usr/lib/nx679j/modem/nx679j-ui-gather.sh ] && sh /usr/lib/nx679j/modem/nx679j-ui-gather.sh 2>/dev/null
    # Cella e carrier aggregation (QMI, sola lettura: verificato che non
    # disturba la sessione dati). Ogni 10 cicli (~30 s): sono valori lenti e
    # ogni query e' un accesso al modem.
    if [ $((n % 10)) -eq 0 ]; then
      [ -x /usr/lib/nx679j/modem/nx679j-cell.sh ] && sh /usr/lib/nx679j/modem/nx679j-cell.sh 2>/dev/null > /tmp/ui-cell.txt
    fi
    [ -s /tmp/ui-cell.txt ] && cat /tmp/ui-cell.txt
    cat "$MC" 2>/dev/null
  } > "$T" 2>/dev/null
  mv "$T" "$D"
  n=$((n + 1))
  sleep 3
done
