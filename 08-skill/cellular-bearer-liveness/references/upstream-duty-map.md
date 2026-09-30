# Which upstream component owns which duty of a bearer supervisor

Before writing a renewer, map each duty to an existing owner. Verified against ModemManager 1.24,
netifd master `06d06c8`, OpenWrt main `proto qmi` (uqmi), libqmi, mwan3 2.12.2 — the full
file:line review lives at `~/reports/bearer-supervisor-upstream-design-review.md`.

| Duty | Upstream owner / mechanism | Residual custom code |
|---|---|---|
| poll liveness | MM bearer connection monitor: 5 s steady / 30 s first, 10 s per-query timeout, WDS Get Packet Service Status, published as `Bearer.Connected`/`ConnectionError`, **never re-establishes**; tag-gated (`ID_MM_QMI_CONNECTION_STATUS_POLLING_ENABLE`) and only for MM's own bearer. `qmicli --wds-get-packet-service-status`, `qmi-network status` are one-shots. `proto qmi` polls only during setup. netifd gives link events only | a probe that can see a **foreign** session (~15-20 lines), or 0 if mwan3track runs the reachability loop and a hotplug subscriber reacts |
| renew the session | only the CID owner can: `proto qmi` stop sequence (`--stop-network 0xffffffff --autoconnect`, `--stop-network $pdh`, `--release-client-id wds`) then `--set-client-id wds,$cid --start-network`; `qmi-network` refuses a second start while a PDH exists | **irreducible** (~25 lines) for an externally held bearer |
| re-apply kernel L3 | netifd applies what the handler notifies: `proto_add_ipv4_address`/`proto_add_ipv4_route`/`proto_send_update`, `NLM_F_CREATE\|NLM_F_REPLACE` so repeats are idempotent | 3 `ip` commands, only because L3 ownership is kept external (`address-external` reports without touching; `proto static`/`none` were falsified) |
| refresh netifd status | `/sbin/ifup <if>` = `ubus call network reload` + down + up (measured safe here); `ubus ... renew/up/set_data`; a handler defining `proto_<name>_renew` is advertised `renew_available` and reachable via `ubus call network.interface.<if> renew` | **zero** (one command) |

## What does not exist upstream (do not pretend otherwise)

- **No periodic liveness poll of a session you do not own.** MM polls, but only its own bearer.
- **No automatic re-establishment** of a bearer by any component: MM publishes `disconnected` and
  stops; netifd's `checkup_interval` watches a *stuck setup* only (`proto-ext.c:657-693`), and
  `proto_run_command` supervision only notices the supervised process dying (marks link lost /
  tears the interface down) — it never re-runs setup.
- **No way to renew through MM without handing it the call**, and MM's first enabling step
  *disables* WDS autoconnect (`mm-broadband-modem-qmi.c:13649-13653`) — the modem-side
  auto-reconnect is not a free renewal path once MM enables the modem.
- **No status field for bearer liveness.** A read-only reporter over a dead session renders green.
  Cheapest honest fix: `ifdown` the interface, or `proto_notify_error` from the handler
  (`interface_add_error`), which surfaces in `ifstatus`. Do not build a second status surface.

## Do-not-duplicate rules

1. Never open a second QMI client/CID to poll or renew — the holder's CID *is* the session.
2. Do not depend on MM's connection-status polling (tag-gated, own-bearer only); keep MM passive,
   and keep it from enabling the modem.
3. Do not use netifd's `checkup_interval` as a liveness watchdog.
4. Do not add a second protocol handler, LuCI asset or status file; reuse the existing read-only
   external-L3 protocol (see `openwrt-external-l3.md`).
5. Do not hand-roll the status refresh (`set_data`, `ifstatus` writes) — `ifup` is the supported,
   measured path.
6. Do not re-implement a generic ping tracker if mwan3track is acceptable: it already provides
   probe pinning, integer-score hysteresis, monotonic-clock state files, edge-triggered hotplug and
   procd respawn.
7. Reuse the bring-up chain's own start/stop commands for renewal; only the trigger is new code.
