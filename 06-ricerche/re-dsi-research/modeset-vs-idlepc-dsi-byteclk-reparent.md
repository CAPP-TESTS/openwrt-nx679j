# Full modeset vs. plane-only idle wake: where the DSI byte-clock (XO→PHY-PLL) reparent happens

Scope: Nubia RedMagic 7 NX679J (SM8450/waipio), vendor msm-5.10 display techpack (SDE + DSI). Research only, no device access.

Sources (all line numbers verified by fetching the exact pinned revision and diffing against the local trees):

* Vendor display drivers (msm-5.10 SM8450): `SOHRA8/android_vendor_qcom_opensource_display-drivers`, commit `7d283391248fc613f9413cc2758bf7f9f0fe91f6`
  base URL: `https://github.com/SOHRA8/android_vendor_qcom_opensource_display-drivers/blob/7d283391248fc613f9413cc2758bf7f9f0fe91f6/msm/`
  (12 cited files fetched from raw at that SHA and byte-compared: all MATCH)
* Vendor kernel clock + DISPCC (msm-5.10 SM8450): `LineageOS/android_kernel_xiaomi_sm8450`, commit `ef362912d37b761041709638c1e571d6394e9558` (= remote HEAD)
  base URL: `https://github.com/LineageOS/android_kernel_xiaomi_sm8450/blob/ef362912d37b761041709638c1e571d6394e9558/`
* Mainline equivalents: `torvalds/linux` tags `v6.12` (drm/msm + clk/qcom) and `v6.11` (sm8450.dtsi). Files fetched raw; line numbers are from those fetches.

## 1. Both commits reach the same reparent site

There is exactly one place in the vendor DSI driver that switches the byte/pixel RCG parent between XO and the PHY PLL output:

* `dsi_clk_update_parent()` — `dsi/dsi_clk_manager.c:172-190`; the switch is `clk_set_parent(child->byte_clk, parent->byte_clk)` at **:177** (pixel at **:183**).
  URL: `.../msm/dsi/dsi_clk_manager.c#L172`
* Its only caller is `dsi_ctrl_set_clock_source()` — `dsi/dsi_ctrl.c:4076-4105` (`:4088` update, `:4090-4094` rollback to `pll_op_clks` with the result ignored, `:4097-4098` cache update).
  URL: `.../msm/dsi/dsi_ctrl.c#L4076`
* The wrappers are `dsi_display_set_clk_src()` — `dsi/dsi_display.c:2847-2891` (PLL parents at `:2866`, XO parents at `:2867-2868`), used by:
  * `dsi_display_phy_pll_enable()` — `dsi/dsi_display.c:2893-2919`: `dsi_phy_pll_toggle(phy, true)` at **:2914**, then `dsi_display_set_clk_src(display, false)` at **:2918** (XO→PLL).
  * `dsi_display_phy_pll_disable()` — `dsi/dsi_display.c:2921-2943`: `set_clk_src(true)` (PLL→XO) at **:2932**, then PLL off at `:2942`.

The reparent is driven by **DSI link-clock ON/OFF transitions**, not by the SDE resource-control (RC) state machine:

* `dsi_display_link_clk_enable()` — `dsi/dsi_clk_manager.c:611-685`; for `DSI_LINK_HS_CLK` and `!mngr->is_cont_splash_enabled` it calls `phy_config_cb(...)` then `phy_pll_toggle_cb(..., true)` at **:638-642**, then `dsi_link_hs_clk_start()` at `:643` (rates/prepare/enable of the branch clocks).
* `dsi_display_link_clk_disable()` — `dsi/dsi_clk_manager.c:726-782`: HS stop at `:774` and `phy_pll_toggle_cb(..., false)` at **:778**.
* These run from `dsi_clk_req_state()` `:1124-1222` → `dsi_recheck_clk_state()` `:1056-1122` → `dsi_update_clk_state()` `:883-1054` → `dsi_clk_update_link_clk_state()` `:784-846`, i.e. from `dsi_display_clk_ctrl()` `:1283-1301`.
* The callbacks are bound to `dsi_display_phy_pll_toggle()` (`dsi_display.c:2945-2961`, registered at `dsi_display.c:5845`) and `dsi_display_phy_configure()` (`dsi_display.c:2963`), which reach `dsi_phy_pll_toggle()` (`dsi/dsi_phy.c:852-860` → `phy->hw.ops.pll_toggle`, set to `dsi_pll_5nm_toggle` in `dsi/dsi_catalog.c:295`) and `dsi_pll_5nm_configure()` (PLL register/VCO reprogram, `dsi/dsi_pll_5nm.c:1545-1571`).

### Path (a) full commit, CRTC MODE_ID+ACTIVE, ALLOW_MODESET

1. `msm_atomic.c:420-458` (`msm_enable`): `drm_bridge_chain_pre_enable(bridge)` at **:451**, then `encoder->enable()` at **:455**; `drm_bridge_chain_enable()` at **:496**.
2. `dsi/dsi_drm.c:196-258` (`dsi_bridge_pre_enable`): non-seamless → `dsi_display_prepare()` at **:234**, `dsi_display_enable()` at **:244**. Seamless/VRR/dyn-clk return early at **:226-231**.
3. `dsi/dsi_display.c:8281-8438` (`dsi_display_prepare`): `DSI_CORE_CLK` ON `:8341`; `dsi_display_phy_sw_reset` `:8357`; `dsi_display_phy_enable` `:8364` → `dsi/dsi_phy.c:1004-1054` → `dsi_phy_enable_hw()` `:1044-1047` (`dsi_phy.c:537-546`); `dsi_display_ctrl_init` `:8372`; `dsi_display_ctrl_host_enable` `:8381`; `DSI_LINK_CLK` ON at **:8388**.
4. Link ON → `dsi_display_link_clk_enable` → PLL configure + PLL start/lock (`dsi_pll_5nm_enable`, `dsi_pll_5nm.c:1480-1517`: PLL_CNTRL start, lock poll 5 ms at `:595-611`) → **reparent XO→PLL** (`dsi_display.c:2918` → `dsi_ctrl.c:4088` → `dsi_clk_manager.c:177`).
5. `sde_encoder_virt_enable` → `sde_encoder_resource_control(KICKOFF)` at `sde/sde_encoder.c:3161`; RC state is OFF → `_sde_encoder_rc_kickoff()` `sde_encoder.c:1969-2019` → `_sde_encoder_resource_control_helper()` `:1753-1811` → `sde_connector_clk_ctrl(conn, true)` `:1784` → `sde/sde_connector.c:1290-1310` (op `:1306-1307`, uses `mdp_clk_handle`, `DSI_ALL_CLKS`) → op = `dsi_display_clk_ctrl` (`sde/sde_kms.c:1798`). Because the DSI client already holds link clocks ON from step 4, `dsi_recheck_clk_state` sees no state change → **no second reparent**.

### Path (b) plane-only commit waking the encoder out of software IDLE

1. `sde_encoder_prepare_for_kickoff()` → `sde_encoder_resource_control(KICKOFF)` at `sde_encoder.c:4659`.
2. Timeout that produced IDLE: `sde_encoder.h:52` `IDLE_POWERCOLLAPSE_DURATION (66 - 16/2)` = **58 ms** (`:53` = 192 ms early-wakeup cap); scheduled in `sde_encoder_control_idle_pc()` `sde_encoder.c:1877-1939` (duration `:1921-1934`).
3. `_sde_encoder_rc_idle()` `sde_encoder.c:2181-2245`: cmd-mode path (is_vid_mode false for `INTF_MODE_CMD`, computed at `:2355`) → `_sde_encoder_update_rsc_client(false)` + `_sde_encoder_resource_control_helper(false)` at **:2230-2231** → link OFF → `set_clk_src(XO)` + PLL stop; state = `SDE_ENC_RC_STATE_IDLE` at `:2240`.
4. `_sde_encoder_rc_kickoff()` from IDLE: `:1992-1994` is the video-mode shortcut (IRQ + pm_qos only, **no clock work**); for cmd mode it falls to `:1997-1998` `_sde_encoder_resource_control_helper(true)` → `sde_connector_clk_ctrl(true)` (`:1784`) → `dsi_display_clk_ctrl(mdp_clk_handle, DSI_ALL_CLKS, ON)` → `dsi_recheck_clk_state`/`dsi_update_clk_state` → `dsi_display_link_clk_enable`.
5. `dsi_display_link_clk_enable` `:638-642` → `dsi_display_phy_pll_enable` → PLL reprogram + PLL start/lock + **reparent XO→PLL** (`dsi_display.c:2918` → `dsi_ctrl.c:4088` → `dsi_clk_manager.c:177`).

**Same function, same reparent site.** What differs is only what precedes it:

| | full modeset (a) | idle wake (b) |
|---|---|---|
| PHY reset / power-on | yes: `phy_sw_reset` `dsi_display.c:8357`, `dsi_display_phy_enable` `:8364` → `dsi_phy_enable_hw` | none |
| DSI controller re-init / host enable | yes: `:8372`, `:8381` | none |
| PLL reprogram + start/lock | yes (`phy_config_cb` + `phy_pll_toggle_cb`) | yes (identical) |
| RCG parent switch | yes, `dsi_display.c:2918` | yes, identical |
| Second reparent from SDE KICKOFF | no (clk-manager state already ON) | n/a (this *is* the reparent) |

Vendor's own comment stating the ordering assumption: `dsi_display.c:2904-2912` ("recommended to turn on the PLL before switching parent of RCG to PLL … Branch clocks and in turn RCG might not get turned off during clock disable sequence if there is a vote from dispcc or any of its other consumers").

## 2. Does a full modeset, or PRE_STOP/STOP + POST_MODESET, avoid it via a cold re-init?

**No.** Per the RC state machine (`sde_encoder.c:2338-2409` dispatch):

* `PRE_STOP` `:2021-2058`: from ON only drops the RSC client and marks `PRE_OFF`; from IDLE/OFF it skips (`:2034-2041`). No clock or PLL action.
* `STOP` `:2060-2098`: calls `helper(false)` **only** from `PRE_OFF` (`:2087-2088`) → link OFF → `set_clk_src(XO)` (`dsi_display.c:2932`) + PLL stop (`:2942`). From IDLE it skips. So PRE_STOP/STOP from an active encoder *adds* an XO switch plus a PLL stop, and the next enable repeats PLL-on + reparent — it does not shorten or bypass the sequence.
* `PRE_MODESET` `:2100-2143` / `POST_MODESET` `:2145-2179`: no clock gating at all. `PRE_MODESET` calls `helper(true)` only if state != ON (`:2113-2132`); with the encoder ON it merely marks `SDE_ENC_RC_STATE_MODESET` (`:2137`). `POST_MODESET` only restores the RSC client + pm_qos. They are issued **only** for seamless DMS / seamless dyn-clk commits — `sde_encoder_virt_modeset_rc()` `:2532-2601` (`:2565-2576` pre, `:2590-2594` post) — and for those commits `dsi_bridge_pre_enable/enable` return early (`dsi_drm.c:226-231`, `:271-276`), so `dsi_display_prepare/enable` never run.
* `EARLY_WAKEUP` `:2247-2336` (input event path, `:2298-2328`) behaves exactly like the kickoff wake: `helper(true)` → same PLL/reparent.

So the *only* structural ways the XO↔PHY-PLL switch is skipped in this driver are:

1. **Link clocks are never gated** — any commit while `rc_state == SDE_ENC_RC_STATE_ON`, and seamless-mode modesets (PRE_MODESET/POST_MODESET + bridge early return). The PLL stays locked and the RCG parent stays PLL, so there is nothing to switch. This is avoidance by *not turning the clock off*, not by a cold re-init.
2. **Continuous splash first enable** — `dsi_clk_manager.c:639-642` skips both `phy_config_cb` and `phy_pll_toggle_cb` while `mngr->is_cont_splash_enabled`; the PLL/parent are left as the bootloader set them.
3. Video-mode panels (`is_vid_mode == true`) never gate DSI clocks on idle at all (`sde_encoder.c:2225-2227`), and their IDLE→ON kickoff does no clock work (`:1992-1994`). For a cmd-mode panel path (both `idle_pc_enabled` and `INTF_MODE_CMD`), the idle→kickoff wake is unavoidable if a commit arrives after 58 ms.

Neither a full modeset nor a STOP+PRE_STOP+POST_MODESET cycle performs a "cold" init that sidesteps the parent switch; both execute `dsi_pll_5nm_configure` + `dsi_pll_5nm_toggle` + `clk_set_parent(byte RCG, phy_pll_out_byteclk)` exactly like the idle wake. The full modeset differs by first resetting/re-enabling the PHY and re-initializing the DSI controller (`dsi_display.c:8357/8364/8372/8381`), which the idle wake does not do.

## 3. The failing errno (for context)

The RCG ack timeout lives in the clock driver, not in DSI:

* Vendor: `drivers/clk/qcom/clk-rcg2.c:112-134` `update_config()` → 500 × `udelay(1)` poll of `CMD_UPDATE`, then `WARN_CLK(hw, 1, "rcg didn't update its configuration.")` **:133** and `return -EBUSY` **:134**; `clk_rcg2_set_parent()` **:137-148**.
  URL: `https://github.com/LineageOS/android_kernel_xiaomi_sm8450/blob/ef362912d37b761041709638c1e571d6394e9558/drivers/clk/qcom/clk-rcg2.c#L112`
* Mainline identical semantics: `drivers/clk/qcom/clk-rcg2.c:111-134` (WARN `:133`, `-EBUSY` `:134`), `clk_rcg2_set_parent` `:137-148`.
* The reparented RCG on SM8450 is `disp_cc_mdss_byte0_clk_src` (vendor: `drivers/clk/qcom/dispcc-waipio.c:308-321`, `parent_map_2` `:194-200` = `{BI_TCXO=0, DSI0_PHY_PLL_OUT_DSICLK=1, DSI0_PHY_PLL_OUT_BYTECLK=2, …}`, ops `clk_byte2_ops` whose `.set_parent = clk_rcg2_set_parent` at `clk-rcg2.c:1019-1029`). The DSI driver binds it by DT name `byte_clk_rcg` (pixel: `pixel_clk_rcg`; XO: `xo`) — `dsi/dsi_ctrl.c:819-866`.
* Failure propagation: `clk_set_parent` → `dsi_clk_manager.c:177-181` logs "failed to set byte clk parent" and returns → `dsi_ctrl.c:4088-4095` logs "Failed to update link clk parent" and rolls back to `pll_op_clks` (byte first, so a byte failure skips the pixel switch) → `dsi_display_phy_pll_enable` `:2914-2918` → `dsi_display_link_clk_enable` `:643-649` → `dsi_update_clk_state` → `dsi_clk_req_state` → `sde_connector_clk_ctrl` → `sde_encoder.c:1784-1790` "failed to enable clk control %d".

## 4. Mainline (msm/dpu) equivalent

* Mainline **has** the RC state machine: `dpu_encoder.c:113-115` (`DPU_ENC_RC_STATE_*`), `:881-...` `dpu_encoder_resource_control()`, KICKOFF from IDLE → `_dpu_encoder_resource_enable()` at `:935-936`, whose body is only `pm_runtime_get_sync()` + `_dpu_encoder_irq_enable()` (`:844-855`); disable is `pm_runtime_put_sync()` + IRQ disable (`:857-879`).
  URL: `https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder.c#L881`
* Crucially, mainline's RC helper has **no connector `clk_ctrl`** (the vendor adds `sde_connector_clk_ctrl` at `sde_encoder.c:1784/1804`), so idle-pc in mainline never gates DSI link clocks from the encoder path.
* Mainline never reparents the DSI byte/pixel RCG at runtime: no `clk_set_parent()` exists anywhere under `drivers/gpu/drm/msm/dsi/` (grep on linux-6.11); the parent is chosen once in DT — `arch/arm64/boot/dts/qcom/sm8450.dtsi:3269-3270` `assigned-clocks = <&dispcc DISP_CC_MDSS_BYTE0_CLK_SRC>, <&dispcc DISP_CC_MDSS_PCLK0_CLK_SRC>; assigned-clock-parents = <&mdss_dsi0_phy 0>, <&mdss_dsi0_phy 1>;`
  URL: `https://github.com/torvalds/linux/blob/v6.11/arch/arm64/boot/dts/qcom/sm8450.dtsi#L3269`
* Mainline dispcc RCG: `disp_cc_mdss_byte0_clk_src` `dispcc-sm8450.c:267-277`, parents `disp_cc_parent_map_2` `:163-170`. Mainline's byte clock is enabled via `clk_prepare_enable(msm_host->byte_clk)` (`dsi/dsi_host.c:384-420`, `:464`), i.e. the PLL is a clk-provider dependency, not an explicit vendor-style PLL toggle.

## 5. What static source cannot decide

* Whether, at 52 min idle on this unit, the byte RCG (or its branch) was still voted/enabled when the parent switch was attempted — the vendor comment at `dsi_display.c:2904-2912` says a dispcc/other-consumer vote can keep the branch+RCG on across the "disable" sequence; the ack-timeout errno is consistent with a switch attempted on a running-but-unsourced RCG, but the code shows both paths equally exposed.
* The NX679J panel's interface mode (cmd vs video) in the failing run: it decides whether idle even gates DSI clocks (cmd) or not (video, `sde_encoder.c:2225-2227`).
* No code path in this tree delays, retries, or re-locks the PLL around the RCG switch; the only mitigations available from this source are "don't gate link clocks" (seamless-style commits) or accept that any post-58 ms commit re-runs the switch.
