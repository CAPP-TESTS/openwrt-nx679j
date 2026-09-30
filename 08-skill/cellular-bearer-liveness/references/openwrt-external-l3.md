# Exposing an externally-owned L3 to netifd / LuCI (read-only)

For the layout where a userspace client (a bring-up script, a QMI helper, a vendor daemon) owns the modem's address and default route, and you still want the interface to appear — and be truthful — in OpenWrt's `ubus`/LuCI.

The goal is a protocol that **reports** the kernel's L3 and never writes it.

## The two pieces

1. **A netifd shell protocol** in `/lib/netifd/proto/<name>.sh`, registered with `add_protocol <name>`, that reads the kernel and reports:

   ```sh
   proto_init_update "$ifname" 1 1     # arg2 = link-up, arg3 = address-external
   proto_add_ipv4_address "$addr" "$mask"
   proto_send_update "$cfg"
   ```

   `address-external` makes netifd record the address in status **without** calling `system_add_address()` / `system_del_address()`. Never call `ip addr`/`ip route` from this protocol — that is what "read-only" means here, and it is worth asserting in the build.

2. **A LuCI frontend asset**, `/www/luci-static/resources/protocol/<name>.js`:

   ```js
   'use strict';
   'require network';
   return network.registerProtocol('<name>', {
       getI18n: function() { return _('My externally-managed link'); }
   });
   ```

   A minimal handler carrying only `getI18n` is enough for the generic interface status to render (protocol name, carrier, uptime, IPv4) — there is no `renderStatus` hook to implement.

## The acceptance test for the frontend: two conditions, not one

LuCI calls `network.get_proto_handlers` and then loads `protocol/<p>.js` **for every protocol the backend advertises**. Therefore:

- the image must contain the JS asset, **and**
- netifd must actually advertise the protocol name.

Verify both before believing a change worked:

```sh
ubus call network get_proto_handlers | grep -i '<name>'   # must be present
ls -l /www/luci-static/resources/protocol/<name>.js      # must exist
```

If the asset is missing, LuCI shows **"Unsupported protocol type."** plus an "Install protocol extensions…" link, and disables the Edit button. If the backend does not advertise the name, the asset alone will not be loaded.

## What this protocol cannot do

- **It cannot distinguish a live bearer from stale L3.** It reports the kernel, so a dead session with a leftover address/route still renders as connected. Liveness needs a reachability check (see the parent skill).
- **`address-external` covers addresses only — there is no route-external.** Verified in netifd source: the flag is passed solely into the address lists (`proto_apply_ip_settings(iface, data, ext)` → `parse_address_list(..., ext)` → `addr->flags |= DEVADDR_EXTERNAL`); routes arrive on a separate path that never receives it. Consequences: an external address is never added or deleted by netifd, while **any route you report becomes netifd's** — deleted at `ifdown` and on config reload / l3-device swap, and silently adopted (`NLM_F_REPLACE`) if your script created it first. So report the address and **no route, no gateway**. The only escape that keeps netifd's hands off a route is the interface's default-route option set to `0`, which suppresses the `/0` route entirely (never added, never deleted) at the cost of it also vanishing from `ifstatus` (it shows under the `inactive` block) — treat that as an accident, not a design.
- **The external bit is part of the node key.** A later notify that omits `external` (or one where you forget to re-publish the whole set after `proto_init_update` resets the accumulators) creates a *different* node: the old one is dropped and the new one is netifd-owned. Keep the flag and the full address set on every notify.
- **With the default route disabled, the protocol doubles as a safe refresh.** Measured: `ifup <iface>` did **not** remove the kernel address or default route and ping survived (+4 s and +10 s), while the manager's view was re-read. Re-verify on a new stack rather than assuming it.
- **Carrier is not a connectivity indicator** for virtual/rmnet-style devices: it can report `Present` independently of whether the packet session exists.

## Avoiding the destructive alternatives

Do **not** configure the interface with an owning protocol (`static`) when a script owns the L3: netifd becomes the L3 owner and removes the address/routes it believes it manages. An unmanaged protocol (`none`) is also wrong — netifd still claims the device and flushes protocol L3 state at bring-up, observed as the address and default route vanishing a few seconds after they appear while the netdev stays `UP`. The external protocol above is the shape that neither takes ownership nor gets flushed.
