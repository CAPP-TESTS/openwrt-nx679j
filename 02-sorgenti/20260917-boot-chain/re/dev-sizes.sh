#!/system/bin/sh
# Read-only inventory of boot-chain partitions. No writes to any partition.
for p in xbl_a xbl_b abl_a abl_b uefi_a uefi_b vbmeta_a vbmeta_b vbmeta_system_a vbmeta_system_b misc dtbo_a dtbo_b boot_a boot_b vendor_boot_a vendor_boot_b keymaster_a keymaster_b devcfg_a devcfg_b qupfw_a qupfw_b; do
  n=$(blockdev --getsize64 /dev/block/by-name/$p 2>/dev/null)
  printf '%-20s %s\n' "$p" "${n:-MISSING}"
done
