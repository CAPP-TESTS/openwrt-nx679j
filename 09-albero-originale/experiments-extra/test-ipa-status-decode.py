#!/usr/bin/env python3
"""Synthetic fixtures, not modem captures."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "ipa_status_decode", Path(__file__).with_name("ipa-status-decode.py"))
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


class DecodeTests(unittest.TestCase):
    def line(self, raw, offset=0x58E18):
        words = " ".join(f"raw{i}=0x{int.from_bytes(raw[i*8:i*8+8], 'little'):x}"
                         for i in range(4))
        return f"fixture 1.250000: parse: pointer=0x1234 caller=0x{offset:x} {words}"

    def test_layout(self):
        raw = bytearray(range(32))
        raw[0] = 1
        raw[4:6] = (8).to_bytes(2, "little")
        raw[6] = 14
        raw[30] = 16
        result = decoder.decode(self.line(raw), 0)
        self.assertEqual(result["raw32"], raw.hex())
        self.assertEqual(result["path"], "LAN")
        self.assertEqual(result["packet_length"], 8)
        self.assertEqual(result["source"], 14)
        self.assertEqual(result["destination"], 16)
        self.assertEqual(result["timestamp"], "1.250000")

    def test_wan_and_unknown(self):
        raw = bytes(32)
        self.assertEqual(decoder.decode(self.line(raw, 0x5A818), 0)["path"], "WAN")
        self.assertEqual(decoder.decode(self.line(raw, 0x123), 0)["path"], "UNKNOWN")

    def test_whole_endpoint_byte(self):
        raw = bytearray(32)
        raw[6] = 0xE1
        raw[30] = 0xE2
        result = decoder.decode(self.line(raw), 0)
        self.assertEqual(result["source"], 0xE1)
        self.assertEqual(result["destination"], 0xE2)

    def test_no_fabricated_fields(self):
        self.assertIsNone(decoder.decode("# no record", 0))
        with self.assertRaises(ValueError):
            decoder.decode("parse: raw0=0x0", 0)
        with self.assertRaises(ValueError):
            decoder.decode(self.line(bytes(32)) + " raw0=0x1", 0)


if __name__ == "__main__":
    unittest.main()
