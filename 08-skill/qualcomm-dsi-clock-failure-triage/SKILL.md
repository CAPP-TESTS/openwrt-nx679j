---
name: qualcomm-dsi-clock-failure-triage
description: Use when Qualcomm DSI clocks fail; trace errno propagation.
version: 0.1.0
author: user, Hermes Agent
license: MIT
platforms: [linux, macos, windows]
metadata:
  hermes:
    tags: [kernel, qualcomm, dsi, clocks, debugging]
    related_skills: [driver-protocol-recovery]
---

# Qualcomm DSI Clock Failure Triage

Trace DSI clock-parent, PLL-source, pixel-rate, and link-clock failures through the exact kernel and vendor source pair. Separate the low-level error that creates an errno from wrappers and later failures.

## When to Use

- Qualcomm MSM DSI logs contain an RCG parent/update warning followed by DSI source or link-rate errors.
- A PLL-source callback, parent-switch return, or pixel/link-clock `-EINVAL` needs causal ranking.
- Don't use this for live-device intervention unless separately requested; this is a source-tracing workflow.

## Prerequisites

- Identify the exact kernel version and the matching Qualcomm DSI/display-driver tree.
- Keep the common-clock/RCG source and DSI vendor source from the same target branch; don't substitute a different kernel's line behavior.
- Use `search_files` and `read_file` to establish function bodies and line ranges before diagnosing.

## Procedure

1. **Identify the caller path.** Trace link-clock enable into PHY configure/toggle callbacks, display PLL enable, and display source switching. Record whether each callback's return value is checked or discarded.
2. **Trace the PLL implementation.** Follow `dsi_display_phy_pll_enable()` → `dsi_phy_pll_toggle()` → the catalog-selected PLL ops. Distinguish PLL configuration/lock errors from the subsequent RCG parent switch. If control returns early on PLL failure, an RCG warning reached later in that function means the PLL-toggle step passed.
3. **Trace source selection.** Follow `dsi_display_set_clk_src()` → `dsi_ctrl_set_clock_source()` → `dsi_clk_update_parent()`. Check call order: the byte parent may be attempted before pixel, and a byte failure can exit before the pixel-parent update. Record rollback behavior, ignored rollback errors, and when cached source fields are committed.
4. **Trace the RCG errno.** Match the concrete clock name to its SoC clock definition and ops. Follow `.set_parent` into `clk_rcg2_set_parent()` and the RCG update-ack polling. State the precise condition for the returned errno; for example, a stuck `CMD_UPDATE` poll is not itself evidence of a PLL-lock failure.
5. **Continue through HS link rates.** Determine whether `DSI_LINK_CLK_START` includes set-rate, prepare, and enable. Follow `dsi_link_hs_clk_set_rate()` in order (byte, pixel, optional byte-interface) and verify whether the rate failure occurs before prepare/enable.
6. **Explain pixel `-EINVAL` from implementation.** Inspect `clk_pixel_determine_rate()` and `clk_pixel_set_rate()`, the available fractional ratios, parent-rate tolerance, and actual target/parent rates. Don't infer the specific failing callback from the outer `clk_set_rate()` log alone.
7. **Rank errors.** Label the earliest low-level failure, direct errno propagation, and later symptoms separately. A later pixel `-EINVAL` is a likely cascade when parent switching failed and the caller still proceeds, but preserve unsupported target-rate or invalid-parent alternatives until live rates and CFG/CMD state are known.
8. **Verify the report.** Cite exact source paths and line ranges for every log/return edge; say what static source cannot determine about hardware state. Do not issue device actions unless explicitly requested.

## Pitfalls

- Do not call an RCG acknowledgement timeout a DSI PLL failure just because the failed parent is a PLL output.
- Do not assume the attempted parent switch changed hardware state: the source callback can fail, and rollback may itself be ignored.
- Do not treat a link-clock `-EINVAL` wrapper as an independent third failure if the HS start path returned the earlier pixel-rate errno.
- Do not claim a later error is certainly cascading without the requested rate, parent rate, selector state, and relevant callback results.

## DRM commit return is not scanout evidence

- In Qualcomm vendor trees, trace the exact atomic-commit implementation rather than assuming upstream helper semantics. A custom commit worker may queue/flush work while a `void` SDE kickoff callback logs resource failure without propagating it to the DRM ioctl; `return 0` can therefore coexist with a failed kickoff.
- Separate installed software state (`FB_ID`/CRTC state) from physical panel latch. If old and new framebuffers contain the same pixels, a matching webcam image is non-discriminating; require a safe, visibly distinct frame or other independently valid completion evidence before calling scanout verified.
- For command-mode panels, map logged event/state values to the exact vendor enum. `KICKOFF` while software state is `IDLE` supports an idle-exit hypothesis but does not prove hardware power-collapse or identify the cause of a clock RCG acknowledgement failure.
- Keep the exact runtime source/version gap explicit; a close vendor peer or a newer-SoC patch is comparison evidence, not authorization to transplant a fix.

## Where the XO <-> PHY-PLL reparent actually happens (modeset vs idle wake)

- The byte/pixel RCG parent switch is driven by DSI **link-clock ON/OFF transitions** (`dsi_display_link_clk_enable/disable` calling the `phy_config_cb`/`phy_pll_toggle_cb` callbacks), never by the SDE resource-control state machine. Any path that gates link clocks (idle-pc, unprepare, PRE_STOP/STOP) sets the parent to XO and stops the PLL, so the next enable re-runs PLL-on-then-reparent. A full modeset and a PRE_STOP/STOP + POST_MODESET cycle therefore do **not** skip the switch.
- The only structural skips are: link clocks never gated (commit while the RC state is ON, or seamless DMS/dyn-clk modesets where the DSI bridge pre_enable returns early) and the continuous-splash first enable, which skips both phy callbacks entirely. Avoidance there comes from never stopping the PLL, not from a cold re-init.
- Do not attribute avoidance to "cold PLL re-init": both a mode-set enable and an idle wake run the same PLL configure + PLL toggle + reparent; the mode-set path only adds a PHY software reset, PHY enable and controller/host re-init ahead of it.
- Upstream contrast: mainline's DPU RC helper only does runtime PM and IRQ control (no connector clk_ctrl), and mainline picks the DSI byte/pixel RCG parents once in DT, so it has no runtime equivalent of this reparent failure.

## Verification

A complete trace names (a) the exact function returning the first errno, (b) every caller that logs or propagates it, (c) whether a later operation proceeds despite callback failure, and (d) the implementation path and runtime values needed to confirm or reject a cascade hypothesis.
