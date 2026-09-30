# Penetration Test: Vector 1 - DTB Bootargs Injection

## Objective
Override ABL's hardcoded `kvm-arm.mode=protected` parameter by injecting `kvm-arm.mode=nvhe` into the device tree `/chosen/bootargs`.

## Hypothesis
If the kernel processes cmdline parameters in order (DTB first, then ABL's additions), our `nvhe` parameter should take precedence and disable protected KVM mode, allowing upstream kernel to boot.

## Implementation

### Step 1: Extract DTB from vendor_boot_a
```bash
python3 << EOF
with open('../vendor_boot_a.img', 'rb') as f:
    data = f.read()
magic = b'\xd0\x0d\xfe\xed'
offset = data.find(magic)
with open('vendor_dtb.dtb', 'wb') as out:
    out.write(data[offset:offset+1048576])
EOF
```

DTB found at offset: `0x995000`

### Step 2: Decompile DTB
```bash
dtc -I dtb -O dts vendor_dtb.dtb > vendor_dtb.dts
```

Original bootargs (line 86):
```
bootargs = "console=ttyMSM0,115200n8 loglevel=6 kpti=0 log_buf_len=256K ...";
```

**Note:** Does NOT contain `kvm-arm.mode=protected` - confirming ABL adds it dynamically!

### Step 3: Inject Override Parameter
Modified bootargs (line 86):
```
bootargs = "kvm-arm.mode=nvhe console=ttyMSM0,115200n8 loglevel=6 ...";
```

### Step 4: Recompile DTB
```bash
dtc -I dts -O dtb -o vendor_dtb_modified.dtb vendor_dtb_clean.dts
```

Result:
- Original DTB: 1.0M (extracted with padding)
- Modified DTB: 422K (recompiled without padding)

## Next Steps (When Device Recovered)

### Option A: Replace DTB in vendor_boot (Safer)
```bash
# 1. Extract vendor_boot components
python3 unpack_vendor_boot.py vendor_boot_a.img

# 2. Replace DTB at offset 0x995000
dd if=vendor_dtb_modified.dtb of=vendor_boot_modified.img bs=1 seek=10055680 conv=notrunc

# 3. Flash modified vendor_boot
adb reboot bootloader
fastboot flash vendor_boot_b vendor_boot_modified.img
fastboot reboot
```

### Option B: Build complete boot image with modified vendor_boot (Current)
```bash
# Use stock kernel + custom initramfs + modified vendor_boot DTB
# This tests DTB injection without kernel changes
```

## Expected Outcomes

### Success Case:
- Boot completes past 15-second mark
- Device enters Android/OpenWrt userspace
- `/proc/cmdline` shows: `kvm-arm.mode=nvhe kvm-arm.mode=protected ...`
- Kernel uses first parameter (nvhe), disables protected mode
- Upstream kernel works without Gunyah drivers

### Partial Success:
- Boot fails but behavior changes
- Crash location different
- Different error mode (not EDL, not fastboot)

### Failure Case:
- Same 15-second crash
- ABL's parameter overrides DTB
- OR kernel always uses last parameter
- OR ABL validates/replaces DTB bootargs

## Risk Assessment
**Risk Level:** LOW
- DTB modification is reversible
- vendor_boot_b can be reflashed easily
- vendor_boot_a untouched (recovery path)
- No bootloader/ABL modification

## Technical Analysis

### Linux Kernel Cmdline Parsing
From `init/main.c` and `kernel/params.c`:
```c
// Kernel processes parameters in order:
// 1. Built-in CONFIG_CMDLINE
// 2. Device tree /chosen/bootargs  
// 3. Bootloader-provided cmdline (ABL)
// 4. Last value wins for duplicate parameters
```

**Problem:** If "last value wins", ABL's `protected` will override our `nvhe`.

### Potential Fix: Early Parameter
Check if `kvm-arm.mode` is an `early_param()` that gets locked:
```c
// In arch/arm64/kernel/setup.c
early_param("kvm-arm.mode", kvm_mode_early_setup);
```

If it's an early parameter, FIRST value might be locked and later ones ignored!

## Alternative Approaches If This Fails

### Vector 1B: Use CONFIG_CMDLINE_FORCE
Build kernel with:
```
CONFIG_CMDLINE="kvm-arm.mode=nvhe"
CONFIG_CMDLINE_FORCE=y  # Ignore bootloader cmdline
```

This would make the kernel completely ignore ABL's parameters.

### Vector 1C: Patch Kernel to Ignore kvm-arm.mode
```c
// In arch/arm64/kernel/setup.c
static int __init kvm_mode_early_setup(char *arg) {
    // Force nvhe regardless of cmdline
    return 0;  // Ignore parameter
}
```

## Files
- `vendor_dtb.dtb` - Original DTB extracted from vendor_boot_a
- `vendor_dtb_modified.dtb` - Modified DTB with kvm-arm.mode=nvhe injected
- `vendor_dtb_clean.dts` - Decompiled source with modification

## Status
✅ DTB extracted  
✅ Bootargs modified  
✅ DTB recompiled  
⏳ Waiting for device recovery to flash and test
