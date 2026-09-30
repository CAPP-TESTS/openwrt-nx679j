# Evidence Timeline - NX679J ABL Analysis

## E-001: Boot Crash Pattern Identified
**Date**: 2026-08-06  
**Method**: Physical device testing  
**Finding**: Custom upstream kernel (6.6.y) crashes exactly at ~15 seconds, device enters EDL mode  
**Evidence**: USB device ID changes to 05c6:900e (Qualcomm QUSB_BULK - EDL/emergency mode)  
**Significance**: Consistent crash timing suggests bootloader watchdog or hypervisor enforcement

## E-002: Stock Kernel Cmdline Analysis
**Date**: 2026-08-06  
**Method**: `adb shell su -c "cat /proc/cmdline"`  
**Finding**: Stock kernel receives additional parameters not in boot_img:
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
**Key Discovery**: `kvm-arm.mode=protected` - Gunyah hypervisor in protected mode  
**Significance**: ABL dynamically adds this parameter at runtime

## E-003: Boot Image Header Analysis
**Date**: 2026-08-06  
**Method**: Hexdump of stock boot_a.img at offset 0x40-0x63F  
**Finding**: Cmdline field in boot_img header is **empty** (all zeros)  
**Significance**: Confirms parameters are NOT embedded in boot image

## E-004: Device Tree Bootargs Analysis
**Date**: 2026-08-06  
**Method**: Decompiled running device tree via dtc  
**File**: `/proc/device-tree` → `running_fdt.dts`  
**Finding**: `/chosen/bootargs` does NOT contain `kvm-arm.mode=protected`  
**Bootargs content**:
```
console=ttyMSM0,115200n8 loglevel=6 kpti=0 log_buf_len=256K 
kernel.panic_on_rcu_stall=1 swiotlb=noforce loop.max_part=7 
cgroup.memory=nokmem,nosocket pcie_ports=compat msm_rtb.filter=0x237 
allow_mismatched_32bit_el0 kasan=off rcupdate.rcu_expedited=1 
rcu_nocbs=0-7 ftrace_dump_on_oops pstore.compress=none 
cpufreq.default_governor=performance
```
**Significance**: ABL prepends parameters AFTER device tree is loaded

## E-005: ABL Binary Structure
**Date**: 2026-08-06  
**Method**: file, readelf, hexdump analysis  
**Binary**: abl_a.img  
**Structure**:
- Format: ELF32 ARM executable
- Entry Point: 0x9fa00000
- Load Address: 0x9fa00000
- Size: 1,048,576 bytes (1 MiB)
- Sections: Stripped (no symbols)
- Embedded: UEFI Firmware Volume at offset 0x1028

**UEFI FV Details**:
- Signature: `_FVH` (Firmware Volume Header)
- GUID: 78e58c8c-3d8a-1c4f-9935-896185c32dd3
- Size: 0x28000 (163,840 bytes)
- Compression: Likely LZMA or proprietary

**Significance**: Cmdline construction logic is inside compressed UEFI firmware volume

## E-006: String Analysis Negative Result
**Date**: 2026-08-06  
**Method**: `strings abl_a.img | grep -iE "(kvm|hypervisor|gunyah)"`  
**Finding**: No direct string matches found  
**Significance**: Strings are either:
1. Compressed within UEFI FV
2. Constructed dynamically at runtime
3. Obfuscated

## E-007: Kernel Image Format Comparison
**Date**: 2026-08-06  
**Method**: Hexdump comparison of kernel Image headers  
**Stock kernel**: Offset 0x00-0x90 → ARM64 PE/COFF format (MZ magic, ARM64 signature)  
**Custom kernel**: Offset 0x00-0x90 → **Identical ARM64 PE/COFF format**  
**Finding**: Image format is NOT the issue  
**Significance**: Rules out image format mismatch as root cause

## E-008: Test - Added kvm-arm.mode=protected in boot_img cmdline
**Date**: 2026-08-06  
**Method**: Built boot image with cmdline parameter injected  
**Result**: Crash at ~15s → **Fastboot mode** (18d1:d00d) instead of EDL  
**Significance**: 
- Progress! Avoided EDL mode
- Kernel recognized parameter but still crashed
- Indicates kernel lacks Gunyah hypervisor drivers

## E-009: Upstream Kernel Config Check
**Date**: 2026-08-06  
**Method**: Grepped kernel .config for Gunyah support  
**Finding**: Only `CONFIG_GUNYAH_WATCHDOG=y` present  
**Missing drivers**:
- CONFIG_GUNYAH (core platform driver)
- CONFIG_GUNYAH_VCPU (vCPU management)
- CONFIG_GUNYAH_IRQ_ROUTING (IRQ virtualization)
- CONFIG_GUNYAH_RESOURCE_MGR (resource manager)
- CONFIG_PROTECTED_VIRTUALIZATION_GUEST (protected mode support)
**Significance**: Mainline 6.6.y lacks full Gunyah integration from Qualcomm vendor kernel

## E-010: DTB Extraction and Modification
**Date**: 2026-08-06  
**Method**: Python script to find DTB magic (0xd00dfeed) in vendor_boot  
**Location**: Offset 0x995000 in vendor_boot_a.img  
**Modification**: Injected `kvm-arm.mode=nvhe` at start of bootargs  
**Status**: Modified DTB compiled, ready for testing  
**Hypothesis**: If kernel uses "first parameter wins", our `nvhe` will override ABL's `protected`

## E-011: Radare2 Analysis Attempt
**Date**: 2026-08-06  
**Method**: r2 analysis with auto-analysis (aaa)  
**Finding**: 
- Entry point decoded but no useful strings
- UEFI firmware volume compressed/obfuscated
- No direct code path to cmdline construction visible
**Limitation**: Without UEFI FV decompression, static analysis limited

---

## Evidence → Finding → Path Chain

### Finding F-001: Root Cause - Missing Gunyah Hypervisor Support
**Evidence**: E-002, E-008, E-009  
**Conclusion**: 
- ABL enforces `kvm-arm.mode=protected` (Gunyah hypervisor)
- Upstream kernel 6.6.y lacks Gunyah platform drivers
- Kernel attempts hardware access, hypervisor kills it at ~15s

**Impact**: HIGH - Blocks all upstream kernel boots

### Finding F-002: ABL Dynamic Parameter Injection
**Evidence**: E-002, E-003, E-004  
**Conclusion**:
- ABL dynamically prepends cmdline parameters at runtime
- Parameters NOT in boot_img header or device tree
- Injection happens AFTER device tree is loaded

**Impact**: MEDIUM - Complicates bypass attempts

### Finding F-003: UEFI Firmware Volume Obfuscation
**Evidence**: E-005, E-006, E-011  
**Conclusion**:
- Cmdline construction logic is in compressed UEFI FV
- Standard string analysis ineffective
- Requires UEFI extraction tools (UEFITool/UEFIExtract)

**Impact**: LOW - Makes static analysis harder but not impossible

---

## Attack Paths

### Path P-001: DTB Bootargs Override (IN PROGRESS)
**Status**: Prepared, awaiting device recovery for testing  
**Method**: Inject `kvm-arm.mode=nvhe` in device tree to override ABL  
**Files**: `dtb-injection-test/vendor_dtb_modified.dtb`  
**Risk**: LOW  
**Success Criteria**: Boot completes past 15 seconds OR different crash behavior

### Path P-002: Kernel CONFIG_CMDLINE_FORCE (PROPOSED)
**Status**: Not started  
**Method**: Build kernel with hardcoded `kvm-arm.mode=nvhe` and `CONFIG_CMDLINE_FORCE=y`  
**Risk**: LOW  
**Effort**: Medium (requires kernel rebuild)

### Path P-003: Port Gunyah Drivers to 6.6.y (PROPOSED)
**Status**: Not started  
**Method**: Extract Qualcomm Gunyah drivers from stock 5.10 kernel, port to 6.6.y  
**Risk**: LOW  
**Effort**: HIGH (2-4 weeks)

### Path P-004: ABL Binary Patching (HIGH RISK)
**Status**: Not started  
**Method**: Extract UEFI FV, patch `protected` → `nvhe`, rebuild  
**Risk**: EXTREME (brick potential)  
**Effort**: HIGH

### Path P-005: Continue Stock Kernel + Initramfs (CURRENT WORKAROUND)
**Status**: Working  
**Method**: Use stock 5.10 kernel (has Gunyah support) with custom OpenWrt initramfs  
**Risk**: NONE  
**Limitation**: Stuck on vendor kernel, no mainline features

---

## Next Actions

1. **IMMEDIATE**: Test Path P-001 (DTB injection) when device recovered
2. **SHORT TERM**: If P-001 fails, try P-002 (CONFIG_CMDLINE_FORCE)
3. **RESEARCH**: Search for Qualcomm SM8450 kernel sources (LA.QSSI.12.0)
4. **LONG TERM**: Consider P-003 if upstream features critically needed

## Tool Index Reference
- radare2: `/usr/bin/r2` (installed)
- Python 3: `/usr/bin/python3` (installed)
- dtc: `/usr/bin/dtc` (installed)

## E-012: DTB Injection Bypass Test (FAILED)
**Date**: 2026-08-06 20:52 UTC  
**Method**: Flashed modified vendor_boot_b with `kvm-arm.mode=nvhe` injected in DTB  
**Result**: Device crashed to fastboot mode at ~15 seconds  
**Significance**: 
- Kernel uses "last parameter wins" for duplicate cmdline parameters
- ABL's `kvm-arm.mode=protected` (added AFTER DTB) overrides our `nvhe`
- DTB injection approach is INEFFECTIVE for this bypass

**Technical Details**:
- Modified vendor_boot successfully flashed to slot B
- Device booted from slot B
- Crash behavior: Same as before (fastboot mode at ~15s)
- No improvement over baseline

**Conclusion**: Path P-001 (DTB Bootargs Override) is NOT viable.  
**Next Steps**: 
- Path P-002: CONFIG_CMDLINE_FORCE kernel build (forces kernel to ignore bootloader cmdline)
- Path P-005: Continue using stock kernel + custom initramfs (current working solution)

---

## Updated Findings

### Finding F-004: Kernel Cmdline Parameter Order - Last Wins
**Evidence**: E-012  
**Conclusion**:
- Linux kernel processes cmdline parameters in order: CONFIG_CMDLINE → DTB → Bootloader
- For duplicate parameters, LAST value wins
- ABL adds parameters AFTER device tree, so ABL always wins
- DTB-based bypasses cannot override ABL parameters

**Impact**: MEDIUM - Eliminates DTB injection as bypass method, requires kernel-level solution

---

## Attack Path Results

### Path P-001: DTB Bootargs Override - ❌ FAILED
**Status**: Tested and failed  
**Test Date**: 2026-08-06 20:52 UTC  
**Result**: Device crashed to fastboot at 15s (same as baseline)  
**Root Cause**: Kernel uses last-parameter-wins, ABL's parameter overrides DTB  
**Conclusion**: NOT viable - requires different approach

### Path P-002: Kernel CONFIG_CMDLINE_FORCE - RECOMMENDED NEXT
**Status**: Not tested  
**Method**: Build kernel with `CONFIG_CMDLINE="kvm-arm.mode=nvhe"` and `CONFIG_CMDLINE_FORCE=y`  
**Expected Result**: Kernel ignores ALL bootloader/DTB cmdline, uses only built-in  
**Success Probability**: HIGH (guaranteed override)  
**Effort**: Medium (requires kernel rebuild)

