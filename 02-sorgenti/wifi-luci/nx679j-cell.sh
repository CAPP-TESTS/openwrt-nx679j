#!/bin/sh
# nx679j-cell — dati della CELLA da QMI, sola lettura.
# NOTA: sul device NON esiste il comando `timeout` (busybox senza timeout):
# se lo si usa, le query falliscono in silenzio. Le interrogazioni rispondono
# in meno di un secondo; se il modem non risponde, qmicli esce da solo.
# Verificato: queste query NON disturbano la sessione dati attiva
# (indirizzo e ping invariati dopo 3 interrogazioni).
# Emette key=value piatte, come il resto del raccoglitore.
D=qrtr://0
RB=$(qmicli -d $D --nas-get-rf-band-info 2>&1)
CI=$(qmicli -d $D --nas-get-cell-location-info 2>&1)
SI=$(qmicli -d $D --nas-get-signal-info 2>&1)

campo() { sed -n "s/.*$1: *'\([^']*\)'.*/\1/p" | head -1; }

# banda attiva + larghezza (la CA attiva farebbe comparire piu' "Band Information")
echo "cell.banda=$(printf '%s\n' "$RB" | campo 'Active Band Class')"
echo "cell.bw=$(printf '%s\n' "$RB" | campo 'Bandwidth')"
# cella servente
echo "cell.earfcn=$(printf '%s\n' "$CI" | campo 'EUTRA Absolute RF Channel Number')"
echo "cell.pci=$(printf '%s\n' "$CI" | campo 'Physical Cell ID')"
echo "cell.rsrp=$(printf '%s\n' "$CI" | campo 'RSRP')"
echo "cell.rsrq=$(printf '%s\n' "$CI" | campo 'RSRQ')"
echo "cell.rssi=$(printf '%s\n' "$CI" | campo 'RSSI')"
echo "cell.snr=$(printf '%s\n' "$SI" | campo 'SNR')"
echo "cell.gid=$(printf '%s\n' "$CI" | campo 'Global Cell ID')"
echo "cell.tac=$(printf '%s\n' "$CI" | campo 'Tracking Area Code')"
echo "cell.plmn=$(printf '%s\n' "$CI" | campo 'PLMN')"
echo "cell.ta=$(printf '%s\n' "$CI" | campo 'Timing Advance')"
# vicina intrafrequenza (2a cella) e interfrequenza (banda 7 vista ma non aggregata)
echo "cell.vicina=$(printf '%s\n' "$CI" | sed -n '/Cell \[1\]/,/Cell \[2\]/p' | grep -E 'Physical Cell ID|RSRP' | tr -d "\t" | tr '\n' ' ' | sed 's/  */ /g')"
echo "cell.inter=$(printf '%s\n' "$CI" | sed -n '/Interfrequency LTE Info/,/Interfrequency LTE Info$/p' | grep -m2 -E 'EUTRA Absolute|RSRP' | tr -d "\t" | tr '\n' ' ' | sed 's/  */ /g')"

# ---- carrier aggregation (qmicli collection full) ----
CA=$(qmicli -d $D --nas-get-lte-cphy-ca-info 2>&1)
blocco() { printf '%s\n' "$CA" | sed -n "/$1/,/$2/p" | sed -n "s/.*$3: *'\([^']*\)'.*/\1/p" | head -1; }
rsrp_di_pci() {
    printf '%s\n' "$CI" | awk -v p="$1" '
        /Physical Cell ID:/ { c=$0; sub(/.*: */,"",c); gsub(/[^0-9]/,"",c) }
        /RSRP:/ { if (c==p) { r=$0; sub(/.*: */,"",r); gsub(/[^0-9.\-]/,"",r); print r; exit } }'
}
echo "ca.totali=$(printf '%s\n' "$CA" | grep -c 'Secondary Cell .* Info')"
echo "ca.attive=$(printf '%s\n' "$CA" | grep -c 'State: .activated.')"
echo "ca.dl=$(printf '%s\n' "$CA" | campo 'DL Bandwidth')"
echo "ca.pcc.band=$(blocco 'Primary Cell Info' 'Secondary Cell' 'LTE Band')"
echo "ca.pcc.earfcn=$(blocco 'Primary Cell Info' 'Secondary Cell' 'RX Channel')"
echo "ca.pcc.pci=$(blocco 'Primary Cell Info' 'Secondary Cell' 'Physical Cell ID')"
PCCBW=$(blocco 'Primary Cell Info' 'Secondary Cell' 'DL Bandwidth')
echo "ca.pcc.bw=$PCCBW"
echo "ca.scc1.band=$(blocco 'Secondary Cell 1 Info' 'Secondary Cell 2' 'LTE Band')"
echo "ca.scc1.earfcn=$(blocco 'Secondary Cell 1 Info' 'Secondary Cell 2' 'RX Channel')"
SC1P=$(blocco 'Secondary Cell 1 Info' 'Secondary Cell 2' 'Physical Cell ID')
SC1BW=$(blocco 'Secondary Cell 1 Info' 'Secondary Cell 2' 'DL Bandwidth')
echo "ca.scc1.pci=$SC1P"
echo "ca.scc1.bw=$SC1BW"
echo "ca.scc1.state=$(blocco 'Secondary Cell 1 Info' 'Secondary Cell 2' 'State')"
[ -n "$SC1P" ] && echo "ca.scc1.rsrp=$(rsrp_di_pci "$SC1P")"
echo "ca.scc1.stima=$(awk -v a="$PCCBW" -v b="$SC1BW" 'BEGIN{ if (a+b>0) printf "%d", b*100/(a+b); else print 0 }')"
