# Distinguishing Qualcomm inter-RIL IPC from QMUXD

## Identity and ABI gates

- Identify a Unix socket by its literal pathname in source, its constructor/bind/connect call chain, and its message consumers; a parent directory such as `qmux_radio` is not protocol evidence.
- Treat public vendor-source mirrors as source evidence for the pinned revision, not proof that a device's proprietary binaries match it. Record the repository, full commit, file and line ranges.
- Follow both transmit and receive paths before describing framing; QtiBus client-to-server and server-to-client envelopes are different and nested.
- Preserve source types in any provisional frame diagram. Direct `write(&value, sizeof(value))` serializes native representations; determine the actual process ABI before assigning byte widths, endianness or offsets. An arm64 SoC does not prove every radio process is 64-bit.
- Treat strings and linked QMI symbols as capability clues, not evidence that a particular socket or device node carries that protocol. Inspect compile-time alternatives and live ownership separately.
- Do not probe an unknown radio socket by sending QMI or substituting it for a QMUXD endpoint. Establish ownership, implementation identity and framing first; malformed frames can affect a working baseline.

## Verified public-source anchors

The following are third-party mirrors of Qualcomm-attributed source, not an official Nubia/SM8450 release. Pin: `OrphyWang/Qcom-CAMX-CHI` commit `36fc163a534963a5b3af52186af5efcc63401ad2`.

Base: https://github.com/OrphyWang/Qcom-CAMX-CHI/tree/36fc163a534963a5b3af52186af5efcc63401ad2

- `qcril-nr/qcril-common/qtibus/src/QtiBusSocketTransport.h`, lines 15–28: Android `IPC_SOCKET_NAME` is `/dev/socket/qmux_radio/ril_ipc`; transport commands are `NEW_CLIENT`, `NEW_MESSAGE`, `CLIENT_DEAD`.
- `qcril-nr/qcril_qmi/qcril.cc`, lines 1496–1515: `qcril_init()` starts `QtiBusTransportServer` only in the primary RIL instance when multi-SIM is supported, and starts `Messenger` for the instance.
- `qcril-nr/qcril-common/qtibus/src/Messenger.cpp`, lines 208–267 and 272–317: registration and delivery are length-prefixed messages keyed by the message-name string, not a QMI service/client address.
- `qcril-nr/qcril-common/qtibus/src/QtiBusSocketTransportServer.cpp`, lines 109–165, 168–220, 226–272 and 322–338: native `size_t` total length; Unix stream listener; server envelopes include `CommandId`, peer `pid_t`, and optional payload length/data; it forwards the complete client frame to other clients.
- `qcril-nr/modules/dms/src/DmsModule.cpp`, lines 84–95 and 119–127: registers/deserializes `IpcRadioPowerStateMesage` (spelling intentional) and handles cross-RILD radio-power propagation.
- `qmi/platform/qmi_platform_qmux_if.h`, lines 22–29 and 53–77: legacy QMUXD endpoint is `/dev/socket/qmux_radio/qmux_connect_socket`, with platform header `{ int total_msg_size; int qmux_client_id; }`.
- `qmi/platform/linux_qmi_qmux_if_client.c`, lines 840–859, 892–972 and 1178–1228: QMUXD connection, initial client-ID read, platform-header insertion and send.
- `qmi/src/qmi_i.h`, lines 35–36 and 140–151: QMUX client ID is `int32_t`; `qmi_qmux_if_msg_hdr_type` includes transaction, connection, service and client IDs.
- `qmi/src/qmi_qmux_if.c`, lines 1931–2032: `qmi_qmux_if_send_raw_qmi_cntl_msg()` is a distinct QMI path and has direct versus `QMI_MSGLIB_MULTI_PD` transport alternatives. Its symbol alone does not prove a running qmuxd.

## Source retrieval procedure

- Resolve the GitHub branch to a full commit before retrieving raw source.
- Traverse only the relevant component tree (`qcril-hal`, `qcril-nr`, `qmi`) to avoid recursive repository-tree truncation.
- If web extraction cannot fetch GitHub source, use the public raw URL or GitHub API rather than repeating failed directory fetches.
- Reduce large API trees within the retrieval script before printing. Terminal output truncation can turn valid JSON into unparseable fragments; retrieve complete JSON into memory and print selected paths.
