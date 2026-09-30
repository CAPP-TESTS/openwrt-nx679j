#!/bin/bash
set -e

KERNEL="/home/user/nx679j-stock/kernel-patch-test/linux-6.6.60/arch/arm64/boot/Image"
RAMDISK="/home/user/nx679j-stock/port-work/openwrt-phase1/openwrt-rootfs.cpio.gz"
OUTPUT="boot_b_openwrt_fixed_cmdline.img"

# Command line Android stock (copiata da /proc/cmdline)
CMDLINE="stack_depot_disable=on kasan.stacktrace=off kvm-arm.mode=protected cgroup_disable=pressure cgroup.memory=nokmem console=ttyMSM0,115200n8 loglevel=6 kpti=0 log_buf_len=256K kernel.panic_on_rcu_stall=1 swiotlb=noforce loop.max_part=7 cgroup.memory=nokmem,nosocket pcie_ports=compat service_locator.enable=1 msm_rtb.filter=0x237 allow_mismatched_32bit_el0 cpufreq.default_governor=performance pelt=8 kasan=off rcupdate.rcu_expedited=1 rcu_nocbs=0-7 irqaffinity=0-3 ftrace_dump_on_oops pstore.compress=none fsa4480_i2c.async_probe=1 can.stats_timer=0 video=vfb:640x400,bpp=32,memsize=3072000 qcom-dload-mode.download_mode=1 bootconfig msm_drm.dsi_display0=qcom,mdss_dsi_r6130_1080_2400_amoled_cmd: rootwait ro init=/init"

# Verifica input
if [ ! -f "$KERNEL" ]; then
  echo "ERROR: Kernel not found: $KERNEL"
  exit 1
fi

if [ ! -f "$RAMDISK" ]; then
  echo "ERROR: Ramdisk not found: $RAMDISK"
  exit 1
fi

echo "=== Building Android boot image v4 with correct cmdline ==="
echo "Kernel: $KERNEL ($(stat -c%s "$KERNEL") bytes)"
echo "Ramdisk: $RAMDISK ($(stat -c%s "$RAMDISK") bytes)"
echo "Cmdline length: ${#CMDLINE} chars"
echo ""

# Build boot image v4 (identico ad Android stock header)
mkbootimg \
  --header_version 4 \
  --kernel "$KERNEL" \
  --ramdisk "$RAMDISK" \
  --cmdline "$CMDLINE" \
  --os_version 12.0.0 \
  --os_patch_level 2022-02 \
  --pagesize 4096 \
  -o "$OUTPUT"

echo ""
echo "=== Image built successfully ==="
ls -lh "$OUTPUT"
sha256sum "$OUTPUT"

# Verifica cmdline nell'immagine
echo ""
echo "=== Verifying cmdline in built image ==="
python3 << 'EOFPYTHON'
import struct

with open('boot_b_openwrt_fixed_cmdline.img', 'rb') as f:
    header = f.read(1584)
    cmdline1 = header[0x30:0x430]
    cmdline2 = header[0x430:0x830]
    cmdline_full = (cmdline1 + cmdline2).split(b'\x00')[0].decode('ascii', errors='ignore')
    
    kernel_size = struct.unpack('<I', header[8:12])[0]
    ramdisk_size = struct.unpack('<I', header[12:16])[0]
    
    print(f"Kernel size: {kernel_size} ({kernel_size/1024/1024:.2f} MiB)")
    print(f"Ramdisk size: {ramdisk_size} ({ramdisk_size/1024/1024:.2f} MiB)")
    print(f"Cmdline length: {len(cmdline_full)} chars")
    print(f"Cmdline preview: {cmdline_full[:80]}...")
    
    if len(cmdline_full) == 0:
        print("\nERROR: Cmdline is EMPTY!")
        exit(1)
    else:
        print("\n✓ Cmdline is NOT empty — ABL should accept this image")
EOFPYTHON

echo ""
echo "=== Ready to flash ==="
echo "Image: $(pwd)/$OUTPUT"
echo ""
echo "Next steps:"
echo "1. Reboot phone to fastboot: adb reboot bootloader"
echo "2. Flash to slot B: fastboot flash boot_b $OUTPUT"
echo "3. Monitor boot with USB watch script"
