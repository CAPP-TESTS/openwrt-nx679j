#!/bin/bash
set -e

# Usa kernel STOCK Android 5.10.66 (già funzionante su slot A) + ramdisk OpenWrt
KERNEL_STOCK="/home/user/nx679j-stock/experiments/20260916-122926-native-baseline/current-readback/boot_a.img"
RAMDISK_OPENWRT="/home/user/nx679j-stock/port-work/openwrt-phase1/openwrt-rootfs.cpio.gz"
OUTPUT="boot_b_stockkernel_openwrt_fixed.img"

# Estrai kernel da boot_a (Android stock)
echo "=== Extracting stock kernel from boot_a ==="
mkdir -p work-stockkernel
unpack_bootimg --boot_img "$KERNEL_STOCK" --out work-stockkernel >/dev/null 2>&1

if [ ! -f work-stockkernel/kernel ]; then
  echo "ERROR: Failed to extract kernel from boot_a"
  exit 1
fi

KERNEL="work-stockkernel/kernel"
echo "Stock kernel: $(stat -c%s "$KERNEL") bytes"
echo "OpenWrt ramdisk: $(stat -c%s "$RAMDISK_OPENWRT") bytes"

# Command line da Android stock
CMDLINE="stack_depot_disable=on kasan.stacktrace=off kvm-arm.mode=protected cgroup_disable=pressure cgroup.memory=nokmem console=ttyMSM0,115200n8 loglevel=6 kpti=0 log_buf_len=256K kernel.panic_on_rcu_stall=1 swiotlb=noforce loop.max_part=7 cgroup.memory=nokmem,nosocket pcie_ports=compat service_locator.enable=1 msm_rtb.filter=0x237 allow_mismatched_32bit_el0 cpufreq.default_governor=performance pelt=8 kasan=off rcupdate.rcu_expedited=1 rcu_nocbs=0-7 irqaffinity=0-3 ftrace_dump_on_oops pstore.compress=none fsa4480_i2c.async_probe=1 can.stats_timer=0 video=vfb:640x400,bpp=32,memsize=3072000 qcom-dload-mode.download_mode=1 bootconfig msm_drm.dsi_display0=qcom,mdss_dsi_r6130_1080_2400_amoled_cmd: rootwait ro init=/init"

echo ""
echo "=== Building boot image with stock kernel + OpenWrt ramdisk ==="

mkbootimg \
  --header_version 4 \
  --kernel "$KERNEL" \
  --ramdisk "$RAMDISK_OPENWRT" \
  --cmdline "$CMDLINE" \
  --os_version 12.0.0 \
  --os_patch_level 2022-02 \
  --pagesize 4096 \
  -o "$OUTPUT"

# Aggiungi signature dummy 4096 bytes
dd if=/dev/zero bs=4096 count=1 >> "$OUTPUT" 2>/dev/null

# Aggiorna header signature_size
python3 << 'EOFPYTHON'
import struct
with open('boot_b_stockkernel_openwrt_fixed.img', 'r+b') as f:
    header = bytearray(f.read(1584))
    header[1568:1572] = struct.pack('<I', 4096)
    f.seek(0)
    f.write(header)
EOFPYTHON

echo ""
echo "=== Image built successfully ==="
ls -lh "$OUTPUT"
sha256sum "$OUTPUT"

# Verifica
python3 << 'EOFPYTHON'
import struct

with open('boot_b_stockkernel_openwrt_fixed.img', 'rb') as f:
    header = f.read(1584)
    kernel_size = struct.unpack('<I', header[8:12])[0]
    ramdisk_size = struct.unpack('<I', header[12:16])[0]
    signature_size = struct.unpack('<I', header[1568:1572])[0]
    cmdline = (header[0x30:0x430] + header[0x430:0x830]).split(b'\x00')[0].decode('ascii', errors='ignore')
    
    print(f"\nKernel: {kernel_size/1024/1024:.2f} MiB (stock 5.10.66)")
    print(f"Ramdisk: {ramdisk_size/1024/1024:.2f} MiB (OpenWrt)")
    print(f"Signature: {signature_size} bytes")
    print(f"Cmdline: {len(cmdline)} chars")
    
    checks = [
        ("Kernel stock 5.10.66", kernel_size == 49108324),
        ("Signature present", signature_size == 4096),
        ("Cmdline not empty", len(cmdline) > 0)
    ]
    
    for check, ok in checks:
        print(f"{'✓' if ok else '✗'} {check}")
    
    if all(ok for _, ok in checks):
        print("\n✓ Ready for boot test")
EOFPYTHON

echo ""
echo "Next: ./flash-stockkernel-test.sh"
