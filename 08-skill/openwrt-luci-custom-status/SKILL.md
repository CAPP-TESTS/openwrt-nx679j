---
name: openwrt-luci-custom-status
description: Publish custom OpenWrt status into LuCI pages.
---

# Publishing custom status to LuCI (OpenWrt)

## When to Use

- A custom netifd proto, service, or package must expose rich status (registration state, operator, signal,
  access tech, SIM ids, bearer duration) to a LuCI page.
- You need to know where LuCI gets status data from, or why a page shows "Access denied".
- You are deciding between a ubus object, an rpcd plugin, and a dashboard include.

LuCI has exactly one supported status channel: **ubus methods of an object + an rpcd ACL role + a JS view
(and/or a dashboard include)**. Every stock status page works this way; there is no other hook.

## Decide the publish mechanism first

| Situation | Mechanism |
|---|---|
| No daemon of your own (usual case for a proto package) | **rpcd ucode plugin**: `root/usr/share/rpcd/ucode/<file>` |
| Shell-tool backend (`qmicli`, `mmcli`, `jsonfilter`) | **rpcd exec plugin**: `root/usr/libexec/rpcd/<name>` (`list`/`call` argv, JSON stdout, args on stdin) |
| You already run a daemon | `ubus_add_object()` from the daemon; LuCI package only needs the ACL entry |
| A few extra scalars on an existing netifd iface | Push via `proto_send_update`/`notify_proto` -> appears in `ubus call network.interface.<if> status` under `.data` |

rpcd registers ucode plugins as real ubus objects; script returns `{ '<object.name>': { method: { args: {...}, call: fn } } }`.
Paths from rpcd source: `/usr/share/rpcd/ucode` (ucode.c), `/usr/libexec/rpcd` (plugin.c).

## Minimal correct file set (one LuCI app)

```
Makefile                                            # DEPENDS: +luci-base +rpcd-mod-ucode
root/usr/share/rpcd/ucode/luci.<app>                # publishes ubus object
root/usr/share/rpcd/acl.d/luci-app-<app>.json       # role -> read.ubus['luci.<app>'] = ["getStatus"]
root/usr/share/luci/menu.d/luci-app-<app>.json      # {action:{type:view,path:...}, depends:{acl:[role]}}
htdocs/luci-static/resources/view/<app>/status.js   # -> /www/luci-static/resources/... rpc.declare + render
htdocs/luci-static/resources/view/status/include/95_<app>.js  # optional Overview section (auto-discovered)
```

Nothing else: no uhttpd change, no init script, no /etc/config edit for root users (`list read '*'` covers new roles).

## ACL schema (`/usr/share/rpcd/acl.d/<name>.json`)

```json
{ "<role>": { "read": { "ubus": { "luci.<app>": ["getStatus"], "network.interface": ["status","dump"] },
                          "uci": ["network"], "file": { "/tmp/x.json": ["read"] } }, "write": {} } }
```

- Top-level key = role name; files are merged; the file names themselves don't matter.
- Method names must match the plugin exactly; matching is `fnmatch` on object *and* function (globs OK, `*`/`*` = superuser).
- `uci` scope is per-config-file, not per-method. Roles bind to users in `/etc/config/rpcd` (`list read '<role>'`).
- Symptom of a wrong ACL is **"Access denied" in the browser**, not a 404.

## Dashboard include hook (cheapest UI surface)

`luci-mod-status/.../view/status/index.js` lists `/www/luci-static/resources/view/status/include` and
`L.require`s every `*.js` in sort order. Drop `NN_<app>.js` (a `baseclass.extend({title,load,render})`) there
and it appears on `admin/status/overview` with no menu entry. The user's role needs
`file: {"/www/luci-static/resources/view/status/include": ["list"]}` — core `luci-mod-status-index` grants it.

## Exec plugin protocol (`/usr/libexec/rpcd/<name>`, verified in rpcd plugin.c)

- Object name = **file name**; rpcd registers it (no daemon needed). Reference packages: `luci-app-squid`
  (39-line plugin + 20-line ACL + 12-line menu + 134-line view = smallest complete template),
  `luci-mod-battstatus/.../luci.battstatus` (48 lines, ACL only), `luci-app-pbr/.../luci.pbr` (arg passing),
  `packages/net/modemmanager/files/usr/libexec/rpcd/modemmanager` (Lua + `main(arg[1],arg[2])`).
- `list` -> ONE JSON object `{"<method>": {"<arg>": <type>}, ...}` (`{}` = no args). An arg value that is a
  JSON **string** declares a string arg; a JSON **number** declares an integer arg and its value picks the width
  (8/16/64 = int8/int16/int64, anything else int32).
- `call <method>` -> args arrive as ONE JSON object on **stdin**, no trailing newline; print ONE JSON object on
  stdout; stderr is dropped. No stdout -> `UBUS_STATUS_NO_DATA`, non-object stdout -> `INVALID_ARGUMENT`.
- `read -r input || input='{}'` **silently discards the payload** (read(1) returns 1 on EOF after assigning).
  Use `input=''; read -r input || true; [ -n "$input" ] || input='{}'`.
- ACL: only `read`/`write` sub-objects are honored; a **read grant also satisfies a write check**, so a read-only
  role must be granted via `list read` in `/etc/config/rpcd` only. uhttpd-mod-ubus asks rpcd `session.access`
  before dispatching, hence "Access denied" instead of 404.

## Pitfalls

- `service rpcd reload` may not pick up plugin changes on some builds — restart rpcd when in doubt.
- Declare `rpcd-mod-ucode` in DEPENDS for standalone app packages; `rpcd-mod-file` only if you use `fs.*`.
- Gate the menu entry with `depends.fs`/`depends.uci` so the page hides when the backend tool is absent.
- Keep `luci-*` dependencies out of the low-level proto package; ship the UI separately (`luci-proto-*`, `luci-app-*`).
- Stock LuCI renders only known fields of `network.interface` status; arbitrary `.data` keys need your own view.
- Custom ACL files baked into an image must be added to `/etc/sysupgrade.conf` or they vanish on upgrade.
- Lua-era hooks (`/usr/lib/lua/luci/view/status/include/*.lua`) are deprecated; use JS view + menu.d + rpcd ucode.

## Verify on the device

```sh
ubus -v list luci.<app>; ubus call luci.<app> getStatus
service rpcd reload
ubus call session access '{"ubus_rpc_session":"<sid>","object":"luci.<app>","function":"getStatus"}'
logread -e rpcd
```

Reference precedents in the LuCI tree: `luci-app-lldpd` (ucode `luci.lldpd` + status page),
`luci-app-ddns` (ucode + `70_ddns.js` include), `luci-app-https-dns-proxy`/`luci-app-pbr` (exec plugins),
`luci-proto-wireguard` (protocol package with its own object + page), `luci-app-mwan3` (daemon-owned object).
