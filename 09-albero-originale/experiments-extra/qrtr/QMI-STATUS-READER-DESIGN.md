# Minimal, maintainable QMI status reader — NX679J (X65 / SM8450)

Target: RedMagic 7 (NX679J), OpenWrt userspace, modem owned by ModemManager
(`qcom-soc` plugin, primary port `qrtr0`). This document fixes **what the reader
opens, what it sends on a poll loop, what it costs, and how it fails** — based on
how `mmcli`, `qmicli` and `uqmi` structure the same job, and on measurements taken
against the live unit.

Status: design. Every fact marked *(verified)* was reproduced on the running device
this session; the raw commands are in §9.

---

## 1. Scope

**In:** read-only liveness/status sampling of the modem, safe to run next to
ModemManager.

**Out (hard boundary):** anything that writes modem or bearer state —
`WDS Start/Stop Network`, `Bind Mux Data Port`, `Set IP Family`, `Set Operating
Mode`, `WDA Set Data Format`, `DPM Open/Close Port`, profile writes, SIM power.
Two writers on the WDS data path fight each other; the data path is MM's job.
The reader must never be able to break the bearer.

---

## 2. Transport model (why this is not a `uqmi` job)

Over QRTR, the modem is **node 0**; each QMI service is a separate QRTR port on
that node. *(verified: 48 services on node 0, WDS at port 61, local node = 1.)*

| Fact | Consequence |
|---|---|
| libqmi **implements CTL locally** on QRTR (`qmi-endpoint-qrtr.c`, "We implement the CTL service here, so divert those messages"), and allocates the client id from a local counter | No `CTL Allocate CID` round trip to the modem. No `ctl_port` exists *(verified: `ctl_port=-1`)*. Do not look for a CTL service. |
| "Opening a service" = NS lookup (`qrtr_node_lookup_port`) + one socket, message tagged with a local CID | One socket per service; no proxy process needed. |
| QRTR multiplexes natively | MM and `qmicli` run **simultaneously** on `qrtr://0` *(verified)*. `qmi-proxy` is unnecessary and must not be introduced. |

`uqmi` is therefore **not usable here**: it is `/dev/cdc-wdm*`/MBIM-oriented
(`qmi_device_open(&dev, device)` on a device path), and this port has no cdc-wdm
node — only `qrtr0`. It stays in the toolbox as prior art for *structure*, not
as a component.

---

## 3. Which services to open

Source of truth for ids: the `qmi-service-*.json` specs in `experiments/refs/`
(extracted per service, §9).

| Service | id | Open? | Why |
|---|---|---|---|
| **NAS** Network Access | `0x03` | **yes** | Registration, serving system, signal. The core of "is it attached". |
| **DMS** Device Mgmt | `0x02` | **yes** | Operating mode = the readiness gate the bring-up chain already keys on. One-shot identity reads (model/ids/revision) at startup only. |
| **WDS** Wireless Data | `0x01` | **yes, read-only** | Packet service status (is the data call up). Never Start/Stop. |
| **UIM** | `0x0B` | optional | Only if SIM/slot detail beyond DMS is wanted. `get-card-status` works *(verified)*; skip in v1. |
| CTL | `0x00` | **no** | Synthesized locally over QRTR (§2). |
| WMS / PDS-LOC / WDA / DPM / PDC / IMS | — | **no** | Messaging, GNSS (expensive), data-format writes, port ownership, config writes. All outside status. |

Floor: **3 services, 4 sockets worst case** (NAS is used for two purpose classes
but one socket is enough).

---

## 4. Poll loop: exact messages

Message ids below are from the per-service spec extract (§9), not from memory.

### Cycle (baseline, mirrors MM's cadence)

| When | Service | Message | id | Purpose |
|---|---|---|---|---|
| every 30 s | NAS | Get Serving System | `0x0024` | registration state, PLMN, RAT, cell id/TAC |
| every 30 s | NAS | Get Signal Info | `0x004F` | RSRP/RSRQ/RSSI/SNR (LTE + NR) |
| every 30 s | WDS | Get Packet Service Status | `0x0022` | connection status |
| every 30 s | DMS | Get Operating Mode | `0x002D` | online / low-power / offline |
| once at start | DMS | Get Model / IDs / Revision | `0x0022`/`0x0025`/`0x0023` | identity for the record |
| on change only | NAS | Get System Selection Preference | `0x0034` | mode/band preference |

MM's own numbers, for reference: `SIGNAL_CHECK_INITIAL_TIMEOUT_SEC 3`,
`SIGNAL_CHECK_TIMEOUT_SEC 30`, `REGISTRATION_CHECK_TIMEOUT_SEC 30`
(`mm-iface-modem.c`, `mm-iface-modem-3gpp.c`). MM's registration check calls NAS
**Get System Info `0x004D`** and falls back to **Get Serving System `0x0024`**;
its signal check calls **Get Signal Info `0x004F`** then **Get Signal Strength
`0x0020`** with a request mask. Same shape, one message fewer.

### Better: subscribe first, poll as a safety net

MM **disables** its generic periodic signal poll when the modem can deliver
indications (`MM_IFACE_MODEM_PERIODIC_SIGNAL_CHECK_DISABLED`, set at
`mm-broadband-modem-qmi.c:5756` / `:5833`). Our unit does support this —
`mm.log` shows `extended signal capabilities supported` and received indications.

So after v1 works, promote to:

1. NAS **Set Event Report `0x0002`** + **Register Indications `0x0003`**
   (signal + serving-system + registration).
2. Drop the 30 s poll to a **60 s safety poll of 2 messages**
   (Get Serving System + Get Packet Service Status) to catch missed events.

That is 4 modem round trips/min instead of 8, and near-zero on an idle network
(events only fire on change).

---

## 5. Expected power cost

Two separate costs; do not confuse them.

**CPU (measured on the unit).** End-to-end `qmi-status.sh` cycle (4 `qmicli`
invocations + shell): **30 ms**. Individual calls over QRTR: NAS serving-system
6 ms, NAS signal-info 4 ms, DMS operating-mode 4 ms, WDS packet-service-status
4 ms (18 ms; the rest is shell/`sed`).

| Cadence | cycles/min | modem round trips/min | round trips/day | CPU (one core) |
|---|---|---|---|---|
| 1 s (naive) | 60 | 240 | 345 600 | 3.00 % |
| 5 s | 12 | 48 | 69 120 | 0.60 % |
| **30 s (recommended)** | 2 | **8** | 11 520 | **0.10 %** |
| 60 s | 1 | 4 | 5 760 | 0.05 % |

The 30 s choice is a **30× reduction** in modem wakeups vs a 1 s loop, and costs
0.10 % of one core. CPU is not the constraint; message count is.

**Modem.** Each QMI round trip is an AP→ADSP IPC and a possible wake from a
low-power state. The number that matters is round trips per minute (§table),
because with a bearer up the baseband is already on and tracking the network —
status reads are marginal there. The regime that hurts is *data call down, modem
otherwise idle*: a 1 s loop holds it awake, a 30 s loop lets it sleep.

**Measured power is not available on this image.** `/sys/class/power_supply/` is
empty (no `current_now`, no fuel gauge) *(verified)*. `soc:qcom,pmic_glink` is
present in debugfs, so the path to telemetry exists but `qcom_battmgr` is not
loaded. To turn the table above into watts: load `qcom_battmgr` and read
`current_now`/`voltage_now`, or use an external USB power meter, and compare
idle vs 30 s-loop vs 1 s-loop over a fixed window with the radio in a known state.
Until then, quote **round trips/min**, not watts.

---

## 6. Failure handling

Rules, each tied to behaviour actually observed:

1. **Timeout.** `qmicli`'s internal per-message timeout is 10 s; healthy calls
   finish in ~5 ms. Treat >3 s as "modem busy/asleep" (retry once next cycle),
   >10 s as a failure. Never let one hung call swallow the next cycle.
2. **Protocol error ≠ broken reader.** Map known-good-state errors to states:
   - `WDS Get Packet Statistics 0x0024` → `QMI error 70 InvalidOperation` when no
     data session *(verified)*.
   - `WDS Get Current Settings 0x002D` → `QMI error 15 OutOfCall` when the call is
     down *(verified)*.
   Neither is a fault; both mean "no data call". Encode this table; do not log them
   as errors.
3. **Do not depend on options this build lacks.** `--dms-uim-get-state` and
   `--dms-get-power-state` are **not compiled in** on the device's libqmi
   *(verified: "Unknown option")*. Use `--uim-get-card-status` for SIM state.
   Probe capability at startup, not per poll.
4. **Modem disappearance / SSR.** QRTR emits `DEL_SERVER` (the existing tool prints
   `(gone) service=… node=… port=…`). On that, re-resolve the NS entry and reopen;
   treat DMS operating mode going `offline` as the same condition. This mirrors MM's
   `node_removed` handling.
5. **Concurrency.** QRTR multiplexes *(verified: MM + qmicli together)*, so running
   alongside MM is safe — **provided rule in §1 holds**: read-only on WDS. The
   existing `qmi-qrtr` binary mixes read and write verbs and must not be used as the
   poller.
6. **Back off, never spin.** Consecutive failures → exponential backoff capped at
   the poll interval; reset on the first good cycle. Cap log lines per unit time.
7. **Last-known-good + staleness.** Publish the last good sample with its timestamp
   and an explicit `stale` flag rather than blanking values. The unit's clock starts
   at 1970 *(verified)*, so stamp with monotonic time, not wall clock.
8. **One reader.** Two readers both subscribing indications would double the event
   load; keep a single instance.

---

## 7. How the existing tools are structured (what we borrow)

| Tool | Structure | What we take |
|---|---|---|
| **qmicli** | One-shot: open device (`QMI_DEVICE_OPEN_FLAGS_*`) → allocate client for **one** service → send one message → `operation_shutdown` releases and closes. One service per invocation; services dispatched from `allocate_client_ready()`. | The message set and the "one request, one reply" model. Also: it is already installed on the unit and speaks `qrtr://0`, so v1 needs **zero new code**. |
| **mmcli** | Thin D-Bus client; all polling lives in the daemon. Its QMI plugin holds a long-lived client per service and relies on indications. | The cadence (30 s / 30 s), the indication-first policy, and the registration/signal message choice. |
| **uqmi** | One-shot CLI over `/dev/cdc-wdm*`: `qmi_device_open` → run commands → close; `--keep-client-id`/`--sync` manage lifetime; `-t` sets the response timeout; `uqmid` is the ubus daemon variant. | The explicit response-timeout and "keep vs release client id" ideas. Not the transport — no cdc-wdm here. |

The existing `experiments/qrtr/qmi-qrtr.c` (1403 lines) is a **bring-up/debug
toolbox** (`playback`, `dpm-session`, `wds-session`, `wda-qmap`, raw hex verbs,
mixed read/write). It is the reference for the QRTR wire format and a fine
interactive probe — it is not the reader. If the reader is ever written in C rather
than shelling out, lift only §4's verbs from it, read-only.

---

## 8. Recommended shape

**v1 (do this): shell-out sampler, no new code, no SDK.**
Implemented and exercised: **`experiments/qrtr/qmi-status.sh`** — one
`qmicli -d qrtr://0 <verb>` per message in §4, hard per-call timeout, one
key=value record per cycle, exponential backoff on consecutive failures, `ok=`
flag, monotonic timestamps. One sample costs 30 ms; `qmi-status.sh 30` loops at
the recommended cadence. Rationale: `qmicli` is already in the image and proven
on `qrtr://0`; the failure table in §6 is easier to keep honest in shell than to
rebuild around libqmi.

**v2 (only if indications or tighter cadence are needed): a libqmi-glib daemon.**
Long-lived client per service, subscribe with `0x0002`+`0x0003`, 60 s safety poll.
This is MM's structure minus the state machine. Cost: needs libqmi-glib for the
target — the SDK is not in the tree, so this is a real, separate build task; that
is exactly why it is not v1.

**Operational notes for v1.** This image has **no coreutils `timeout`**, and a
naive subshell watchdog stalls `$( )` capture for the whole timeout because the
watchdog inherits the pipe — the watchdog in `qmi-status.sh` is therefore
redirected to `/dev/null`. Keep that redirect if the timeout logic is rewritten.
Do not ship the sampler as a permanent 1 s poll: 30 s is the point (§5).

---

## 9. Evidence (commands run against the live unit)

```
# transport + concurrency
qmicli -d qrtr://0 --dms-get-operating-mode        # -> Mode: 'online'   (while MM held the modem)
mmcli -m any | grep state                          # -> disabled (MM connected concurrently)

# status reads that work
qmicli -d qrtr://0 --nas-get-serving-system        # -> registered, WINDTRE, MCC 222 MNC 88, lte, cell 138240769, TAC 12995
qmicli -d qrtr://0 --nas-get-signal-info           # -> RSSI -65 dBm, RSRQ -13 dB, RSRP -104 dBm, SNR 4.6 dB
qmicli -d qrtr://0 --dms-get-model / --dms-get-ids # -> model '0', IMEI 86*************
qmicli -d qrtr://0 --uim-get-card-status           # -> Slot 1 present, usim ready
qmicli -d qrtr://0 --wds-get-packet-service-status # -> 'disconnected'

# expected-error cases (must map to states, not faults)
qmicli -d qrtr://0 --wds-get-packet-statistics     # -> QMI error 70 InvalidOperation
qmicli -d qrtr://0 --wds-get-current-settings      # -> QMI error 15 OutOfCall

# options NOT compiled into the device's libqmi
qmicli -d qrtr://0 --dms-uim-get-state             # -> Unknown option
qmicli -d qrtr://0 --dms-get-power-state           # -> Unknown option

# QRTR namespace
qmi-qrtr watch 3                                   # -> local node=1; services=64; modem(node0)=48; wds_port=61; ctl_port=-1

# timing (fine, /proc/uptime float)
5x --dms-get-operating-mode = 0.020s ; --nas-get-serving-system = 0.030s ;
--nas-get-signal-info = 0.020s ; --wds-get-packet-service-status = 0.020s

# no power telemetry
ls /sys/class/power_supply/                        # -> empty
```

Sampler validation (on the unit, `/tmp/qmi-status.sh` — the same file as
`experiments/qrtr/qmi-status.sh`):

```
sh /tmp/qmi-status.sh
  ts=1408.89 reg=registered rat=lte mcc=222 mnc=88 op=WINDTRE rsrp=-104 dBm
  rsrq=-13 dB snr=5.2 dB data=disconnected mode=online ok=1
  rc=0   elapsed 0.030 s

sh /tmp/qmi-status.sh 5   -> 3 samples, 5.03 s apart
sh /tmp/qmi-status.sh 30  -> 2 samples, 30.03 s apart
```

Message-id extraction: `scratch/qmi-status-ids.py` (parses
`experiments/refs/qmi-service-{wds,dms,nas,uim}.json`).

## 10. Files

| File | Role |
|---|---|
| `experiments/qrtr/QMI-STATUS-READER-DESIGN.md` | this spec |
| `experiments/qrtr/qmi-status.sh` | v1 read-only sampler (verified) |
| `experiments/qrtr/qmi-qrtr.c` | existing bring-up/debug toolbox — probe only, not the poller |
