# Case: NX679J ABL Bootloader Reverse Engineering

## Authorization
**Status**: GRANTED
**Type**: Local hardware analysis (owned device)
**Justification**: Personal device with unlocked bootloader. Educational research to understand boot process.

## Target
**Type**: Bootloader binary (ABL - Android Boot Loader)
**File**: abl_a.img (1,048,576 bytes)
**Hash**: SHA256: 443d1956e836aea3fe30ac13113dae787d88223c1e24f875c6831f69d86acea3
**Architecture**: ARM 32-bit ELF + UEFI Firmware Volume
**Source**: Extracted from Nubia Red Magic 7 (NX679J) via `dd if=/dev/block/by-name/abl_a`

## Objective
**Primary**: Understand why custom upstream kernels crash at 15 seconds
**Secondary**: Locate where ABL injects `kvm-arm.mode=protected` cmdline parameter
**Goal**: Find method to bypass or disable Gunyah hypervisor protected mode enforcement

## Network Profile
**Profile**: OFFLINE_LOCAL_ANALYSIS
**External Access**: None required - all analysis local

## Tools Available
- radare2 (installed)
- Python 3
- dtc (device tree compiler)
- Binary analysis tools (strings, hexdump, dd, file)

## Evidence Chain
See: evidence-timeline.md

## Constraints
- No modification of bootloader partitions on device (reversible tests only)
- DTB/vendor_boot modifications acceptable (easily reversible)
- No bricking risk tolerance
