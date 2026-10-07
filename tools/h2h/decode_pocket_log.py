#!/usr/bin/env python3
"""Decode validated public records from an explicitly supplied offline capture.

No serial access, credentials, raw-sector output or automatic upload. This does
not authorize obtaining a device capture or reading an unallocated factory tail.
"""
import argparse
import json
from pathlib import Path
import struct
import zlib

BYTES = 131072


def valid_crc(data):
    return len(data) == 64 and struct.unpack_from("<I", data, 60)[0] == zlib.crc32(data[:60])


def record(data):
    if not valid_crc(data) or data[:8] not in (b"PKLR\1\0\1\0", b"PKLR\1\0\2\0", b"PKLR\1\0\3\0") or any(data[52:60]):
        return None
    sequence, boot, uptime = struct.unpack_from("<QII", data, 8)
    reset, page, game, key, event, cloud, result, error = data[32:40]
    if not sequence or not boot or reset not in (*range(16), 255) or page not in (*range(32), 255) or game > 2 or key > 3 or event > 5 or cloud not in (*range(9), 255) or result > 2 or error > 7:
        return None
    metrics = struct.unpack_from("<6H", data, 40)
    return dict(sequence=sequence, boot=boot, kind=data[6], uptime_seconds=uptime,
                firmware_elf_prefix=data[24:32].hex(), reset_reason=reset,
                page=page, game=game, key=key, event=event, cloud_state=cloud,
                result=result, error=error,
                **dict(zip(("heap_kib", "minimum_kib", "largest_kib", "main_stack_64b", "log_stack_64b", "errors"), metrics)))


def decode(data):
    if len(data) != BYTES:
        raise ValueError("Capture must be exactly 128 KiB from the verified dedicated partition")
    sectors = []
    for base in range(0, BYTES, 4096):
        header = data[base:base+64]
        if not valid_crc(header) or header[:8] != b"PKLS\1\0\x40\0" or any(header[24:60]):
            continue
        sequence, first = struct.unpack_from("<QQ", header, 8)
        if sequence and first:
            sectors.append((sequence, first, base))
    rows = []
    previous = 0
    for _, first, base in sorted(sectors):
        for slot in range(63):
            start = base+64*(slot+1)
            row = record(data[start:start+64])
            if row is None or row["sequence"] != first+slot:
                break
            if row["sequence"] <= previous:
                raise ValueError("Validated sector history has overlapping/out-of-order records")
            rows.append(row)
            previous = row["sequence"]
    return dict(schema=1, records=rows, count=len(rows),
                scope="Validated public records only; no raw bytes or account/save data")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    text = json.dumps(decode(args.capture.read_bytes()), indent=2)+"\n"
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
