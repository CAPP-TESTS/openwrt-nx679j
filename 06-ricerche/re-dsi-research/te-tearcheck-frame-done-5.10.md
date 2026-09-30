# TE / tear-check, `switch_te`, `frame_trigger_mode` and the "no TE → commit never completes" failure mode

Scope: Qualcomm vendor msm-5.10 SM8450 (waipio) SDE + DSI display techpack. Research only, no device access.
Every line number below was read out of the local trees listed in §0.

## 0. Sources (pinned)

| # | Tree | Identity | Use |
|---|------|----------|-----|
| A | `/home/user/sde-re/waipio/msm` | `SOHRA8/android_vendor_qcom_opensource_display-drivers`, HEAD `7d283391248fc613f9413cc2758bf7f9f0fe91f6`, branch `DISPLAY.LA.2.0.r1-11100-WAIPIO.0` (SM8450). URL form: `https://github.com/SOHRA8/android_vendor_qcom_opensource_display-drivers/blob/7d283391248fc613f9413cc2758bf7f9f0fe91f6/msm/...` | **Primary** (all `sde_*.c`/`dsi_*.c` citations, `msm/...`) |
| B | `/home/user/re-dsi-research/techpack/LA.VENDOR.13.2.1.c25` | newer WAIPIO display-driver drop (flat file set) | comparison for POSTED_START default & line drift |
| C | `/home/user/sde-re/hwc-lahaina` | QCOM HWC/SDM (lahaina-era HAL) | how *userspace* picks the trigger mode, hw-recovery registration |
| D | `/home/user/sde-re/lineage20/msm`, `/home/user/sde-re/nubia8550/msm`, `/home/user/re-dsi-research/techpack/LA.UM.9.14.1.c30` | other 5.10/5.15 drops | behaviour-difference checks |
| E | `torvalds/linux` master (`dpu_encoder_phys_cmd.c`, cached at `~/.hermes/profiles/kernel-re/cache/scratch/dpu_cmd.c`) | mainline DPU | upstream contrast |

## 1. How TE is wired and enabled for a DSI command-mode panel

Chain: **panel TE pin → MDP INTF tear-check block → INTF `TEAR_WR_PTR`/`TEAR_RD_PTR` IRQs + CTL start scheduling**.

* TE support is a **hardware-catalog property of the INTF block**, not a panel DT flag:
  `sde_encoder_phys_cmd_intf_te_supported()` tests `BIT(SDE_INTF_TE)` in
  `sde_cfg->intf[idx].features` — `msm/sde/sde_encoder_phys_cmd.c:2030-2037`; result stored in
  `phys_enc->has_intf_te` at `:2080-2081`. (Without it the same code paths fall back to ping-pong TE.)
* **Tear-check programming** — `sde_encoder_phys_cmd_tearcheck_config()`, `:1027-1150`:
  * `vsync_count = vsync_clk / (vtotal * vrefresh)` `:1089` (tear-check counter tick per panel line);
  * `sync_cfg_height = 0xFFF0` `:1099` with the comment "essentially disable sde hw generated TE signal, since hw TE will arrive first" `:1094-1098`; `hw_vsync_mode = 0` `:1092` ("enable external TE after kickoff to avoid premature autorefresh" `:1091`);
  * `vsync_init_val = vdisplay` `:1100`, `start_pos = vdisplay` `:1103`, `rd_ptr_irq = vdisplay + 1` `:1104`, `wr_ptr_irq = 1` `:1105`, thresholds 4/4 (`DEFAULT_TEARCHECK_SYNC_THRESH_START/CONTINUE` `:33-34`, threshold computed in `_get_tearcheck_threshold()` `:932-1025`);
  * written through `hw_intf->ops.setup_tearcheck/enable_tearcheck` `:1140-1144` (ping-pong variant `:1146-1148`).
* **Hardware registers** (`sde_hw_intf.c`): `INTF_TEAR_*` block `:80-94`; `sde_hw_intf_setup_te_config()` `:630-673` writes `SYNC_CONFIG_VSYNC` (external/hw vsync bit + `VSYNC_COUNTER_EN` BIT(19) `:664`), `SYNC_CONFIG_HEIGHT`, `VSYNC_INIT_VAL`, `RD_PTR_IRQ`, `WR_PTR_IRQ`, `START_POS`, `SYNC_THRESH`; `sde_hw_intf_enable_te()` writes `INTF_TEAR_TEAR_CHECK_EN` `:731-746`;
  `sde_hw_intf_connect_external_te()` is BIT(20) of `SYNC_CONFIG_VSYNC` `:764-784` ("read, modify, write to either set or clear listening to external TE", header doc `sde_hw_intf.h:145-147`);
  `sde_hw_intf_poll_timeout_wr_ptr()` polls `INTF_TEAR_LINE_COUNT >= 1` `:714-729` — i.e. the tear-check line counter *is* the progress signal used to prove the frame started.
  **Caveat:** the field named `hw_vsync_mode` sets the same BIT(20) that `connect_external_te` calls "external TE" (`:643-644` vs `:775-781`), and the driver sets `hw_vsync_mode = 0` while its comment claims external TE. The vendor naming is inverted/ambiguous — do **not** infer the TE source polarity from the field name.
* **IRQ wiring** — `sde_encoder_phys_cmd_init()` `:2091-2133`: with `has_intf_te`, `INTR_IDX_WRPTR → SDE_IRQ_TYPE_INTF_TEAR_WR_PTR` `:2129-2133`, `INTR_IDX_RDPTR → SDE_IRQ_TYPE_INTF_TEAR_RD_PTR` `:2107-2112`, `INTR_IDX_AUTOREFRESH_DONE → SDE_IRQ_TYPE_INTF_TEAR_AUTO_REF` `:2117-2120`; handlers `sde_encoder_phys_cmd_wr_ptr_irq()` `:324-364`, `..._te_rd_ptr_irq()` `:276-322`, `..._pp_tx_done_irq()` `:206-249`.
* **DSI side**
  * TE input source for the controller: `qcom,mdss-dsi-te-pin-select` → `host->te_mode` (`dsi_panel.c:1289-1295`; enum `DSI_TE_ON_DATA_LINK`/`DSI_TE_ON_EXT_PIN` `dsi_defs.h:411-419`); DSI `TRIG_CTRL` BIT(31) set only for external-pin TE (`dsi_ctrl_hw_cmn.c:104-107`); `qcom,mdss-dsi-te-dcs-command` `dsi_panel.c:1876-1890`.
  * TE-based **ESD** path reads the TE pulse as a GPIO IRQ: `qcom,platform-te-gpio` (`dsi_display.c:720`), `dsi_display_status_check_te()` `dsi_display.c:965-994` (15*20 ms timeout per recheck, `MAX_TE_RECHECKS` when `te_check_override` `:1058-1059`), called from `dsi_display_check_status()` `:1021-1114`.
  * **Watchdog-TE / "sim panel"**: `qcom,mdss-dsi-te-using-wd` → `panel->te_using_watchdog_timer` `dsi_panel.c:2477-2478`; `is_sim_panel()` is exactly that flag `dsi_display.c:117-122`; it feeds `info->is_te_using_watchdog_timer` for command mode `:6918`, and `|= display->sw_te_using_wd` (set by the `:sim-swte` cmdline override `:2731-2733`) at `:4383`. WD TE is therefore the *no-real-TE* configuration, not a normal mode.
  * Panel TE source pin for the MDP vsync source: `qcom,panel-te-source` → `display->te_source` `:720-735` → `info->te_source` `:6929`.
  * Panel jitter (used by `switch_te`, §3): `qcom,mdss-dsi-panel-jitter` → `panel_jitter_numer/denom` `dsi_panel.c:2518-2545` → `mode_info->jitter_numer/denom` + `frame_rate` `dsi_drm.c:650-654`.
  * DCS command scheduling window references the same tear-check window: `TEARCHECK_WINDOW_SIZE 5` `dsi_ctrl.h:60-61`, used to size the cmddma window in command mode `dsi_ctrl.c:1352-1362`; the DSI can also *read back* the MDP INTF tear line count (`dsi_ctrl_hw_2_2.c:271-285`).

## 2. Where the encoder waits for TE / frame-done

Blocking atomic-commit chain (ioctl without `DRM_MODE_ATOMIC_NONBLOCK`):

1. `msm_atomic_commit()` → `msm_atomic_commit_dispatch()`: **`kthread_flush_work(&commit->commit_work)`** — the ioctl blocks until the whole commit worker finishes (`msm/msm_atomic.c:656-658`).
2. `complete_commit()` `msm_atomic.c:525-565` → `msm_atomic_wait_for_commit_done()` **`:556`** (decl. `:141-156`).
3. → `sde_kms_wait_for_commit_done()` `sde/sde_kms.c:1587-1649` → `sde_encoder_wait_for_event(enc, MSM_ENC_COMMIT_DONE)` **`:1633`**; on error: `SDE_ERROR("wait for commit done returned %d\n")` + `sde_crtc_request_frame_reset()` `:1634-1637`.
4. → dispatch table `sde_encoder.c:5592-5641` → `phys->ops.wait_for_commit_done` = `sde_encoder_phys_cmd_wait_for_commit_done()` `sde_encoder_phys_cmd.c:1664-1725`.
5. Master encoder → `_sde_encoder_phys_cmd_wait_for_wr_ptr()` **`:1511-1577`**:
   `wait_info.wq = &phys_enc->pending_kickoff_wq`, `wait_info.atomic_cnt = &phys_enc->pending_retire_fence_cnt`, `wait_info.timeout_ms = phys_enc->kickoff_timeout_ms` (×2 in LP1/LP2 `:1531-1533`), then `sde_encoder_helper_wait_for_irq(phys_enc, INTR_IDX_WRPTR, ...)` **`:1543`**. This is the **TE wait**: the WR_PTR IRQ is the INTF tear-check `WR_PTR_IRQ` (line 1) event (§1).
   * On timeout it re-checks hardware: `ctl->ops.get_start_state()` (CTL_START register, `sde_hw_ctl.c:336-342`) and ESD status — if the CTL start was already consumed (or ESD reports fine) the timeout is **converted to success** and the retire fence is signalled `:1545-1558`.
   * Timeout budget: `sde_encoder_helper_wait_for_irq()` splits the wait in `EVT_TIME_OUT_SPLIT = 2` halves (`sde_encoder.c:71`, `:487-494`); `kickoff_timeout_ms` default **220 ms** (`sde_encoder_phys.h:26`), or `(1000/fps)*2` for fps < 24 (`sde_encoder.h:56`, `sde_encoder.c:4801-4825`).
6. If still `-ETIMEDOUT` → `_sde_encoder_phys_cmd_handle_wr_ptr_timeout()` **`:1608-1662`** (§3/§5 logs `wr_ptr_irq wait failed, switch_te:%d`).
7. Then (or if the wr_ptr wait succeeded) `sde_encoder_phys_cmd_wait_for_commit_done()` decides whether it must also wait for the **pp-done**: `goto wait_for_idle` when `pending_cnt > 1`, or `pending_cnt && (CTL_STATUS & BIT(0))`, or `SERIALIZE` `:1703-1708`; `wait_for_idle` loops `sde_encoder_wait_for_event(..., MSM_ENC_TX_COMPLETE)` `:1712-1725` → `wait_for_tx_complete` `:1580-1606` → `_sde_encoder_phys_cmd_wait_for_idle()` `:754-789` (waits `INTR_IDX_PINGPONG`, i.e. pp-done, same `kickoff_timeout_ms`; timeout → `_sde_encoder_phys_cmd_handle_ppdone_timeout()` `:514-589`).
8. Additionally, a *new* commit is gated by the previous one: `wait_event_interruptible_locked(priv->pending_crtcs_event, ...)` `msm_atomic.c:746-758`, cleared only in `commit_destroy()` `:125-135` at the very end of `complete_commit()` `:564`.

Counters that must be decremented by TE-driven IRQs:
* `pending_retire_fence_cnt` — incremented per kickoff `sde_encoder.c:3756-3760`, decremented by the WR_PTR IRQ `sde_encoder_phys_cmd.c:340-348` (that IRQ also signals the **retire fence**).
* `pending_kickoff_cnt` — incremented per kickoff `sde_encoder.c:3988-4006` (before `_sde_encoder_trigger_start()` `:4020`), decremented by the pp-done IRQ `sde_encoder_phys_cmd.c:223-233`.
* CRTC out-fence (release fence) is signalled **only** from the pp-done / pp-done-timeout events: `SDE_ENCODER_FRAME_EVENT_DONE | SDE_ENCODER_FRAME_EVENT_SIGNAL_RELEASE_FENCE` `:225-226` and `:521-522` → `sde_crtc_frame_event_work()` `sde_crtc.c:2754-2840` → `sde_fence_signal(sde_crtc->output_fence, ...)` `:2819-2833`; retire fence `SIGNAL_RETIRE_FENCE` `:2827-2831`.

`frame_trigger_mode` is re-read from the connector at every kickoff: `sde_connector_get_property(..., CONNECTOR_PROP_CMD_FRAME_TRIGGER_MODE)` `sde_encoder.c:4621-4625` → `params->frame_trigger_mode` `:4633` → `phys_enc->frame_trigger_mode` `sde_encoder_phys_cmd.c:1404`.

## 3. `switch_te` — what it is and what it is *not*

```
1614 bool switch_te;
1618 switch_te = _sde_encoder_phys_cmd_needs_vsync_change(phys_enc, profile_timestamp);
```
(`profile_timestamp` is captured at entry of `wait_for_commit_done()`, `:1669`.)

* `_sde_encoder_phys_cmd_needs_vsync_change()` `:1458-1509` walks `cmd_enc->te_timestamp_list` (filled only by the TE RD_PTR IRQ, `:299-305`) and compares **consecutive TE timestamps that arrived after `profile_timestamp`** against `[1/fps − jitter, 1/fps + jitter]` (`sde_encoder_helper_get_jitter_bounds_ns()` `sde_encoder.c:5643-5655`, jitter from panel DT §1). It returns `true` iff at least one such pair is out of bounds.
* Consequence (proven by the loop shape `:1483-1492`): a pair needs **two TE events after the commit started**. With zero or one TE event since the profile timestamp, `switch_te == false`.
* Meaning: `switch_te == 1` = "TE-to-TE spacing is irregular → retry the wait with the INTF watchdog timer as the vsync source" `:1625-1636` (`sde_encoder_helper_switch_vsync(parent, true)` `sde_encoder.c:1446-1468` → `control_te(false)`, `_sde_encoder_update_vsync_source()` with `is_te_using_watchdog_timer=true` → `SDE_VSYNC_SOURCE_WD_TIMER_4 + te_source` `:1427-1430`, `control_te(true)`; WD timer programming `sde_hw_intf.c:463-484`, source select `:849-860`).
* **`switch_te == 0` is *not* evidence of a healthy TE.** It is exactly what this code prints when the jitter test found nothing to compare — which includes "no TE arrived after the commit started", "only one TE arrived", or "TE was regular but the WR_PTR IRQ/completion was lost for another reason". In the observed log (`wr_ptr_irq wait failed, switch_te:0`, `:1643-1645`) the driver therefore **skipped the watchdog-TE retry** (`:1625` branch not taken) and fell straight through to the "signal retire fence to avoid device freeze" path `:1638-1657`. (Whether TE was physically absent or merely its WR_PTR completion was lost is *not* decidable from `switch_te:0` alone — hypothesis, see §6.)
* Related note: `if (sde_connector_panel_dead(connector)) ret = _sde_encoder_phys_cmd_wait_for_wr_ptr(phys_enc);` `:1623-1624` — for a *dead* panel the driver re-waits without the watchdog retry, and `is_te_using_watchdog_timer || panel_dead` is what routes the vsync source to the WD timer `sde_encoder.c:1427`, `sde_encoder_phys_cmd.c:1973-1985`.

## 4. `frame_trigger_mode` (connector/CRTC property, 0/1/2) and the three trigger paths

Canonical definitions (kernel): `sde_kms.h:177-189`
```
FRAME_DONE_WAIT_DEFAULT     : waits for frame N pp_done interrupt before triggering frame N+1
FRAME_DONE_WAIT_SERIALIZE   : serialize pp_done and ctl_start irq for frame N without next frame trigger wait
FRAME_DONE_WAIT_POSTED_START: Do not wait for pp_done interrupt for any frame. Wait will trigger only for error case.
```
Canonical definitions (userspace): `sdm/include/core/display_interface.h:236-242`
```
kFrameTriggerDefault   = 0  Wait for pp_done of previous frame to trigger new frame
kFrameTriggerSerialize = 1  Trigger new frame and wait for pp_done of this frame
kFrameTriggerPostedStart = 2 Posted start mode, trigger new frame without pp_done
```
Property: installed **only for command-mode displays** as enum `"frame_trigger_mode"` = `{"default","serialize_frame_trigger","posted_start"}` (`sde_connector.c:87-91`, install `:3221-3227`), UAPI id `CONNECTOR_PROP_CMD_FRAME_TRIGGER_MODE` (`msm/msm_drv.h:235`). HAL sets it through `DRMOps::CONNECTOR_SET_FRAME_TRIGGER` (`hw_peripheral_drm.cpp:862-886`).

Where each mode changes behaviour (all `sde_encoder_phys_cmd.c` unless noted):

| Mode | Pre-kickoff (`prepare_for_kickoff` `:1388-1456`) | Commit wait (`wait_for_commit_done` `:1664-1725`) | Recovery/reset |
|---|---|---|---|
| 0 default | **waits for idle (pp-done) before starting the next frame** `:1410-1423`; on failure `atomic_set(pending_kickoff_cnt, 0)` and `SDE_ERROR("failed wait_for_idle")` | wr_ptr wait `:1679-1692`; `wait_for_idle` if `pending_cnt>1` or `CTL_STATUS&BIT(0)` `:1703-1708` | `sde_crtc_reset_hw` runs on error (`sde_crtc.c:4071-4079`) |
| 1 serialize | same as default (no mode-specific code) | extra `goto wait_for_idle` when `!rc && mode == SERIALIZE` `:1707` → pp-done is serialized with ctl_start for the frame | reset as default |
| 2 posted_start | **no pp-done wait at all** — flush + `CTL_START` are posted immediately `_sde_encoder_kickoff_phys()` `sde_encoder.c:3925-4033` | still does the master wr_ptr (TE) wait `:1679-1692`; maps a **missed pp-done** using `CTL_STATUS` bit0 and self-completes the frame: `_sde_encoder_phys_cmd_is_scheduler_idle()` `:709-752` (comment "Handle cases where a pp-done interrupt is missed due to irq latency with POSTED start" `:724-727`), called from `_sde_encoder_phys_cmd_wait_for_idle()` `:776`, `:782` | **hw reset deliberately skipped**: `sde_crtc.c:4071-4079` and `sde_crtc_request_frame_reset()` `sde_crtc.h:655-665` |

`CTL_STATUS` = control scheduler status register (`sde_hw_ctl.c:30, 782-787`); the driver reads it to decide "a frame is still pending in the control scheduler".

**Who chooses which mode**
* Driver default in the newer WAIPIO drop: "Keep posted start as default configuration in driver if SBLUT is supported on target. Do not allow HAL to override driver's default frame trigger mode." — forced to `FRAME_DONE_WAIT_POSTED_START` when `sde_kms->catalog->dma_cfg.reg_dma_blks[REG_DMA_TYPE_SB].valid` (tree B, `sde_encoder.c:5507-5512`). The primary tree A (LA.2.0.r1-11100) has **no** such forced default — only the property.
* HAL: posted start is the steady state; it dynamically drops to `kFrameTriggerDefault` when a **single-buffer** colour feature (IGC/Gamut) is enabled, "Due to lack of hardware support for SB LUTDMA on older targets, control path has to be switched dynamically to non posted start and switch back to posted start after programming the SB LUTs" (`color_manager.cpp:800-830`, feature states `:690-693`), and exposes `SetFrameTriggerMode()` upwards (`hwc_display_builtin.cpp:904-922`).

## 5. The failure mode: no TE → no frame-done → commit never completes

Sequence with TE absent (or its WR_PTR completion never delivered), for a plane-FB-only commit on a command-mode panel:

1. Commit worker: flush, `pending_retire_fence_cnt++` (`sde_encoder.c:3756-3760`), `pending_kickoff_cnt++` (`:3988-4006`), `_sde_encoder_trigger_start()` writes `CTL_START` (`sde_encoder.c:4020` → `sde_hw_ctl.c:327-334`).
2. The frame is only *actually* started when the INTF tear-check block sees the panel TE and the counter crosses the programmed window (`START_POS`/`SYNC_THRESH`, §1). No TE ⇒ `INTF_TEAR_LINE_COUNT` never advances (the same register `sde_hw_intf_poll_timeout_wr_ptr()` polls `:714-729`) ⇒ **no `INTF_TEAR_WR_PTR` IRQ**.
3. `_sde_encoder_phys_cmd_wait_for_wr_ptr()` times out after `kickoff_timeout_ms` (**220 ms**, ×2 in LP1/LP2) `:1511-1544`. Because the frame really is not started, `ctl->ops.get_start_state()` still returns CTL_START set → `frame_pending == true` → the timeout is **not** converted to success `:1545-1551`.
4. `_sde_encoder_phys_cmd_handle_wr_ptr_timeout()` `:1608-1662`: `switch_te` is computed (§3). With `switch_te == 0` the watchdog-TE retry is skipped and the driver logs
   `SDE_ERROR_CMDENC(cmd_enc, "wr_ptr_irq wait failed, switch_te:%d\n", switch_te)` `:1643-1645` — the observed line — then, "Signaling the retire fence at wr_ptr timeout to allow the next commit and avoid device freeze" `:1638-1657`, it calls `handle_frame_done(..., SDE_ENCODER_FRAME_EVENT_SIGNAL_RETIRE_FENCE)` and decrements `pending_retire_fence_cnt`.
5. `wait_for_commit_done()` sees `rc == -ETIMEDOUT` → `goto wait_for_idle` `:1690-1691` → for each pending kickoff it waits `MSM_ENC_TX_COMPLETE` (pp-done) with the same timeout (`:1712-1725`), which also cannot arrive (pp-done is produced after the frame has been transferred, which needs the TE-gated start) → `_sde_encoder_phys_cmd_handle_ppdone_timeout()` `:514-589`, which on the **first** timeout: logs `"kickoff timed out ctl %d koff_cnt %d"`, takes a full register dump, increments `pp_timeout_report_cnt`, sets `enable_state = SDE_ENC_ERR_NEEDS_HW_RESET` `:578`, and reports `FRAME_EVENT_ERROR | SIGNAL_RELEASE_FENCE` `:521-522, 581-586`.
6. `sde_kms_wait_for_commit_done()` logs `"wait for commit done returned %d"` and calls `sde_crtc_request_frame_reset()` `sde_kms.c:1634-1637`; the *next* kickoff will run `sde_crtc_reset_hw()` (CTL reset) `sde_crtc.c:4071-4079`, `:3902-4004`.
7. Panic vs. recovery: without a registered recovery listener the pp-done timeout path requests a **kernel panic** `:571-575`; the panic is gated by the sde_dbg `panic_on_err` knob (`sde_dbg.c:1237-1250`, `:1198-1199`).

**Why userspace sees a hang, not just a 220 ms stall:**
* The commit's out-fence (`sde_crtc->output_fence`, exposed via `CRTC_PROP_OUTPUT_FENCE`/`_sde_crtc_get_output_fence()` `sde_crtc.c:6052-6090`) is signalled **only** on the pp-done/pp-done-timeout events (`:2819-2833`). While TE is missing, an out-fence requested on that commit stays unsignalled for as long as the condition lasts — the kernel puts no timeout on a fence. Userspace (SurfaceFlinger or any client waiting on that fence, or on the next commit's acquire fences) blocks unboundedly.
* Subsequent commits on the same CRTC serialize on `priv->pending_crtcs`, released only in `commit_destroy()` at the end of the stuck worker (`msm_atomic.c:746-758`, `:125-135`, `:564`) — again no timeout.
* Each individual kernel-side wait is bounded (220 ms, ×2 in LP, ×`EVT_TIME_OUT_SPLIT` split internally, plus one pp-done timeout per pending kickoff), so the *ioctl* is not literally infinite by itself; the unbounded parts are the fence the commit promises and the CRTC serialization.
* Mainline contrast (tree E): upstream `dpu_encoder_phys_cmd_wait_for_commit_done()` waits for `INTR_IDX_CTL_START` (`_dpu_encoder_phys_cmd_wait_for_ctl_start()`, `KICKOFF_TIMEOUT_MS`, `dpu_cmd.c:643-666`) and then `pending_kickoff_cnt`/pp-done `:683-695`; `prepare_for_kickoff()` unconditionally waits for idle `:596-607`. Upstream has **no** `frame_trigger_mode`, no posted start, no `switch_te` jitter heuristic and no watchdog-TE fallback — so the exact vendor log message and the "posted start" semantics are vendor-specific, not generic DPU behaviour.

## 6. Legitimate recovery options

**Kernel-side (already in this tree)**
* Bounded waits + escape hatches: `get_start_state()`/ESD-status conversion of the wr_ptr timeout to success `:1545-1558`; retire-fence signalling on wr_ptr timeout "to allow the next commit and avoid device freeze" `:1638-1657`; release-fence + ERROR event on pp-done timeout `:521-522`.
* Watchdog-TE fallback: `sde_encoder_helper_switch_vsync(enc, true)` → INTF WD timer as TE source (`sde_encoder.c:1446-1468`, `:1427-1430`; `sde_hw_intf.c:463-484`, `:849-860`) — used on bad TE jitter `sde_encoder_phys_cmd.c:1625-1636`, on RSC vsync wait failure `sde_encoder.c:1515-1524`, and while the panel is dead `sde_encoder_phys_cmd.c:1973-1985`. Note that on tree A this path is **not** triggered by "TE missing" unless the jitter heuristic fires, and `is_te_using_watchdog_timer` is otherwise reserved for sim panels (§1) — this is the main in-driver gap.
* CTL reset + re-kickoff: `sde_crtc_request_frame_reset()` → `sde_crtc_reset_hw()` (`sde_crtc.c:3902-4004`), triggered from the failed commit-done path (`sde_kms.c:1634-1637`) and from `needs_hw_reset` at the next kickoff (`sde_crtc.c:4071-4079`).
* ESD detection of the *real* root cause (TE stopped): the DSI TE-based ESD check is the only in-tree mechanism that actively treats "no TE pulse" as a fault, with a 300 ms timeout and up to `MAX_TE_RECHECKS` attempts, then `esd_recovery_pending` and panel recovery (`dsi_display.c:965-994`, `:1021-1114`). Tuning that (`esd_config.status_mode = ESD_MODE_PANEL_TE`) is a driver/DT-level fix, not a userspace one.

**Userspace-side (legitimate)**
1. Choose the trigger mode per commit via the `frame_trigger_mode` connector property (HAL: `SetFrameTrigger`/`CONNECTOR_SET_FRAME_TRIGGER`, `hw_peripheral_drm.cpp:862-886`). Serialize (1) adds a pp-done wait for the current frame; it reduces missed-completion races but **cannot remove the TE dependency** — every mode still does the master wr_ptr (TE) wait.
2. Use `DRM_MODE_ATOMIC_NONBLOCK`: the ioctl then returns without `kthread_flush_work()` (`msm_atomic.c:656-658`) and the waiting happens in the commit worker, so a missing TE cannot hold the ioctl. The worker still blocks, and the commits still serialize, so this is a mitigation, not a fix.
3. Register for `DRM_EVENT_SDE_HW_RECOVERY` (`DRM_IOCTL_MSM_REGISTER_EVENT`, HAL `hw_events_drm.cpp:608-630`, decode `:825-880`). Kernel path: `sde_connector_register_custom_event()` → `_sde_conn_enable_hw_recovery()` → `sde_encoder_enable_recovery_event()` → `recovery_events_enabled = true` (`sde_connector.c:3478-3489`, `sde_encoder.c:6019-6030`). Effect: pp-done timeouts emit `SDE_RECOVERY_CAPTURE` to userspace instead of requesting a panic (`sde_encoder_phys_cmd.c:571-575`), and a later successful kickoff reports `SDE_RECOVERY_SUCCESS` (`:1425-1436`). Userspace then performs the display power reset / register capture (HAL `HWRecovery` flow).
4. Bound its own fence waits (timeout on the commit out-fence, treat expiry as a stall) and escalate to a controlled recovery: full modeset / DPMS off-on / display power reset rather than continuing to submit into a stalled CRTC.
5. Sequence any single-buffer colour feature (IGC/Gamut) through the serialize/default trigger as the HAL already does (`color_manager.cpp:800-830`) — relevant because posted start + SB LUT programming is the combination that needs the control path to be idle.

**Panel / hardware (needs device access — out of scope here, listed as the honest next step)**
* Confirm the panel actually emits TE: DSI `te-pin-select` value (`dsi_panel.c:1289-1295`), the panel's DCS TE-on sequence (`mipi_dsi_dcs_set_tear_off` exists as a mirror operation `dsi_display.c:8953`), the TE/GPIO routing (`qcom,platform-te-gpio`, `qcom,panel-te-source`), panel reset state, and DSI error/`SDE_EVTLOG` output. If TE pulses are present on the pin but the INTF tear-check does not see them, the fault is in INTF/tear-check programming or IRQ delivery; if they are absent, it is panel/reset/routing.

**Not available (checked, so nobody looks for them)**
* `frame_trigger_mode` debugfs is **read-only** (`debugfs_create_u32("frame_trigger_mode", 0400, ...)` `sde_encoder.c:5218-5219`) — you cannot force a mode from debugfs.
* No userspace knob disables the wr_ptr/TE wait, and no in-tree path declares the panel dead merely because TE stopped inside the commit path (`panel_dead` comes from the ESD/recovery machinery).

## 7. Proven vs. hypothesis

**Proven (source-cited above)**
* Tear-check config values, INTF registers, IRQ types and which counters each IRQ decrements; the wr_ptr wait is the TE wait and the pp-done wait is the frame-done wait; both are bounded by `kickoff_timeout_ms` (220 ms default).
* The blocking ioctl waits on the whole commit worker (`kthread_flush_work`), i.e. it includes the wr_ptr + pp-done waits, and the next commit serializes on `pending_crtcs` with no timeout.
* The release/out-fence is signalled only from pp-done (or the pp-done-timeout ERROR event); the retire fence is signalled from the wr_ptr IRQ or the wr_ptr-timeout fallback; the wr_ptr-timeout fallback exists specifically "to avoid device freeze".
* `switch_te` is a jitter-based heuristic, needs ≥2 TE events after the commit profile timestamp, and its `false` value skips the watchdog-TE retry; the printed `switch_te:0` therefore does *not* prove TE was healthy.
* `frame_trigger_mode` semantics per enum doc, property plumbing, per-mode code differences, the posted-start reset skip, and the HAL's SB-LUTDMA reason for serializing.

**Hypothesis (consistent with the source, not proven by it)**
* That the observed run had *no* TE pulses at all vs. TE present but the WR_PTR completion lost (e.g. masked/disabled IRQ, or the CTL start latch lost in the tear-check trigger window — the `_sde_encoder_override_tearcheck_rd_ptr()` comment `:190-193` shows such latch hazards are real). Distinguishing them needs `INTF_TEAR_LINE_COUNT`/`INTF_TEAR_INT_COUNT_VAL` readback (`sde_hw_intf_get_vsync_info()` `sde_hw_intf.c:786-811`), `SDE_EVT32`/evtlog IRQ entries, and the ESD TE-check result from the device.
* That the watchdog-TE fallback would have unwedged this specific commit — it depends on the WD timer being usable for that INTF/panel and on `disp_info->is_te_using_watchdog_timer` being settable at that point.
* That the userspace hang was on the commit out-fence or the next commit's CRTC serialization specifically (as opposed to a vendor-specific `sde` ioctl) — the source shows those are the only unbounded waits in this path, but the observed stack was not available.
