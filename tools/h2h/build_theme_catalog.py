#!/usr/bin/env python3
"""Build the official H2H theme catalog and tiny device theme packs.

The three themes choose existing embedded outfits and rooms. No music, user
progress, credentials, or firmware bytes are included in a theme pack.
"""
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
THEMES = (
    ('sky', '晴空水手服', 0, 0),
    ('lemon', '柠檬针织衫', 1, 1),
    ('pink', '粉色舞台装', 2, 2),
)
MAGIC = b'H2HSKIN\0'
FORMAT = '<8sBBBBI'
PACK_SIZE = struct.calcsize(FORMAT)


def build(destination: Path):
    if destination.exists():
        raise ValueError('Choose a new catalog directory')
    destination.mkdir(parents=True)
    catalog = []
    for theme_id, title, outfit, room in THEMES:
        data = struct.pack(FORMAT, MAGIC, 1, outfit, room, 0, 0)
        file = theme_id + '.bin'
        (destination / file).write_bytes(data)
        catalog.append({
            'id': theme_id, 'title': title, 'version': 1,
            'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data), 'file': file,
            'preview_url': f'https://ai-passport.randomdance.cn/assets/images/h2h/ian_{outfit}_0.png',
        })
    (destination / 'catalog.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + '\n')
    return catalog


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    result = build(args.destination)
    print(f'Built {len(result)} official theme packs in {args.destination}')
