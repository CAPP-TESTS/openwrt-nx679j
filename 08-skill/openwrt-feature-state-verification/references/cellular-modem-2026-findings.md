# OpenWrt cellular/on-SoC modem landscape (verified 2026-09-20, master @ 2026-09-19)

Baseline facts worth re-checking before reuse (each is a fetchable URL):

## Packages (packages feed)
- `libqrtr-glib` 1.2.2-3 (added 2021-02-24), `libqmi` 1.38.0 master / 1.36.0 in 25.12 / 1.34.0 in 24.10, built with `-Dqrtr=$(CONFIG_LIBQMI_WITH_QRTR_GLIB)`, option default y (libs/libqmi/Config.in).
- `modemmanager` 1.24.0 + `modemmanager-rpcd`; Config.in has `MODEMMANAGER_WITH_QRTR` default y (present in branch 22.03+, absent in 21.02) and `select LIBQMI_WITH_QRTR_GLIB`.
- Plugins are separate packages `modemmanager-plugin-<name>`; `qcom-soc` plugin is built (`BuildPlugin,qcom-soc,@MODEMMANAGER_WITH_QMI`).
- `wwand` submitted as PR openwrt/packages#30185 (open since 2026-08-07).

## Kernel modules (openwrt/openwrt)
- `KernelPackage/rmnet` in package/kernel/linux/modules/netdevices.mk (commit c3251f5d52, 2024-10-09, Robert Marko) -> kmod-rmnet in 24.10/25.12/master.
- `KernelPackage/qrtr|qrtr-tun|qrtr-smd|qrtr-mhi` in netsupport.mk (QRTR first packaged 2021-12-21, commit 5968290a). `qrtr-smd` is `@TARGET_qualcommax` only; no qrtr-glink transport.
- No IPA kmod anywhere; `target/linux/qualcommax|qualcommbe/config-*` carry `# CONFIG_QCOM_IPA is not set` (qualcommbe: disabled 2025-04-10, commit 779f730, because it conflicts with the mac80211 QMI-helpers backport).
- No BAM-DMUX / qmi_rmnet kmod in the tree.

## Protos (main tree)
- `qmi` (uqmi): control device is a char device — `$devpath/usbmisc/cdc-wdm*` or `$devpath/*/wwan/wwan0/wwan0qmi0` (MHI/PCIe); device existence checked with `[ -c ... ]`. No QRTR (socket, not chardev).
- `mbim` (umbim, 2025.10.04) gained "wwan device class" support; `ncm` (comgt-ncm); `3g`; `modemmanager`.
- `modemmanager` is built `-Dudev=false -Dudevdir=/lib/udev`.
- Location trap: `/lib/netifd/proto/modemmanager.sh` is NOT in openwrt/openwrt — it ships from the **packages** feed at `net/modemmanager/files/lib/netifd/proto/modemmanager.sh`, installed only when `CONFIG_MODEMMANAGER_WITH_NETIFD=y` (Makefile:158-162). Same package also ships `/usr/share/ModemManager/modemmanager.common`, `/etc/hotplug.d/{net,tty,wwan}/25-modemmanager-*` and the ModemManager connection dispatcher `usr/lib/ModemManager/connection.d/10-report-down` (the actual auto-reconnect path: on `disconnected` it re-ups the UCI interface via ubus). `/lib/netifd/netifd-proto.sh` comes from netifd.git `scripts/netifd-proto.sh`; `/lib/netifd/ppp.sh` from `package/network/services/ppp/files/ppp.sh`.
- luci protos: 3g, mbim, ncm, qmi, modemmanager (nothing for qrtr/on-SoC).

## Upstream (libqmi / ModemManager / kernel)
- libqmi: QRTR backend introduced 1.26.0 (`--enable-qrtr`, qmi_device_new_from_node); libqrtr-glib split out at 1.30.0; qmicli accepts `qrtr://0` device URIs; `--link-list/--link-add/--link-delete` (rmnet over netlink); DPM service "required for IPA based Qualcomm SoCs"; WDA data-format endpoint types incl. `hsic`, `bam-dmux`, `embedded`, `pcie`.
- ModemManager 1.18.0: qcom-soc plugin supports QRTR+IPA setups and the WWAN subsystem; 1.24.0: qrtr-bus-watcher tuning. Plugin matches `SUBSYSTEM=="net", DRIVERS=="ipa"` and `DRIVERS=="bam-dmux"`, `wwan/rpmsg qcom-q6v5-mss`; data path picks endpoint via `net_port_driver == "ipa"` (QMI WDA) and creates rmnet links through libqmi (mm-port-qmi.c `get_rmnet_device_add_link_flags`).
- Kernel: QRTR multi-endpoint (QRTR_BIND_ENDPOINT, LWN 1030775) is still a patch series (v7 00/15), NOT in v6.18. QRTR ns limit raises + HELLO-on-endpoint-register landed 2026 (net/qrtr). SM8450 IPA (IPA v5.1) still not upstream: `drivers/net/ipa/data/ipa_data-v5.1.c` absent in master; v2 series 2026-09-09.
- rmnet caveat over USB: stable commit 662dc80a5e86 (v6.12.74) broke MAP aggregation (wwan0 capped ~1500B) — LKML thread 2026-02.

## FUjr/QModem (channel-manager feed, checked 2026-09-24 @ c49654e, v3.4.0_rc3)
- **QModem has no QMI userspace stack.** `grep -ri uqmi` = 0 hits repo-wide; no `libqmi`/`qmicli`/ModemManager dep in any Makefile. `application/qmodem/Makefile` DEPENDS is `ubus-at-daemon +tom_modem +sms-tool_q +modem_scan +qmodem-settings`, plus QMI/MHI *kernel driver* choices only.
- Control plane = serial AT. `tom_modem/src/modem_types.h` has exactly `TRANSPORT_TTY`/`TRANSPORT_UBUS`; one sender `at()` in `qmodem/files/usr/share/qmodem/modem_util.sh` (`tom_modem -d $at_port -o a -c "$atcmd"`). All vendor `base_info`/`cell_info`/`get_stats` are AT (`AT+CGMM`, `AT+QGDNRCNT`, ...). `get_tty_ports` only enumerates `ttyUSB*`/`ttyACM*`/`mhi_*`/`wwan*at`.
- Data plane = bundled Quectel CM only: `modem_dial.sh` `qmi_dial()` execs `quectel-CM` / `quectel-CM-M`; transport is `/dev/cdc-wdmN` (or `/dev/qcqmi` GobiNet) via `device.c:qmidevice_detect()`. No libqmi — QMI encoded in `QCQMUX.c`/`QMIThread.c`, links only `-ljson-c`.
- **QRTR is source-present but compiled out**: `quectel_CM_5G_M/src/Makefile` has `#QL_CM_SRC+=qrtr.c rmnetctl.c` and `release: clean qmi-proxy mbim-proxy atc-proxy #qrtr-proxy`; `QMIThread.h` has `//#define CONFIG_QRTR`; CMake option defaults `USE_QRTR OFF`. So `main.c`'s `qrtr` channel / `SOFTWARE_QRTR -> qrtr_qmidev_ops` fallback (and `device.c` `qmichannel="qrtr-3"`) are unreachable — a QRTR-only modem fails with `qmidevice_detect failed` and `qmi_dial()` retries forever in `while true`.
- rpcd object `qmodem` (`files/usr/libexec/rpcd/qmodem`, 44 methods keyed on `config_section`) returns only AT/host-derived JSON; `dial_status` is just `/etc/init.d/qmodem_network modem_status` liveness. Docs: `docs/qmodem-rpcd-interface.md`.
