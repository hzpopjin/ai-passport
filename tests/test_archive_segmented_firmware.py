#!/usr/bin/env python3
"""Check unpadded packages, ELF identity and protected flash erase sectors."""
import hashlib
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import archive_segmented_firmware as ARCHIVE


class SegmentedArchiveTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="passport-segments-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.build = self.root / "build"
        self.output = self.root / "archive"
        self.build.mkdir()
        elf = bytearray(96)
        elf[:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into("<H", elf, 18, 243)
        self.write(f"{ARCHIVE.APP}.elf", elf)
        self.write(f"{ARCHIVE.APP}.map", b"synthetic test linker map")
        app = bytearray(24 + 8 + 256 + 16)
        app[:2] = b"\xe9\x01"
        app[3] = 0x3f
        struct.pack_into("<H", app, 12, 5)
        struct.pack_into("<II", app, 24, 0x3c000020, 256)
        struct.pack_into("<I", app, 32, 0xabcd5432)
        app[176:208] = hashlib.sha256(elf).digest()
        self.write(f"{ARCHIVE.APP}.bin", app)
        self.write("bootloader/bootloader.bin", app[:24])
        self.table()
        self.write("flash_args", (
            "--flash_mode dio --flash_freq 80m --flash_size 8MB\n" +
            "\n".join(f"{offset:#x} {name}" for name, offset in ARCHIVE.EXPECTED_OFFSETS.items()) + "\n"
        ).encode())
        arguments = {"flash_files": {hex(offset): name for name, offset in ARCHIVE.EXPECTED_OFFSETS.items()},
                     "flash_settings": {"flash_mode": "dio", "flash_freq": "80m", "flash_size": "8MB"},
                     "extra_esptool_args": {"chip": "esp32c3"}}
        for role, name in (("app", f"{ARCHIVE.APP}.bin"), ("bootloader", "bootloader/bootloader.bin"),
                           ("partition-table", "partition_table/partition-table.bin")):
            arguments[role] = {"file": name, "offset": hex(ARCHIVE.EXPECTED_OFFSETS[name])}
        self.arguments = arguments
        self.write_json(arguments)

    def write(self, name, data):
        path = self.build / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def write_json(self, arguments):
        self.write("flasher_args.json", json.dumps(arguments).encode())

    def table(self, entries=None):
        entries = entries or ARCHIVE.EXPECTED_PARTITIONS
        table = bytearray(b"\xff" * 0xc00)
        for index, (kind, subtype, offset, size, label) in enumerate(entries):
            struct.pack_into("<HBBII16sI", table, index * 32, 0x50aa, kind, subtype, offset, size,
                             label.encode().ljust(16, b"\0"), 0)
        marker = len(entries) * 32
        struct.pack_into("<H", table, marker, 0xebeb)
        table[marker + 16:marker + 32] = hashlib.md5(table[:marker]).digest()
        self.write("partition_table/partition-table.bin", table)

    def test_round_trip_has_no_padded_image_and_hashes_every_segment(self):
        path = ARCHIVE.create_archive(self.build, self.output)
        report = ARCHIVE.verify_archive(path)
        self.assertEqual(set(report["files"]), set(ARCHIVE.ARTIFACTS))
        self.assertEqual(path.name, hashlib.sha256((self.build / f"{ARCHIVE.APP}.bin").read_bytes()).hexdigest())
        self.assertEqual(report["image_offsets"], ARCHIVE.EXPECTED_OFFSETS)
        self.assertFalse(list(path.glob("*-full.bin")))
        self.assertEqual(report["image_ranges"]["partition_table/partition-table.bin"]["sector_end_exclusive"], 0x9000)

    def test_rejects_elf_mismatch(self):
        self.write(f"{ARCHIVE.APP}.elf", (self.build / f"{ARCHIVE.APP}.elf").read_bytes() + b"changed")
        with self.assertRaisesRegex(ValueError, "ELF SHA256"):
            ARCHIVE.create_archive(self.build, self.output)
        self.assertFalse(self.output.exists())

    def test_rejects_changed_protected_layout(self):
        self.table(((1, 2, 0x9000, 0x5000, "nvs"), *ARCHIVE.EXPECTED_PARTITIONS[1:]))
        with self.assertRaisesRegex(ValueError, "partition table differs"):
            ARCHIVE.inspect_build(self.build)

    def test_rejects_bootloader_erasing_nvs(self):
        self.write("bootloader/bootloader.bin", (self.build / "bootloader/bootloader.bin").read_bytes().ljust(0x9001, b"\0"))
        with self.assertRaisesRegex(ValueError, "protected partition nvs"):
            ARCHIVE.inspect_build(self.build)

    def test_rejects_erasing_phy_or_extra_image(self):
        self.arguments["flash_files"]["0xf000"] = "phy.bin"
        self.write_json(self.arguments)
        with self.assertRaisesRegex(ValueError, "disagrees with flash_args"):
            ARCHIVE.inspect_build(self.build)

    def test_rejects_argument_role_or_settings_mismatch(self):
        for key, replacement in (("app", {"file": f"{ARCHIVE.APP}.bin", "offset": "0x0"}),
                                 ("flash_settings", {"flash_mode": "dio", "flash_freq": "40m", "flash_size": "8MB"})):
            saved = self.arguments[key]
            self.arguments[key] = replacement
            self.write_json(self.arguments)
            with self.assertRaisesRegex(ValueError, "disagree"):
                ARCHIVE.inspect_build(self.build)
            self.arguments[key] = saved

    def test_rejects_insufficient_application_headroom(self):
        self.write(f"{ARCHIVE.APP}.bin", (self.build / f"{ARCHIVE.APP}.bin").read_bytes().ljust(0x7f0000 - 524287, b"\0"))
        with self.assertRaisesRegex(ValueError, "512 KiB"):
            ARCHIVE.inspect_build(self.build)

    def test_archive_detects_mutation_and_unexpected_files(self):
        path = ARCHIVE.create_archive(self.build, self.output)
        (path / "unexpected.bin").write_bytes(b"private")
        with self.assertRaisesRegex(ValueError, "unexpected"):
            ARCHIVE.verify_archive(path)
        (path / "unexpected.bin").unlink()
        (path / f"{ARCHIVE.APP}.map").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "manifest does not match"):
            ARCHIVE.verify_archive(path)

    def test_identical_executables_reuse_original_map(self):
        path = ARCHIVE.create_archive(self.build, self.output)
        original = (path / f"{ARCHIVE.APP}.map").read_bytes()
        self.write(f"{ARCHIVE.APP}.map", b"other temporary build path")
        self.assertEqual(ARCHIVE.create_archive(self.build, self.output), path)
        self.assertEqual((path / f"{ARCHIVE.APP}.map").read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
