#!/usr/bin/env python3
"""Build an unsigned visual bundle from real tracked RGB565A8 arrays.

Does not generate signing keys, register a release or pretend to include music.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
SIZE = 512 + 15 * 15360 + 4 * 768

def array(source, name, expected):
    match = re.search(rf'\b{re.escape(name)}\[\]\s*=\s*\{{(.*?)\}};', source, re.S)
    if not match:
        raise ValueError(f'Missing actual source array: {name}')
    values = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-fA-F]{2})\b', match[1]))
    if len(values) != expected:
        raise ValueError(f'Unexpected source array size: {name}')
    return values

def build(resource_id, group, member, title, kind='member_visual', version=1, min_firmware=1, max_firmware=1):
    if not re.fullmatch(r'[a-z0-9_-]{1,32}', resource_id) or kind not in ('member_visual', 'group_visual'):
        raise ValueError('Invalid resource identity')
    header = bytearray(512)
    header[:8] = b'PKVIS02\0'
    struct.pack_into('<9I', header, 8, 2, SIZE, 15, 64, 80, 15360, 4, 768, 15360)
    header[48] = 1 if kind == 'member_visual' else 2
    for value, offset, capacity in ((resource_id,64,33),(group,100,33),(member,136,33),(title,172,65)):
        encoded = value.encode('ascii')
        if not encoded or len(encoded) >= capacity or any(c < 32 or c > 126 for c in encoded):
            raise ValueError('Labels must fit the existing printable ASCII font')
        header[offset:offset+len(encoded)] = encoded
    struct.pack_into('<3I', header, 240, 0xDCEFFA, 0xEBF3CE, 0xF8E5EE)
    sprites = (ROOT/'assets/images/h2h/sprites.c').read_text()
    icons = (ROOT/'assets/images/h2h/icons.c').read_text()
    payload = bytes(header) + b''.join(array(sprites,f'ian_{outfit}_{pose}_data',15360) for outfit in range(3) for pose in range(5))
    payload += b''.join(array(icons,f'icon_{name}_data',768) for name in ('wave','heart','shirt','scene'))
    assert len(payload) == SIZE
    manifest = {'kind':kind,'model':'esp32c3-pocket-8m','format_version':2,'resource_id':resource_id,
                'version':version,'min_firmware':min_firmware,'max_firmware':max_firmware,'size':SIZE,
                'decoded_bytes':15360,'sha256':hashlib.sha256(payload).hexdigest(),'key_id':'','signature':''}
    return payload, {'title':title,'group':group,'member':member,'manifest':manifest}

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('output', type=Path)
    p.add_argument('--resource-id', default='h2h-ian-visual')
    p.add_argument('--group', default='HEARTS2HEARTS')
    p.add_argument('--member', default='IAN')
    p.add_argument('--title', default='IAN visual room')
    a=p.parse_args()
    payload, entry=build(a.resource_id,a.group,a.member,a.title)
    a.output.mkdir(parents=True,exist_ok=True)
    (a.output/(entry['manifest']['sha256']+'.bin')).write_bytes(payload)
    (a.output/'unsigned-entry.json').write_text(json.dumps(entry,indent=2)+'\n')
    print(f'Unsigned actual-source visual bundle: {len(payload)} bytes. Music is not included.')
