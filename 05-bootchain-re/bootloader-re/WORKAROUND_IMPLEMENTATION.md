# DTB Injection Workaround - Implementation Summary

## Status: PREPARED & VERIFIED ✅

The workaround has been fully implemented and is ready for deployment once device connectivity is restored.

## What Was Implemented

### 1. Modified Device Tree (DTB)
**File**: `dtb-injection-test/vendor_dtb_modified.dtb` (422 KB)  
**Modification**: Injected `kvm-arm.mode=nvhe` at the start of `/chosen/bootargs`  
**Verification**: ✅ Confirmed parameter present at correct offset

**Original bootargs**:
```
console=ttyMSM0,115200n8 loglevel=6 kpti=0 ...
```

**Modified bootargs**:
```
kvm-arm.mode=nvhe console=ttyMSM0,115200n8 loglevel=6 kpti=0 ...
```

### 2. Modified Vendor Boot Image
**File**: `dtb-injection-test/vendor_boot_modified.img` (96 MB)  
**Method**: DTB injected at offset 0x995000 using dd  
**Verification**: ✅ DTB magic (0xd00dfeed) confirmed at correct offset  
**Verification**: ✅ `kvm-arm.mode=nvhe` string confirmed present in DTB section

### 3. Technical Approach

**Hypothesis**: Linux kernel cmdline parameter parsing follows this order:
1. Built-in `CONFIG_CMDLINE` (if set)
2. Device tree `/chosen/bootargs`
3. Bootloader-provided cmdline (ABL)

**Strategy**: 
- ABL will add `kvm-arm.mode=protected` AFTER device tree bootargs
- Final cmdline will be: `kvm-arm.mode=nvhe ... kvm-arm.mode=protected`
- If kernel uses "first parameter wins" → our `nvhe` disables protected mode
- If kernel uses "last parameter wins" → ABL's `protected` still enforced

**Alternative Strategy (if first fails)**:
- Build kernel with `CONFIG_CMDLINE="kvm-arm.mode=nvhe"`
- Set `CONFIG_CMDLINE_FORCE=y` to completely ignore bootloader cmdline
- This guarantees override regardless of parameter order

## Expected Test Results

### Success Case:
- Boot completes past 15-second mark
- Device enters Android/OpenWrt userspace
- `/proc/cmdline` shows both parameters (order determines winner)
- Upstream kernel 6.6.y boots without Gunyah drivers

### Partial Success:
- Different crash behavior (not at 15 seconds)
- Different error mode (not EDL, not fastboot)
- Indicates parameter was parsed but other issue exists

### Failure Case:
- Same 15-second crash to fastboot/EDL
- Indicates "last parameter wins" and ABL overrides our value
- Next step: Try CONFIG_CMDLINE_FORCE approach

## Deployment Steps (When Device Ready)

```bash
# 1. Enter fastboot mode
adb reboot bootloader

# 2. Flash modified vendor_boot to slot B
fastboot flash vendor_boot_b dtb-injection-test/vendor_boot_modified.img

# 3. Flash custom kernel to boot_b (or keep stock for first test)
fastboot flash boot_b <custom_or_stock_boot.img>

# 4. Set slot B active
fastboot --set-active=b

# 5. Reboot and monitor
fastboot reboot
# Watch for:
# - Boot time (should exceed 15 seconds if successful)
# - USB device state (should NOT become EDL 05c6:900e)
# - adb connectivity (indicates successful boot)

# 6. Verify if booted successfully
adb shell su -c "cat /proc/cmdline" | grep kvm-arm.mode
# Should show BOTH parameters if successful
```

## Recovery Steps

If device crashes:
```bash
# Physical button recovery
# Power off: Hold Power ~10s
# Enter fastboot: Power + Vol Down

# Or via USB if detected
fastboot reboot bootloader

# Flash stock vendor_boot back to slot B
fastboot flash vendor_boot_b <stock_vendor_boot_a.img>

# Or just boot from slot A (stock)
fastboot --set-active=a
fastboot reboot
```

## Files Ready for Testing

```
dtb-injection-test/
├── vendor_boot_modified.img      # Modified vendor_boot (96 MB)
├── vendor_dtb_modified.dtb       # Modified DTB only (422 KB)
├── vendor_dtb.dtb                # Original DTB (1 MB extracted)
├── vendor_dtb_clean.dts          # Decompiled source (readable)
└── TEST_PLAN.md                  # Detailed test plan
```

## Risk Assessment

**Risk Level**: LOW ✅

**Why Safe**:
- Only modifying vendor_boot_b (slot B)
- Stock vendor_boot_a (slot A) untouched
- Can always boot from slot A
- No bootloader/ABL modification
- Easily reversible via fastboot

**Worst Case**: Boot fails, revert to slot A

## Alternative Workarounds (If This Fails)

### Option B: CONFIG_CMDLINE_FORCE
```bash
# In kernel .config:
CONFIG_CMDLINE="kvm-arm.mode=nvhe"
CONFIG_CMDLINE_FORCE=y
```
Kernel completely ignores bootloader cmdline. Guaranteed to work.

### Option C: Kernel Parameter Lock
Patch kernel to lock `kvm-arm.mode` early:
```c
// In arch/arm64/kernel/setup.c
early_param("kvm-arm.mode", kvm_mode_early_setup) {
    // Force nvhe, ignore all subsequent values
    return 0;
}
```

### Option D: Continue Stock Kernel
Use stock 5.10 kernel (has Gunyah support) + custom initramfs.  
Already working for OpenWrt - safest option long-term.

## Monitoring During Test

Watch these indicators:
1. **Time**: Should boot past 15 seconds (previous crash point)
2. **USB ID**: Should NOT become 05c6:900e (EDL mode)
3. **Fastboot**: Should NOT enter fastboot mode (18d1:d00d)
4. **ADB**: Should respond if boot successful

## Success Metrics

✅ Boot time > 20 seconds  
✅ USB device maintains normal Android/ADB ID  
✅ `adb devices` shows device online  
✅ `/proc/cmdline` contains our injected parameter  

## Current Status

- [x] DTB extracted from vendor_boot
- [x] DTB modified with kvm-arm.mode=nvhe
- [x] DTB recompiled and verified
- [x] Modified DTB injected into vendor_boot
- [x] Injection verified in vendor_boot image
- [x] All files prepared
- [ ] **NEXT**: Flash to device when connectivity restored
- [ ] **NEXT**: Monitor boot behavior
- [ ] **NEXT**: Capture /proc/cmdline if successful
- [ ] **NEXT**: Document results

## Evidence Reference

See: `work/nx679j-abl-re/evidence-timeline.md` - Evidence E-010  
Attack Path: P-001 (DTB Bootargs Override)
