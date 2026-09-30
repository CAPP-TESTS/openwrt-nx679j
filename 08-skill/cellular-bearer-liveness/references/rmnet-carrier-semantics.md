# What `carrier` means for an rmnet netdev (and why it lies in both directions)

Verified against torvalds/linux master and v5.10, and netifd master (2026-09). Line numbers move; function names are the citation.

## 1. carrier is the lower-layer bit, not link health

`netif_carrier_ok(dev)` == `!test_bit(__LINK_STATE_NOCARRIER, &dev->state)` (include/linux/netdevice.h). Docs are explicit:

- `Documentation/networking/operstates.rst` (3. Kernel driver API): "__LINK_STATE_NOCARRIER, maps to !IFF_LOWER_UP: The driver uses netif_carrier_on() to clear and netif_carrier_off() to set this flag ... The name 'carrier' and the inversion are historical, think of it as lower layer."
- include/linux/netdevice.h, next to the `netif_carrier_on/off` declarations: "The name carrier is inappropriate, these functions should really be called netif_lowerlayer_*() because they represent the state of any kind of lower layer not just hardware media."

**A driver must maintain the bit. Nobody else does.** A soft device whose driver never calls the pair keeps whatever it was allocated with, forever.

Default at alloc (operstates.rst 3): "On device allocation, both flags __LINK_STATE_NOCARRIER and __LINK_STATE_DORMANT are cleared" — i.e. carrier reads **1** on a fresh netdev, and `ip link` shows LOWER_UP as soon as the device is admin-up.

## 2. Three surfaces, one bit — and netifd uses none of them directly

| Surface | Source | Notes |
|---|---|---|
| `/sys/class/net/<if>/carrier` | net/core/net-sysfs.c `carrier_show()` | returns `!!netif_carrier_ok()` **only if `netif_running()`**, else `-EINVAL`; calls `linkwatch_sync_dev()` first |
| `ip link` flags / `ip -d` | net/core/rtnetlink.c `rtnl_fill_ifinfo()`: `ifm->ifi_flags = dev_get_flags(dev)` (v5.10) / `netif_get_flags(dev)` (mainline) | v5.10 net/core/dev.c `dev_get_flags()`: `if (netif_running(dev)) { ... if (netif_carrier_ok(dev)) flags |= IFF_LOWER_UP; }` — note the gate |
| `ethtool` | net/ethtool/common.c `__ethtool_get_link()` = `if (!ops->get_link) return -EOPNOTSUPP; return netif_running(dev) && ops->get_link(dev);`; ioctl `ETHTOOL_GLINK` in net/ethtool/ioctl.c. Driver default: `ethtool_op_get_link()` = `netif_carrier_ok(dev) ? 1 : 0`. Doc (include/linux/ethtool.h): "@get_link: Report whether physical link is up. Will only be called if the netdev is up." |
| **netifd** | netlink only: system-linux.c `cb_rtnl_event()` -> `system_device_update_state()` -> `device_set_link(dev, flags & IFF_LOWER_UP ? true : false)` | netifd **never reads sysfs or runs ETHTOOL_GLINK** for carrier, and ignores IFLA_CARRIER / IFLA_OPERSTATE. `device_set_link()` (device.c) logs "Network device 'X' link is up/down" |

Exposed as `ubus call network.device status '{"name":"<dev>"}'` -> `"carrier": !!dev->link_active` (device.c `device_dump_status()`); the device event blob calls the same field `link_active`.

## 3. Why operstate is 'unknown'

`dev->operstate` is zero-initialised = `IF_OPER_UNKNOWN` and only moves in net/core/link_watch.c `rfc2863_policy()` -> `default_operstate()`, called from `linkwatch_do_dev()` (linkwatch workqueue) and from `linkwatch_init_dev()` at registration. Events are fired only when NOCARRIER/DORMANT/TESTING **change** (`netif_carrier_on/off`, `netif_dormant_on/off`, `netif_testing_on/off`), and `linkwatch_init_dev()` is itself guarded:

```c
if (!netif_carrier_ok(dev) || netif_dormant(dev) || netif_testing(dev))
        rfc2863_policy(dev);
```

So a device allocated with carrier-OK that never toggles it **never leaves IF_OPER_UNKNOWN** — the signature of every soft device (lo, dummy, rmnet). `netif_oper_up()` still counts UNKNOWN as operational (`/* backward compat */`), so IFF_RUNNING is set and `ip link` prints `UP,LOWER_UP state UNKNOWN`.

Related traps:
- operstates.rst IF_OPER_UNKNOWN: "neither driver nor userspace has set operational state ... setting operational state has not been implemented in every driver".
- `operstate_show()` (net-sysfs.c) forces `IF_OPER_DOWN` when `!netif_running()` — so sysfs says "down" while `ip link` says "unknown" for the same admin-down device.
- Stacked devices (ifindex != iflink, e.g. rmnet mux links) report `IF_OPER_LOWERLAYERDOWN` when the lower's carrier is off.

Measured on a 7.x host: `lo` -> carrier=1, operstate=unknown, `<UP,LOWER_UP state UNKNOWN>`; unplugged NIC -> `<NO-CARRIER,...,UP> state DOWN`, carrier=0; admin-down wireguard -> reading carrier gives **EINVAL**, operstate=down.

## 4. Why a live bearer reports carrier absent

1. **The bit is not maintained for rmnet at all.** `drivers/net/ethernet/qualcomm/rmnet/{rmnet_vnd,rmnet_config,rmnet_handlers}.c` contain zero `netif_carrier_*` calls, and `rmnet_vnd_ops` has no `ndo_change_carrier`; `rmnet_ethtool_ops` has no `get_link`/`get_link_ksettings` (stats/coalesce only). Confirmed identically on CodeLinaro msm-5.10 `kernel.lnx.5.10.r1-rel`. Consequence: no signal path exists from the QMI WDS session to the netdev, and userspace cannot correct it — `ip link set dev <rmnet> carrier on` and writing the sysfs `carrier` both need `ndo_change_carrier` (net-sysfs `carrier_store()` -> -EOPNOTSUPP; rtnl setlink IFLA_CARRIER -> `dev_change_carrier()`), so a stale NOCARRIER is permanent until the device is reallocated.
2. **netifd's carrier is admin-up-gated.** With IFF_LOWER_UP only present while `netif_running()`, "carrier: false" for a soft device usually means "this netdev is not admin-up" (or netifd never saw an up event for it) — which is orthogonal to the bearer, because a WDS session survives host-side link down; only an explicit WDS Stop / CTL Release CID ends it.
3. **netifd deliberately ignores carrier** when keeping an interface up: `interface_check_state()` uses `iface->link_state || interface_force_link(iface) || iface->carrier_loss_timer.pending`, carrier loss only tears down after `carrier_loss_delay` (interface.c), and the interface's link state follows `device_link_active(main_dev)` — the *main* device, not necessarily the L3 device carrying the traffic.
4. netifd learns link only from RTM_NEWLINK/RTM_DELLINK for AF_UNSPEC and only for devices already in its table; it re-syncs on add/config with `system_if_check()` (RTM_GETLINK round-trip via `device_check_state()`).

Therefore carrier and bearer liveness are **decoupled by construction, in both directions**: dead bearer + carrier present (false green) and live bearer + carrier absent (false red). Never use either as the liveness signal — use reachability plus a modem-side packet-service-status query.

## 5. Reading them apart on a target

```sh
ip -d link show dev <dev>            # NO-CARRIER flag vs 'state'
cat /sys/class/net/<dev>/carrier     # EINVAL => not admin-up
ethtool <dev>                        # EOPNOTSUPP on rmnet (no get_link)
ubus call network.device status '{"name":"<dev>"}'   # carrier == IFF_LOWER_UP
logread | grep "link is up\|link is down"            # netifd's own view
```
