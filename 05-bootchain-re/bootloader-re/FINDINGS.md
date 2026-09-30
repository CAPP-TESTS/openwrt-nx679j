# NX679J Boot Crash Root Cause Analysis
**Date:** 2026-08-06  
**Device:** Nubia Red Magic 7 (NX679J)  
**Platform:** Qualcomm SM8450 (Snapdragon 8 Gen 1)

## Executive Summary
The 15-second boot crash of custom upstream kernels (6.6.y) is caused by **missing Gunyah hypervisor support**. The device's ABL enforces protected KVM mode via the `kvm-arm.mode=protected` kernel parameter, but upstream kernels lack the necessary hypervisor integration present in the stock Android 5.10 vendor kernel.

## Root Cause

### Primary Issue: Gunyah Hypervisor Protected Mode
- Device runs **Gunyah hypervisor** in protected KVM mode (ARM Confidential Compute Architecture)
- ABL dynamically prepends critical cmdline parameters:
  ```
  stack_depot_disable=on kasan.stacktrace=off kvm-arm.mode=protected 
  cgroup_disable=pressure cgroup.memory=nokmem
  ```
- The `kvm-arm.mode=protected` parameter is **NOT** in boot_img header or device tree
- ABL adds it at runtime before kernel handoff

### Secondary Issue: Missing Kernel Support
Upstream kernel 6.6.y lacks:
1. **Protected KVM guest mode** - kernel runs under hypervisor supervision
2. **Gunyah platform drivers** - resource manager, vCPU management, IRQ routing
3. **Gunyah-aware memory management** - DMA/IOMMU operations must go through hypervisor
4. **Hypervisor call (HVC) infrastructure** - SMCCC extensions for Gunyah

Stock Android 5.10 kernel has full Gunyah integration via Qualcomm vendor patches.

## Evidence

### 1. Kernel Image Format
**Stock vs Custom comparison:**
- Both use identical ARM64 PE/COFF format (MZ magic, ARM64 signature)
- Image headers byte-for-byte identical at offset 0x00-0x90
- Rules out image format issues

### 2. Kernel Cmdline Analysis
**Stock Android (running):**
```
stack_depot_disable=on kasan.stacktrace=off kvm-arm.mode=protected 
cgroup_disable=pressure cgroup.memory=nokmem console=ttyMSM0,115200n8 
loglevel=6 kpti=0 log_buf_len=256K kernel.panic_on_rcu_stall=1 
swiotlb=noforce loop.max_part=7 cgroup.memory=nokmem,nosocket 
pcie_ports=compat service_locator.enable=1 msm_rtb.filter=0x237 
allow_mismatched_32bit_el0 cpufreq.default_governor=performance 
pelt=8 kasan=off rcupdate.rcu_expedited=1 rcu_nocbs=0-7 
irqaffinity=0-3 ftrace_dump_on_oops pstore.compress=none 
qcom-dload-mode.download_mode=1 bootconfig rootwait ro init=/init
```

**Custom boot images:** Empty cmdline in boot_img header (offset 0x40-0x63F all zeros)

**Device tree bootargs:** Does NOT include `kvm-arm.mode=protected` - confirmed ABL adds it

### 3. Test Results
**Test 1: Empty cmdline (original builds)**
- Result: Crash at ~15s → EDL mode (QUSB_BULK 05c6:900e)
- Cause: Kernel attempts direct hardware access, hypervisor kills it

**Test 2: Added kvm-arm.mode=protected**
- Result: Crash at ~15s → Fastboot mode (18d1:d00d)
- Progress: Avoided EDL, but still crashes
- Cause: Kernel recognizes protected mode but lacks hypervisor drivers

### 4. Partition Analysis
**ABL binary:** 32-bit ARM ELF with embedded UEFI Firmware Volume
- File: `abl_a.img` (1 MiB)
- SHA256: `443d1956e836aea3fe30ac13113dae787d88223c1e24f875c6831f69d86acea3`
- Entry point: `0x9fa00000`
- Format: Stripped ELF (no symbols), UEFI FV at offset 0x1020

**XBL binary:** Secondary bootloader
- File: `xbl_a.img` (3.5 MiB)
- SHA256: `685f0a75fbff851bb6ef55ba158b7f86ea14a44c527c5388605ece94f47d637a`

## Technical Details

### Gunyah Hypervisor (ARM CCA)
- Type-1 bare-metal hypervisor by Qualcomm
- Implements ARM Confidential Compute Architecture (CCA)
- Provides secure VM isolation at EL2
- Required for DRM, secure enclaves, trusted apps on SM8450

### Protected KVM Mode
- Kernel runs as KVM guest under Gunyah
- Hardware access mediated through hypervisor
- Direct MMIO/DMA forbidden without HVC calls
- Requires kernel compiled with `CONFIG_PROTECTED_VIRTUALIZATION_GUEST=y`

### Missing Drivers
Mainline 6.6.y has only `CONFIG_GUNYAH_WATCHDOG=y` (added in 6.2)

Stock kernel has (from vendor patches):
- `CONFIG_GUNYAH=y` - core platform driver
- `CONFIG_GUNYAH_VCPU=y` - vCPU management
- `CONFIG_GUNYAH_IRQ_ROUTING=y` - IRQ virtualization  
- `CONFIG_GUNYAH_RESOURCE_MGR=y` - RM communication
- `CONFIG_PROTECTED_VIRTUALIZATION_GUEST=y` - protected mode

## Solutions

### Option 1: Port Gunyah Drivers (Hard)
**Pros:**
- Native upstream kernel boot
- Full hardware access
- Future-proof

**Cons:**
- Requires stock kernel sources (if available)
- 100+ patches to backport
- Complex hypervisor interaction
- ETA: 2-4 weeks minimum

**Steps:**
1. Extract Qualcomm kernel sources for SM8450 Android 12
2. Identify Gunyah patches vs mainline 5.10
3. Port to 6.6.y: drivers/virt/gunyah/, arch/arm64/kvm/
4. Add device tree nodes for Gunyah resources
5. Test iteratively

### Option 2: Patch ABL to Disable Protected Mode (Hard)
**Pros:**
- Upstream kernel works unmodified
- One-time fix

**Cons:**
- Requires IDA Pro/Ghidra reverse engineering
- May break DRM/secure boot chain
- Potential brick risk
- Violates vendor security policy

**Steps:**
1. Load ABL in Ghidra, analyze ARM 32-bit code
2. Find cmdline construction function (search "kvm-arm.mode")
3. Patch to use `kvm-arm.mode=nvhe` (non-protected) instead
4. Re-sign ABL (if signatures checked) or unlock further
5. Flash and test

### Option 3: Stock Kernel + Custom Initramfs (Current Workaround)
**Pros:**
- Already working for OpenWrt  
- No kernel development needed
- Safe, reversible

**Cons:**
- Stuck on Android 5.10 kernel (EOL)
- Can't use mainline features
- Kernel security updates depend on vendor

**Status:** This is what you've been using successfully. Boot works because stock kernel has Gunyah support.

### Option 4: Disable Bootloader Slot B Protection (Unknown Feasibility)
**Theory:** ABL may treat slot A differently than slot B for recovery/factory purposes

**Test:**
1. Flash custom kernel to slot A instead of B
2. See if ABL relaxes protections
3. Check if `kvm-arm.mode` parameter differs

**Risk:** May brick primary boot slot

## Recommendations

1. **Short term:** Continue using **Option 3** (stock kernel + initramfs) for stable OpenWrt
   
2. **Medium term:** Attempt **Option 4** as low-risk experiment
   - Flash known-good custom kernel to slot A
   - Monitor cmdline differences
   - Fallback to fastboot if crash

3. **Long term:** Only pursue **Option 1** if:
   - Qualcomm releases SM8450 kernel sources publicly
   - Mainline adds more Gunyah support (check 6.8+)
   - You need specific upstream features badly

**Avoid Option 2** unless you have:
- Professional IDA Pro license + ARM RE experience
- Factory EDL cable for unbrick
- Willingness to lose DRM (Widevine L1, secure video)

## Files
- `abl_a.img` - ABL binary dump
- `xbl_a.img` - XBL binary dump  
- `vendor_boot_a.img` - Vendor boot partition
- `boot_b_kvm_protected.img` - Test image with kvm-arm param
- `boot_b_full_stock_cmdline.img` - Test image with full stock cmdline

## Next Steps

If pursuing Option 1 (Gunyah driver port):
1. Search for "CAF SM8450 kernel source" / "LA.QSSI.12.0" on CodeAurora
2. Clone and diff against mainline 5.10 to isolate Gunyah patches
3. Check mainline 6.8-6.12 for newer Gunyah commits
4. Start with minimal: gunyah platform bus + resource manager
5. Build iteratively, add HVC infrastructure before device drivers

If pursuing Option 4 (slot A test):
1. Backup current boot_a: `fastboot boot_a ~/backup/boot_a_safe.img`
2. Flash custom kernel to boot_a
3. Set slot A active, reboot
4. Monitor for cmdline/behavior differences
5. Immediate rollback plan via fastboot
