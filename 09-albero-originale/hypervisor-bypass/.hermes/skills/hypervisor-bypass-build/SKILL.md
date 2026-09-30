---
name: hypervisor-bypass-build
description: Build and verify the local Gunyah project.
version: 0.1.0
author: user, Hermes Agent
license: MIT
platforms: [linux]
metadata:
  hermes:
    tags: [Android, arm64, Gunyah, kernel, build, verification]
---

# Hypervisor Bypass Build Skill

This skill builds and verifies the local arm64 Gunyah project from the trusted
workspace. It does not flash a device, install a Magisk module, run the
`/dev/mem` attack, or claim that a hypervisor bypass worked.

## When to Use

- Build or check this project's kernel module and arm64 helper.
- Package the existing Magisk module for inspection.
- Capture reproducible toolchain and artifact evidence.
- Do not use for device flashing, boot-image replacement, or runtime MMIO writes.

## Prerequisites

- Linux host with `make`, `aarch64-linux-gnu-gcc`, and `sha256sum`.
- The configured kernel tree at the `KDIR` Makefile default, or an override
  passed to `make`.
- Run from the project root, or set `HYPERVISOR_BYPASS_ROOT`.

## How to Run

Use the `terminal` tool from the project root:

    bash ${HERMES_SKILL_DIR}/scripts/workflow.sh check
    bash ${HERMES_SKILL_DIR}/scripts/workflow.sh build
    bash ${HERMES_SKILL_DIR}/scripts/workflow.sh package
    bash ${HERMES_SKILL_DIR}/scripts/workflow.sh evidence

The `build` action only compiles local artifacts. The `package` action creates
an inspectable ZIP and never sends it to a device.

## Quick Reference

- `check` — validate files, toolchain, and kernel-tree availability.
- `build` — run `make build-all`, then record evidence.
- `module` — compile only `kexec_injector.ko`.
- `userspace` — compile only `build/hyp_attack`.
- `package` — create `build/gunyah_watchdog_pet.zip` from `magisk_module/`.
- `evidence` — write hashes and host/toolchain metadata under `build/evidence/`.

## Procedure

1. Run `check`; stop if the target kernel tree or cross-compiler is missing.
2. Run `build`; require both the module build and arm64 userspace build to
   complete without errors.
3. Run `package` only when the Magisk archive is needed for review.
4. Inspect `build/evidence/manifest.txt` and confirm every generated artifact
   has a SHA-256 entry.
5. Treat deployment and runtime behavior as a separate, manually approved
   operation.

## Pitfalls

- The hardcoded MMIO addresses and watchdog register layout are device-specific
  assumptions; a successful build does not validate them.
- `hyp_attack` requires the target Android device, root, and appropriate
  `/dev/mem` access; it is not runnable on the desktop host.
- A missing kernel `.config` may make the external-module build fail even when
  the source directory exists.
- Existing source and boot-image scripts can be destructive; this workflow
  intentionally does not invoke them.

## Verification

`check` must exit zero and report the expected cross-compiler. `build` must
produce `build/hyp_attack` and the kernel module output. Confirm the userspace
binary with `file` and review `build/evidence/manifest.txt`; no device-side
success claim is valid until logs from the target are separately observed.
