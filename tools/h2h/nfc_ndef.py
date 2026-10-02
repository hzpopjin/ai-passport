#!/usr/bin/env python3
"""Build an NTAG213 user-memory NDEF image for an assigned AI Passport card URL.

This does not write a physical tag. The first four bytes of NTAG213 user memory
are the NFC Forum Type 2 TLV; capability/lock/config pages are not included.
"""
import argparse
import re
from pathlib import Path

ORIGIN = 'https://ai-passport.randomdance.cn/card/'
URL = re.compile(r'^https://ai-passport\.randomdance\.cn/card/[0-9a-f]{32}$')
USER_BYTES = 144


def ndef_user_memory(url: str) -> bytes:
    if not URL.fullmatch(url):
        raise ValueError('expected an assigned https://ai-passport.randomdance.cn/card/<32 hex> URL')
    # NFC Forum RTD URI: D1 = MB|ME|SR|well-known, type U, 04 = https://.
    payload = b'\x04' + url.removeprefix('https://').encode('ascii')
    record = bytes((0xd1, 0x01, len(payload))) + b'U' + payload
    tlv = bytes((0x03, len(record))) + record + b'\xfe'
    if len(tlv) > USER_BYTES:
        raise ValueError('NDEF URI exceeds NTAG213 user memory')
    return tlv.ljust(USER_BYTES, b'\x00')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('card_url', help='Assigned card URL returned by device registration')
    parser.add_argument('output', type=Path, help='New 144-byte NTAG213 user-memory image')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Choose a new output file; an existing image is never overwritten')
    args.output.write_bytes(ndef_user_memory(args.card_url))
    print(f'Wrote {args.output} ({USER_BYTES} bytes); verify the URL on the tag before use')


if __name__ == '__main__':
    main()
