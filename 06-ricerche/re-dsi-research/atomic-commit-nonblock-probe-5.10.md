# Probing a DRM atomic commit without hanging userspace — upstream v5.10 + Qualcomm SDE vendor (NX679J / SM8450, 5.10)

Scope: research only, no device access. Goal: a commit probe that (a) cannot block userspace
indefinitely, (b) still gives a *hardware* completion verdict.

Sources
- **Upstream v5.10** (torvalds/linux @ tag `v5.10`), files fetched to
  `~/.hermes/profiles/kernel-re/cache/scratch/up-v510/` (raw.githubusercontent.com/torvalds/linux/v5.10/<path>).
  Line numbers below are from those exact v5.10 objects.
- **Vendor SDE display drivers (device-relevant)**: `NX679J_DD` =
  `/home/user/nx679j-display-diff/nx679j_display/vendor/qcom/opensource/display-drivers`
  (ZTEMT NX679S `display-drivers.zip`, git commit `0280bdce975602ff17161f5de1b8fc63dc96bd47`;
  running image is `5.10.66-android12-9-...`, so treat this as the matching vendor snapshot, not bit-exact).
- **Vendor DRM core**: `/home/user/nx679j-kernel/kernel_platform/msm-kernel/drivers/gpu/drm`
  (5.10.101). Verified byte-identical *semantics* for the items cited (out-fence, nonblock,
  `wait_for_fences`, `drm_read` O_NONBLOCK) to upstream v5.10.

---

## 1. TL;DR

1. `DRM_MODE_ATOMIC_NONBLOCK` (`0x0200`) is what makes the *ioctl itself* bounded on this driver.
   With `flags=0` the vendor path calls `kthread_flush_work()` (uninterruptible, **no timeout**) and
   can additionally sit in `dma_fence_wait(fence, false)` (uninterruptible, **no timeout**) inside
   the display worker. `flags=0` is therefore never acceptable for probing — it is exactly the
   observed ">120 s, never returns" class of hang (D-state, hung-task detector fires at 120 s).
2. `flags=DRM_MODE_ATOMIC_NONBLOCK` returns `0` **before any hardware programming** (kickoff happens
   on the per-CRTC `disp_thread` kthread). The ioctl return value says nothing about the panel.
3. The only userspace-visible *hardware-latch* signal on the nonblock path is the **CRTC
   `OUT_FENCE_PTR`** (and, optionally, the `DRM_MODE_PAGE_FLIP_EVENT` event on the same event
   object). On the vendor SDE path that fence is signalled only when
   `sde_kms_wait_for_commit_done()` → `sde_crtc_complete_flip()` → `drm_crtc_send_vblank_event()`
   runs, i.e. **after** the encoder reported `MSM_ENC_COMMIT_DONE`.
4. Probe recipe: `NONBLOCK` + `OUT_FENCE_PTR` (+ `PAGE_FLIP_EVENT`), then `poll()` the out-fence fd
   with a deadline; treat "ioctl ok but fence not signalled" as a **distinct, expected** failure
   class. Always send `IN_FENCE_FD = -1`. Bake a signal/alarm escape around the ioctl (nonblock can
   still block on the vendor's global `pending_crtcs` wait — that one *is* interruptible).
5. Vendor-specific trap: on an error in the commit-done wait the driver `break`s **before**
   signalling the event (`NX679J_DD/msm/sde/sde_kms.c:1606-1614`), so the out-fence/event can stay
   pending forever while the ioctl has already returned 0. Same class of gap when kickoff prep fails
   (logged + `needs_hw_reset`, not returned).

---

## 2. UAPI: exact values, structs, ioctls

`include/uapi/drm/drm_mode.h` @ v5.10:

```
734: #define DRM_MODE_PAGE_FLIP_EVENT 0x01
735: #define DRM_MODE_PAGE_FLIP_ASYNC 0x02
736: #define DRM_MODE_PAGE_FLIP_TARGET_ABSOLUTE 0x4
737: #define DRM_MODE_PAGE_FLIP_TARGET_RELATIVE 0x8
740: #define DRM_MODE_PAGE_FLIP_FLAGS (EVENT | ASYNC | TARGET)
834: #define DRM_MODE_ATOMIC_TEST_ONLY 0x0100
835: #define DRM_MODE_ATOMIC_NONBLOCK  0x0200
836: #define DRM_MODE_ATOMIC_ALLOW_MODESET 0x0400
838: #define DRM_MODE_ATOMIC_FLAGS (PAGE_FLIP_EVENT | PAGE_FLIP_ASYNC |
                              ATOMIC_TEST_ONLY | ATOMIC_NONBLOCK | ALLOW_MODESET)
```

```
845: struct drm_mode_atomic {
846:     __u32 flags;
847:     __u32 count_objs;
848:     __u64 objs_ptr;          /* u32 ids */
849:     __u64 count_props_ptr;   /* u32 per object */
850:     __u64 props_ptr;         /* u32 per prop */
851:     __u64 prop_values_ptr;   /* u64 per prop */
852:     __u64 reserved;          /* must be 0 or -EINVAL */
853:     __u64 user_data;         /* copied into drm_event_vblank.user_data */
854: };
```

Other UAPI bits (`include/uapi/drm/drm.h`):
- `DRM_IOCTL_MODE_ATOMIC = DRM_IOWR(0xBC, struct drm_mode_atomic)` (drm.h:929);
  `DRM_IOCTL_MODE_PAGE_FLIP = 0xB0` (drm.h:916); `DRM_IOCTL_WAIT_VBLANK = DRM_IOWR(0x3a, …)` (drm.h:892);
  `DRM_IOCTL_MODE_GETCRTC = 0xA1` (drm.h:900).
- `DRM_CLIENT_CAP_ATOMIC 3` (drm.h:683) — must be enabled or the ioctl returns `-EINVAL`
  (`drm_atomic_uapi.c:1302-1304`).
- `struct drm_event { __u32 type; __u32 length; }` (drm.h:976-979);
  `DRM_EVENT_VBLANK 0x01`, `DRM_EVENT_FLIP_COMPLETE 0x02` (drm.h:981-982);
  `struct drm_event_vblank { struct drm_event base; __u64 user_data; __u32 tv_sec;
   __u32 tv_usec; __u32 sequence; __u32 crtc_id; }` (drm.h:985-992).
- Fence fds come out of `get_unused_fd_flags(O_CLOEXEC)` (`drm_atomic_uapi.c:1094`), i.e. CLOEXEC.

Property names (created in `drivers/gpu/drm/drm_mode_config.c:288-298` @ v5.10):
- `"IN_FENCE_FD"` — plane property, `DRM_MODE_PROP_ATOMIC`, signed range **-1 … INT_MAX**;
- `"OUT_FENCE_PTR"` — CRTC property, `DRM_MODE_PROP_ATOMIC`, **unsigned range 0 … U64_MAX**
  (value is a **userspace pointer**, not an fd: kernel does `put_user(fd, (s32 __user *)val)`).

Atomic-ioctl validation you must respect (v5.10 `drm_atomic_uapi.c`):
- `1298-1300` `DRIVER_ATOMIC` required; `1302-1304` client cap required;
- `1309-1310` `flags & ~DRM_MODE_ATOMIC_FLAGS` → `-EINVAL`;
- `1312-1313` `arg->reserved` → `-EINVAL`;
- `1315-1316` **`PAGE_FLIP_ASYNC` (0x02) is always `-EINVAL` in the atomic ioctl**;
- `1319-1321` `TEST_ONLY + PAGE_FLIP_EVENT` → `-EINVAL`;
- `1329` `allow_modeset = flags & DRM_MODE_ATOMIC_ALLOW_MODESET`.

> ⚠️ Tool bug found in the workspace: `nx679j-display-resume.c:113` defines
> `#define DRM_MODE_ATOMIC_TEST_ONLY 0x02` — that is `DRM_MODE_PAGE_FLIP_ASYNC`, so the "TEST_ONLY"
> runs were *not* test-only; the kernel rejected them with `-EINVAL` before touching anything.
> `nx679j-atomic-fbid-testonly.c:76` has the correct `0x0100`.

---

## 3. Upstream v5.10 semantics of NONBLOCK

Driver callback signature: `struct drm_mode_config_funcs.atomic_commit(dev, state, bool nonblock)`.

- `drm_mode_atomic_ioctl()` (`drm_atomic_uapi.c:1283`):
  - `1410-1411` `TEST_ONLY` → `drm_atomic_check_only()` (no commit, no hw);
  - `1412-1413` **`NONBLOCK` → `drm_atomic_nonblocking_commit(state)`**;
  - `1414-1418` else → `drm_atomic_commit(state)` (blocking).
- `drm_atomic.c`: `drm_atomic_commit()` = `check_only()` + `config->funcs->atomic_commit(dev, state, false)`
  (1334-1347); `drm_atomic_nonblocking_commit()` = `check_only()` + `…atomic_commit(dev, state, true)`
  (1363-1376).
- `drm_atomic_helper_commit()` (`drm_atomic_helper.c:1812-1884`):
  - `1829` `drm_atomic_helper_setup_commit(state, nonblock)`;
  - `1839-1842` **only for blocking** commits: `drm_atomic_helper_wait_for_fences(dev, state, true)`
    (interruptible input-fence wait *in the caller's context*);
  - `1851` `drm_atomic_helper_swap_state(state, true)` — the documented point of no return
    (`1845-1847`: "everything below never fails except when the hw goes bonghits");
  - `1876-1879` `if (nonblock) queue_work(system_unbound_wq, &state->commit_work); else commit_tail(state);`
    → **nonblocking: `commit_tail()` (and therefore all hardware flushes) runs after the ioctl
    already returned; the ioctl returns 0.**
- The tail is then also asynchronous: `commit_work()` (`1625-1684`) → `commit_tail()`, which runs
  `drm_atomic_helper_wait_for_fences(old_state, false)` (line 1638,
  **uninterruptible**), `wait_for_dependencies()`, then the driver's `atomic_commit_tail` (1662-1665).
- `drm_atomic_helper_wait_for_fences()` (1429-1458): `ret = dma_fence_wait(new_plane_state->fence, pre_swap)`
  — `pre_swap=false` ⇒ `intr=false` ⇒ **uninterruptible and unbounded**. An `IN_FENCE_FD` whose
  producer never signals hangs the worker forever.
- Completion plumbing: `drm_atomic_helper_setup_commit()` (`2118-2125`) binds the CRTC event to the
  commit's `flip_done` (`new_crtc_state->event->base.completion = &commit->flip_done`).
  `drm_crtc_commit` fields are documented in `include/drm/drm_atomic.h:70-140`
  (`crtc`, `ref`, `flip_done`, `hw_done`, `cleanup_done`, `commit_entry`) — `drm_atomic.h:88-97`:
  *"flip_done … Completion of this stage is signalled implicitly by calling
  `drm_crtc_send_vblank_event()` on `&drm_crtc_state.event`."*

### Out- and in-fences in the core (v5.10)

- Setting a CRTC `OUT_FENCE_PTR` (`drm_atomic_uapi.c:462-471`): value is a `s32 __user *`; kernel
  writes `-1` immediately (`put_user(-1, fence_ptr)`) and records it in
  `state->crtcs[idx].out_fence_ptr` (`340-343`, field declared `include/drm/drm_atomic.h:177`).
- `prepare_signaling()` (`1110-1210`) runs **before** the commit callback: for each new CRTC with
  `PAGE_FLIP_EVENT` or an out-fence ptr it allocates a `drm_pending_vblank_event`
  (`create_vblank_event(crtc, arg->user_data)`, `1110-1135`), and for an out fence:
  `fence = drm_crtc_create_fence(crtc)` (`1170`) + `setup_out_fence()` (`1088-1108`, writes the fd
  number to userspace, creates the `sync_file`), then `crtc_state->event->base.fence = fence` (`1181`).
- After the commit: `complete_signaling(dev, state, fence_state, num_fences, !ret)` (`1422`).
  - success → `fd_install()` the out-fence fds (`1243-1251`);
  - failure → fds are dropped and `*out_fence_ptr = -1` (`1275-1277`).
  - `DRM_MODE_PAGE_FLIP_EVENT` additionally reserves the event for delivery to the file (`1141-1158`).
    With only `OUT_FENCE_PTR` (no `PAGE_FLIP_EVENT`) `base.file_priv == NULL`: no event is queued,
    but the fence is still signalled — `drm_send_event_locked()` signals the fence **before** the
    `!e->file_priv` early-return (`drm_file.c:797-805`).
- `drm_crtc_send_vblank_event()` (`drm_vblank.c:1069-1087`) → `send_vblank_event()`
  (`drm_vblank.c:977-1004`) → `drm_send_event_locked()`:
  `complete_all(e->completion)` (that is `flip_done`), `dma_fence_signal(e->fence)` (that is the
  out fence), then queue the `DRM_EVENT_FLIP_COMPLETE` event and wake readers.
  ⇒ **out fence and page-flip event fire at the same instant, when the driver calls
  `drm_crtc_send_vblank_event()` — not at an actual vblank IRQ.**
- `IN_FENCE_FD` (`drm_atomic_uapi.c:530-540`): `-1` means none; otherwise
  `state->fence = sync_file_get_fence(val)`; `drm_atomic_set_fence_for_plane()` (`269-279`) fills
  `plane_state->fence` only if the user didn't set one (explicit overrides implicit).
- `DRM_IOCTL_MODE_PAGE_FLIP` (legacy, `drm_plane.c:1044`) is a separate path
  (`crtc->funcs->page_flip_target`), `DRM_MODE_PAGE_FLIP_EVENT` queues the same
  `DRM_EVENT_FLIP_COMPLETE`; it has no out-fence. Prefer the atomic ioctl for probing.

---

## 4. Vendor msm/SDE path (this is what actually runs on the NX679J)

Registration (`NX679J_DD/msm/msm_drv.c`):
- `159: .atomic_commit = msm_atomic_commit` — **the vendor does not use
  `drm_atomic_helper_commit`** (upstream v5.10 msm uses it: `drivers/gpu/drm/msm/msm_drv.c:50`).
- `166: .atomic_commit_tail = msm_atomic_commit_tail` — dead code on this path, because the core
  tail/`commit_work` machinery is never invoked (see below).

`msm_atomic_commit()` (`NX679J_DD/msm/msm_atomic.c:741-865`), called from the ioctl with
`nonblock = (flags & DRM_MODE_ATOMIC_NONBLOCK)`:

```
39: struct msm_commit {
40:     struct drm_device *dev;
41:     struct drm_atomic_state *state;
42:     uint32_t crtc_mask;
43:     uint32_t plane_mask;
44:     bool nonblock;
45:     struct kthread_work commit_work;
46: };
647: commit_init(state, nonblock)          /* stores c->nonblock, kthread_init_work */
    764: c = commit_init(state, nonblock);
    ...  /* pick implicit fences (dma_resv_get_excl_rcu) -> drm_atomic_set_fence_for_plane */
    794: retry: drm_modeset_lock(connection_mutex) … -EDEADLK backoff
    811: ret = wait_event_interruptible_locked(priv->pending_crtcs_event,
             !(priv->pending_crtcs & c->crtc_mask) && !(priv->pending_planes & c->plane_mask));
    816: priv->pending_crtcs |= c->crtc_mask;   /* global, per drm_device */
    824: WARN_ON(drm_atomic_helper_swap_state(state, false) < 0);   /* point of no return */
    832: priv->kms->funcs->prepare_fence(priv->kms, state);        /* out-fence prep, before return */
    853: msm_atomic_commit_dispatch(dev, state, c);
    858: return 0;
```

Dispatch (`msm_atomic.c:665-727`):
```
665: msm_atomic_commit_dispatch()
    675: nonblock = commit->nonblock;
    688-698: kthread_queue_work(&priv->disp_thread[j].worker, &commit->commit_work); ret = 0;
    706: if (ret) { … fallback complete_commit(commit) … }        /* couldn't queue -> synchronous */
    720-721: } else if (!nonblock) {
    721:     kthread_flush_work(&commit->commit_work);             /* <-- blocking commit waits HERE */
    724-725: if (!nonblock) kfree(commit);
```

`msm_atomic_commit_dispatch()` returns as soon as the work is queued; `kthread_flush_work()`
(`kernel/kthread.c:989-1017` @ v5.10) ends in `wait_for_completion(&fwork.done)` — **uninterruptible,
no timeout**. Nothing (not even SIGKILL) releases it before the worker finishes.

The worker (`_msm_drm_commit_work_cb` → `complete_commit()`, `msm_atomic.c:559-620`) is where all
hardware work happens, **for blocking and nonblocking commits alike**:
```
571: drm_atomic_helper_wait_for_fences(dev, state, false);   /* uninterruptible dma_fence_wait */
574: kms->funcs->prepare_commit(kms, state);
576: msm_atomic_helper_commit_modeset_disables(dev, state);
577: drm_atomic_helper_commit_planes(dev, state, DRM_PLANE_COMMIT_ACTIVE_ONLY);
593: msm_atomic_helper_commit_modeset_enables(dev, state);     /* -> 495-496: kms->funcs->commit(kms, state) = SDE hardware kickoff */
608: msm_atomic_wait_for_commit_done(dev, state);             /* -> kms->funcs->wait_for_crtc_commit_done */
610: drm_atomic_helper_cleanup_planes(dev, state);
612: kms->funcs->complete_commit(kms, state);
```
Notes:
- `complete_commit()` **never calls `drm_atomic_helper_commit_hw_done()`** and never calls
  `drm_atomic_helper_setup_commit()`; `crtc->commit` / `hw_done` / `flip_done` core object is not
  used at all on this path. Only `msm_atomic_commit_tail()` (`msm_atomic.c:898-923`, the dead hook)
  calls `hw_done` (line 920).
- Hardware kickoff therefore happens **inside the worker** — i.e. *after* the ioctl returned for
  `NONBLOCK`, and *inside* the flush for `flags=0`.

The kickoff and the completion wait (all inside `NX679J_DD/msm/sde/`):
```
sde_kms_commit()                       sde_kms.c:1212-1237   (funcs .commit, sde_kms.c:4111)
  -> sde_crtc_commit_kickoff()         sde_crtc.c:3999
     4050-4051: if (sde_encoder_prepare_for_kickoff(encoder, &params)) sde_crtc->needs_hw_reset = true;  (only logged)
     4062-4069: if (needs_hw_reset) -> sde_crtc_reset_hw(crtc, old_state, ...) -> is_error = true (still continues)
     4099: sde_encoder_kickoff(encoder, true)
     4104-4110: "store the event after frame trigger":
                sde_crtc->event = crtc->state->event;        sde_crtc.c:4108
sde_kms_wait_for_commit_done()          sde_kms.c:1560      (funcs .wait_for_crtc_commit_done, sde_kms.c:4114)
     1606: ret = sde_encoder_wait_for_event(encoder, MSM_ENC_COMMIT_DONE);
     1607-1610: if (ret && ret != -EWOULDBLOCK) { SDE_ERROR(...); sde_crtc_request_frame_reset(...); break; }
     1613: sde_crtc_complete_flip(crtc, NULL);   /* <-- only place the event/out-fence is signalled */
sde_crtc_complete_flip()                sde_crtc.c:2609-2637
     2632: drm_crtc_send_vblank_event(crtc, event);  -> out fence + DRM_EVENT_FLIP_COMPLETE
```
`MSM_ENC_COMMIT_DONE` waits are **bounded**: `sde_encoder_phys_cmd_wait_for_commit_done()`
(`sde_encoder_phys_cmd.c:1587`) → `_sde_encoder_phys_cmd_wait_for_wr_ptr()`
(`sde_encoder_phys_cmd.c:1434`, `timeout_ms = phys_enc->kickoff_timeout_ms`, doubled in LP1/LP2)
→ `sde_encoder_helper_wait_for_irq()` (`sde_encoder.c:446`) with the timeout split in
`EVT_TIME_OUT_SPLIT = 2` halves (`sde_encoder.c:76: 491-497`). Default
`DEFAULT_KICKOFF_TIMEOUT_MS = 84` (`sde_encoder_phys.h:26`); for fps < 24 it becomes
`2 * (1000/fps)` (`sde_encoder.c:4633-4651`).

### What "completion" looks like to userspace on this driver

| Signal | Where set up | What actually signals it | Bounded by |
|---|---|---|---|
| CRTC `OUT_FENCE_PTR` (`sync_file` fd written to your `s32*`) | core `prepare_signaling()`/`setup_out_fence()` before the commit; vendor `kms->funcs->prepare_fence` (`sde_kms.c:1624`) | `sde_crtc_complete_flip()` → `drm_crtc_send_vblank_event()` → `dma_fence_signal(out_fence)` | *nothing* — pending forever if the path above never runs |
| Plane `IN_FENCE_FD` | core `drm_atomic_plane_set_property` | consumed by `dma_fence_wait(fence, false)` in the worker | *nothing* — an unsignalled input fence hangs the worker |
| `DRM_MODE_PAGE_FLIP_EVENT` event (`drm_event_vblank`, type `DRM_EVENT_FLIP_COMPLETE`, your `user_data`) | core, same event object | same call as the out fence (`drm_send_event_locked` → `wake_up_interruptible_poll`) | *nothing*; read with `O_NONBLOCK` on the drm fd (`drm_file.c:595-596: EAGAIN`) or `poll()` on the drm fd |
| Connector property `"RETIRE_FENCE"` + `"RETIRE_FENCE_OFFSET"` (**vendor-only**, not upstream) | `sde_connector.c:3169-3175`; set via the atomic ioctl: value = pointer to `u64`, kernel `sde_fence_create()`s a fence, `copy_to_user`s the fd, requires the previous value to be `-1` (`sde_connector.c:1740-1790`, dispatch `1831`) | `sde_connector_complete_commit()` → `sde_fence_signal(retire_fence, …)` (`sde_connector.c:1969-1980`, called from `sde_crtc.c:2743`) | nothing; per-frame retire semantics |
| Non-DRM liveness: per-CRTC sysfs `retire_frame_event` (`RETIRE_FRAME_TIME=<ns>`, pollable via `sysfs_notify`) and `vsync_event`, `measured_fps` on device `sde-crtc-%d` (`sde_crtc.c:426-466`, `7335-7337`, notify at `2538-2541`) | — | SDE frame-event callback (`SDE_ENCODER_FRAME_EVENT_SIGNAL_RETIRE_FENCE`) | — |
| Debugfs event log: `…/debug/dump` (`sde_dbg.c:2250: debugfs_create_dir("debug", …primary->debugfs_root)`, `2260: "dump"`, mode 0600) + `/sys/kernel/debug/dri/<N>/…` | — | SDE `SDE_EVT32` traces incl. kickoff/commit-done | — |

Upstream comparison (why the vendor differs): upstream v5.10 msm uses
`drm_atomic_helper_commit` + `msm_atomic_commit_tail`, whose tail calls
`drm_atomic_helper_commit_hw_done()` (`msm_atomic.c:279`); the event is sent by DPU itself
(`dpu_crtc.c:258` in `_dpu_crtc_complete_flip`, `dpu_crtc.c:764`); `commit_hw_done()` only asserts
that the backend already consumed the event (`drm_atomic_helper.c:2329-2331`).

---

## 5. How a tool should probe: time-bounded and verified

### 5.1 Rules

1. **Never issue `flags = 0`** (blocking) commits on this driver while debugging. Blocking means:
   `kthread_flush_work()` (no timeout, uninterruptible) possibly preceded by an unbounded
   `dma_fence_wait()`; the global `pending_crtcs` mask means it also blocks *every other* commit
   from any process. A blocking probe is only acceptable if the process is deliberately abandonable
   (throwaway child, and even then it cannot be killed while in D-state).
2. Always pass `IN_FENCE_FD = -1` explicitly (or don't set the property) so no unsignalled fence
   can be waited on. Never hand the kernel a fence whose producer you don't control.
3. Probe with `flags = DRM_MODE_ATOMIC_NONBLOCK` (+ `DRM_MODE_ATOMIC_ALLOW_MODESET` if the change
   needs a modeset). Add `DRM_MODE_PAGE_FLIP_EVENT` (0x01) if you also want the event on the drm fd.
4. Set the CRTC `OUT_FENCE_PTR` property to the address of a local `s32 out_fence = -1;`. It is
   written **-1 on failure** and with a real fd on success; only read it **after** the ioctl returns.
5. Bound the ioctl itself: nonblocking commits still wait (interruptibly) on `pending_crtcs`
   (`msm_atomic.c:811`). Install a `SIGALRM`/`timer_create` handler with `SA_RESTART` cleared, or
   run the ioctl in a helper thread and give up after the deadline. Note: a signal escapes that
   `wait_event_interruptible`, but it cannot escape `kthread_flush_work()`/`dma_fence_wait()` —
   another reason to never use `flags=0`.
6. Take a `poll(out_fence_fd, POLLIN, deadline_ms)` verdict. Fence signalled ⇒ the vendor commit
   pipeline reached `sde_crtc_complete_flip()` ⇒ the encoder reported `MSM_ENC_COMMIT_DONE`.
   Timeout ⇒ **indeterminate hardware state**, not "commit failed cleanly"; log it as its own class
   (`COMMIT_NO_COMPLETION`) and stop issuing commits on that CRTC until the pipe is proven alive.
7. Also set the drm fd `O_NONBLOCK` and `ppoll()` the drm fd for the `DRM_EVENT_FLIP_COMPLETE`
   event (if `PAGE_FLIP_EVENT` was requested); `read()` with `O_NONBLOCK` returns `-EAGAIN` rather
   than blocking (`drm_file.c:595-596`).
8. Verify a *pre-existing* pipe liveness before committing anything: read
   `/sys/…/sde-crtc-<idx>/retire_frame_event` (`RETIRE_FRAME_TIME`) twice a frame apart, or
   `measured_fps`; or use `DRM_IOCTL_WAIT_VBLANK` (0x3a) with a bounded relative sequence. If the
   pipe is not retiring frames, every commit probe on it will end in a fence timeout — that is the
   informative result, and issuing the commit adds nothing.

### 5.2 Suggested probe skeleton

```c
/* setup: open /dev/dri/card0, set DRM_CLIENT_CAP_ATOMIC=3, take DRM_MASTER.
 * helpers: drmModeGetPropertyId() lookups by NAME.
 */
int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC | O_NONBLOCK);

volatile s32 out_fence = -1;                 /* OUT_FENCE_PTR target */
uint64_t objs[]   = { crtc_id, plane_id, ... };
uint32_t counts[] = { 2, 4, ... };
uint32_t props[]  = { p_out_fence_ptr, p_idle_pc,
                      p_fb_id, p_crtc_id, p_src_w, p_src_h, ... };
uint64_t vals[]   = { (uint64_t)(uintptr_t)&out_fence, 2,
                      (uint64_t)fb_id, (uint64_t)crtc_id, ... };

struct drm_mode_atomic req = {
    .flags          = DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT,  /* 0x0200|0x01 */
    .count_objs     = 2,
    .objs_ptr       = (uint64_t)(uintptr_t)objs,
    .count_props_ptr= (uint64_t)(uintptr_t)counts,
    .props_ptr      = (uint64_t)(uintptr_t)props,
    .prop_values_ptr= (uint64_t)(uintptr_t)vals,
    .reserved       = 0,
    .user_data      = my_tag,
};

/* (a) bound the ioctl in time --------------------------------------------- */
struct sigaction sa = { .sa_handler = on_alarm, .sa_flags = 0 };  /* no SA_RESTART */
sigaction(SIGALRM, &sa, NULL);
alarm(2);                                   /* escape hatch for pending_crtcs wait */
int r = ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &req);
int e = errno;
alarm(0);
if (r < 0) { /* real, synchronous rejection: -EINVAL/-EACCES/-ENOMEM/-EINTR ... */ }
else       { /* "accepted"; hardware NOT touched yet */ }

/* (b) completion fence: the hardware verdict ------------------------------ */
s32 f = out_fence;
if (f < 0) { /* core reported failure (or TEST_ONLY): nothing was committed */ }
else {
    struct pollfd p = { .fd = f, .events = POLLIN };
    int pr = poll(&p, 1, 200);              /* >= 2 frame periods */
    if (pr == 0)  class = COMMIT_NO_COMPLETION;    /* vendor never signalled */
    else if (pr < 0) class = POLL_ERROR;
    else          class = COMMIT_RETIRED;          /* encoder hit COMMIT_DONE */
    int err = 0;
    if (class == COMMIT_RETIRED) sync_file_fence_wait / read fence status via DMA_BUF? /* optional */
    close(f);
}

/* (c) event (only if PAGE_FLIP_EVENT was requested) ----------------------- */
struct drm_event_vblank ev;
if (read(fd, &ev, sizeof ev) == sizeof ev && ev.base.type == DRM_EVENT_FLIP_COMPLETE
    && ev.user_data == my_tag) { /* sequence = ev.sequence, ts = tv_sec/tv_usec */ }

/* (d) independent liveness cross-check ------------------------------------ */
/* cat /sys/class/.../sde-crtc-<idx>/retire_frame_event  (RETIRE_FRAME_TIME=<ns>)
 *   must advance after (b); if it does not, the panel never retired the frame
 *   even though the commit pipeline returned. */
```

Verdict matrix (what to report):

| ioctl | out-fence within deadline | reading |
|---|---|---|
| `-EINVAL`/`-EACCES` | n/a (ptr set to -1) | state/property rejected synchronously; hardware untouched |
| 0 | signalled | commit reached the encoder's commit-done; hardware latched (verify with `retire_frame_event` / pixels) |
| 0 | **not signalled** | `COMMIT_NO_COMPLETION`: software state swapped, hardware state unknown; check dmesg for `wait for commit done returned`, `resource kickoff failed`, frame-reset lines |
| hangs / `-EINTR` from alarm | n/a | blocked in the vendor's global `pending_crtcs` wait → a previous commit is stuck |

Diagnostics that do not require another commit: `/proc/<pid>/stack` (root) or `/proc/<pid>/wchan`,
`echo w > /proc/sysrq-trigger` to dump blocked tasks, watch for the hung-task backtrace at 120 s,
`/sys/kernel/debug/dri/<N>/debug/dump` (SDE event log).

---

## 6. Known caveats when this vendor driver never signals completion

1. **No error return for hardware failures.** `complete_commit()` has no return value; kickoff
   failures set `sde_crtc->needs_hw_reset` (logged at `sde_crtc.c:4050-4051`, recovery attempted at
   `4062-4069`), and `sde_encoder_prepare_for_kickoff()` failure is only logged. The ioctl
   result cannot express them.
2. **The event/out-fence is skipped on error.** `sde_kms_wait_for_commit_done()` does
   `SDE_ERROR("wait for commit done returned %d")` + `sde_crtc_request_frame_reset()` + `break`
   (`sde_kms.c:1606-1613`) — the `break` leaves the loop **without** calling
   `sde_crtc_complete_flip()`, so `crtc_state`/`sde_crtc->event` (and with it the CRTC out-fence and
   the `DRM_EVENT_FLIP_COMPLETE`) is never signalled. The commit is "successful" as far as
   userspace is concerned and the fence stays pending forever.
3. **Return-before-hardware is by design on nonblock.** After `swap_state()` the core will not roll
   the requested state back (`drm_atomic_helper.c:1845-1853`); `FB_ID` readback shows the new
   buffer even if the panel never latched it (the earlier FB57/FB64 ambiguity on this device).
4. **Blocking commits are unkillable.** `kthread_flush_work()` is an uninterruptible, unbounded
   `wait_for_completion` (`kernel/kthread.c:989-1017`); `drm_atomic_helper_wait_for_fences(dev,
   state, false)` is an uninterruptible, unbounded `dma_fence_wait` (`drm_atomic_helper.c:1429-1458`)
   executed *in the worker*. A `flags=0` commit can therefore hang with no timeout, no signal
   escape, and no userspace-observable reason; the process is in D-state, which is what makes the
   hung-task detector fire at 120 s.
5. **A stuck commit blocks the whole device, not just that CRTC.** `priv->pending_crtcs` /
   `pending_planes` are global and are only cleared in `commit_destroy()` (`msm_atomic.c:160-170`);
   every later commit (blocking *or* nonblocking) parks in
   `wait_event_interruptible_locked(priv->pending_crtcs_event, …)` (`msm_atomic.c:811`). Serialize
   probes and never queue a second commit after a fence timeout.
6. **Core commit-tracking is absent on this path** (`setup_commit` never called), so
   `crtc->commit->flip_done`/`hw_done` are not usable as completion signals — the only available
   signals are the ones listed in §4/§5.
7. **`OUT_FENCE_PTR` is core-only on this driver** — the string `out_fence` does not appear anywhere
   in the vendor `display-drivers` tree (verified by recursive grep). Correctness of the fence path
   therefore depends entirely on the driver reaching `drm_crtc_send_vblank_event()`; there is no
   driver-side "always signal on error" fallback.
8. **`RETIRE_FENCE` quirks** (vendor connector property): the client must reset the property to
   `-1` before requesting a new fd (`sde_connector.c:1760-1763`); the fd is produced during
   `atomic_set_property` (i.e. at the set-property phase of the ioctl, before the commit), and
   `sde_fence_signal()` on retire is the frame-event path, not the commit path — useful as a
   per-frame signal, but it is not a commit-status channel either.
9. **`DRM_MODE_ATOMIC_TEST_ONLY` (0x0100) is the only zero-risk probe** (`drm_atomic_check_only`,
   no worker, no hw) — but it cannot detect hardware problems, and it may not be combined with
   `PAGE_FLIP_EVENT`; with TEST_ONLY no out-fence is created either (`prepare_signaling()` returns
   early at `drm_atomic_uapi.c:1123-1124`), so `*out_fence_ptr` stays `-1`. Beware the wrong
   constant `0x02` in `nx679j-display-resume.c` (that is `PAGE_FLIP_ASYNC` → `-EINVAL`).
10. **fd installation timing**: the out-fence fd number is written into your memory during
    `prepare_signaling()` but only installed in the process fd table at `complete_signaling()`
    (both inside the ioctl). Do not read the pointer from another thread while the ioctl is in
    flight, and do not use it if the ioctl returned an error (`fd_install` is skipped, pointer
    forced to `-1`).
