# Late netdev discovery on OpenWrt — netifd / procd-hotplug / ModemManager

Scope: how a netdev that appears *late in boot* (our `rmnet_ipa0`, `qmapmux*`, `rmnet_data0`) is noticed by
(a) procd hotplug, (b) netifd, (c) ModemManager; and whether a missing/added link event can be synthesized safely.

Evidence base (all local, no guessing):
- procd source clone: `~/.hermes/profiles/kernel-re/cache/scratch/procd` (openwrt/procd)
- netifd source clone: `.../scratch/netifd-git`
- ModemManager 1.24.0 source clone: `.../scratch/ModemManager` (tag 1.24.0)
- kernel: vendor copy of the device's own net-sysfs (`.../scratch/ksrc/net_core_net-sysfs.c`) + upstream v5.10 `lib/kobject_uevent.c`, `drivers/base/core.c`
- image: `build-v64/check/owrt` rootfs (MM 1.24.0, OpenWrt feed modemmanager) and `mm-final/`

---

## 1. What the kernel guarantees (this is not the problem)

`register_netdevice()` reaches `netdev_register_kobject()` (`ksrc/net_core_net-sysfs.c:2312`), which:

```c
/* Hold back the KOBJ_ADD uevent until the device is listed. */
dev_set_uevent_suppress(dev, 1);
error = device_add(dev);
...
return error;
}
/* Announce a fully registered device to userspace. This pairs with the uevent
 * suppression from netdev_register_kobject(). */
void netdev_uevent_add(struct net_device *ndev)   /* :2358 */
{ dev_set_uevent_suppress(dev, 0); kobject_uevent(&dev->kobj, KOBJ_ADD); }
```

- `netdev_uevent()` (`:2197`) adds `INTERFACE=<name>` and `IFINDEX=<n>`; the driver core adds
  `ACTION`, `DEVPATH`, `SUBSYSTEM=net`, `SEQNUM` at emit time.
- Broadcast: `kobject_uevent_net_broadcast()` → `NETLINK_KOBJECT_UEVENT`, group 1
  (`lib/kobject_uevent.c:592`). Any number of readers, each gets its own copy.
- `cat /sys/class/net/<if>/uevent` prints **only** `INTERFACE=`/`IFINDEX=` — `ACTION`/`DEVPATH`/`SUBSYSTEM` are added at emit time, they are not readable from the file.

⇒ **Every netdev that registers successfully — including runtime-created rmnet/qmapmux devices — produces exactly one genuine `add` uevent, emitted deliberately *after* its sysfs is complete** (so hotplug consumers never see a half-registered netdev).

⇒ The premise written in `netlink-watch3.c` ("the kernel does not deliver kobject uevents for runtime-created netdevs, procd hotplug-call is never invoked") is **wrong** for netdevs registered through the normal path. If our hotplug scripts never saw the event, the cause is downstream of the kernel (§5).

## 2. procd (PID 1) — the OpenWrt hotplug dispatcher

- `plug/hotplug.c:613-616`: opens `NETLINK_KOBJECT_UEVENT` with `nl_pid = 0`, `nl_groups = -1` (i.e. group 1);
  the handler raw-`recv()`s (`:571`) and parses NUL-separated `KEY=VALUE` — **no sender check**.
- Dispatch is data-driven by `/etc/hotplug.json` (libubox `json_script_run`, rules = `/etc/hotplug.json` via `state.c:131`):
  - top-level `case ACTION` handles **only `add` and `remove`** → a `change` uevent dispatches nothing;
  - fallback branch: `if isdir /etc/hotplug.d/%SUBSYSTEM% → exec /sbin/hotplug-call %SUBSYSTEM%`;
  - `exec` handlers export the whole uevent env, so `/etc/hotplug.d/net/*` sees `ACTION/DEVPATH/SUBSYSTEM/INTERFACE`.
- procd also republishes the event on ubus (`hotplug_ubus_event`, `hotplug-dispatch.c:463`).
- Coldplug: `plug/coldplug.c` forks `/sbin/udevtrigger`; procd's `udevtrigger` walks `/sys/bus/*/devices` and
  `/sys/class/*` and **writes `"add"` into `/sys<devpath>/uevent`** (`plug/udevtrigger.c:80-105`, scan at `:253-258`).
  That is the kernel-supported re-emit primitive, used by OpenWrt itself at every boot.

## 3. netifd — two independent channels

- **rtnetlink**: `NETLINK_ROUTE` socket + `nl_socket_add_membership(RTNLGRP_LINK)`
  (`system-linux.c:371-379`). `cb_rtnl_event()` (`:755`) only *updates* devices netifd already knows:
  `dev = device_find(ifname); if (!dev) return 0;`. A brand-new netdev with no UCI config creates no netifd device.
- **kernel uevents**: `create_hotplug_event_socket(&hotplug_event, NETLINK_KOBJECT_UEVENT, handle_hotplug_event)`
  (`system-linux.c:374`) → `handle_hotplug_msg()` (`:785`) parses `add@`/`move@`/`remove@` plus `SUBSYSTEM`
  (must be `net`) and `INTERFACE`, then calls `device_hotplug_event(ifname, add)` — i.e. it creates/updates the
  device object for an externally-created link. It **drops anything whose netlink source portid is not 0**
  (`if (nla.nl_pid == 0)`, `:841`).

⇒ netifd's knowledge of a late netdev comes from the `SUBSYSTEM=net` uevent, not from rtnetlink.

## 4. ModemManager 1.24.0 on this image: D-Bus ReportKernelEvent is the *only* inbound port channel

Build facts: OpenWrt feed `MESON_ARGS += -Dudev=false -Dudevdir=/lib/udev`
(`ow-packages/net/modemmanager/Makefile:80-82`); `usr/sbin/ModemManager` NEEDED has no `libudev`/`libgudev`;
no udevd and no `libudev.so` in the rootfs (`/lib/udev` holds only `rules.d`). So `WITH_UDEV` is undefined.

- MM has **no uevent listener at all**. The only netlink socket in MM is `src/mm-netlink.c` (`NETLINK_ROUTE`,
  `:546`) used by the QMI plugin to *create/destroy* mux netdevs — a writer, not a listener.
- Port ingress = D-Bus `ReportKernelEvent` (`mm-base-manager.c:1264` → auth → `handle_kernel_event`, `:639`):
  mandatory `action` ∈ {`add`,`remove`}, `subsystem` must be one of MM's probed subsystems, `name`; on `add`:
  `mm_kernel_device_generic_new(properties)` (`:690`, the no-udev branch, guarded by `#if defined WITH_UDEV` →
  we always take the generic branch) then `device_added(self, kernel_device, hotplugged=TRUE, manual_scan=TRUE)` (`:694`).
- The generic backend resolves sysfs itself: `/sys/class/<subsystem>/<name>`
  (`kerneldevice/mm-kernel-device-generic.c:168-190`) — a hand-made `sysfspath` is unnecessary and risky.
- It also evaluates `/lib/udev/rules.d/*.rules` internally (`mm-kernel-device-generic-rules.c`) → our
  `80-mm-nx679j.rules` (`rmnet_ipa0 → ID_MM_PHYSDEV_UID=qcom-soc`, `rmnet_data0 → ID_MM_PORT_IGNORE`,
  `qmapmux* → qcom-soc`) still applies **without udev**, and `ID_MM_CANDIDATE` gates `device_added`
  (`mm-base-manager.c:565`).
- Who feeds it: `/etc/hotplug.d/net/25-modemmanager-net` (shipped by the MM package, run by procd) →
  `mm_report_event()` (`usr/share/ModemManager/modemmanager.common:121`) → `mmcli --report-kernel-event=...`;
  it also appends to `/var/run/modemmanager/events.cache`, which `ModemManager-wrapper` replays at MM start —
  MM's only "catch-up" mechanism on OpenWrt.
- Timing windows (they define the race):
  - plugin manager: `MIN_WAIT_TIME_MSECS 2000` (after the 1st port of a device), `MIN_PROBING_TIME_MSECS 4000`,
    `EXTRA_PROBING_TIME_MSECS 4000` without udev (2000 with udev). The comment there names our exact path:
    "Longer time when not using udev, as we rely on mmcli --report-kernel-event events to report new port
    additions, e.g. via openwrt hotplug scripts."
  - bearer: `WAIT_LINK_PORT_TIMEOUT_MS 2500` (`mm-bearer-qmi.c:448`, `mm-bearer-mbim.c:247`) → after the WDS/mux
    link is created MM waits 2.5 s for port `net/<linkname>` to be **grabbed** (`mm-base-modem.c:669`), else
    "Timed out waiting for link port 'net/rmnet_data0'". Grabbing requires the port to have entered MM via a report.
- A report that arrives after that device context finished **cannot retrofit a port into the existing modem**
  (`device_added` → existing modem already owns that physdev UID) → restarting MM is the only way to pick it up.
  That is exactly why `mm-standard-boot.sh` does `kill -9` + restart.

## 5. Why we "miss" the late rmnet — check in this order

1. Timing: the uevent fires at netdev registration, but MM was already probing/past the window for that physdev
   (2 s/4 s/4 s), so the modem exists without its data port; a later report cannot fix it.
2. `25-modemmanager-net` (our patched copy `mm-final/25-modemmanager-net`, 886 B) filters virtual devices: only
   `qmapmux*`/`qmimux*`/`mbimmux*`/`rmnet*` pass, and it hands MM `/sys${DEVPATH}` — a wrong path makes the
   generic backend read the wrong attributes (driver/physdev/uid).
3. `mmcli` failing/racing (dbus not up, MM not running, `/var/run/modemmanager` missing) → event only lands in
   `events.cache`, delivered at the next MM start, not now.
4. Event fired before MM's first start and was never cached → invisible forever (no udev ⇒ MM never enumerates).
5. Not a kernel problem: `KOBJ_ADD` is emitted exactly once, on purpose, after sysfs is complete (§1).

Live checks (read-only): `/tmp/hpl.log` (our `00-logger`), `logread | grep -i 'hotplug\|modemmanager'`,
`grep -nE 'kernel event reported|Timed out waiting for link port' /var/log/mm.log`,
`ls -l /var/run/modemmanager/`.

## 6. Can a link event be synthesized safely?

### 6.1 Recommended, kernel-blessed: re-emit through sysfs
```sh
echo add > /sys/class/net/<if>/uevent      # root only; same primitive as procd's udevtrigger
```
Kernel path: `store_uevent()` (`drivers/base/core.c`, `DEVICE_ATTR_RW(uevent)` `:1995`, attribute created in
`device_add()` `:2880`) → `kobject_action_type()` → `kobject_uevent(KOBJ_ADD)` → the **kernel regenerates the
whole environment** (ACTION/DEVPATH/SUBSYSTEM/INTERFACE/IFINDEX/SEQNUM) → broadcast on group 1 → procd hotplug
scripts *and* netifd consume it as genuine.

Acceptable side effects (all idempotent-ish, nothing about device state changes):
- `/etc/hotplug.d/net/*` run again for an existing device (identical to what coldplug does at boot);
- MM receives a second `ReportKernelEvent` (MM's dedupe/`device_added` no-ops for an already-known port);
- `00-sysctl` re-applies `net.*.<devname>.*` sysctls; procd republishes the ubus event.

Rules: action must be `add` (or `remove`), never `change` (`/etc/hotplug.json` dispatches only add/remove).
Do not announce a netdev that MM must ignore (`rmnet_data0` → `ID_MM_PORT_IGNORE`) — announce the physdev.

### 6.2 Broken as written: raw `NETLINK_KOBJECT_UEVENT` injection (`netlink-watch3.c`)

Kernel side is permissive: `uevent_net_rcv_skb()` (`lib/kobject_uevent.c:724`) needs only `CAP_SYS_ADMIN` in the
netns userns; `uevent_net_broadcast()` (`:681`) then appends `SEQNUM`, strips the `nlmsghdr` and sets
`NETLINK_CB(skbc).portid = 0`. So a synthetically injected uevent *is* indistinguishable from a kernel one —
even netifd's `nl_pid == 0` check passes. But our implementation cannot work:

1. `nlh->nlmsg_type = 0` is `NLMSG_NOOP`; `netlink_rcv_skb()` skips NOOP messages **before** the uevent input
   handler runs → the datagram is silently discarded (nothing ever reaches procd or netifd).
2. The payload format string embeds `\0` inside a C literal, so `snprintf` stops at the first NUL; its return
   value `n` (used as the length) covers only `add@/devices/virtual/net/<if>` — `ACTION=`, `SUBSYSTEM=`, `INTERFACE=`
   are never transmitted (and `nlmsg_len = NLMSG_LENGTH(n)` inherits the truncation).
3. `DEVPATH` is hardcoded to `/devices/virtual/net/<if>` — wrong for netdevs whose sysfs parent is not virtual
   (`qmi_wwan`/USB, platform/IPA); the hotplug script would then hand MM a non-existent `/sys…` path.

If this path is ever needed: `nlmsg_type` must be anything but NOOP/DONE/ERROR (e.g. `1`), destination
`nl_pid = 0`/`nl_groups = 1`, payload = NUL-separated `var=value` with the first token `action@devpath`, correct
DEVPATH, sender needs `CAP_SYS_ADMIN`. No advantage over 6.1, and it can lie about DEVPATH — so don't.

### 6.3 Keep: direct D-Bus notification
`mmcli --report-kernel-event="action=add,name=<if>,subsystem=net"` (or raw `ReportKernelEvent`) is the documented
API and MM treats it identically to the hotplug path. Omit `sysfspath` and let MM resolve `/sys/class/net/<name>`;
if you must pass it, compute it with `readlink -f /sys/class/net/<if>`. Announce the physdev (`rmnet_ipa0`,
`qmapmux*`), never `rmnet_data0`.

### 6.4 Never: mutate the interface to force an event
`ip link set down/up` or a rename only produces `RTM_NEWLINK` change events: netifd ignores unknown devices and
MM has no rtnetlink listener → zero benefit, plus real risk of breaking the bearer/data path.

## 7. Recommended shape for our stack

- One rtnetlink watcher (`RTMGRP_LINK`, filter `rmnet*|qmapmux*|qmimux*|mbimmux*`) that:
  1. notifies MM immediately via D-Bus (`mmcli --report-kernel-event`, no `sysfspath`), and
  2. re-emits for the OpenWrt side with `echo add > /sys/class/net/<if>/uevent`.
  Delete the raw netlink injection function (dead code, and misleading while debugging).
- Ordering beats synthesis: let the rmnet/qmapmux netdev exist before MM probes that physdev, or (re)start MM
  after it appears (current `mm-standard-boot.sh` flow) so the report lands inside the 4 s + 4 s window.
- For events that must survive until MM starts, rely on `/var/run/modemmanager/events.cache` +
  `ModemManager-wrapper` replay.
- Safe verification recipe for the *synthesis* itself: pick a disposable netdev (e.g. a mux you create on purpose
  with `qmicli --device-open-... --wds-create-mux/` or a dummy link if the driver is present), have a listener
  watching `/tmp/hpl.log`, `readlink -f /sys/class/net/<if>`, `ip -d link show <if>`, then
  `echo add > /sys/class/net/<if>/uevent` and confirm: one new `net … add <if>` line in `/tmp/hpl.log`, one
  `kernel event reported:` block in `/var/log/mm.log` (`mmcli -m any` unchanged), `logread` unchanged otherwise.
