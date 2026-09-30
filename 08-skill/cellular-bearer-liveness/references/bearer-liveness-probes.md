# Probes for a dead cellular bearer behind stale kernel L3

Which signal actually proves the bearer is alive, and what each one costs. Instantiations for this project: `references/nx679j-x65-bearer.md`.

## 1. Why the kernel address proves nothing

- The session owner is the **QMI WDS client**, not the kernel IP stack. When the session ends (WDS Stop `0x0021`, a network-initiated release, or a holder that dies), nothing removes the address or the default route: `ip -4 addr`, `ip -4 route`, `ubus`/LuCI all keep reporting green. Measured on NX679J: address + default route present, `ping` 100% loss, the session holder in state `Z`, `[session] stopping handle=` in `/tmp/wds-session.log`.
- There is **no link-layer signal that goes red on session end.** Wi-Fi has deauth/beacon loss, Ethernet has carrier; an RMNET netdev is a virtual QMAP mux device (`Documentation/networking/device_drivers/cellular/qualcomm/rmnet.rst`) that stays `UP`/`RUNNING` with its address configured regardless of the modem's PDP state. Do not assume the same about `carrier`/neighbour state — read `ip -d link show dev rmnet_data0` on the actual device before trusting either. (ifconfig on a phone shows rmnet as `Link encap:UNSPEC`, no L2 addressing to speak of.)
- On Android, the IPv4 address can additionally be a **CLAT/464XLAT synthesised address** (`v4-rmnet_data0`, RFC 6877) whose lifetime tracks a v6 bearer that may already be gone. Kernel IPv4 present + dead path is the normal shape there too.
- Consequence: only two sources are evidence. (a) the session owner's own state (QMI / RIL), (b) a packet that leaves the device and comes back.

## 2. Signal inventory

| Signal | Proves | Cannot prove | Radio cost |
|---|---|---|---|
| `ip addr` / route / ubus / LuCI | nothing (kernel state only) | alive vs stale | 0 |
| Session-holder process state (`Z`) + session log markers (`HOLDING`, `stopping handle`) | local session bookkeeping | a network-initiated release that the holder never learned about | 0 |
| `rmnet_data0` RX counter stalls while TX climbs | the uplink is being handed to the modem and nothing comes back | distinguished dead-bearer vs blocking/PGW-side loss (needs a second signal) | 0 |
| QMI WDS Get Packet Service Status (`0x0022`) / packet-status indication | the modem's view of the session | user-plane forwarding (PGW/NAT/firewall/route breakage) | 0 (control channel, no user-plane packets) |
| ICMP echo to a numeric IP, bound to the data interface | forward+return path through the operator user plane, end to end | who is at fault when it fails | 1-3 packets + RRC promotion if the radio was idle |
| DNS query to a numeric resolver | that a resolver answered | that the *answer* came over this bearer (cache/stub/walled garden) | 1 packet pair + RRC promotion |
| TCP SYN to a known `:443` / HTTP `204` | the same as ICMP, plus stateful middleboxes and NAT path | more bytes, more packets, same promotion cost | handshake + promotion |

## 3. QMI packet-service status: authoritative about the session, blind to the path

- `WDS Get Packet Service Status` = message `0x0022`, output TLV `0x01` *Connection Status* (`guint8`). Values: `0x01` disconnected, `0x02` connected, `0x03` suspended (libqmi also models `authenticating`). The unsolicited form is `QMI_WDS_PKT_STATUS_IND` (`34`), which carries the connection-status TLV plus reconfigure/end-reason TLVs.
- ModemManager ≥ 1.6 listens for that indication and drops the bearer to *disconnected* when the TLV says disconnected, so "the stack gives up on its own" is exactly this mechanism. `qmicli --wds-get-packet-service-status --client-cid=<cid>` reads the same field on demand.
- **Failure modes**: (a) *session attached ≠ packets flow* — PGW-side/NAT/firewall/APN-restriction and host-side L3 mistakes both show `connected`; (b) if you never subscribed to the indication, detection latency equals your poll interval; (c) a second QMI client on the wrong endpoint / a second WDS call can collide with the holder — poll the **same client-CID** and never re-issue `Start Network` from a monitoring path; (d) a query timeout means **unknown**, not dead: renewing on “unknown” turns a slow modem into an outage.
- Design rule: tri-state (`connected` / `not connected` / `no answer`), and never let a control-plane answer alone declare liveness to the user.

## 4. ICMP to a numeric IP, bound to the data interface: the cheapest real proof

- Why numeric: no resolver in the path, so a DNS failure can never be mistaken for a dead bearer.
- Why **bound to the interface**: an unbound ping is routed by the main table. On a phone the default may be Wi-Fi/VPN, and Android deliberately routes per-network via fwmark + `ip rule` (`iif lo oif rmnet_data0 uidrange ...` style rules). An unbound probe can therefore pass over Wi-Fi and report the cell path healthy.
- Binding, precisely:
  - iputils: `ping -I <iface>` sets the **source interface** (bound to device); `ping -I <address>` sets only the **source address** and does *not* prevent the kernel from picking another egress. Use the interface form; `-B` also stops ping from re-selecting the source.
  - Android app: get a `Network` and use network-bound sockets (`ConnectivityManager.bindProcessToNetwork`, `Network.bindSocket`, `Network.getSocketFactory`); this is the app-level equivalent of the device bind.
  - Raw ICMP sockets are not available to Android apps; the unprivileged path is an ICMP **datagram** socket (`IPPROTO_ICMP` + `SOCK_DGRAM`), allowed only when the caller's GID is inside `net.ipv4.ping_group_range` (“1 0” by default = nobody). Check `cat /proc/sys/net/ipv4/ping_group_range` on the device. Executing `/system/bin/ping` works where the binary is present (not all OEM builds ship it) but goes through `Runtime.exec` and inherits the process network binding.
  - Root/OpenWrt (this project): plain `ping -I rmnet_data0 -n -c4 -W3 <ip>`.
- Target choice: 2-3 **independent** anycast/numeric addresses (e.g. `1.1.1.1`, `8.8.8.8`, `9.9.9.9`) plus the bearer's own next hop (the gateway from the QMI settings) so a failure can be localised: gateway responds + public address silent ⇒ path/interconnect problem with the bearer up; neither responds ⇒ bearer or host L3 dead.
- **A single probe target is a failure generator (measured, NX679J/WindTre 2026-09)**: with only `1.1.1.1` as target, an operator path that answers ICMP to `8.8.8.8`/`8.8.4.4`/`9.9.9.9`/`208.67.222.222` but not to `1.1.1.1` produced a false-red verdict every cycle — the renewer fired every ~74 s for ~2 days (~1641 renewals) and the churn eventually **wedged the modem firmware itself** (WDS/DMS silent, `qrtr_tx_wait` in kernel logs), after which the user-facing "reconnect" could no longer work either. Two rules: probe **2+ independent targets** and declare dead only if all fail; and **never let a renewer keep spinning against a control plane that no longer answers** — a renew loop that cannot restore service must stop (bounded attempts) and escalate to modem recovery (stop/start the remote processor, or reboot), or it destroys the very thing it is trying to repair.
- Hysteresis and reading the error: 2 consecutive failures spaced ≥ 2 s before declaring dead. Distinguish `Destination Host Unreachable` / `ENETUNREACH` (local routing problem — on the measured stack this was the stale `via 10.97.82.165` route) from plain timeout (path problem). Require the RX counter to have stayed flat across the probe window, otherwise “no reply” is just loss.
- **Failure modes**: ICMP filtered/rate-limited by the operator or by anycast targets ⇒ false red (mitigate: multiple targets, TCP fallback, never act on a single failure); unbound probe ⇒ false green; Doze/App Standby on Android suspends network access and defers jobs, so a probe that fails while the device is dozing proves nothing — gate the probe on device state or run it from a privileged/system context; IPv6-only + CLAT ⇒ probe v6 as well as v4.

## 5. DNS: never the proof, sometimes a useful hint

A resolved name says only that *some* resolver answered. Real false greens:

- local cache/stub answers (dnsmasq, netd's resolver, systemd-resolved) — the query may never leave the device;
- walled gardens/portals that answer DNS (and often ICMP) while the data path is closed;
- an unbound query going out Wi-Fi/VPN.

The one sound variant is the inverse test: query a **random non-existent name** and require NXDOMAIN — an answer instead of NXDOMAIN means DNS is being hijacked (portal, or an operator/CPE proxy). That detects a middlebox, not liveness. If DNS is used as a liveness probe, send it to a numeric server address bound to the interface and treat a *positive* answer as weak evidence.

## 6. Cost: bytes are free, RRC promotions are not

- A probe costs almost nothing in bytes; what it costs is the **radio state change**. 3GPP RRC keeps the connection in a high-power state for a tail after the last packet: 12.5 s on 3G, ~6 s on GSM (TailEnder), and LTE tails measured at ~5-10 s depending on carrier. Radio-on time is dominated by continuous reception plus tails/paging, not by payload bytes.
- Therefore the cost driver is the **number of probe events per hour**, not packets per probe. A probe every 30 s means the radio never leaves the connected/tail window — the pathological case. Interval must be ≫ tail (minutes, not seconds) or the probe must be control-plane (QMI), which generates no air-interface traffic at all.
- If a probe is also meant to keep a CGNAT/UDP binding alive, note carrier NAT binding timeouts of roughly 30-120 s (mobile CGNAT often 30-90 s) — that argues for a short keepalive, i.e. a *keepalive*, not a liveness probe. Separate the two: cheap frequent keepalive if you need one, infrequent bound probe for liveness.
- Android's own answer to this economics: data-stall detection counts **DNS timeouts** (default 5 consecutive) and reads **TCP fail-rate from kernel `tcp_info`** (min 10 packets, evaluation window ≥ 60 s), with no injected probe packets at all (`DataStallUtils`: `DATA_STALL_EVALUATION_TYPE_DNS` | `DATA_STALL_EVALUATION_TYPE_TCP`). Injected probes are for bounding a verdict, not for continuous polling.

## 7. Recommended design

Two tiers, one verdict, tri-state:

1. **Fast tier, free, every ~20 s**: session-holder liveness (process not `Z`, no `stopping handle` since boot) + QMI Get Packet Service Status on the existing client-CID. `connected` ⇒ still alive (not proven); `disconnected`/`suspended` ⇒ **dead now**, renew immediately.
2. **Truth tier, paid, every 3-5 min, and on any QMI anomaly, RX-counter stall, or user-visible failure**: one bound ICMP probe to 2 numeric targets + gateway comparison + interface counter delta.

Verdicts: `session_dead` (QMI says so) / `path_broken` (QMI connected but bound probes fail, counters flat RX) / `alive` (QMI connected + probe reply received on the data interface) / `unknown` (QMI silent, probe inconclusive) — never collapse `unknown` into `dead`.

Renewal after a dead verdict is two moves, and only the first is what QMI gives you: re-establish the session **and** re-apply L3 from the *new* settings (addr/gateway/netmask, then replace the default route), otherwise the bearer is up while the host still pings the old, gone peer (measured).

## 8. If the verdict has to drive a UI

Kernel state is what a status page renders, so a read-only protocol that reports `ip addr` keeps the false green. Any connectivity claim shown to a user must be fed by the verdict above, and stale L3 (address present with `path_broken`) must render as *not connected*, or the surface keeps lying after the fact.

## Sources

- QMI WDS: libqmi `--wds-get-packet-service-status`, `WDS Get Packet Service Status` = `0x0022`, TLV `0x01` Connection Status — <https://www.freedesktop.org/software/libqmi/man/latest/qmicli.1.html>, chromiumos `data/qmi-service-wds.json`; connection-status values + `QMI_WDS_PKT_STATUS_IND` (34) and ModemManager behavior — libqmi-devel archive, <https://lists.freedesktop.org/archives/libqmi-devel/2017-April/002285.html>.
- RMNET driver — <https://www.kernel.org/doc/html/latest/networking/device_drivers/cellular/qualcomm/rmnet.html>.
- `ping -I` semantics (interface vs address) — <https://man7.org/linux/man-pages/man8/ping.8.html>.
- Unprivileged ICMP datagram sockets / `net.ipv4.ping_group_range` — `icmp(7)`, <https://manpages.debian.org/buster/manpages/icmp.7.en.html> (`IPPROTO_ICMP` echo sockets, default “1 0”); Android ICMP libraries that rely on it — <https://github.com/marsounjan/icmp4a>.
- Android routing by fwmark/uid and network-bound sockets — <https://unix.stackexchange.com/questions/635577/>.
- Android data-stall signals (DNS timeouts, TCP fail-rate, no injected packets) — `src/android/net/util/DataStallUtils.java`, `packages/modules/NetworkStack`; public constants — <https://developer.android.com/reference/android/net/ConnectivityDiagnosticsManager.DataStallReport>.
- Doze/App Standby suspend app network access — <https://developer.android.com/training/monitoring-device-state/doze-standby>.
- Radio tail energy: TailEnder, <https://people.cs.umass.edu/~arun/papers/TailEnder.pdf> (3G tail 12.5 s, GSM 6 s); event-based modem power model, SIGMETRICS'18, <https://ranger.uta.edu/~jmeng/pubs/sigmetrics18.pdf> (LTE tails 5-10 s; continuous reception + paging dominate radio-on time).
- 464XLAT/CLAT synthesised IPv4 — RFC 6877.
- NAT/CGNAT UDP binding timeouts 30-120 s (practitioner figures, not a standard) — WireGuard keepalive discussions.
