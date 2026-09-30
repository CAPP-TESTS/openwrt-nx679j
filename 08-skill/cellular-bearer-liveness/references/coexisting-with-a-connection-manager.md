# Co-existing with a connection manager on the same modem port

For any stack where you own a data session on an embedded/multiplexed QMI port and a distribution's connection manager (ModemManager, uqmid, wwand, a vendor netmgr/ipacm stack) is also installed.

The question this file answers: **can the manager stay installed "passively" while I keep my session?** Not by configuration — and the answer is checkable instead of arguable.

## The manager acts before any connect

These happen at *modem object* creation, before any Enable/connect, so disabling your UCI interface, the monitor service, a `connection.d` dispatcher, or adding filter rules does **not** prevent them:

- the control port is opened `O_EXCL`, and the manager keeps its own service clients (DMS/NAS/UIM/WDS) for the object's lifetime;
- a **data-format write** during port open — on an embedded/IPA setup that is MUX_RMNET + QMAPV5 with aggregation and endpoint info whenever the current combination differs;
- a **data-interface reset against the master data netdev** (the same netdev your mux links hang off);
- modem-side autoconnect explicitly disabled as the first enabling step (a writes to the modem's own profile state);
- kernel link create/delete for the mux netdevs it believes it owns.

There is no read-only mode to opt into. Treat "keep it for its status object" as a data-path decision, not a packaging one.

## Verify on the device instead of reasoning about intent

Grep the manager's log for the reset and data-format steps, and read **your** netdev name in the target field:

```sh
grep -c -e 'set data format' -e 'reset with data interface' <manager-log>
```

A line naming your netdev means it is already touching your data path, whatever the configuration says. Run this before accepting any co-existence plan, and again after upgrading the manager.

## Survival is often accidental — do not bank on it

A session can ride through those attempts because the manager's own path aborts early. Measured example: the reset step resolved a QRTR control port as a filesystem path and died before reaching the kernel (`Couldn't query file info: "/dev/<node>": No such file or directory`), so 7 reset attempts in one boot changed nothing. That is the manager's bug shielding you: it is invisible, unversioned, and can disappear in any release. Never cite it as a compatibility argument.

## Status from the manager is not a contract

If the manager's status object is what the UI consumes, measure its availability across boots rather than reading the code: identical builds produced one boot where the object appeared after ~179 s and another where it never appeared at all while the daemon thrashed through 122+ internal cleanups. A page built on it is sometimes simply empty.

## Decision rule

- **Status data is the goal** — read it with a client that only reads, from a port the manager does not hold (e.g. `qmicli -d qrtr://0 --nas-get-serving-system`), and remove the manager.
- **Manager must stay** — budget for your data path being reset/rewritten, give it a disjoint mux window if the hardware allows, and re-run the grep check above as part of every upgrade.
- **Manager detection is late or flaky on QRTR** — its QRTR node watcher waits a fixed window for the required services (WDS+NAS+DMS) and then drops the node with **no retry**; a manual rescan re-runs only the udev path (and is compiled out of `-Dudev=false` builds), so a **daemon restart** is the only reliable re-trigger. Budget a boot-time restart instead of waiting, and expect the modem object to be absent on some boots.
