#!/bin/bash
# Hardware test for v6 on slot B: push the static-init gadget probe, write it to
# boot_b with a readback check, activate B, watch USB, then read the evidence
# back and return the phone to slot A.
#
# Reuses the proven control procedure (control-test-slotb.sh) and monitor-boot.sh.
# The phone MUST stay in Android on slot A when this starts (adb + root).
#
# usage: test-v6-slotb.sh [monitor_seconds]      (default 180)
set -u
MON="${1:-180}"
A="timeout 30 adb -s 0123456789ABCDEF"
IMG=/home/user/nx679j-stock/experiments/20260916-122926-native-baseline/candidate-minimal-gadget-v6/boot_b-staticinit-v6.img
D=/home/user/nx679j-stock/experiments/20260916-122926-native-baseline/v6-test-slotb-$(date +%Y%m%d-%H%M%S)
mkdir -p "$D"
exec > >(tee -a "$D/test.log") 2>&1

EXPECT=$(sha256sum "$IMG" | cut -d' ' -f1)
echo "== immagine v6 =="
echo "path   $IMG"
echo "size   $(stat -c %s "$IMG") byte"
echo "sha256 $EXPECT"

echo "== slot attivo =="
$A shell su -c 'getprop ro.boot.slot_suffix'

echo "== hash pre-intervento (slot B) =="
for p in boot_b vendor_boot_b dtbo_b misc; do
  $A exec-out su -c "cat /dev/block/by-name/$p" > "$D/pre-$p.img" 2>/dev/null
  printf '%-14s %s\n' "$p" "$(sha256sum "$D/pre-$p.img" | cut -d' ' -f1)"
done

echo "== push e scrittura su boot_b =="
$A push "$IMG" /data/local/tmp/v6-boot.img
GOT=$($A shell su -c 'sha256sum /data/local/tmp/v6-boot.img' 2>/dev/null | awk '{print $1}')
[ "$GOT" = "$EXPECT" ] || { echo "!!! push non integro ($GOT): mi fermo"; exit 1; }
$A shell su -c 'dd if=/data/local/tmp/v6-boot.img of=/dev/block/by-name/boot_b bs=1M && sync' 2>&1 | tail -2
GOT=$($A shell su -c 'sha256sum /dev/block/by-name/boot_b' 2>/dev/null | awk '{print $1}')
echo "atteso $EXPECT"
echo "letto  $GOT"
[ "$GOT" = "$EXPECT" ] || { echo "!!! readback non corrisponde: nessun riavvio"; exit 1; }
echo "readback OK"
echo "== fastboot: attivazione B e riavvio =="
$A reboot bootloader
for i in $(seq 1 20); do timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && break; sleep 3; done
for v in current-slot slot-successful:a slot-unbootable:b slot-retry-count:b unlocked secure; do
  printf '%-22s ' "$v"; timeout 10 fastboot getvar $v 2>&1 | head -1
done
timeout 15 fastboot set_active b 2>&1 | head -1
timeout 15 fastboot reboot 2>&1 | head -1

echo "== monitor USB ${MON}s (segnale atteso: 18d1:4ee7) =="
bash /home/user/nx679j-stock/experiments/20260916-122926-native-baseline/monitor-boot.sh "$D" "$MON"

echo "== valutazione =="
USB=$(grep -oE 'ID 18d1:4ee7[^;]*' "$D/usb-monitor.log" | head -1)
if [ -n "$USB" ]; then
  echo "*** GADGET NCM VISIBILE: $USB ***"
  IF=$(ip -o link show 2>/dev/null | awk -F': ' '{print $2}' | grep -E '^usb|^enx' | head -1)
  if [ -n "$IF" ]; then
    echo "interfaccia host: $IF  mac: $(cat /sys/class/net/$IF/address 2>/dev/null)"
    ip -4 addr show "$IF" 2>/dev/null | sed -n 's/^ *//p'
    ping -c3 -W2 10.0.0.1 && echo "*** ping 10.0.0.1 OK ***" || echo "ping 10.0.0.1 FALLITO"
  else
    echo "nessuna interfaccia usb* sul host: il gadget e' enumerato ma senza link?"
  fi
else
  echo "*** nessun 18d1:4ee7: leggi il journal rawdump qui sotto ***"
fi
echo "== ritorno a slot A, poi evidenza =="
timeout 20 adb -s 0123456789ABCDEF reboot bootloader 2>/dev/null
for i in $(seq 1 20); do timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && break; sleep 3; done
timeout 15 fastboot set_active a 2>&1 | head -1
timeout 15 fastboot reboot 2>&1 | head -1
echo "attendo Android su A (fino a 180 s)..."
for i in $(seq 1 60); do
  timeout 8 adb -s 0123456789ABCDEF get-state >/dev/null 2>&1 && break
  sleep 3
done
if timeout 8 adb -s 0123456789ABCDEF get-state >/dev/null 2>&1; then
  echo "slot ora: $(timeout 10 adb -s 0123456789ABCDEF shell getprop ro.boot.slot_suffix 2>/dev/null)"
  echo "== journal rawdump (primi 64 KiB) =="
  $A exec-out su -c 'head -c 65536 /dev/block/by-name/rawdump' > "$D/rawdump-journal.bin" 2>/dev/null
  echo "bytes: $(stat -c %s "$D/rawdump-journal.bin")"
  if grep -qa 'nx679j-v6' "$D/rawdump-journal.bin"; then
    echo "*** JOURNAL v6 TROVATO SU RAWDUMP ***"
    strings -a "$D/rawdump-journal.bin" | grep 'nx679j-v6' | head -40
  else
    echo "nessun testo nx679j-v6 nel rawdump (journal non scritto: UFS non e' salita?)"
  fi
  echo "== pstore (ramoops console/pmsg) =="
  $A shell su -c 'ls -la /sys/fs/pstore' 2>&1 | head -20
  $A shell su -c 'cat /sys/fs/pstore/*pmsg* 2>/dev/null | tail -40' 2>&1 | tail -40
else
  echo "Android non e' salito su A: controlla a mano (fastboot set_active a)"
fi
echo "log completo: $D/test.log"
