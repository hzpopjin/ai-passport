import importlib.util
import json
from pathlib import Path
import struct
import unittest
import zlib

path = Path(__file__).resolve().parents[1]/"tools/h2h/decode_pocket_log.py"
spec = importlib.util.spec_from_file_location("decoder", path)
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


def crc(data):
    struct.pack_into("<I", data, 60, zlib.crc32(data[:60]))
    return data


def fixture():
    data = bytearray(b"\xff"*131072)
    h = bytearray(64)
    struct.pack_into("<4sHHQQ", h, 0, b"PKLS", 1, 64, 1, 1)
    data[:64] = crc(h)
    r = bytearray(64)
    struct.pack_into("<4sHBBQII", r, 0, b"PKLR", 1, 2, 0, 1, 1, 60)
    r[24:32] = bytes(range(8))
    r[32:40] = bytes((4, 13, 1, 2, 1, 7, 0, 0))
    struct.pack_into("<6H", r, 40, 77, 70, 64, 73, 32, 0)
    data[64:128] = crc(r)
    return data


class Decoder(unittest.TestCase):
    def test_c_codec_golden(self):
        data = bytes.fromhex("504b4c52010003000100000000000000020000007b0000000102030405060708040c0203040701064d004600400049002800020000000000000000002beec010")
        row = decoder.record(data)
        self.assertEqual((row["boot"], row["kind"], row["uptime_seconds"], row["key"], row["main_stack_64b"], row["errors"]), (2, 3, 123, 3, 73, 2))

    def test_schema_and_no_raw_tail(self):
        data = fixture()
        data[9000:9040] = b"private raw tail sentinel".ljust(40, b"!")
        result = decoder.decode(data)
        self.assertEqual(result["count"], 1)
        row = result["records"][0]
        self.assertEqual((row["page"], row["heap_kib"], row["firmware_elf_prefix"]), (13, 77, "0001020304050607"))
        self.assertNotIn("private", json.dumps(result))

    def test_torn_and_invalid_fields(self):
        for i in range(64):
            data = fixture()
            data[64+i] ^= 1
            self.assertEqual(decoder.decode(data)["count"], 0)
        data = fixture()
        r = bytearray(data[64:128])
        r[37] = 254
        data[64:128] = crc(r)
        self.assertEqual(decoder.decode(data)["count"], 0)
        for n in (0, 64, 131071, 131073):
            with self.assertRaises(ValueError):
                decoder.decode(bytes(n))


if __name__ == "__main__":
    unittest.main()
