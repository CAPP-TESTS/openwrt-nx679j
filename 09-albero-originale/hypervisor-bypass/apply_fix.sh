#!/bin/bash
# Apply Gunyah bypass to custom kernel boot image

set -e

BOOT_IMG="$1"
if [ -z "$BOOT_IMG" ]; then
    echo "Usage: $0 <boot.img>"
    exit 1
fi

WORK=$(mktemp -d)
echo "[*] Working directory: $WORK"

# Unpack boot image
echo "[+] Unpacking boot image..."
cd "$WORK"
unpack_bootimg.py --boot_img "$BOOT_IMG" --out .

# Modify ramdisk
echo "[+] Modifying ramdisk..."
mkdir ramdisk
cd ramdisk
gunzip -c ../ramdisk | cpio -i

# Add watchdog pet service
cat > init.watchdog_pet.rc << 'INIT_RC'
# Disable watchdog by petting it continuously
on boot
    write /sys/devices/platform/soc/soc:qcom,gh-virt-wdt/watchdog/watchdog0/timeout 300
    write /dev/watchdog0 V
INIT_RC

# Repack ramdisk
find . | cpio -o -H newc | gzip > ../ramdisk_new

# Repack boot image with modified cmdline
cd ..
mkbootimg \
    --kernel kernel \
    --ramdisk ramdisk_new \
    --dtb dtb \
    --header_version $(cat header_version) \
    --os_version $(cat os_version) \
    --os_patch_level $(cat os_patch_level) \
    --cmdline "$(cat cmdline) nowatchdog gunyah.disable=1" \
    -o boot_fixed.img

echo "[+] Created boot_fixed.img with Gunyah bypass"
cp boot_fixed.img /home/user/nx679j-stock/kernel-patch-test/
