---
name: debugfs-callback-triage
description: "Use when debugging debugfs controls. Trace I/O and state."
version: 0.1.0
author: User, Hermes Agent
license: MIT
platforms: [linux]
metadata:
  hermes:
    tags: [linux, kernel, debugfs, research]
    category: kernel-re
    phase: evidence
---

# Debugfs Callback Triage

Use this workflow when a debugfs write appears successful but readback is blank, surprising, or inconsistent.

## When to Use

- Use when diagnosing a debugfs control's read/write behavior or verifying a stored setting.
- Do not use for ordinary sysfs attributes or device behavior without a debugfs callback path.

## Procedure

1. **Pin the implementation before interpreting behavior.** Identify the running kernel build and exact vendor source revision when possible. Treat mainline and sibling Qualcomm branches as examples only until they match the target build.
2. **Read the matching debugfs documentation and source.** Locate the `debugfs_create_file()` call, supplied `struct file_operations`, inode mode, and parent/name construction. Permissions and available operations are separate facts.
3. **Trace the version-matched VFS dispatch.** Check how `read(2)` and `write(2)` dispatch to `.read`/`.read_iter` and `.write`/`.write_iter`; distinguish a missing callback error from a callback returning zero (EOF).
4. **Inspect callback input and return paths.** Preserve exact bytes, including newline, truncation limits, position checks, parser failures, partial counts, and paths that return a positive count without changing state.
5. **Inspect readback as bytes.** Follow buffer length, formatting, `copy_to_user()`, returned count, newline/NUL behavior, and `*ppos`. A same-descriptor write may advance the offset before a subsequent read; test with a fresh descriptor or seek to zero.
6. **Trace the stored field to its consumer.** Determine whether it changes a real hardware operation, selects a simulated result, or only affects logging. A readable software field is not hardware-verification evidence.
7. **Verify runtime claims with syscall-level evidence.** Record the exact write payload and return value, then read from a known offset with a nonzero buffer and preserve the byte count, errno, exit status, and hex output. Correlate this with the active build/source and the state-consuming path.
8. **Report applicability and gaps.** Cite exact source revisions and URLs; label any reference branch that is not proven to match the target. Do not infer the active setting from a shell variable alone.

## Pitfalls

- A positive `write(2)` count proves only what the callback returned; it does not prove a requested token matched or a state transition occurred.
- A missing `.read`/`.read_iter` is not equivalent to a successful empty read; inspect errno and callback dispatch.
- Mode bits such as `0644` do not create read semantics when `.read` is absent.
- Text output may contain no newline or may be padded with NULs; use a byte-oriented read when shell output is ambiguous.
- Debugfs is not a stable userspace ABI. Never project a public upstream or Qualcomm reference implementation onto an unverified vendor build.

## Verification

Before concluding, confirm that the report names the exact or best-known kernel/source revision, distinguishes missing callbacks from zero-byte EOF, cites the callback's input/return/offset behavior, traces the field to its consumer, and labels any runtime evidence or target-source gap explicitly.
