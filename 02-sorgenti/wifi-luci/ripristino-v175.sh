#!/bin/sh
# Ripristino dell'immagine boot buona (v175, md5 98ac83f5...).
# Serve il device in FASTBOOT (da spento: Volume Giu + Power).
set -e
IMG=/home/user/nx679j-stock/experiments/20260920-wifi-luci/boot_b-current-v170.img
GEMELLO=boot_b-v90-mmwd.img
echo "=== 1. controllo che il file sia quello buono ==="
H=$(md5sum "$IMG" | cut -c1-32)
echo "md5 atteso : 98ac83f587bd6bb29ae263b48ebd35c7"
echo "md5 trovato: $H"
[ "$H" = "98ac83f587bd6bb29ae263b48ebd35c7" ] || { echo "MD5 DIVERSO: mi fermo"; exit 1; }
echo "=== 2. device in fastboot? ==="
fastboot devices || { echo "nessun fastboot: collega e riprova"; exit 1; }
echo "=== 3. slot b e scrittura ==="
fastboot --set-active=b
fastboot flash boot_b "$IMG"
fastboot reboot
echo "=== 4. fatto: attendi l'avvio (~90 s) ==="
