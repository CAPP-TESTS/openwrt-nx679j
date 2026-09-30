# ABL Reverse Engineering & Boot Chain Analysis
**Target:** Nubia Red Magic 7 (NX679J) Android Bootloader (ABL)  
**Date:** 2026-08-06

## Binary Analysis Summary

### ABL Structure
```
File: abl_a.img (1,048,576 bytes)
Format: ELF32 ARM executable
Load Address: 0x9fa00000
Entry Point: 0x9fa00000
Architecture: ARM 32-bit (not ARM64!)
Stripped: Yes (no symbols)
Static: Yes
```

### Key Findings
1. **32-bit ARM bootloader** - Runs in ARM32 mode before handing off to ARM64 kernel
2. **UEFI Firmware Volume embedded** - Starts at offset 0x1028, signature `_FVH`
3. **Compressed/Obfuscated** - No readable strings for "kvm-arm", "linux", "kernel" visible
4. **Cmdline constructed at runtime** - Parameters not embedded as strings

### UEFI Firmware Volume
```
Offset: 0x1028 (4136 bytes)
Signature: _FVH (UEFI Firmware Volume Header)
GUID: 78e58c8c-3d8a-1c4f-9935-896185c32dd3
Size: 0x28000 (163,840 bytes)
```

The FV likely contains:
- DXE (Driver Execution Environment) modules
- Boot services drivers
- Kernel loader code
- Device tree manipulation code
- **Cmdline construction logic**

## Reverse Engineering Approaches

### Approach 1: Dynamic Analysis (Recommended)
Since static analysis is blocked by compression/obfuscation, dynamic tracing would be ideal:

**Tools needed:**
- JTAG/SWD debugger (expensive, requires hardware access)
- QEMU ARM emulation (complex to set up with proper SM8450 peripherals)
- ABL printf/logging backdoor (if exists)

**What to trace:**
1. Entry point → UEFI initialization
2. Device tree loading/patching
3. Cmdline buffer construction
4. Where `kvm-arm.mode=protected` gets added
5. Kernel handoff (jump to 0x9fa00000 → kernel Image)

### Approach 2: Comparative Analysis
Compare multiple ABL versions to find differences:

```bash
# If you have ABL from other devices:
- SM8450 devices with unlocked bootloader
- Different firmware versions for NX679J
- Similar Nubia devices (Red Magic 6/8)
```

Look for:
- Cmdline differences
- Boot mode checks (slot A vs B behavior)
- Security policy changes

### Approach 3: Firmware Extraction & Decompression
Use specialized UEFI tools:

```bash
# Install UEFITool
git clone https://github.com/LongSoft/UEFITool.git
cd UEFITool && qmake && make

# Extract FV sections
UEFIExtract abl_a.img

# Look for decompressed PE32 executables
find . -name "*.efi" -o -name "*.pe32"
```

### Approach 4: Patch & Test (Penetration Testing)
Instead of fully understanding ABL, try targeted patches:

## Penetration Testing Vectors

### Vector 1: Cmdline Injection via Device Tree
**Hypothesis:** ABL reads `/chosen/bootargs` from DTB and appends to it

**Test:**
1. Extract vendor_boot DTB
2. Modify `/chosen/bootargs` to include `kvm-arm.mode=nvhe` (override protected)
3. Repack and flash
4. See if our parameter wins or ABL's wins

**Risk:** Low - DTB modification is reversible

---

### Vector 2: Slot A vs Slot B Behavior Differences
**Hypothesis:** ABL may apply different security policies to recovery slot (A) vs user slot (B)

**Test:**
1. Flash custom kernel to **boot_a** instead of boot_b
2. Set slot A active
3. Check if boot behavior differs
4. Monitor for different cmdline parameters

**Risk:** Medium - May need fastboot recovery

**Already completed:** Device crashed to fastboot (not EDL) when booting slot B with kvm-arm param

---

### Vector 3: Boot Mode Manipulation
**Hypothesis:** ABL behavior changes based on boot reason/mode

**Test boot modes:**
- Normal boot: `fastboot reboot`
- Recovery boot: `fastboot boot recovery.img`
- Fastboot boot: `fastboot boot boot.img` (doesn't flash, temporary)
- Crash/panic reboot: Kernel panic triggers different path

**Try:** `fastboot boot boot_b_kvm_protected.img` - boots without flashing

**Risk:** Low - no persistent changes

---

### Vector 4: ABL Version Downgrade
**Hypothesis:** Older ABL versions may not enforce protected mode

**Test:**
1. Find older NX679J firmware packages (Android 11 vs 12)
2. Extract older abl_a.img
3. Flash to abl_a partition
4. Test custom kernel boot

**Risk:** HIGH - May brick if anti-rollback fuse is blown

---

### Vector 5: Device Tree Bootconfig Override
**Hypothesis:** Android 12+ bootconfig may override cmdline

**From stock cmdline:** `bootconfig` parameter present

**Test:**
1. Create bootconfig file with `kvm-arm.mode=nvhe`
2. Append to ramdisk as per Android bootconfig spec
3. Flash and test

**Risk:** Low - bootconfig is parsed after kernel starts (may be too late)

---

### Vector 6: ABL Binary Patching (Advanced)
**Goal:** Patch ABL to change `kvm-arm.mode=protected` → `kvm-arm.mode=nvhe`

**Steps:**
1. Extract UEFI FV using UEFIExtract
2. Find PE32 executable containing cmdline logic
3. Disassemble with IDA Pro / Ghidra / radare2
4. Locate string or string-building code
5. Patch binary bytes
6. Repack UEFI FV
7. Rebuild abl_a.img
8. Flash and test

**Challenges:**
- UEFI FV may have checksums/signatures
- ABL may verify itself via XBL
- Anti-rollback protection
- SecureBoot chain validation

**Risk:** EXTREME - High brick risk

---

### Vector 7: XBL Analysis (Lower Level)
**Theory:** XBL (eXtensible Boot Loader) runs before ABL and may be less restrictive

**Test:**
1. Analyze xbl_a.img (3.5 MB)
2. Check if XBL enforces protected mode
3. Look for XBL configuration flags
4. Potentially patch XBL instead of ABL

**Risk:** EXTREME - XBL modification is even more dangerous

---

### Vector 8: Alternative Boot Path via Recovery
**Theory:** Recovery mode may bypass some restrictions

**Test:**
1. Boot to recovery: `adb reboot recovery`
2. Flash custom kernel while in recovery
3. Reboot and monitor behavior

**Risk:** Low

---

### Vector 9: Fastboot OEM Commands
**Theory:** Hidden fastboot commands may expose debug features

**Test:**
```bash
fastboot oem device-info
fastboot oem enable-developer-mode
fastboot oem unlock-critical  # Already tried, failed
fastboot oem disable-protection
fastboot oem help
```

**Risk:** Low - Read-only exploration

---

### Vector 10: Bootloader Unlock Level 2
**Theory:** Device may have deeper unlock levels

**Current state:** `unlocked: yes`, `critical unlocked: false`

**Test:**
```bash
# Try alternative unlock methods
fastboot flashing unlock_critical
fastboot oem unlock-go
fastboot oem nubia-unlock
```

**Risk:** Medium - May trigger data wipe again

---

## Practical Next Steps (Recommended Order)

### Immediate (Low Risk):
1. **Vector 3:** Test `fastboot boot` without flashing
2. **Vector 9:** Explore fastboot OEM commands
3. **Vector 1:** Device tree bootargs modification

### Short Term (Medium Risk):
4. **Vector 2:** Flash to slot A instead of slot B
5. **Vector 8:** Test via recovery mode

### Research Phase:
6. **Approach 3:** Extract UEFI FV modules for analysis
7. **Vector 5:** Implement bootconfig override
8. Compare with other SM8450 device ABLs

### Long Term (High Risk):
9. **Vector 6:** ABL binary patching (only if desperate)
10. **Vector 4:** ABL version downgrade (check anti-rollback first)

## Tools & Resources

### Essential Tools
```bash
# Already installed
radare2        # Disassembler
strings        # String extraction
hexdump        # Binary inspection

# To install
sudo pacman -S ghidra      # Advanced RE (GUI)
git clone https://github.com/LongSoft/UEFITool.git  # UEFI extraction
```

### Reference Materials
- [Qualcomm Boot Chain](https://lineageos.org/engineering/Qualcomm-Firmware/)
- [Android Boot Image Format](https://source.android.com/docs/core/architecture/bootloader/boot-image-header)
- [UEFI Firmware Volume Format](https://uefi.org/specs/PI/1.7A/V3_PI_Firmware_Storage.html)
- [ARM Gunyah Hypervisor](https://www.qualcomm.com/news/onq/2023/08/gunyah-qualcomm-s-new-hypervisor-extends-security-across-devices)

## Key Questions to Answer

1. **Where exactly does ABL add `kvm-arm.mode=protected`?**
   - UEFI DXE driver?
   - Kernel loader module?
   - Hardcoded in compressed PE32?

2. **Is the parameter conditional?**
   - Based on boot slot?
   - Based on device unlock status?
   - Based on boot mode?

3. **Can we override it?**
   - Via device tree?
   - Via bootconfig?
   - Via boot_img cmdline (already tried, ABL ignores)?

4. **What happens if we patch ABL?**
   - Will XBL verify ABL integrity?
   - Will device brick or just refuse to boot?
   - Can we recover via EDL?

## Current Status

**Confirmed:**
- ✅ ABL adds `kvm-arm.mode=protected` dynamically
- ✅ Upstream kernel lacks Gunyah hypervisor support
- ✅ Adding parameter via boot_img cmdline has partial effect (crash → fastboot instead of EDL)
- ✅ Image format is not the issue
- ✅ Device tree doesn't contain the parameter

**Unknown:**
- ❓ Exact ABL code location for cmdline construction
- ❓ Whether parameter is conditional on boot slot
- ❓ Whether fastboot boot (temp) behaves differently
- ❓ Whether recovery mode bypasses restrictions
- ❓ Whether older ABL versions are less restrictive

**Next Test:** Vector 3 - `fastboot boot` temporary boot without flashing
