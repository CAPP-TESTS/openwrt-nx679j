# PBL/Sahara: no HELLO, wedged states, transport choice

Case: device enumerates as `05c6:9008` but the Sahara conversation never starts.

## Transport
- EDL is USB bulk (two bulk endpoints), not a UART. A `/dev/ttyUSB*` exists only
  because a kernel driver (`qcserial`) binds that interface.
- `modprobe -r qcserial usb_wwan` is NOT enough: udev reloads it on the next
  enumeration. Stop the autoload with `/etc/modprobe.d/blacklist-qcserial.conf`
  (`blacklist qcserial`) or a udev rule, otherwise libusb gets EBUSY and every
  tool looks broken.
- With only the tty available, reads/writes still work but packet boundaries are
  not guaranteed; treat those results as indicative, not authoritative.

## A stalled bulk endpoint fakes every failure
- Any aborted session can leave EP OUT halted: writes then fail instantly with
  EIO and the handle dies with ENODEV while the device stays enumerated.
- Issue CLEAR_FEATURE(HALT) on both endpoints before the first write.

## Status semantics
- The PBL can answer `END_OF_IMAGE(0x04) image, status` instead of HELLO.
  A non-zero status is not "abort": the flow still requires the host to send
  DONE(0x05). Stock clients (`qdl`, `qdl_rs`, `edlclient`) stop there, which is
  why they never reach Firehose.
- status 1 = INVALID_CMD, 8 = INVALID_DATA_SIZE; 33/34/41/48 are auth/hash
  diagnostics that do not prevent the already-loaded code from executing.

## Announcement timing
- A fresh PBL may emit its first packets only while a read is already pending.
  Poll for the device before triggering the EDL entry and keep the transport
  resilient across re-enumeration (the tty number changes).
- `RESET_REQ(0x07)` makes the device detach and re-enumerate; it does not
  necessarily clear an error state living outside the Sahara state machine.
- If a *fresh* PBL still announces a stale image failure, the state is read from
  something that survives the reset (SMEM/IMEM cookie, flash flag). Test that
  instead of retrying packets.

## When no HELLO ever arrives
- `emmcdl` PblHack: HELLO_RSP(mode=COMMAND) -> read (CMD_READY or END_OF_IMAGE)
  -> SWITCH_MODE(COMMAND) -> device re-announces HELLO.
- Vary one packet at a time and flag any reply differing from the baseline
  END_OF_IMAGE answer. Identical answers to every packet mean the device is not
  in a state that accepts host commands; stop and find the source of that state.
