#!/bin/bash
# Hardware test for the v7 / v7b "sonda" images on slot B.
#
# ONE QUESTION: does the kernel reach userspace when the ramdisk is ours?
# The probe is a static /init that writes one marker line and then asks the
# kernel to restart, for ever, every ~10 s. So:
#
#   phone restarts by itself, repeatedly  -> userspace WAS reached
#   frozen RedMagic logo, nothing at all  -> userspace was NOT reached
#                                            (same failure as v5/v6)
#
# The script does: push -> sha check -> dd to boot_b -> sha READBACK -> getvars
# -> set_active b -> reboot -> USB monitor (default 240 s) -> evaluation.
# It never reboots the phone unless the readback of the image on the partition
# matches the file on disk, so a failed push cannot brick the test.
#
# usage: test-probe-v7-slotb.sh <v7|v7b> [monitor_seconds] [--no-restore]
#   default: v7b is the recommended FIRST test (its ramdisk content is the
#   Android one, which probe6 proved boots), 240 s, and slot A is restored at
#   the end so that the phone is left in a bootable, sane state.
set -u
SEL="${1:?usage: test-probe-v7-slotb.sh <v7|v7b> [monitor_seconds] [--no-restore]}"
MON="${2:-240}"
RESTORE=1
for a in "$@"; do [ "$a" = "--no-restore" ] && RESTORE=0; done

BASE=/home/user/nx679j-stock/experiments/20260916-122926-native-baseline
PROBE=$BASE/probe-v7-sonda
IMG=$PROBE/boot_b-probe-$SEL.img
SER=0123456789ABCDEF
A="timeout 30 adb -s $SER"
D=$PROBE/test-slotb-$SEL-$(date +%Y%m%d-%H%M%S)
mkdir -p "$D"
exec > >(tee -a "$D/test.log") 2>&1

[ -f "$IMG" ] || { echo "!! immagine assente: $IMG"; exit 1; }
EXPECT=$(sha256sum "$IMG" | cut -d' ' -f1)
echo "== immagine $SEL =="
echo "path   $IMG"
echo "size   $(stat -c %s "$IMG") byte"
echo "sha256 $EXPECT"
python3 - "$PROBE/manifest.json" "$SEL" <<'PY'
import json, sys
man = json.load(open(sys.argv[1]))
i = man['images'][sys.argv[2]]
print(f"       manifest: ramdisk_size={i['header']['ramdisk_size']} "
      f"kernel_size={i['header']['kernel_size']} signature_size="
      f"{i['header']['signature_size']} header_version={i['header']['header_version']}")
print(f"       /init sha256={i['init_sha256'][:32]} ({i['init_bytes']} byte)")
PY

echo
echo "== 0. stato di partenza (serve per il confronto del contatore A/B) =="
$A get-state >/dev/null 2>&1 || { echo "!! nessun device adb: serve Android avviato"; exit 1; }
echo "slot attivo ora: $($A shell getprop ro.boot.slot_suffix | tr -d '\r')"
gpt_state() {  # decode the A/B metadata that ABL keeps in the GPT attributes
  $A exec-out su -c 'dd if=/dev/block/sde bs=4096 count=34 2>/dev/null' > "$1" 2>/dev/null
  python3 - "$1" <<'PY'
import struct, sys
b = open(sys.argv[1], 'rb').read()
if b[0x1000:0x1008] != b'EFI PART':
    print('    (GPT non leggibile: ' + str(len(b)) + ' byte)'); raise SystemExit
n, esz = struct.unpack_from('<II', b, 0x1000 + 80)
tbl = struct.unpack_from('<Q', b, 0x1000 + 72)[0] * 4096
for i in range(n):
    e = tbl + i * esz
    if len(b) < e + 128: break
    name = b[e + 56:e + 128].decode('utf-16-le', 'ignore').split('\x00')[0]
    if name not in ('boot_a', 'boot_b'): continue
    a = struct.unpack_from('<Q', b, e + 48)[0]
    prio, succ, tries = (a >> 48) & 3, (a >> 50) & 1, (a >> 51) & 7
    print(f'    GPT {name}: priority={prio} successful={succ} '
          f'tries_remaining={tries}   (bits 48-49 / 50 / 51-53)')
PY
}
echo "-- metadati A/B PRIMA (lettura diretta della GPT, nessuna scrittura)"
gpt_state "$D/gpt-before.bin"

echo
echo "== 1. push + verifica hash sull'host e sul device =="
$A push "$IMG" /data/local/tmp/probe-$SEL.img
GOT=$($A shell su -c "sha256sum /data/local/tmp/probe-$SEL.img" 2>/dev/null | awk '{print $1}')
if [ "$GOT" != "$EXPECT" ]; then echo "!! push non integro ($GOT): mi fermo"; exit 1; fi
echo "push OK"

echo
echo "== 2. scrittura su boot_b + READBACK (nessun riavvio se non combacia) =="
$A shell su -c "dd if=/data/local/tmp/probe-$SEL.img of=/dev/block/by-name/boot_b bs=1M && sync" 2>&1 | tail -2
GOT=$($A shell su -c 'sha256sum /dev/block/by-name/boot_b' 2>/dev/null | awk '{print $1}')
echo "atteso $EXPECT"
echo "letto  $GOT"
if [ "$GOT" != "$EXPECT" ]; then echo "!! readback NON corrisponde: nessun riavvio, nulla e' cambiato"; exit 1; fi
echo "readback OK"

echo
echo "== 3. fastboot: metadati PRIMA dell'attivazione =="
$A reboot bootloader
for i in $(seq 1 20); do timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && break; sleep 3; done
for v in current-slot slot-successful:a slot-successful:b slot-unbootable:b slot-retry-count:b slot-retry-count:a; do
  printf '%-22s ' "$v"; timeout 10 fastboot getvar $v 2>&1 | head -1
done

echo
echo "== 4. attivazione slot B e riavvio =="
timeout 15 fastboot set_active b 2>&1 | head -1
timeout 15 fastboot reboot 2>&1 | head -1

echo
echo "== 5. monitor USB ${MON}s =="
echo "   ATTENZIONE: la sonda non espone nessun USB. Il segnale primario e' il"
echo "   TELEFONO: guarda lo schermo. Logo RedMagic che si RIPETE ~ogni 10 s ="
echo "   la sonda gira (userspace raggiunto). Logo FERMO una volta sola = non"
echo "   raggiunto (stesso esito di v5/v6). Sui due casi il host non vede USB."
bash "$BASE/monitor-boot.sh" "$D" "$MON"

echo
echo "== 6. valutazione =="
LOOP=$(grep -c 'USB=' "$D/usb-monitor.log")
ANDROID=0
for i in $(seq 1 40); do
  timeout 8 adb -s $SER get-state >/dev/null 2>&1 && { ANDROID=1; break; }
  sleep 3
done
if [ "$ANDROID" = 1 ]; then
  echo "*** (A) IL TELEFONO E' TORNATO AD ANDROID DA SOLO ***"
  echo "    -> userspace RAGGIUNTO e contatore A/B consumato fino al fallback."
  echo "    slot ora: $($A shell getprop ro.boot.slot_suffix | tr -d '\r')"
  echo "-- metadati A/B DOPO (GPT, nessuna scrittura)"
  gpt_state "$D/gpt-after.bin"
  echo "-- pstore (se il kernel ha lasciato il buffer console del boot fallito)"
  $A shell su -c 'ls -la /sys/fs/pstore/ 2>/dev/null' | head
  $A shell su -c 'grep -a -h nx679j-sonda /sys/fs/pstore/* 2>/dev/null | head -5'
  echo "-- rawdump (la sonda NON scrive qui: nessun modulo caricato, UFS giu')"
  $A shell su -c 'head -c 4096 /dev/block/by-name/rawdump | tr -d "\0" | wc -c'
  echo
  echo "   prova finale del consumo del contatore, da fastboot:"
  echo "     adb reboot bootloader && fastboot getvar slot-retry-count:b \\"
  echo "       && fastboot getvar slot-unbootable:b && fastboot set_active a && fastboot reboot"
elif [ "$LOOP" -ge 3 ]; then
  echo "*** (B) ATTIVITA' USB MULTIPLA SENZA ANDROID ($LOOP righe di log) ***"
  echo "    Guarda lo schermo: se il logo si ripete, la sonda gira ma il"
  echo "    contatore A/B NON si e' consumato (l'ipotesi va verificata: la GPT"
  echo "    dell'unità e' l'unico giudice). Recupero manuale:"
else
  echo "*** (C) NESSUN SEGNALE USB E NESSUN ANDROID ENTRO ${MON}s ***"
  echo "    Se lo schermo mostra il logo FERMO: userspace NON raggiunto,"
  echo "    stesso esito di v5/v6 (nessun riavvio, contatore intatto)."
  echo "    Recupero manuale:"
fi
echo "     1) tieni premuto POWER ~15 s (telefono spento)"
echo "     2) POWER + VOLUME- per entrare in fastboot"
echo "     3) fastboot set_active a && fastboot reboot"
echo "        (slot A e' Android sano e non viene toccato da questo test)"
if [ "$RESTORE" = 1 ] && [ "$ANDROID" = 1 ]; then
  echo
  echo "== 7. ripristino slot A (per lasciare il telefono sano) =="
  $A reboot bootloader
  for i in $(seq 1 20); do timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && break; sleep 3; done
  timeout 15 fastboot set_active a 2>&1 | head -1
  timeout 15 fastboot reboot 2>&1 | head -1
  for i in $(seq 1 60); do
    timeout 8 adb -s $SER get-state >/dev/null 2>&1 && break
    sleep 3
  done
  if timeout 8 adb -s $SER get-state >/dev/null 2>&1; then
    echo "Android di nuovo su: $($A shell getprop ro.boot.slot_suffix | tr -d '\r')"
  else
    echo "!! Android non risponde su A: controlla a mano (fastboot set_active a)"
  fi
fi
echo
echo "log completo: $D/test.log"
