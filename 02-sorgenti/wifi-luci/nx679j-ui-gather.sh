#!/bin/sh
# nx679j-ui-gather — raccoglie la superficie di controllo (parita' con LuCI) in
# un file piatto key=value, che la UI sul display legge.
#
# Nomi pensati per essere iterabili per prefisso:
#   svc.<nome>=<en>|<run>        servizi (rc list)
#   net.if.<nome>=<up>|<dev>|<ip> interfacce
#   wifi.<campo>=...             radio e SSID
#   log.<n>=... / klog.<n>=...   log di sistema / kernel
#   proc.<n>=<pid> <nome> <rss>
#   lease.<n>=<ip> <mac> <nome>
#   route.<n>=...
#   mnt.<n>=<dev> <punto> <usato>
#   set.<campo>=...              valori modificabili (uci) da mostrare
#
# Tutto in sola lettura: nessuna scrittura su uci/rete da qui.

# Campi modificabili dal display (tastiera a schermo). Il PRIMO campo del valore
# e' la chiave che la UI usa per scegliere il comando da eseguire.
ed() {
  printf 'ed.hostname=hostname %s\n' "$(uci -q get system.@system[0].hostname 2>/dev/null)"
  printf 'ed.ssid=ssid %s\n' "$(uci -q show wireless 2>/dev/null | sed -n "s/.*\.ssid='\([^']*\)'.*/\1/p" | head -1)"
  printf 'ed.key=key %s caratteri\n' "$(uci -q show wireless 2>/dev/null | sed -n "s/.*\.key='\([^']*\)'.*/\1/p" | head -1 | tr -d '\n' | wc -c)"
  printf 'ed.rootpw=rootpw (si imposta, non si legge)\n'
  printf 'ed.lanip=lanip %s\n' "$(uci -q get network.lan_wifi.ipaddr 2>/dev/null)"
  printf 'ed.netmask=netmask %s\n' "$(uci -q get network.lan_wifi.netmask 2>/dev/null)"
  printf 'ed.dhcp_start=dhcp_start %s\n' "$(uci -q get dhcp.lan.start 2>/dev/null)"
  printf 'ed.dhcp_limit=dhcp_limit %s\n' "$(uci -q get dhcp.lan.limit 2>/dev/null)"
  printf 'ed.dns=dns %s\n' "$(uci -q get dhcp.@dnsmasq[0].server 2>/dev/null | tr ' ' ',')"
}

# Attivita' pianificate (crontab). Il PRIMO campo e' il NUMERO DI RIGA nel file
# (non l'indice della lista): le righe vuote si saltano per la visualizzazione,
# ma la numerazione resta quella reale, cosi' modifica e cancellazione colpiscono
# la riga giusta.
# Lease statici (dhcp host). Primo campo: id sezione uci (per la cancellazione).
sh() {
  # La sezione si chiama @host[N] (NON host.name): la regex deve tenerne conto,
  # altrimenti la lista resta vuota anche con lease configurati.
  uci -q show dhcp 2>/dev/null | awk -F'[.=]' '
    /=host$/ { s=$2 }
    /@host.*\.name=/ { n=$0; sub(/.*=./,"",n); gsub(/\047/,"",n) }
    /@host.*\.ip=/   { i=$0; sub(/.*=./,"",i); gsub(/\047/,"",i);
                        printf "sh.%s=%s %s %s\n", s, s, n, i; s=""; n=""; i="" }
  '
}

# Hostname statici (dhcp @domain[N]): nome -> indirizzo, serviti da dnsmasq.
# Stessa trappola dei lease: la sezione e' @domain[N], la regex deve tenerne conto.
dn() {
  uci -q show dhcp 2>/dev/null | awk '
    /=domain$/ { s=$0; sub(/=domain$/,"",s); s=substr(s,6) }
    /@domain.*\.name=/ { n=$0; sub(/.*=./,"",n); gsub(/\047/,"",n) }
    /@domain.*\.ip=/   { i=$0; sub(/.*=./,"",i); gsub(/\047/,"",i);
                          printf "dn.%s=%s %s %s\n", s, s, n, i; s=""; n=""; i="" }
  '
}
# Fuso orario: lista CURATA (l'elenco completo e' di ~450 voci, inutilizzabile
# su questo schermo senza ricerca). La tzstring arriva da luci.getTimezones,
# quindi non e' inventata: nome e stringa vanno presi insieme.
tz() {
  ubus call luci getTimezones 2>/dev/null | tr -d '\t' | awk '
    /^ *"[A-Za-z]+\/[A-Za-z_+0-9-]+": \{/ { z=$1; gsub(/[":]/,"",z); next }
    /tzstring/ { s=$2; gsub(/[",]/,"",s);
      if (z=="UTC"||z=="Europe/Rome"||z=="Europe/London"||z=="Europe/Paris"||z=="Europe/Berlin"||z=="Europe/Madrid"||z=="Europe/Athens"||z=="Europe/Moscow"||z=="America/New_York"||z=="America/Chicago"||z=="America/Los_Angeles"||z=="America/Sao_Paulo"||z=="Asia/Tokyo"||z=="Asia/Shanghai"||z=="Asia/Kolkata"||z=="Asia/Dubai"||z=="Australia/Sydney"||z=="Africa/Cairo"||z=="Pacific/Auckland")
        printf "tz.%s=%s %s\n", z, z, s;
      z="" }
  '
}
cron() {
  n=0
  while IFS= read -r l; do
    n=$((n + 1))
    [ -n "$l" ] && printf 'cr.%s=%s %s\n' "$n" "$n" "$(printf '%s' "$l" | cut -c1-90)"
  done < /etc/crontabs/root 2>/dev/null
}
svc() {  ubus call rc list 2>/dev/null | tr -d ' \t\r' | awk '
    /^"[^"]+":\{/ { name=$0; sub(/^"/,"",name); sub(/":\{.*/,"",name); en="-"; run="0"; next }
    /"enabled":/  { en=(index($0,"true")>0)?"1":"0" }
    /"running":/  { run=(index($0,"true")>0)?"1":"0" }
    /^\},?$/      { if (name!="") {
                      st=(run=="1")?((en=="1")?"ON auto":"ON man"):((en=="1")?"off auto":"off");
                      printf "svc.%s=%s %s\n", name, name, st; name="" } }
  '
}

ifaces() {
  for i in $(ubus list 'network.interface.*' 2>/dev/null | sed 's/^network\.interface\.//'); do
    s=$(ubus call network.interface."$i" status 2>/dev/null | tr -d '\n\t ')
    up=$(printf '%s' "$s" | grep -o '"up":[a-z]*' | head -1 | cut -d: -f2)
    case "$up" in true) up=up ;; false) up=down ;; *) up="?" ;; esac
    dev=$(printf '%s' "$s" | sed -n 's/.*"l3_device":"\([^"]*\)".*/\1/p' | head -1)
    ip4=$(printf '%s' "$s" | grep -o '"ipv4-address":\[{"address":"[^"]*","mask":[0-9]*' | head -1 | sed 's/.*address":"//; s/","mask":/\//')
    printf 'net.if.%s=%s %s %s %s\n' "$i" "$i" "$up" "${dev:--}" "${ip4:--}"
  done
}

wifi() {
  st=$(ubus call network.wireless status 2>/dev/null | tr -d '\n\t ')
  r=$(printf '%s' "$st" | grep -o '"radio0":{"up":[a-z]*' | head -1)
  up=$(printf '%s' "$r" | grep -o '[a-z]*$')
  case "$up" in true) up=up ;; false) up=down ;; *) up="?" ;; esac
  dis=$(printf '%s' "$st" | grep -o '"disabled":[a-z]*' | head -1 | cut -d: -f2)
  ch=$(printf '%s' "$st" | grep -o '"channel":"[^"]*"' | head -1 | sed 's/.*:"//; s/"//')
  ssid=$(printf '%s' "$st" | grep -o '"ssid":"[^"]*"' | head -1 | sed 's/.*:"//; s/"//')
  printf 'wifi.state=%s\n' "$up"
  printf 'wifi.disabled=%s\n' "${dis:-?}"
  printf 'wifi.channel=%s\n' "${ch:--}"
  printf 'wifi.ssid=%s\n' "${ssid:--}"
  n=$(iwinfo phy0-ap0 assoclist 2>/dev/null | grep -c '^[0-9A-Fa-f][0-9A-Fa-f]:')
  printf 'wifi.clients=%s\n' "${n:-0}"
  sig=$(iwinfo phy0-ap0 info 2>/dev/null | sed -n 's/.*Signal: *//p' | head -1)
  printf 'wifi.signal=%s\n' "${sig:--}"
}

logs() {
  i=0
  logread 2>/dev/null | tail -30 | while IFS= read -r l; do
    i=$((i+1)); printf 'log.%s=%s\n' "$i" "$(printf '%s' "$l" | cut -c1-150)"
  done
  i=0
  dmesg 2>/dev/null | tail -20 | while IFS= read -r l; do
    i=$((i+1)); printf 'klog.%s=%s\n' "$i" "$(printf '%s' "$l" | cut -c1-150)"
  done
}

procs() {
  # busybox ps: PID USER VSZ STAT COMMAND  -> VSZ e' $3, il comando da $5 in poi
  ps 2>/dev/null | awk 'NR>1 && NF>=5 { printf "proc.%d=%s %s %s\n", NR-1, $1, $3, substr($0, index($0,$5)) }' | head -16
}

leases() {
  [ -s /tmp/dhcp.leases ] || return 0
  awk '{ printf "lease.%d=%s %s %s\n", NR, $3, $2, ($4=="*"?"-":$4) }' /tmp/dhcp.leases | head -20
}

routes() {
  ip -4 route show 2>/dev/null | awk '{ printf "route.%d=%s\n", NR, $0 }' | head -20
  printf 'net.gw=%s\n' "$(ip -4 route show default 2>/dev/null | awk '{print $3; exit}')"
}

mounts() {
  # df -h qui elenca solo la rootfs: nessun filtro per nome, si prende cio' che c'e'
  df -h 2>/dev/null | awk 'NR>1 && NF>=6 { printf "mnt.%d=%s %s %s/%s\n", NR, $1, $6, $3, $2 }' | head -8
}

settings() {
  v() { printf 'set.%s=%s\n' "$1" "$(uci -q get "$2" 2>/dev/null)"; }
  v hostname system.@system[0].hostname
  v tzname   system.@system[0].zonename
  v timezone system.@system[0].timezone
  v ntp      system.ntp.enabled
  v lan_ip   network.lan_wifi.ipaddr
  v lan_mask network.lan_wifi.netmask
  v wan_proto network.wan_early.proto
  # DHCP: sezione dnsmasq che serve la LAN (il nome della sezione non e' noto a priori)
  DH=$(uci -q show dhcp 2>/dev/null | sed -n "s/^dhcp\.\([^.=]*\)\.interface='\(lan_wifi\|lan\)'.*/\1/p" | head -1)
  printf 'set.dhcp_sec=%s\n' "${DH:--}"
  printf 'set.dhcp_on=%s\n' "$(uci -q get dhcp.$DH.ignore 2>/dev/null | sed 's/^1$/no/; s/^0$/si/; s/^$/si/')"
  printf 'set.dhcp_from=%s\n' "$(uci -q get dhcp.$DH.start 2>/dev/null)"
  printf 'set.dhcp_limit=%s\n' "$(uci -q get dhcp.$DH.limit 2>/dev/null)"
  printf 'set.dhcp_leasetime=%s\n' "$(uci -q get dhcp.$DH.leasetime 2>/dev/null)"
  printf 'set.hosts=%s\n' "$(uci -q show dhcp 2>/dev/null | grep -c "=host$")"
  printf 'set.dns=%s\n' "$(uci -q get network.wan_early.dns 2>/dev/null | tr ' ' ',' | cut -c1-60)"
  printf 'set.domain=%s\n' "$(uci -q get dhcp.@dnsmasq[0].domain 2>/dev/null)"
  printf 'set.wifi_ssid=%s\n' "$(uci -q show wireless 2>/dev/null | sed -n "s/.*\.ssid='\([^']*\)'.*/\1/p" | head -1)"
  printf 'set.wifi_key_len=%s\n' "$(uci -q show wireless 2>/dev/null | sed -n "s/.*\.key='\([^']*\)'.*/\1/p" | head -1 | tr -d '\n' | wc -c)"
  printf 'set.wifi_enc=%s\n' "$(uci -q show wireless 2>/dev/null | sed -n "s/.*\.encryption='\([^']*\)'.*/\1/p" | head -1)"
  printf 'set.wifi_disabled=%s\n' "$(uci -q get wireless.radio0.disabled 2>/dev/null)"
  printf 'set.fw=%s\n' "$(ubus call rc list 2>/dev/null | grep -A3 '"firewall"' | sed -n 's/.*"running": *\([a-z]*\).*/\1/p' | head -1)"
  printf 'set.rc_local=%s\n' "$([ -s /etc/rc.local ] && wc -l < /etc/rc.local || echo 0)"
  printf 'set.crontab=%s\n' "$(crontab -l 2>/dev/null | grep -cv '^#' || echo 0)"
  printf 'set.rootpw=%s\n' "$(awk -F: '/^root:/{print ($2=="!"||$2=="*"||$2=="") ? "assente" : "impostata"}' /etc/shadow 2>/dev/null)"
  printf 'set.time=%s\n' "$(date +%H:%M:%S)"
  printf 'set.date=%s\n' "$(date +%Y-%m-%d)"
  printf 'set.upgrade=%s\n' "$(cat /etc/openwrt_release 2>/dev/null | sed -n "s/^DISTRIB_DESCRIPTION='\(.*\)'/\1/p" | cut -c1-40)"
}

cron
svc
sh
dn
tz
ifaces
wifi
logs
procs
leases
routes
mounts
settings

ed
