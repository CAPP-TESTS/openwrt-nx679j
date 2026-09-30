---
name: modemmanager-upstream-research
description: Investigate ModemManager behavior upstream (QRTR, hotplug).
version: 1.0.0
author: Hermes (kernel-re)
license: MIT
metadata:
  hermes:
    tags: [modemmanager, qrtr, qmi, openwrt, upstream-research]
    related_skills: [nx679j-openwrt, openwrt-feature-state-verification]
---

# ModemManager upstream research

## When to Use

Use when a device-side bug needs to be matched against upstream ModemManager issues/PRs (detection timing, QRTR, hotplug races) before touching kernel or userspace code.

ModemManager lives on freedesktop GitLab (NOT GitHub; the GitHub repo is a read-only mirror that accepts no issues).

## Query the tracker without a token

```bash
P="mobile-broadband%2FModemManager"
# issues / merge requests (full-text search, all states)
curl -sS "https://gitlab.freedesktop.org/api/v4/projects/$P/issues?search=qrtr&state=all&per_page=100" \
  | jq -r '.[] | "#\(.iid)\t[\(.state)]\t\(.updated_at[:10])\t\(.title)"'
curl -sS "https://gitlab.freedesktop.org/api/v4/projects/$P/merge_requests?search=qrtr&state=all" \
  | jq -r '.[] | "!\(.iid)\t[\(.state)]\t\(.updated_at[:10])\t\(.title)"'
# a single MR with its diff (best place to find the real fix + commit message)
curl -sS "https://gitlab.freedesktop.org/api/v4/projects/$P/merge_requests/1216/changes"
# commits touching one file
curl -sS "https://gitlab.freedesktop.org/api/v4/projects/$P/repository/commits?path=src/mm-qrtr-bus-watcher.c"
# which release shipped a commit
curl -sS "https://gitlab.freedesktop.org/api/v4/projects/$P/repository/commits/<sha>/refs?type=tag"
```

- `.../merge_requests/<id>/notes` and issue notes need auth (401 Unauthorized without a token). Read the public MR web page HTML instead when discussion matters.
- The authoritative changelog is `NEWS` in the repo root; grep it per release for the affected component (e.g. `qrtr-bus-watcher:`).
- Mirror for shallow clones: `git clone --depth 60 https://github.com/linux-mobile-broadband/ModemManager.git`.

## QRTR detection facts (verify against source, they drift)

- QRTR nodes are a **synthetic subsystem**: `src/kerneldevice/mm-kernel-device-qrtr.c` builds the MMKernelDevice from a `QrtrNode`, and `mm-filter.c` states qrtr devices "don't have a sysfs path". Consequence: **no udev, no uevent, no udev tag, and no `--report-kernel-event`** can create or drive a QRTR modem object, even though the qcom-soc plugin lists `qrtr` as an allowed subsystem. Detection comes only from `src/mm-qrtr-bus-watcher.c` riding libqrtr-glib's `QrtrBus` netlink `node-added` signal.
- `handle_qrtr_node_added()` waits `qrtr_node_wait_for_services(node, {WDS,NAS,DMS}, <ms>)`. It is a **one-shot** wait started when the node is first announced; on timeout MM logs "qrtr node %u doesn't have required services to be considered a control node" and drops the node with **no retry**. libqrtr-glib emits node-added only when the node object is created (its first service), so late-arriving control services never re-trigger detection. Only a daemon restart (or the node disappearing and being recreated) re-runs it.
- `ScanDevices` (`mmcli --scan-modems`) calls `mm_base_manager_start(manual_scan=TRUE)`, which re-runs only the **udev** scan (`process_scan`) and never touches the QRTR watcher. It is compiled out entirely (`MM_CORE_ERROR_UNSUPPORTED`) in `-Dudev=false` builds, which is how OpenWrt builds ModemManager.
- Log strings worth grepping in a boot log: `[qrtr] created new node N`, `qrtr node N added`, `waiting for modem services on node N`, `qrtr services ready for node N`, `doesn't have required services to be considered a control node`. Compare the node-added timestamp against modem object creation to tell "MM dropped a late node" from "the node itself appeared late".
- Known upstream fix for late modems: MR !1216 / commit 7de52c0b raised that wait 1000 ms -> 4000 ms, shipped in 1.24.0 ("qrtr-bus-watcher: Increase wait time after probing to more reliably detect modems"). Still hardcoded in master; no config knob exists (only `--test-no-qrtr`, a test flag).

## QMI client transports: uqmi vs qmicli vs mmcli

When asked whether a shell/LuCI QMI tool can use QRTR, check the *client*, not just the kernel:

- **uqmi** (OpenWrt's own tiny client; OpenWrt `package/network/utils/uqmi` builds git HEAD, no patches) has **zero QRTR support**: it opens the device with `open(path, O_RDWR|O_EXCL|O_NONBLOCK|O_NOCTTY)` and writes raw QMI frames (`uqmi/dev.c:qmi_device_open`). Only a QMI *character device* works — `/dev/cdc-wdmX` (qmi_wwan), `/dev/wwanXqmiY` (WWAN framework, e.g. mhi_wwan_ctrl), `/dev/wwanXmbimY` with `-m`. `-d qrtr://0` fails with `Failed to open device` (exit 2). `-s` is *single-line output*, not "single transaction".
- **qmicli** (libqmi) does support QRTR: `-d, --device=[PATH|URI] ... e.g. qrtr://0` appears in `--help` only when built `-Dqrtr=true` (needs libqrtr-glib). On OpenWrt that is the `LIBQMI_WITH_QRTR_GLIB` config of package `libqmi`/`qmi-utils`. Verify with `qmicli --help | grep -i qrtr`. qmicli has no JSON output mode.
- **mmcli** (`-J`) is the only JSON-emitting option and is QRTR-capable, but rides ModemManager's QRTR detection (see above).

Check the wrapper's own guards before blaming the binary: modemdata's `params_qmi.sh` starts with `[ -z "$DEVICE" ] || [ ! -e "$DEVICE" ]` → `{"error":"Device not found"}`, so any non-path URI (including `qrtr://0`) is rejected before uqmi is even called, and `pidof uqmi` makes concurrent polling return `{"error":"Device is busy"}`.

## Reporting rule

Distinguish the three trigger options explicitly: uevent/udev (not applicable to QRTR), manual rescan (`ScanDevices`: udev-only, cannot recover a dropped QRTR node), daemon restart (the only reliable trigger). Never claim a workaround exists in-tree without showing the code path.
