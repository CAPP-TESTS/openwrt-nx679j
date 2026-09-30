# Surgical Modification Penetration Tests

## Safe Testing Methodology
1. Always backup original partition
2. Test on slot B (keep slot A stock for recovery)
3. Have fastboot access ready
4. Each test is reversible

## Test Vector 1: Hypervisor Partition Patch (MEDIUM RISK)
**Target**: hyp_b (modify slot B hypervisor)
**Method**: Binary patch to disable kvm-arm.mode injection
**Recovery**: Flash hyp_a to hyp_b OR boot from slot A

### Strategy:
Since we can't find "protected" string, the hypervisor likely:
- Constructs it dynamically (char array: 'p','r','o','t','e','c','t','e','d')
- OR has it in a different encoding
- OR uses a config flag that enables/disables it

### Approach A: NOP out cmdline append function
Find function that appends to cmdline, replace with NOP/RET

### Approach B: Replace function return value
Find where it decides to add protected mode, force it to skip

### Approach C: Patch DT property reader
If hypervisor reads a DT property for mode, patch the property name

## Test Vector 2: ABL Cmdline Function Hook (MEDIUM RISK)
**Target**: abl_b
**Method**: Patch AppendVBCmdLine or UpdateCmdLine to skip protected param

We found these functions in decompressed UEFI:
- AppendVBCmdLine
- AppendVBCommonCmdLine  
- UpdateCmdLine
- CatCmdLine

### Strategy:
Patch one of these functions to:
1. Check if parameter starts with "kvm-arm.mode"
2. If yes, skip appending it
3. Continue normal flow

## Test Vector 3: Early Kernel Hook (LOW RISK)
**Target**: Boot image kernel
**Method**: Patch kernel early init to rewrite cmdline in memory

### Strategy:
In arch/arm64/kernel/head.S very early:
1. Read cmdline from bootloader
2. Search for "kvm-arm.mode=protected"
3. Replace "protected" with "nvhe\0\0\0\0" (same length!)
4. Continue boot

This is LOW RISK because kernel is easiest to rebuild if needed.

## Test Vector 4: Hypervisor SMC Hook (HIGH RISK)
**Target**: Hypervisor SMC handler
**Method**: Patch hypervisor to return success for all SMC calls without actually requiring drivers

### Strategy:
Find SMC handler in hypervisor (HVC/SMC opcode handlers)
Patch to return success (x0=0) immediately
This fakes Gunyah driver presence

⚠️ HIGH RISK: Could break security, other features

## Recommended Test Order:

1. **Test Vector 3 (Kernel patch)** - Safest, most controllable
2. **Test Vector 2 (ABL patch)** - Medium risk, well understood
3. **Test Vector 1 (Hypervisor patch)** - Higher risk but most direct
4. **Test Vector 4** - Last resort, highest risk

