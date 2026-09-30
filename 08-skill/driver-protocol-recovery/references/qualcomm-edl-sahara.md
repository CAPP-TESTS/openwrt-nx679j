# Qualcomm EDL / Sahara: what actually blocks clients

Hard-won rules for talking to a Qualcomm `05c6:9008` boot ROM. Each rule exists
because a stock tool produced a *plausible but false* conclusion without it.

## 1. Clear the endpoint halt before the first write

An aborted session leaves the bulk OUT endpoint stalled. Afterwards every write
returns `EIO` (errno 5) and the handle then dies with `ENODEV` (errno 19), while
the device stays enumerated — which reads like "the loader was rejected".

```python
dev.ctrl_transfer(0x02, 0x01, 0x00, ep_addr, None, timeout=2000)
# bmRequestType 0x02 = endpoint recipient, standard, host->device
# bRequest      0x01 = CLEAR_FEATURE, wValue 0x00 = ENDPOINT_HALT
# do it for both the OUT (0x01) and IN (0x81) endpoint
```

Verify the channel works with `SAHARA_RESET_REQ` (`07 00 00 00 08 00 00 00`):
a healthy link answers `08 00 00 00 08 00 00 00` (`RESET_RSP`). The device may
re-enumerate afterwards; the tty/device number can change.

## 2. `SAHARA_END_IMAGE` with a non-zero status is not a stop condition

Flow: `HELLO_REQ(0x01)` -> `HELLO_RSP(0x02)` -> `READ_DATA(0x03)` /
`READ_DATA64(0x12)` -> data -> `END_IMAGE(0x04)`, then the **host must send
`DONE_REQ(0x05)`** and the device answers `DONE_RSP(0x06)` before control jumps
to the loaded image.

When `END_IMAGE` carries a non-zero status (1, 33, 34, 41, 48 observed), the
code is still placed in SRAM: the status reports the *verification* result. The
device repeats the packet until the host answers `DONE_REQ`. Clients that treat
the status as fatal never reach Firehose at all.

Consequence for debugging: a device that keeps re-sending a 16-byte packet
`04 00 00 00 | 10 00 00 00 | <image_id> | <status>` is waiting for `DONE_REQ`,
not dead and not "stuck".

## 3. Verify your client before trusting its verdict

Known traps in widely used clients:

- `edlclient` `firehose.getstatus()` returns `True` when the XML reply has no
  `value` attribute, so it prints "succeeded" without any device answer.
- `edlclient` upstream switches to a separate "streaming" backend as soon as
  `HELLO_REQ` reports mode 2 (`MEMORY_DEBUG`), and then never completes the
  classic image transfer.
- `qdl` / `qdl-rs` abort on any non-success `END_IMAGE` status.

Measure with your own reader: open the endpoints, print every packet's command,
length and raw payload. Never let a client's summary line be the evidence.

## 4. Kernel driver binding blocks raw access

`qcserial` claims `05c6:9008` and libusb then fails with `Resource busy`.
Unbinding needs root (`/sys/bus/usb/drivers/qcserial/unbind`). Without root the
tty path works as an alternative transport: `/dev/ttyUSB*` is group `uucp` and
speaks the same bytes (the number changes after re-enumeration).

## 5. Images are not where the driver lives - check the config

Extract the embedded config to learn built-in vs module (`IKCFG_ST` marker, then
the gzip member up to `IKCFG_ED`):

```python
d = open('Image','rb').read()
i = d.find(b'IKCFG_ST'); j = d.find(b'\x1f\x8b', i); k = d.find(b'IKCFG_ED', i)
cfg = gzip.decompress(d[j:k])
```

On GKI Android devices the vendor drivers (`ufs_qcom`, `dwc3_msm`, the UFS and
USB PHYs) are modules built out-of-tree, so the kernel config can legitimately
show `# CONFIG_SCSI_UFS_QCOM is not set` while the running system loads
`ufs_qcom.ko`. Always confirm with `/proc/modules` on the live device.

## 6. Compare module vermagic against a module the device really loads

Vermagic equality matters, but do not assume which build is "right": pull a
module the running system actually has (`/vendor/lib/modules/<name>.ko`) and
read its `vermagic` string. On Android, `/proc/modules` order is load order, so
it is also a dependency-correct insmod order for a custom initramfs.

## 7. Persistent evidence on a device without bootloader logs

Qualcomm phones keep XBL/ABL logs in `rawdump` / `logdump`; on some units both
partitions are entirely zero, so there is nothing to read. Check before relying
on them. When they are empty, add a breadcrumb channel: have the custom init
append its journal to an unused, backed-up partition (identify it by size in
`/sys/class/block/*/size`, `mknod` it, `dd conv=notrunc`, then `sync`), and read
it back later from a rooted Android. `pstore`/ramoops is the other channel, but
the DT node must have a fixed `reg` to actually reserve memory.
