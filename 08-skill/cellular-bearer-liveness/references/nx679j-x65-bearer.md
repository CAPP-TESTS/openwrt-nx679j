# Measured instantiation: NX679J / X65 (SM8450), OpenWrt 25.12.5

Project-specific numbers behind the parent skill. The authoritative project skill for everything else is the user-owned `nx679j-openwrt` skill plus `STATO-ATTUALE.md`.

## Where the session lifetime is set

| Item | Value | Source |
|---|---|---|
| WDS session | `wds-session <apn> 4 1 1 3600` | `/usr/lib/*/modem/openwrt-data-staged.sh` (~line 78); `/tmp/openwrt-data-*.sh` are **symlinks** to these ramdisk files |
| DPM holder | `dpm-session 4 1 2 23 3600` | `chain.sh` (~line 91), `«modem-prepare».sh` (~line 47) |
| Tool's accepted range | `hold: 1..86400 s` (rejects `0` and `>86400`) | `qmi-qrtr-observed` source |

Because the launcher lives in the ramdisk, changing the hold is an **image** change, not a runtime tweak. And the ramdisk copy is not the only copy: the launchers ship inside the project's persistence `tools.tar`, while the builder re-injects some of them (`chain.sh`) from its own source tree **after** the persistence layer is extracted — so a value that appears in both must be changed in both, or the build silently wins with the stale one. Assert the value on the **final assembled payload**, never on the source file.

Once raised, the same sessions hold for a day: measured after `hold=86400` on a fresh boot — sessions alive, `hold=86400` in the session log, data ready in the same ~64 s, no loss over the following minutes. Changing the hold does not move the bring-up timing.

## Observed failure mode

At uptime ≈ 37300 s with a `hold=3600`:

```
ps w                     -> helper processes in state Z
ping -c 5 1.1.1.1        -> 5 transmitted, 0 received, 100% packet loss
ip -4 addr show          -> 10.97.82.164/29       (still present)
ip -4 route show default -> via 10.97.82.165      (still present)
/tmp/wds-session.log     -> [session] HOLDING seconds=3600
                            ...
                            [request] 00 06 00 21 ...   (WDS Stop 0x0021)
                            [session] stopping handle=635623792
```

LuCI meanwhile rendered `Connected: yes`, `Carrier: Present`, `Uptime: 10h 20m`, `IPv4: 10.97.82.164/29` — a false green built entirely from stale kernel state.

## Renewal measured

```
before : 10.97.82.164/29 via 10.97.82.165   ping KO
renew  : dpm-session 4 1 2 23 86400 + wds-session internet.it 4 1 1 86400
         new settings -> IPv4 addr 10.99.143.159, gateway 10.99.143.160, netmask 255.255.255.192
         (session restarted, L3 NOT re-applied -> ping still 100% loss)
apply  : ip addr add 10.99.143.159/26 ; route del default ; route add default via 10.99.143.160 ;
         ip addr del 10.97.82.164/29
after  : 10.99.143.159/26 via 10.99.143.160   ping 4/4, 0% loss
```

Later `ifup wan_early` did not disturb address or route, kept ping OK at +4 s and +10 s, and refreshed `ubus` to the current values.

## Order of operations, and the wedged-endpoint state (measured)

- **The data format must be set before the call.** With a WDS session already holding the endpoint, `wda-qmap` is refused — `[wda] returned format is not verified plain QMAP; stop` — and the endpoint stays raw-IP with aggregation off (`wda-get` returns 0x11=2, 0x12=0, 0x13=0), so a correct address plus default route on the netdev still give 100% loss. Killing an extra DPM holder does **not** clear the refusal.
- **A clean reboot is the cheap way out of a wedged endpoint.** After one, the boot chain completed end-to-end with no manual steps: `data wds rc=0`, `bearer_ready=1`, `cell ping rc=0`, and the chain itself created the UCI interface — `v113: interfaccia UCI 'wan_early' ... su rmnet_data0` — and the second chain pass reported `healthy: rmnet_data0 con IP e ping ok, catena saltata`. When the endpoint state is in doubt, reboot before hand-driving sessions.
- Acceptance signature for a manual `wda-qmap`: the tool prints `SET_VERIFIED raw-IP=2 UL-QMAP=5 DL-QMAP=5 QoS=0`, and the endpoint TLVs read back 0x12=5/0x13=5. Anything else means the modem did not apply the format — do not proceed to the call.
- The chain is timing-sensitive to the MSS: a first pass that starts while the modem processor is not up fails its data steps, and its one-time gates then make the retry skip them. Do not read a failed first pass as "the stack is broken" — read `/tmp/chain.log` end to end and judge the last pass.

## Status exposure

`wan_early` runs a custom read-only protocol (`proto_init_update "$ifname" 1 1` = address-external, reporting the kernel's address and gateway, no `ip` writes) with the matching LuCI asset `protocol/<name>.js`. Recipe and acceptance test: `references/openwrt-external-l3.md`.

Two earlier configurations are **falsified** on this stack and must not be retried:

- `proto static` on the early interface — netifd becomes the L3 owner and removes the address/routes it manages.
- `proto none` — netifd still claims the device and flushes protocol L3 at bring-up: the address and default route disappeared ~73 s in while the netdev stayed `UP`.

Also falsified as a remedy: tagging the modem's net port with `ID_MM_PORT_IGNORE` in ModemManager — the tag suppresses probing but does **not** prevent the port from being admitted as an additional port.

## Resolved since the first measurement

- **The long-hold question is settled**: the hold was raised (3600 → 86400) and measured. Rationale and the alternatives that were rejected on evidence: `references/wds-bearer-ownership-models.md`.
- **The distribution's modem manager is not the answer here.** It never re-establishes a session by itself, and letting it own the data path conflicts with this bring-up (it deletes `upper_*` links of the master netdev and opens its own port on the embedded endpoint). It is kept **passive**: no connect call, no bringing its interface up.
- **But it does have to be restarted after the bring-up** so it creates its modem object: without that, it thrashes (hundreds of `cleaning up port` lines in its log) and never publishes one — `mmcli -L -J` returns an empty modem list, so a UI's cellular page renders empty. With the restart it publishes in ~3 min with a two-digit cleanup count. Assert in the build that no active line runs the interface's `up` (the log-marker string contains that command name too — exclude the marker lines when scanning, or the assertion trips on its own message).

## Open work

- **Event-less drops are still unhandled in the image**: a network-initiated release would leave stale L3, because nothing polls the session. The supervisor contract is in `references/wds-bearer-ownership-models.md`; it is designed but not built.
- Making the status surface stop claiming connectivity when the bearer is dead (the read-only protocol cannot tell).
- A campaign over repeated boots covering at least one full hold window.
- Visual/touch verification with real input. For screen evidence, a host webcam captured through the running compositor's own API is non-destructive; treat a blurred, colour-cast frame as unreadable rather than reading structure into it, and never call pixels verified from a frame you cannot align to the client's known output.
