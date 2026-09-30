# CRTC_PROP_IDLE_PC_STATE — how it is exposed and used (vendor SM8450 / msm-5.10 SDE + Qualcomm SDM HWC)

Sources fetched and unpacked locally (raw archives, exact commits):

| Tree | Repo | Branch / commit |
|---|---|---|
| Kernel display driver (SDE, 5.10) | `clo/la/platform/vendor/opensource/display-drivers` | `display-kernel.lnx.5.10.c2`, sha `8c23a76b747cd703124f95402fa861da483fb347` |
| Kernel display driver (A13/SM8450 vendor) | same | `LA.VENDOR.13.2.1.c25`, sha `ba3acb383f708e9c9b4a297682d8d5ebd508255f` |
| Kernel display driver (later SDE lineage) | same | `DISPLAY.LA.2.0.c27`, sha `be6a772ecb00305c93d3359208867e8e21d4d925` |
| Userspace HWC (SDM core + DRM atomic) | `clo/la/platform/hardware/qcom/display` | `DISPLAY.LA.2.0.c27`, sha `11991cb5abbd57ad9be129f10216cbe0918f6507` ; `LA.VENDOR.13.2.1.c25`, sha `ee8d6dbd61712500fad79691f6dfb446c7cb2e83` |

## 1. Userspace-visible DRM property

* **Property name string is lower-case `idle_pc_state`** (NOT `IDLE_PC_STATE`). The kernel-internal enum name is `CRTC_PROP_IDLE_PC_STATE`.
  * 5.10: `msm/sde/sde_crtc.c:6403-6407` → `msm_property_install_enum(&sde_crtc->property_info, "idle_pc_state", 0x0, 0, e_idle_pc_state, ARRAY_SIZE(e_idle_pc_state), 0, CRTC_PROP_IDLE_PC_STATE);`
  * `DISPLAY.LA.2.0.c27`: `msm/sde/sde_crtc.c:5952-5957` (same text).
* **Enum value strings and numeric values**: `msm/sde/sde_crtc.c:6343-6347` (5.10) / `6460-6464` (c25) / `5893-5897` (c27)
  ```c
  static const struct drm_prop_enum_list e_idle_pc_state[] = {
      {IDLE_PC_NONE,    "idle_pc_none"},
      {IDLE_PC_ENABLE,  "idle_pc_enable"},
      {IDLE_PC_DISABLE, "idle_pc_disable"},
  };
  ```
  Backing enum: `msm/sde/sde_crtc.h:73-77` → `IDLE_PC_NONE = 0`, `IDLE_PC_ENABLE = 1`, `IDLE_PC_DISABLE = 2`.
  Property index: `msm/msm_drv.h:185` (`CRTC_PROP_IDLE_PC_STATE` inside `enum msm_mdp_crtc_property`).
  Note: the property is **not** part of the UAPI header `include/uapi/display/drm/sde_drm.h`; it is created at runtime by the kms driver.

## 2. Is it settable from userspace? What is the default if never set?

`msm_property_install_enum()` (`msm/msm_prop.c:203-255`):
* flags are passed straight to `drm_property_create_enum(info->dev, DRM_MODE_PROP_ENUM | flags, name, …)` (line 231-233). Callers pass `flags = 0x0` ⇒ **no `DRM_MODE_PROP_IMMUTABLE` ⇒ the property is writable by userspace** through the atomic CRTC-property path.
* default: `property_data[idx].default_value = 0;` (line 239) and it is only overwritten when `init_idx != 0` (line 243-245). Callers pass `init_idx = 0`, so **the attach-time default is 0 = `idle_pc_none`**, attached via `drm_object_attach_property(info->base, *prop, default_value)` (line 249-251). `info->base` is `&crtc->base` (sde_crtc.c:7821).
* userspace writes land in the CRTC state via `msm_property_atomic_set()` (`msm/msm_prop.c:359+`) and are read back per-state by `sde_crtc_get_property(cstate, CRTC_PROP_IDLE_PC_STATE)` = `cstate->property_values[X].value` (`msm/sde/sde_crtc.h:570-571`); state is duplicated per commit (`sde_crtc.c:4654-4668`) so a previously-set value sticks until changed.
* **Absent any userspace write → value 0 (`idle_pc_none`)**, which is a no-op (see §4).

## 3. Which component sets it

**Qualcomm's SDM/HWC userspace (vendor/qcom/opensource/display → codelinaro `clo/la/platform/hardware/qcom/display`). NOT AOSP SurfaceFlinger.**

* HWC's DRM property-name table: `sde-drm/drm_property.cpp:148` → `if (name == "idle_pc_state") { return DRMProperty::IDLE_PC_STATE; }` (c25: line 183); enum `sde-drm/drm_property.h:156`.
* HWC's opcode + values: `libdrmutils/drm_interface.h:83` `enum struct DRMOps`, `:347-353` `CRTC_SET_IDLE_PC_STATE` ("Sets Idle PC state for CRTC", args crtc id + state), `:536-540` `enum struct DRMIdlePCState { NONE, ENABLE, DISABLE }`.
* Atomic write: `sde-drm/drm_crtc.cpp:862-882` — maps ENABLE→1, DISABLE→2, default→NONE→0 and calls `AddProperty(..., prop_mgr_.GetPropertyId(DRMProperty::IDLE_PC_STATE), idle_pc_state, …)`; guarded by `IsPropertyAvailable()` (line 863) so it silently no-ops on targets where the kernel did not create the property.
  * Static fallbacks `IDLE_PC_STATE_NONE/ENABLE/DISABLE = 0/1/2` at `drm_crtc.cpp:105-108`; the real numeric values are **read from the kernel property enums** in `PopulateIdlePCStates()` (`drm_crtc.cpp:155-170`, wired from `:397-399`).
* Who triggers it:
  * `sdm/libs/core/drm/hw_peripheral_drm.h:121-124` (c27) / `:127-128` (c25): `void SetIdlePCState() { drm_atomic_intf_->Perform(DRMOps::CRTC_SET_IDLE_PC_STATE, token_.crtc_id, idle_pc_state_); }`
  * called from `HWPeripheralDRM::Validate()` (c27:243, c25:233) and `HWPeripheralDRM::Commit()` (c27:256, c25:245) ⇒ **HWC sends `idle_pc_state` on essentially every atomic commit**, usually with value `NONE` (0) because `idle_pc_state_` is reset to `NONE` after every successful commit (c27:284, c25:273).
  * `ENABLE`/`DISABLE` only come from `ControlIdlePowerCollapse(enable, sync)` (`hw_peripheral_drm.cpp:520-527` c27 / `:558+` c25), which is driven by TUI/secure transitions: `kTUITransitionStart → ControlIdlePowerCollapse(false)` (c27:458, sets DISABLE) and `kTUITransitionEnd → ControlIdlePowerCollapse(true)` (c27:475, sets ENABLE); `PowerOn()` re-issues `ENABLE` if it was previously disabled (c27:548-551) and then resets to NONE (c27:563-564). Plumbed through `sdm/libs/core/display_builtin.cpp:1811/1822` and `sdm/include/core/display_interface.h:1066`.
* AOSP: full-text search on cs.android.com for `idle_pc_state`, `IDLE_PC_STATE`, `IdlePowerCollapse`, `SetIdlePCState` → **no matching results** in any indexed AOSP repo (frameworks/native, hardware/interfaces, kernel/common, …). SurfaceFlinger/HWC2 never set this property; only the vendor HWC (`hardware/qcom/display`) does. The only AOSP-visible similarity is upstream DPU's internal `idle_pc_supported` in `drivers/gpu/drm/msm/disp/dpu1/dpu_encoder.c` (kernel/common), which is not a DRM property.

## 4. Does it gate the DSI/MDP clock-off + RCG "reparent"/reprogram wake-up path?

It is a **control knob over an already-default-on mechanism**, and yes — it gates the RC idle path that turns off DSI+MDP clocks and the kickoff path that turns them back on.

5.10 (`display-kernel.lnx.5.10.c2`):
* property installation condition: `catalog->has_idle_pc` (`sde_crtc.c:6403`), fed by the DT bool `qcom,sde-has-idle-pc` (`sde/sde_hw_catalog.c:605` prop table, `:4069` `cfg->has_idle_pc = PROP_VALUE_ACCESS(...)`). If the DT flag is absent, the property does not exist at all.
* **encoder default is ON whenever the catalog has idle-pc**, independent of userspace: `sde_encoder.c:5459` `sde_enc->idle_pc_enabled = sde_kms->catalog->has_idle_pc;`
* property handling in commit: `sde_crtc_commit_kickoff()` reads it (`sde_crtc.c:4456`) and, for each encoder of the CRTC, `if (idle_pc_state != IDLE_PC_NONE) sde_encoder_control_idle_pc(encoder, (idle_pc_state == IDLE_PC_ENABLE));` (`sde_crtc.c:4472-4474`). NONE ⇒ nothing changes. `sde_encoder_control_idle_pc()` only flips the flag (`sde_encoder.c:1948-1966`). Also reset to enabled on CRTC disable/suspend: `sde_crtc.c:5008-5013`.
* **gate**: `sde_encoder_resource_control()` returns early for every event except KICKOFF/PRE_MODESET/POST_MODESET/STOP/PRE_STOP when `!sde_enc->idle_pc_enabled` (`sde_encoder.c:2419-2425`) — i.e. `ENTER_IDLE` and `EARLY_WAKEUP` are dropped.
* timer: after a kickoff completes, `_sde_encoder_rc_kickoff_delayed` → `_sde_encoder_rc_restart_delayed` schedules `delayed_off_work` for `IDLE_POWERCOLLAPSE_DURATION` = `(66 - 16/2)` = 58 ms (`sde_encoder.h:52`; scheduling `sde_encoder.c:1968-2003`, `2024-2031`); `IDLE_SHORT_TIMEOUT` = 1 ms when the connector LP is LP2 (`sde_encoder.c:69`, `1987-1990`). `sde_encoder_off_work()` (`:3145-3160`) → `sde_encoder_idle_request()` (`:3801-3815`) → `sde_encoder_resource_control(SDE_ENC_RC_EVENT_ENTER_IDLE)`.
* **clock-off (ENTER_IDLE)**: `_sde_encoder_rc_idle()` (`sde_encoder.c:2248-2312`). For non-video (command-mode, non-clone) displays: `_sde_encoder_update_rsc_client(enc, false)` (RSC → `SDE_RSC_IDLE_STATE`) and `_sde_encoder_resource_control_helper(enc, false)` (`:2297-2298`), which does `pm_runtime_put_sync()` (SDE/MDP core clocks) and `sde_connector_clk_ctrl(connector, false)` (DSI clocks) (`:1868-1879`; `sde_connector_clk_ctrl()` → `DSI_ALL_CLKS, DSI_CLK_OFF` at `sde/sde_connector.c:1198-1218`). For video-mode panels only IRQs + pm_qos are dropped (`:2292-2303`) — **no clock collapse**.
* **clock-on / wake-up on next commit**: `sde_encoder_prepare_for_kickoff()` calls `sde_encoder_resource_control(KICKOFF)` and reports `"resource kickoff failed rc %d"` on error (`sde_encoder.c:4654-4656`). `_sde_encoder_rc_kickoff()` (`:2033-2086`) sees `rc_state != ON` and runs `_sde_encoder_resource_control_helper(enc, true)` = `pm_runtime_get_sync()` (SDE core clocks) + `sde_connector_clk_ctrl(conn, true)` (DSI clocks) + IRQ enable (`:1844-1866`), then re-votes RSC (CLK/CMD state). `SDE_ENC_RC_EVENT_EARLY_WAKEUP` (touch) does the same (`:2314-2394`).
* DSI side of that wake-up: `dsi_display_clk_ctrl()` / clock manager re-programme the DSI link clocks — `clk_set_rate(byte_clk|pixel_clk)` (`msm/dsi/dsi_clk_manager.c:336,343`), `clk_set_parent(byte_clk|pixel_clk)` (`:177,183`), `clk_prepare_enable(byte_clk|pixel_clk)` (`:202,208`). On waipio `disp_cc_mdss_byte0_clk_src` is a `clk_rcg2` (`drivers/clk/qcom/dispcc-waipio.c:308`), so a rate/parent change issued while the MDSS/DSI context is down is exactly the case that ends in the clk-rcg2 `CMD_UPDATE` ack timeout ("rcg didn't update its configuration", `-EBUSY`) and then the DSI byte/pixel clock errors, which propagate back as the kickoff failure.
* suspend-time nudge: `_sde_kms_pm_suspend_idle_helper()` calls `sde_encoder_idle_request(conn->encoder)` for LP2 connectors (`sde/sde_kms.c:3849-3890`, call at `:3884`).

Branch differences (same design, different gating expression/line numbers):
* `LA.VENDOR.13.2.1.c25` (A13 / SM8450-era SDE): property installed on `test_bit(SDE_FEATURE_IDLE_PC, catalog->features)` (`sde_crtc.c:6524-6528`), encoder default `sde_enc->idle_pc_enabled = test_bit(SDE_FEATURE_IDLE_PC, …)` (`sde_encoder.c:5348`), same enum/strings (`sde_crtc.h:69-76`, `sde_crtc.c:6460-6464`), same RC gate (`sde_encoder.c:2345-2354`), same helper on/off (`:1781-1814`), `IDLE_POWERCOLLAPSE_DURATION (66 - 16/2)` (`sde_encoder.h:52`).
* `DISPLAY.LA.2.0.c25/26/27`: identical mechanism (install `sde_crtc.c:5952-5957`, kickoff use `:4062` / `:4078-4080`).
* GKI/DPU kernels (AOSP `kernel/common`, LineageOS/OnePlus/Xiaomi sm8450 GKI repos at 5.10.66/5.10.205 — checked `Makefile` and trees) use `drivers/gpu/drm/msm/disp/dpu1` and have **no `idle_pc_state` CRTC property**; this property is SDE(techpack)-only.

## 5. Explicitly NOT confirmed

1. **Whether the target device's vendor DTS sets `qcom,sde-has-idle-pc`** (i.e. whether the property exists at all on this phone). The vendor DTS (`arch/arm64/boot/dts/vendor/qcom/waipio-sde.dtsi` or equivalent) is not present in any public tree reachable here: codelinaro `display-devicetree` has no 5.10/waipio files, and LineageOS/OnePlus/Xiaomi sm8450 GKI mirrors contain neither vendor DTS nor the SDE techpack. The reference kernel tree with WAIPIO support is the techpack repo itself (`sde_hw_catalog.c:5213` `IS_WAIPIO_TARGET(hw_rev) || IS_CAPE_TARGET(hw_rev)`).
2. The HWC2-layer call site that fires `ControlIdlePowerCollapse()` beyond the TUI secure-event path (the hwc2/hwc_session sources are not in the public `hardware/qcom/display` branch used here; only the SDM `sdm/libs/core` half is).
3. The exact numeric enum mapping *as seen by userspace* cannot be confirmed from source alone for a given running kernel — the HWC reads it from the live property (`PopulateIdlePCStates`), and public sources only guarantee the kernel's 0/1/2.

## 6. Practical consequence for the observed failure

* A plane-only atomic commit carrying `idle_pc_state = 0` (`idle_pc_none`) is normal HWC behaviour and is a **no-op** in the kernel (`sde_crtc.c:4472`).
* The DSI/MDP clock collapse is **not** enabled by that commit: it is enabled by default from the catalog/DT flag and runs on the 58 ms `delayed_off_work` timer whenever `idle_pc_enabled` is true.
* Userspace can only disable it by sending `idle_pc_disable` (2) on the CRTC (HWC does this only around TUI), and the encoder-side wake path is `SDE_ENC_RC_EVENT_KICKOFF/EARLY_WAKEUP` → DSI + MDP clocks back on → `clk_set_rate/clk_set_parent` on the DSI byte/pixel RCGs, which is where a stuck `disp_cc_mdss_byte0_clk_src` RCG shows up as the `-EBUSY`/`-22`/`sde_encoder_prepare_for_kickoff: resource kickoff failed rc -22` sequence.
