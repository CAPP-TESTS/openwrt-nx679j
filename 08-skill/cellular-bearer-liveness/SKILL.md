---
name: cellular-bearer-liveness
description: "Use when a cellular data bearer looks up but is dead."
version: 0.1.0
author: Hermes Agent
license: MIT
platforms: [linux]
metadata:
  hermes:
    tags: [modem, qmi, wds, openwrt, netifd, lte, 5g, liveness]
---

# Cellular data bearer: liveness and renewal

For embedded Linux / OpenWrt boxes where a userspace client (libqmi/qmicli/`qmi-network`, a vendor helper, or your own tool) holds a QMI **WDS** packet-data session on an embedded data port, with QMAP multiplexing, and the kernel `rmnet` netdev carries the L3.

The recurring failure is not "the modem broke": it is **the session ended and nobody noticed**, because Linux keeps the address and the default route after the bearer is gone. Status surfaces then report a healthy link.

## When to Use

- A link is reported up (LuCI, `ubus status`, `ip addr`) but traffic does not pass.
- Connectivity must hold for hours, and you need to know whether the bearer or only its leftovers are still there.
- A session ends on a suspiciously round number of seconds, and you must tell a client-side timer apart from a network or modem fault.
- You must re-establish a data session and are about to conclude that restarting the session is enough.

## Always-on rules

1. **An address is not proof of connectivity.** After the bearer dies, IPv4/IPv6 and the default route **remain** in the kernel as stale state. `ubus call network.interface.<if> status`, LuCI ("Connected: yes", "Carrier: Present", a long uptime) and `ip addr` all keep reporting health. Never declare "connectivity is continuous" from those.
2. **Liveness needs three things together** — Procedure step 1. Reachability alone, or process liveness alone, is not enough.
3. **Sample beyond the longest internal timer** of the system under test. A sample taken *inside* the session's hold window proves nothing about continuity. Find the hold/lease/keepalive values in the launcher before designing a measurement campaign.
4. **Renewing a bearer is two moves, not one**: re-establish the QMI session **and** re-apply L3. A new PDN usually gets a **different subnet**, so the old address/route are not merely stale, they are wrong.
5. **Find who owns the timer before blaming the hardware.** A session that dies on a round number of seconds is a client-side `hold`/timeout, not a modem fault. The QMI session has no inherent short lifetime; the client's own deadline does.
6. **The session outlives the process; the CID is what holds it.** Closing the QMI socket sends nothing, and `SIGKILL` is session-safe — only an explicit `CTL Release CID` (or a WDS Stop) makes the modem drop the call. So a long-lived holder process is *not* what keeps the bearer up; the un-released CID is. Before blaming the network, check what your client does on its **normal** exit path: a clean exit that releases the CID kills the session, a crash does not.
7. **Grow the client timer before building a renewer.** There is no session-duration timer in QMI WDS and multi-hour sessions are normal practice, so raising the `hold`/deadline is a one-value change that buys hours. Do it first, then add renewal for the drops a timer cannot cover (network-initiated releases, operator reclaim of an idle context).
8. **No upstream component re-establishes a WDS bearer by itself.** A connection manager keeps the CID, reports the disconnect with a call-end reason, and stops there; a distribution's "force/reconnect" option may only remove an error latch on a couple of paths. The re-establishment loop is the integrator's job — decide the owner explicitly (`references/wds-bearer-ownership-models.md`) instead of assuming one exists.
9. **`carrier` is not the bearer.** It is the driver-maintained `__LINK_STATE_NOCARRIER` bit, and for an `rmnet` netdev the driver never touches it (`ip link`, `ubus ... network.device status .carrier`, `/sys/class/net/*/carrier`, `ethtool` all read that one bit, netifd via netlink `IFF_LOWER_UP`). A live bearer can report carrier absent and a dead one can report it present — read the bearer from reachability plus a packet-service-status query, see `references/rmnet-carrier-semantics.md`.
10. **Poll: a disconnect indication you never subscribed to is a disconnect you never see.** Blocking on receive is not monitoring. A client that never sent WDS indication registration may have a "network disconnected" branch that can never fire; an explicit periodic packet-service-status query is what makes detection independent of indications. Cadence reference is ModemManager: 5 s steady state, first poll after 30 s, 10 s timeout per query (a figure quoted as "20 s" is a timeout, not a poll period). Poll the control plane on the existing client-CID/port, and treat a query timeout as *unknown*, not as dead. Because the control plane only reports the modem's session, pair that fast free poll with an occasional paid data-plane probe (ICMP to a numeric IP, bound to the data interface) — the choice and the failure modes of each probe are in `references/bearer-liveness-probes.md`.
11. **The operator can end the context without telling you, and usually does.** Three distinct endings: an explicit deactivation (a Delete Bearer / PDU Session Release / Detach *does* arrive, with a cause you must act on — ESM/5GSM #39 "reactivation requested" means reconnect at once), the reachability chain (`T3412`/`T3512`, default 54 min → network mobile-reachable timer, T3412+4 min → network-dependent implicit detach — **no message at all**, all PDN connections/PDU sessions deleted), and non-3GPP state timeouts on the Gi/SGi path (context alive, path dead → do not re-dial). An S1/NG release for "user inactivity" is *not* an ending. No 3GPP inactivity timer exists for a normal PDN connection; the only standardized one is PCC's, for IMS emergency. Timer values, causes and the resilience checklist: `references/operator-timer-deactivation.md`.

## Procedure

### 1. Decide: alive, or stale L3?

Run `scripts/bearer-alive.sh` on the device (read-only; it changes nothing):

```sh
ssh -i ~/.ssh/<key> root@<device> 'sh -s' < scripts/bearer-alive.sh
```

It reports, and you must combine:

| Signal | Alive | Dead |
|---|---|---|
| `ping -c 4 -W 3 1.1.1.1` | OK | 100% loss |
| session processes (`ps w` for the helper, e.g. `qmi-qrtr`) | running | **`Z` zombie** (or absent) |
| session log | `HOLDING …` still current | `stopping handle=…` / a Stop request |
| kernel `ip -4 addr` + default route | present | **also present** ← the trap; ignore for liveness |

**Dead + L3 present = stale L3**, and every status surface is showing a false green. Report it as such rather than as a link failure.

A decisive tell is in the session log: `[session] HOLDING seconds=<N>` states the granted lifetime, and a later client-originated Stop request (QMI WDS Stop `0x0021`, DPM Close `0x0021`) means the client stopped it, not the network.

### 2. Renew

Bracketed values are one measured instance — read the current ones from the session log, never hardcode.

```sh
# a) re-establish: the data-port holder AND the WDS session
setsid <helper> dpm-session <ep-type> <ep-id> <rx> <tx> <hold> > /tmp/dpm-renew.log 2>&1 </dev/null &
setsid <helper> wds-session <apn> <ep-type> <ep-id> <mux> <hold> > /tmp/wds-renew.log 2>&1 </dev/null &
# poll the log until it holds BOTH the settings block and the HOLDING line — a marker alone is not enough,
# and truncate the log file before starting the new session (see the stale-log pitfall)

# b) read the NEW settings from the fresh session log
#    (measured field names: "  IPv4 addr: " / "  IPv4 gateway: " / "  IPv4 netmask: ")

# c) re-apply L3 — add the new address, replace the default route, THEN drop the old address
ip -4 addr add <new_addr>/<prefix> dev <iface>
ip -4 route del default ; ip -4 route add default via <new_gw> dev <iface>
ip -4 addr del <old_addr> dev <iface>

# d) refresh the network-manager view so status stops showing the old values
ifup <external_iface>
```

Verified outcome: `10.97.82.164/29` stale with ping KO → `10.99.143.159/26` via a new gateway with `ping 4/4, 0% loss`. Repeating step (a) **without** step (c) left ping at 100% loss: the session restart alone is not sufficient.

Step (d) was measured safe on a read-only external protocol: `ifup <iface>` did not remove the kernel address or the default route (ping OK at +4 s and +10 s) and did refresh the manager's view. Verify on a new stack before relying on it — the safety depends on the interface configuration, notably a disabled default-route option.

### 3. Prove it stays up

Campaign over **repeated boots and at least one full hold window**, recording per boot: boot ID, first reachability, the hold value, and reachability again **after** the hold expires. A single early sample is the trap this workflow exists to prevent.

## Pitfalls

- **Zombie processes are the cheapest liveness tell.** `ps w | grep '[q]mi-qrtr'` showing `Z` means the session is over even while LuCI says connected.
- **BusyBox**: no `timeout`, no `stat -c`, no `pkill`, no `install` — wrap SSH from the host with the host's `timeout`, and use `cp` + `chmod` to place files.
- **`/tmp` helpers may be symlinks** into the volatile rootfs; editing the symlink's target is an image change, not a runtime test.
- **Don't infer the cause from a round number**: confirm against the session log (`HOLDING`, then a client Stop request) before attributing the drop to the network or the modem.
- **A second client on the same control/data pair can destroy the first session.** Do not "check status" by starting another WDS call; reuse the existing client/CID.
- **Never start a WDS call while one is up.** It fails with `CallFailed` / a verbose call-end reason of `[internal] call-already-present`. A renewal loop must stop the old session and confirm its handle is gone first — otherwise it spins on a hard error instead of recovering.
- **Establish in order: data format → WDS call → L3.** The aggregation format is negotiated on the embedded endpoint *before* the data call; with a WDS call already up, the format setter refuses (measured: `wda-qmap` → "returned format is not verified plain QMAP", and `wda-get` shows the aggregation TLVs at 0 instead of 5) — the call pins the endpoint's format. After a clean boot the same endpoint read as `0x11=2 / 0x12=5 / 0x13=5` (plain QMAP) and the whole bring-up chain completed in one run (`wds rc=0`, ICMP OK, external interface created). So a manual bring-up must keep the launcher's step order; if the format is pinned, a fresh boot is the measured way to free it, not a re-run with the steps shuffled.
- **Bring-up chains carry "/tmp gates" ("already done" markers).** A second run of a chain that failed midway skips steps ("MSS already running, skipping", "dms already done in this boot") and is NOT equivalent to a fresh boot: measured, the half-completed run left the data steps failing while a clean reboot brought the whole chain up. Read the chain log for skipped-step lines before designing a retry.
- **A distribution's connection manager can be an adversary to your session.** Watch for it deleting every `upper_*` link of the master netdev before its data-format setup, rewriting the data format (aggregation/QMAP) on a shared endpoint, or opening its own port on the embedded endpoint you already hold. Keep it passive (no connect, no interface `up`) or give it a disjoint mux window — but **passivity is not protection, so verify it rather than assuming it**: a manager was caught attempting a **data-interface reset on the master netdev you are using** with no interface configured and no connect ever issued, its log naming your netdev as the target. If it aborts early — e.g. it resolves a QRTR control port to a non-existent `/dev/<node>` path, or the interface is already down — your session survived by luck, not by design. Grep its log for the reset / data-format / link-delete steps and read **your** netdev in the target field before trusting any co-existence plan — the hazard list, the verification grep and the decision rule are in `references/coexisting-with-a-connection-manager.md`.

## References and scripts

- `references/rmnet-carrier-semantics.md` — what `carrier`/`operstate` actually mean for an rmnet netdev (kernel and netifd citations), why `state` can stay `unknown`, and why carrier is decoupled from bearer liveness in both directions.
- `references/wds-bearer-ownership-models.md` — who should own a long-lived data call: what ends a session, the three upstream ownership models and their hazards, and the contract for a renewal supervisor.
- `references/operator-timer-deactivation.md` — which operator-side timers end an idle bearer (T3412/T3512, mobile reachable, implicit detach/deregistration, PCC inactivity timer, GTP cause #11), the causes a UE must react to, and the resilience checklist — with clause-level citations and the spec versions they were read from.
- `references/nx679j-x65-bearer.md` — the measured instantiation on the NX679J / X65: exact launcher lines, hold values and observed numbers.
- `references/openwrt-external-l3.md` — how to expose an externally-owned L3 to netifd/LuCI read-only (the `address-external` protocol), and what that protocol can and cannot report.
- `references/bearer-liveness-probes.md` — which probe actually proves liveness (QMI packet-service status vs ICMP bound to the data interface vs DNS), what each costs in radio energy, and the failure modes that make a probe lie.
- `references/upstream-duty-map.md` — which upstream component (MM / netifd / proto qmi / qmi-network / mwan3track) owns each duty of a supervisor (poll, renew, re-apply L3, refresh status), what none of them does, and the do-not-duplicate rules for the residual custom code.
- `references/coexisting-with-a-connection-manager.md` — whether a distribution's modem manager may stay installed "passively" beside your session (it may not, by configuration): the actions it takes before any connect, the log grep that proves it is touching your netdev, why survival is often its own bug, non-deterministic status objects, and the decision rule.
- `scripts/bearer-alive.sh` — read-only liveness probe: process state, real reachability, kernel L3 vs the last session settings.
