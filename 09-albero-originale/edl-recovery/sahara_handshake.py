#!/usr/bin/env python3
"""
Raw Sahara handshake to upload loader and transition 900e -> 9008.
"""

import usb.core
import usb.util
import struct
import sys
import time

VID = 0x05c6
PID_900E = 0x900e
PID_9008 = 0x9008

SAHARA_HELLO_REQ = 0x01
SAHARA_HELLO_RSP = 0x02
SAHARA_READ_DATA = 0x03
SAHARA_END_TRANSFER = 0x04
SAHARA_DONE_REQ = 0x05
SAHARA_DONE_RSP = 0x06
SAHARA_MODE_IMAGE_TX_PENDING = 0x00

def main():
    loader_path = sys.argv[1] if len(sys.argv) > 1 else "/home/user/nx679j-stock/edl-recovery/prog_firehose_ddr.melf"
    with open(loader_path, "rb") as f:
        loader_data = f.read()
    print(f"Loader: {len(loader_data)} bytes")

    dev = usb.core.find(idVendor=VID, idProduct=PID_900E)
    if dev is None:
        dev = usb.core.find(idVendor=VID, idProduct=PID_9008)
    if dev is None:
        print("No device!"); sys.exit(1)
    print(f"Device: VID=0x{dev.idVendor:04x} PID=0x{dev.idProduct:04x}")

    if dev.is_kernel_driver_active(0):
        dev.detach_kernel_driver(0)
    dev.set_configuration()

    # Direct iteration — pyusb makes each iface iterate as Endpoint objects
    ep_out = ep_in = None
    for iface in dev.get_active_configuration():
        for ep in iface:
            if ep.bmAttributes == 2:  # Bulk
                if ep.bEndpointAddress & 0x80 == 0:
                    ep_out = ep.bEndpointAddress
                else:
                    ep_in = ep.bEndpointAddress

    if ep_out is None or ep_in is None:
        print("No bulk EPs found!"); sys.exit(1)
    print(f"Bulk EPs: OUT=0x{ep_out:02x} IN=0x{ep_in:02x}")

    # Phase 1: Read HELLO_REQ
    print("\n--- Phase 1: HELLO_REQ ---")
    for trial in range(5):
        try:
            data = dev.read(ep_in, 1024, timeout=5000)
            raw = bytes(data)
            if len(raw) >= 8:
                cmd = struct.unpack("<I", raw[:4])[0]
                length = struct.unpack("<I", raw[4:8])[0]
                print(f"cmd=0x{cmd:02x} len={length}  {raw[:32].hex()}")
                if cmd == SAHARA_HELLO_REQ:
                    version = struct.unpack("<I", raw[8:12])[0]
                    pktsize = struct.unpack("<I", raw[16:20])[0]
                    print(f"Hello: version={version}, pktsize={pktsize}")

                    # Phase 2: HELLO_RSP in IMAGE_TX_PENDING
                    print("\n--- Phase 2: HELLO_RSP (IMAGE_TX_PENDING) ---")
                    hello_resp = struct.pack("<IIIIIIIIIIII",
                        SAHARA_HELLO_RSP, 0x30, version, 1, pktsize,
                        SAHARA_MODE_IMAGE_TX_PENDING, 1, 2, 3, 4, 5, 6)
                    dev.write(ep_out, hello_resp, timeout=5000)

                    # Phase 3: Second HELLO_REQ
                    print("\n--- Phase 3: Second HELLO_REQ ---")
                    for trial2 in range(5):
                        data2 = dev.read(ep_in, 1024, timeout=10000)
                        raw2 = bytes(data2)
                        if len(raw2) >= 8:
                            cmd2 = struct.unpack("<I", raw2[:4])[0]
                            print(f"cmd=0x{cmd2:02x} len={struct.unpack('<I', raw2[4:8])[0]}  {raw2[:32].hex()}")
                            if cmd2 == SAHARA_HELLO_REQ:
                                version2 = struct.unpack("<I", raw2[8:12])[0]
                                pktsize2 = struct.unpack("<I", raw2[16:20])[0]
                                print(f"Hello2: version={version2}, pktsize={pktsize2}")

                                # Phase 4: HELLO_RSP
                                print("\n--- Phase 4: HELLO_RSP (IMAGE_TX_PENDING) ---")
                                hello_resp2 = struct.pack("<IIIIIIIIIIII",
                                    SAHARA_HELLO_RSP, 0x30, version2, 1, pktsize2,
                                    SAHARA_MODE_IMAGE_TX_PENDING, 1, 2, 3, 4, 5, 6)
                                dev.write(ep_out, hello_resp2, timeout=5000)

                                # Phase 5: Upload loader
                                print("\n--- Phase 5: Loader upload ---")
                                loop = 0
                                while loop < 100:
                                    data3 = dev.read(ep_in, 1024, timeout=30000)
                                    raw3 = bytes(data3)
                                    if len(raw3) < 8:
                                        print(f"Short response"); break
                                    cmd3 = struct.unpack("<I", raw3[:4])[0]
                                    print(f"[{loop}] cmd=0x{cmd3:02x}  {raw3[:32].hex()}")

                                    if cmd3 == SAHARA_READ_DATA:
                                        if len(raw3) < 20:
                                            loop += 1; continue
                                        iid = struct.unpack("<I", raw3[8:12])[0]
                                        off = struct.unpack("<I", raw3[12:16])[0]
                                        ln = struct.unpack("<I", raw3[16:20])[0]
                                        print(f"  READ: id=0x{iid:08x} off=0x{off:08x} len=0x{ln:08x}")
                                        if off + ln > len(loader_data):
                                            chunk = loader_data[off:] + b'\xFF' * (off + ln - len(loader_data))
                                        else:
                                            chunk = loader_data[off:off + ln]
                                        dev.write(ep_out, chunk, timeout=10000)
                                        print(f"  Sent {len(chunk)} bytes")
                                        loop += 1
                                    elif cmd3 == SAHARA_DONE_REQ:
                                        print("  DONE_REQ")
                                        dev.write(ep_out, struct.pack("<III", SAHARA_DONE_RSP, 0x0C, 0x00), timeout=5000)
                                    elif cmd3 == SAHARA_END_TRANSFER:
                                        if len(raw3) >= 16:
                                            status = struct.unpack("<I", raw3[12:16])[0]
                                            print(f"  END_TRANSFER status=0x{status:08x}")
                                            if status == 0:
                                                print("\n  SUCCESS! Waiting for 9008...")
                                                for k in range(30):
                                                    time.sleep(1)
                                                    dev2 = usb.core.find(idVendor=VID, idProduct=PID_9008)
                                                    if dev2 is not None:
                                                        print(f"  -> Device is 9008 after {k+1}s!")
                                                        return
                                                print("  WARNING: Device didn't switch")
                                        break
                                    elif cmd3 == SAHARA_HELLO_REQ:
                                        v = struct.unpack("<I", raw3[8:12])[0]
                                        ps = struct.unpack("<I", raw3[16:20])[0]
                                        print(f"  Stage: v={v} ps={ps}")
                                        hello_resp3 = struct.pack("<IIIIIIIIIIII",
                                            SAHARA_HELLO_RSP, 0x30, v, 1, ps,
                                            SAHARA_MODE_IMAGE_TX_PENDING, 1, 2, 3, 4, 5, 6)
                                        dev.write(ep_out, hello_resp3, timeout=5000)
                                    else:
                                        print(f"  Unknown 0x{cmd3:02x}")
                                        loop += 1
                                break
                            else:
                                print(f"Not HELLO_REQ (0x{cmd2:02x})")
                                break
                        else:
                            print(f"Short response ({len(raw2)} bytes)")
                    break
                else:
                    print(f"Not HELLO_REQ (0x{cmd:02x})")
                    break
            else:
                print(f"Short response ({len(raw)} bytes)")
        except usb.core.USBError as e:
            print(f"Trial {trial}: {e}")
            time.sleep(0.5)
            continue
    else:
        print("All trials failed")
    print("Done.")

if __name__ == "__main__":
    main()
