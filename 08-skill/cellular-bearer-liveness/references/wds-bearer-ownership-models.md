# Who owns a long-lived WDS bearer

Decision material for "keep this data call up for hours". Distilled from libqmi / ModemManager / OpenWrt `uqmi` + netifd sources; verify against the version on the target before relying on it.

## Two lifetimes, and only the modem-side one matters

| Object | Lives where | What ends it |
|---|---|---|
| QMI client / socket fd | host, userspace only | nothing modem-side — `close(fd)` sends no QMI message |
| CID allocation + packet-data handle + session | modem firmware | an explicit `CTL Release CID` (the modem then disconnects on its own) or an explicit `WDS Stop Network <handle>` |

Consequences that decide designs:

- A holder process is **not** required for persistence. The canonical pattern is: start the network, **then exit**, having kept the client-ID (`--client-no-release-cid` plus an explicit `--client-cid`) so a later, unrelated process can query and stop the same call. Persisting the handle (for a deliberate stop) and the CID (for the client slot) in a state file is part of that pattern.
- `SIGKILL`/crash is session-safe. A **clean** exit that releases the CID is not — it is indistinguishable from hanging up. Know which one your client does.
- Pass `--client-cid=N` **and** `--client-no-release-cid` together; the first alone still releases on exit.
- Unreleased CIDs accumulate in a finite pool and later allocations fail — carry the same CID across renewals rather than allocating a fresh one each time.

There is **no session-duration timer** in the WDS service: no TLV in Start Network or Get Packet Service Status carries a lifetime. Multi-hour single sessions are normal. What ends a call is a network-initiated deactivation (operator-configured), a UE reachability failure (missed periodic TAC/TAU → implicit detach, which releases all PDN connections), or your own client.

## The three ownership models

| Model | Who holds the call | Renewal | Hazards to check |
|---|---|---|---|
| Distribution connection manager owns it (a modem daemon keeping a per-family WDS client, subscribing to packet-service-status indications, owning the data link and mux id) | the manager | you re-issue connect when its bearer reports disconnected; its dispatcher scripts are the usual hook | it may tear down links and re-negotiate the data format on ports you thought were yours (below) |
| In-distro protocol handler with modem-side autoconnect | the **modem** | none needed for the call itself; you still need a supervisor for the host-side state | autoconnect must be enabled explicitly (a start-network flag or a set-autoconnect call), and some managers deliberately disable it while they manage the link |
| Your own client + supervisor | your client | your loop | you must implement detection, backoff and L3 re-application yourself |

Signals that the "reconnect" you are counting on is not automatic: a reconnect option that only swaps an error latch for a retry, a manager that publishes `disconnected` with a call-end reason and nothing more, and a monitor path documented as needing a minimum version you do not have.

## Contract for a renewal supervisor

1. **Detect**: a periodic packet-service-status query **plus** an out-of-band reachability probe. A modem-level "connected" is not routability, and registered-without-internet is a known state. Cadence reference: ModemManager 1.24 polls every 5 s (first poll at 30 s) with a 10 s per-query timeout (`mm-base-bearer.c:50-54,196-245`; `mm-bearer-qmi.c:315-360`) — a number quoted as "20 s" is a timeout, not a poll period.
2. **Stop before start**: a WDS start while a session is up fails hard (`call-already-present`). Stop the old handle and confirm it is gone.
3. **Re-establish**: the data-port holder session *and* the bearer session, in the order the bring-up used (holder first if the data path needs the endpoint open).
4. **Re-apply L3**: read the **new** settings from the fresh session log; add the new address, replace the default route, then drop the old address. A new PDN usually gets a different subnet, so the old L3 is wrong, not just stale.
5. **Refresh the status surface** so it stops reporting the previous values (an `ifup` of the read-only external interface does this — see `references/openwrt-external-l3.md`).
6. **Bound it**: retry with backoff, log every attempt with its outcome, and stop after N failures rather than looping forever. Do not repeat the same step twice without new information.

## Exposing the state honestly

Core netifd proto handlers set `up`/`uptime` once at setup and never re-validate, and a UI's "Connected" is derived from that uptime — so a read-only reporter over a dead session renders as healthy. Options, cheapest first: have the supervisor push a negative signal into the manager's state (it will actually go down), or expose your own health flag and consume it where you display status. A periodic liveness check with a score/threshold (the shape of the well-known multi-wan trackers: probe, hysteresis, an `online/offline` state file, a hotplug call on transition) is the reference implementation to copy.

## Coexistence with a connection manager

Before letting a manager and your own session share one modem, check whether it:

- deletes **every** `upper_*` link of the master netdev before its data-format setup (this removes mux links it did not create);
- rewrites the data format (aggregation/QMAP, max datagram) on a shared endpoint — the negotiated set can depend on the netdev driver name, so a vendor-named driver may get settings that break an existing QMAP session;
- opens its own port on the embedded endpoint you already hold;
- treats a start-network answer that did not take effect as "adopt the global handle" and later stops *that*.

- Mitigations, in order: keep the manager passive on the data path (never connect it, never bring its interface up); reach a shared control port through the same proxy the manager uses rather than a second direct opener; or give the manager a disjoint mux window / preallocated links via its udev tags, or tell it to leave the network port alone.

## CID lifetime mechanics (verified in libqmi main)

What libqmi asserts, so you can design around it without guessing:

- Releasing a CID is opt-in per call: `QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID`; without it the library only invalidates the local object (`QMI_CLIENT_CID = QMI_CID_NONE`) and sends nothing (`- /* And now, really try to release the CID */`, `- /* No need to release the CID, so just done */`).
- On `QmiDevice` teardown with clients left registered it *warns and deliberately does not release*: `"client for service '%s' with CID '%u' wasn't released"`, with the comment `"There is no point in trying to request CID releases, as the device itself is being disposed."` So a crash of your supervisor never hangs up the call by itself.
- `qmi-proxy` implements exactly "the CID outlives the process": CIDs whose app went away without releasing are moved to `disowned_qmi_client_info_array` (`/* Disown all QMI clients that were not explicitly released */`), the device is kept open, and a later `--client-cid=N` re-claims them (`"QMI client reowned"`). Log lines to look for: `QMI client disowned` / `QMI client reowned`.
- Port type decides whether the modem cleans up instead: rpmsg/smdpkt are stateful — closing the chardev makes the modem free all unreleased CIDs (libqmi issue #51), and `device_close_if_unused()` ignores disowned CIDs, so a proxy exit on such a port kills the lease. USB (and QRTR) get no open/close notification, so leaked CIDs persist until an explicit release — and then pile up into `QMI protocol error (5): 'ClientIdsExhausted'` (issue #92).
- On QRTR the CID never reaches the modem at all: libqmi's QRTR endpoint answers `CTL Allocate/Release CID` locally and `Release CID` only unrefs a `QrtrClient` socket (`qmi-endpoint-qrtr.c`), so `--client-cid/--client-no-release-cid` semantics are host bookkeeping there.
- The only host-forced wipe documented: `QMI_DEVICE_OPEN_FLAGS_SYNC` (qmicli `--device-open-sync`) — *"Synchronize with endpoint once the device is open. Will release any previously allocated client ID."*
- Cleanup tool for a leaked lease: `qmicli --wds-noop --client-cid=N` ("Just allocate or release a WDS client") — cheaper than Stop Network when only the CID is stale.
- Do not claim "releasing the CID hangs up the call" as libqmi-documented: the CTL Release CID entry in libqmi's message JSON has no prose at all, and every shipped caller ends the call explicitly first (qmi-network: `--wds-stop-network=$PDH`; OpenWrt qmi.sh: `--stop-network 0xffffffff --autoconnect` then the handle then `--release-client-id`; ModemManager: WDS Stop Network per IP family, CID released when the port closes). The coupling is behavioural, not stated in libqmi.
