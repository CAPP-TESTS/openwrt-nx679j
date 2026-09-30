#!/bin/bash
# Control test: does ABL accept and boot a KNOWN-GOOD image on slot B?
# Hypothesis A: slot B is refused by the boot chain. Hypothesis B: the OpenWrt
# image itself is the problem. Writing the image that already boots on A to B
# separates them with one flash and one reboot.
set -u
A="timeout 30 adb -s 0123456789ABCDEF"
IMG=/home/user/nx679j-stock/magisk_patched-30700_Zx2eF.img
EXPECT=0cd94d5627501f3c65c94542093ad18b1ec6c1c215dc6a7ecab4f941057e8364
D=/home/user/nx679j-stock/experiments/20260916-122926-native-baseline/control-slotb-$(date +%Y%m%d-%H%M%S)
mkdir -p "$D"
exec > >(tee -a "$D/control.log") 2>&1

echo "== slot attivo =="
$A shell su -c 'getprop ro.boot.slot_suffix'

echo "== hash pre-intervento (slot B) =="
for p in boot_b vendor_boot_b dtbo_b vbmeta_b misc; do
  $A exec-out su -c "cat /dev/block/by-name/$p" > "$D/pre-$p.img" 2>/dev/null
  printf '%-14s %s\n' "$p" "$(sha256sum "$D/pre-$p.img" | cut -d' ' -f1)"
done

echo "== push immagine di controllo =="
$A push "$IMG" /data/local/tmp/control-boot.img
$A shell su -c 'sha256sum /data/local/tmp/control-boot.img'
echo "== scrittura su boot_b =="
$A shell su -c 'dd if=/data/local/tmp/control-boot.img of=/dev/block/by-name/boot_b bs=1M && sync' 2>&1 | tail -2

echo "== readback e confronto =="
GOT=$($A shell su -c 'sha256sum /dev/block/by-name/boot_b' 2>/dev/null | awk '{print $1}')
echo "atteso  $EXPECT"
echo "letto   $GOT"
if [ "$GOT" != "$EXPECT" ]; then
  echo "!!! readback NON corrisponde: mi fermo, nessun riavvio"
  exit 1
fi
echo "readback OK"

echo "== fastboot: metadati e attivazione B =="
$A reboot bootloader
for i in $(seq 1 20); do timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && break; sleep 3; done
for v in current-slot slot-successful:a slot-successful:b slot-unbootable:b slot-retry-count:b unlocked secure; do
  printf '%-22s ' "$v"; timeout 10 fastboot getvar $v 2>&1 | head -1
done
timeout 15 fastboot set_active b 2>&1 | head -1
timeout 15 fastboot reboot 2>&1 | head -1

echo "== monitor 120s =="
bash /home/user/nx679j-stock/experiments/20260916-122926-native-baseline/monitor-boot.sh "$D" 120

echo "== esito =="
if timeout 8 adb -s 0123456789ABCDEF get-state >/dev/null 2>&1; then
  echo "*** ANDROID E' PARTITO DA SLOT B *** (ABL accetta immagini scritte su B)"
  echo "slot ora: $(timeout 10 adb -s 0123456789ABCDEF shell getprop ro.boot.slot_suffix 2>/dev/null)"
else
  echo "*** slot B NON ha avviato Android ***"
fi
echo "log completo: $D/control.log"
