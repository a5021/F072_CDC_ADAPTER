import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from f072_adapter import (build_frame, build_script, crc16_ccitt, parse_frame,
                          parse_script_result)


class ProtocolTest(unittest.TestCase):
    def test_crc_vector(self):
        self.assertEqual(crc16_ccitt(b"123456789"), 0x29B1)

    def test_frame_round_trip(self):
        payload = bytes(range(32))
        raw = build_frame(7, 0x12, payload)
        self.assertEqual(len(raw), 7 + len(payload))
        seq, status, data = parse_frame(raw)
        self.assertEqual((seq, status, data), (7, 0x12, payload))

    def test_frame_crc_detects_corruption(self):
        raw = bytearray(build_frame(1, 0x10, b"abc"))
        raw[6] ^= 0xFF
        with self.assertRaises(ValueError):
            parse_frame(bytes(raw))

    def test_frame_max_payload(self):
        raw = build_frame(255, 0x10, b"\x00" * 505)
        self.assertEqual(len(raw), 512)

    def test_script_encoding(self):
        blob = build_script(0x30, [
            ("write", b"\x01\x02"),
            ("read", 3, 108),
            ("repeat", 1000, 0),
        ])
        self.assertEqual(blob[0], 0x30)
        self.assertEqual(blob[1], 3)
        self.assertEqual(blob[2], 0x01)          # OP_WRITE
        self.assertEqual(blob[3], 2)
        self.assertEqual(blob[6], 0x02)          # OP_READ
        self.assertEqual(struct.unpack_from("<H", blob, 8)[0], 3)
        self.assertEqual(struct.unpack_from("<H", blob, 10)[0], 108)
        self.assertEqual(blob[12], 0x07)         # OP_REPEAT
        self.assertEqual(struct.unpack_from("<H", blob, 14)[0], 1000)
        self.assertEqual(struct.unpack_from("<H", blob, 16)[0], 2)

    def test_script_result_decoding(self):
        blob = (bytes([0, 0]) + struct.pack("<H", 2) + struct.pack("<I", 1234) +
                b"\xAA\xBB")
        result = parse_script_result(blob)
        self.assertEqual(result[0]["op"], 0)
        self.assertEqual(result[0]["ts_us"], 1234)
        self.assertEqual(result[0]["data"], b"\xAA\xBB")


if __name__ == "__main__":
    unittest.main()
