#!/usr/bin/env python3
"""Verify and retain unpadded firmware for the existing minimal 8 MiB layout."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shlex
import struct
import sys
from dataclasses import asdict
from pathlib import Path

sys.dont_write_bytecode = True
from archive_firmware import (
    APP, SHA256, application_descriptor, parse_flash_args, read_artifact,
    safe_path, unique_json_object,
)
from verify_firmware import FLASH_SIZE, parse_partition_table

ARTIFACTS = (
    f"{APP}.elf", f"{APP}.map", f"{APP}.bin",
    "bootloader/bootloader.bin", "partition_table/partition-table.bin",
    "flash_args", "flasher_args.json",
)
EXPECTED_PARTITIONS = (
    (1, 2, 0x9000, 0x6000, "nvs"),
    (1, 1, 0xf000, 0x1000, "phy_init"),
    (0, 0, 0x10000, 0x7f0000, "factory"),
)
EXPECTED_OFFSETS = {
    "bootloader/bootloader.bin": 0,
    "partition_table/partition-table.bin": 0x8000,
    f"{APP}.bin": 0x10000,
}


def inspect_build(directory: Path) -> tuple[dict, dict[str, bytes]]:
    directory = safe_path(directory)
    artifacts = {name: read_artifact(directory, name) for name in ARTIFACTS}
    offsets = parse_flash_args(artifacts["flash_args"])
    if offsets != EXPECTED_OFFSETS:
        raise ValueError("segmented preservation requires the existing minimal image offsets")
    arguments = json.loads(artifacts["flasher_args.json"], object_pairs_hook=unique_json_object)
    if {name: int(offset, 0) for offset, name in arguments["flash_files"].items()} != offsets:
        raise ValueError("flasher_args.json disagrees with flash_args")
    if arguments["flash_settings"]["flash_size"] != "8MB":
        raise ValueError("flasher_args.json must select 8 MB")
    tokens = shlex.split(artifacts["flash_args"].decode("utf-8").splitlines()[0])
    options = dict(zip(tokens[::2], tokens[1::2]))
    if any(options.get("--" + key) != value for key, value in arguments["flash_settings"].items()):
        raise ValueError("flasher_args.json settings disagree with flash_args")
    if arguments["extra_esptool_args"]["chip"] != "esp32c3":
        raise ValueError("flasher_args.json must target ESP32-C3")
    for role, name in (("bootloader", "bootloader/bootloader.bin"),
                       ("partition-table", "partition_table/partition-table.bin"),
                       ("app", f"{APP}.bin")):
        if arguments[role]["file"] != name or int(arguments[role]["offset"], 0) != offsets[name]:
            raise ValueError(f"flasher_args.json {role} entry disagrees with flash_files")
    partitions, md5 = parse_partition_table(artifacts["partition_table/partition-table.bin"])
    if not md5 or tuple((p.kind, p.subtype, p.offset, p.size, p.label) for p in partitions) != EXPECTED_PARTITIONS:
        raise ValueError("partition table differs from the existing minimal layout")
    app = artifacts[f"{APP}.bin"]
    headroom = partitions[-1].size - len(app)
    if headroom < 512 * 1024:
        raise ValueError("application must leave at least 512 KiB in factory")
    ranges = {}
    for name, offset in offsets.items():
        size = len(artifacts[name])
        start, end = offset & ~4095, (offset + size + 4095) & ~4095
        if end > FLASH_SIZE:
            raise ValueError(f"image {name} exceeds flash")
        for protected in partitions[:2]:
            if start < protected.end and end > protected.offset:
                raise ValueError(f"image {name} erases protected partition {protected.label}")
        ranges[name] = {"offset": offset, "sector_start": start, "sector_end_exclusive": end}
    ordered = sorted(ranges, key=lambda name: ranges[name]["sector_start"])
    for left, right in zip(ordered, ordered[1:]):
        if ranges[left]["sector_end_exclusive"] > ranges[right]["sector_start"]:
            raise ValueError(f"image erase sectors overlap: {left}, {right}")
    elf = artifacts[f"{APP}.elf"]
    if len(elf) < 52 or elf[:6] != b"\x7fELF\x01\x01" or struct.unpack_from("<H", elf, 18)[0] != 243:
        raise ValueError("application ELF must be 32-bit little-endian RISC-V")
    descriptor = application_descriptor(app)
    files = {name: {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
             for name, data in artifacts.items()}
    if descriptor["embedded_elf_sha256"] != files[f"{APP}.elf"]["sha256"]:
        raise ValueError("application descriptor ELF SHA256 does not match the ELF")
    # esptool's 8 MB header encoding is 0x3; both images must target C3.
    for name in (f"{APP}.bin", "bootloader/bootloader.bin"):
        image = artifacts[name]
        if len(image) < 24 or image[0] != 0xe9 or image[3] >> 4 != 3 or struct.unpack_from("<H", image, 12)[0] != 5:
            raise ValueError(f"image {name} is not ESP32-C3 / 8 MB")
    manifest = {
        "schema_version": 1, "archive_mode": "segmented", "target": "esp32c3",
        "flash_size_bytes": FLASH_SIZE, "app_sha256": files[f"{APP}.bin"]["sha256"],
        "app_elf_sha256": files[f"{APP}.elf"]["sha256"], "app_descriptor": descriptor,
        "image_offsets": offsets, "image_ranges": ranges,
        "partitions": [asdict(p) for p in partitions], "app_headroom_bytes": headroom,
        "files": files,
    }
    return manifest, artifacts


def verify_archive(directory: Path) -> dict:
    directory = safe_path(directory)
    if not SHA256.fullmatch(directory.name):
        raise ValueError("archive directory name must be application SHA256")
    expected = json.loads(read_artifact(directory, "manifest.json"), object_pairs_hook=unique_json_object)
    actual, _ = inspect_build(directory)
    if expected != actual or directory.name != actual["app_sha256"]:
        raise ValueError("segmented archive manifest does not match artifacts")
    allowed = set(ARTIFACTS) | {"manifest.json", "bootloader", "partition_table"}
    for parent, directories, files in os.walk(directory, followlinks=False):
        for name in directories + files:
            path = safe_path(Path(parent) / name)
            if path.relative_to(directory).as_posix() not in allowed:
                raise ValueError(f"unexpected archive entry: {path}")
    return actual


def create_archive(build: Path, archive_root: Path) -> Path:
    build, archive_root = safe_path(build), safe_path(archive_root)
    if archive_root in (Path(archive_root.anchor), Path.home(), Path.cwd()) or build.is_relative_to(archive_root):
        raise ValueError("use a dedicated archive directory separate from the build")
    expected, artifacts = inspect_build(build)
    destination = safe_path(archive_root / expected["app_sha256"])
    if destination.exists():
        actual = verify_archive(destination)
        if any(actual[k] != expected[k] for k in expected if k != "files") or any(
            actual["files"][name] != expected["files"][name]
            for name in ARTIFACTS if name != f"{APP}.map"
        ):
            raise ValueError("existing archive has different build artifacts")
        return destination
    archive_root.mkdir(parents=True, exist_ok=True)
    destination.mkdir()
    # Exclusive files and a last-written manifest prevent incomplete success claims.
    # Leave an interrupted directory for inspection; never replace its contents.
    for name, data in artifacts.items():
        path = safe_path(destination / name)
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("xb") as output:
            output.write(data)
    actual, _ = inspect_build(destination)
    if actual != expected:
        raise ValueError("copied segmented archive differs from checked build")
    with (destination / "manifest.json").open("x", encoding="utf-8") as output:
        output.write(json.dumps(expected, indent=2, sort_keys=True) + "\n")
    verify_archive(destination)
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("check", "create", "verify"))
    parser.add_argument("directory", type=Path)
    parser.add_argument("--archive-root", type=Path, default=Path("build/firmware-segmented"))
    args = parser.parse_args()
    try:
        if args.command == "create":
            path = create_archive(args.directory, args.archive_root)
            print(f"Segmented firmware archive: {path}")
        elif args.command == "verify":
            verify_archive(args.directory)
            print(f"Segmented firmware archive: PASS ({args.directory})")
        else:
            report, _ = inspect_build(args.directory)
            print(f"Segmented firmware: PASS; app {report['files'][f'{APP}.bin']['size']} bytes; headroom {report['app_headroom_bytes']} bytes")
    except (OSError, UnicodeError, ValueError, KeyError, TypeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
