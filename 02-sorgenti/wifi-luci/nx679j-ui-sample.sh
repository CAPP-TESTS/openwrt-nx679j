#!/bin/sh
# nx679j-ui-sample.sh — un campione in una riga, per il monitoraggio dal host.
#
# Stampa: U|ui_vivi|fps|release|retire|gap|irq|ping
#
# Perche' le due fence SEPARATE e non "l'ultima riga del file": fence_status ha
# piu' sezioni (release sul crtc, retire sul connector) e prenderne una a caso
# produce letture non interpretabili.
#   release = il commit e' stato accettato
#   retire  = il frame e' stato DAVVERO ritirato dal pannello (e' questa che
#             dice se lo schermo sta mostrando qualcosa di nuovo)
F=/sys/kernel/debug/dri/0/crtc152/fence_status

# processi UI VIVI (esclude gli zombie, che pidof conta e che hanno ingannato
# piu' di una diagnosi)
vivi=0
for p in $(pidof nx679j-ui); do
  st=$(awk '{print $3}' /proc/$p/stat 2>/dev/null)
  [ "$st" = Z ] || vivi=$((vivi+1))
done

ref=$(sed -n '/===Release fence===/{n;s/.*done_count:\([0-9]*\) commit_count:\([0-9]*\).*/\1\/\2/;p;}' $F)
ret=$(sed -n '/===Retire fence===/{n;s/.*done_count:\([0-9]*\) commit_count:\([0-9]*\).*/\1\/\2/;p;}' $F)
fps=$(sed -n 's/.*fps: *//p' /sys/kernel/debug/dri/0/crtc152/fps)
gap=$(tail -c 80 /tmp/display-late.log | sed -n 's/.*gap_max_ms=\([0-9]*\).*/\1/p')
irq=$(awk '/msm_drm/{printf "%s", $2}' /proc/interrupts)
ping -c1 -W2 8.8.8.8 >/dev/null 2>&1 && pg=PING_OK || pg=PING_KO

printf '%s|%s|%s|%s|%s|%s|%s|%s\n' "$(cut -d. -f1 /proc/uptime)" "$vivi" "$fps" "$ref" "$ret" "$gap" "$irq" "$pg"
