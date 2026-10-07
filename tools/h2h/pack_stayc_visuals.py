#!/usr/bin/env python3
"""Verify the supplied STAYC atlases and losslessly pack their RGB565A8 pixels."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def encode(data):
    out = bytearray()
    i = 0
    while i < len(data):
        count = 1
        while i + count < len(data) and data[i + count] == data[i] and count < 128:
            count += 1
        if count >= 3:
            out.extend((127 + count, data[i]))
            i += count
        else:
            start = i
            i += 1
            while i < len(data) and i - start < 128:
                if i + 2 < len(data) and data[i] == data[i + 1] == data[i + 2]:
                    break
                i += 1
            out.append(i - start - 1)
            out.extend(data[start:i])
    return bytes(out)


def pixels(image):
    rgb, alpha = bytearray(), bytearray()
    for r, g, b, a in image.convert('RGBA').getdata():
        value = ((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)) if a else 0
        rgb.extend(value.to_bytes(2, 'little'))
        alpha.append(a)
    return bytes(rgb + alpha)


def build(source, destination):
    manifest = json.loads((source / 'manifest.json').read_text())
    destination.mkdir(parents=True, exist_ok=True)
    payload = bytearray()
    entries = []
    members = manifest['members']
    if {m['member'] for m in members} != {'sieun', 'sumin', 'isa', 'seeun', 'yoon', 'j'}:
        raise ValueError('Expected the six STAYC members')
    declarations = ['/* Generated; source hashes are in visual-catalog.json. */', '#include "stayc_scene.h"',
                    'const char *const stayc_members[6] = {' + ','.join(json.dumps(m['label']) for m in members) + '};',
                    'const stayc_sprite_t stayc_sprites[6][2][32] = {']
    for member in members:
        declarations.append('{')
        if len(member['outfits']) != 2:
            raise ValueError('Expected two complete outfits per member')
        for outfit in member['outfits']:
            declarations.append('{')
            count = 0
            for atlas in outfit['atlases']:
                meta_file = source / atlas['manifest']
                meta = json.loads(meta_file.read_text())
                png = source / atlas['atlas']
                if hashlib.sha256(png.read_bytes()).hexdigest() != meta['sha256']:
                    raise ValueError(f'Atlas checksum mismatch: {png}')
                if (meta['memberId'], meta['outfitId'], meta['groupId']) != (member['member'], outfit['id'], 'stayc'):
                    raise ValueError('Atlas identity mismatch')
                image = Image.open(png).convert('RGBA')
                if image.size != tuple(meta['sheet']) or meta['cell'] != [64, 80]:
                    raise ValueError('Atlas dimensions mismatch')
                for n, pose in enumerate(meta['poses']):
                    if pose.get('approved') is not True or pose.get('embeddedProps') is not False:
                        raise ValueError('Unapproved or embedded-prop pose')
                    raw = pixels(image.crop((n % 4 * 64, n // 4 * 80, n % 4 * 64 + 64, n // 4 * 80 + 80)))
                    compressed = encode(raw)
                    offset = len(payload)
                    payload.extend(compressed)
                    hands = pose['hands']
                    points = [hands['left'] or [-1, -1], hands['right'] or [-1, -1], pose.get('mouth', [-1, -1]), pose.get('grip', [-1, -1])]
                    if any(p is None or len(p) != 2 for p in points):
                        raise ValueError('Missing required anchor')
                    hand = {'left': 0, 'right': 1, 'center': 4}[pose.get('propHand', pose.get('activeHand', 'right'))]
                    anchor = ','.join('{' + ','.join(str(v) for v in point) + '}' for point in points)
                    declarations.append(f'{{{offset},{len(compressed)},{{{anchor}}},{pose.get("registrationOffsetY", 0)},{round(pose.get("sceneScale", 1) * 256)},{hand}}},')
                    entries.append({'member': member['member'], 'outfit': outfit['id'], 'pose': count,
                                    'kind': atlas['kind'], 'source_pose': pose['id'], 'source': atlas['atlas'],
                                    'source_sha256': meta['sha256'], 'offset': offset, 'bytes': len(compressed),
                                    'rgb565a8_sha256': hashlib.sha256(raw).hexdigest()})
                    count += 1
            if count != 32:
                raise ValueError('Incomplete member outfit')
            declarations.append('},')
        declarations.append('},')
    declarations.append('};')
    (destination / 'stayc_visuals.bin').write_bytes(payload)
    (destination / 'sprites.c').write_text('\n'.join(declarations) + '\n')
    report = {'schema': 'pocket.stayc.visuals.v1', 'source_commit': manifest['source_commit'],
              'encoding': 'RGB565 little endian followed by A8; bounded byte RLE',
              'frames': len(entries), 'frame_bytes': 15360, 'flash_bytes': len(payload),
              'sha256': hashlib.sha256(payload).hexdigest(), 'entries': entries}
    (destination / 'visual-catalog.json').write_text(json.dumps(report, indent=2) + '\n')
    export = json.loads(subprocess.check_output(['node', str(ROOT / 'tools/h2h/export_stayc_actions.mjs'), str(source)]))
    code = ['/* Generated from the validated original action adapter and room geometry. */', '#include "stayc_scene.h"']
    props = {None: 0, 'heart': 1, 'phone': 2, 'racket': 3, 'basketball': 4}
    attachments = {None: 0, 'right_hand': 1, 'hands_center': 2, 'right_hand_below': 3}
    for mi, outfits in enumerate(export['members']):
        for oi, actions in enumerate(outfits):
            for ai, action in enumerate(actions):
                code.append(f'static const stayc_phase_t phases_{mi}_{oi}_{ai}[] = {{')
                for phase in action['phases']:
                    effects = phase['effects']
                    if len(effects) > 1:
                        raise ValueError('Unsupported multiple effects')
                    effect = effects[0] if effects else None
                    geometry = ([1 if effect['id'] == 'ball' else 2] + effect['from_xy'] + effect['to_xy'] + [effect['arc']]) if effect else [0] * 6
                    fields = [phase['ms'], phase['pose'], phase['x'], phase['y'], props[phase['prop']], attachments[phase['prop_attachment']], int(phase['id'] == 'flash'), int(phase['id'] == 'scroll')] + geometry
                    code.append('{' + ','.join(map(str, fields)) + '},')
                code.append('};')
    code.append('const stayc_action_t stayc_actions[6][2][8] = {')
    for mi, outfits in enumerate(export['members']):
        code.append('{')
        for oi, actions in enumerate(outfits):
            code.append('{')
            for ai, a in enumerate(actions):
                code.append('{' + json.dumps(a['label'], ensure_ascii=False) + f',phases_{mi}_{oi}_{ai},{len(a["phases"])},{sum(p["ms"] for p in a["phases"])}}},')
            code.append('},')
        code.append('},')
    code.append('};')
    for ri, boxes in enumerate(export['rooms']):
        code.append(f'static const stayc_box_t room_{ri}[] = {{')
        for x, y, w, h, color, radius in boxes:
            code.append(f'{{{x},{y},{w},{h},0x{color[1:]},{radius}}},')
        code.append('};')
    code.append('const stayc_room_t stayc_rooms[3] = {' + ','.join(f'{{room_{i},{len(b)}}}' for i, b in enumerate(export['rooms'])) + '};')
    for i, decor in enumerate(export['decor']):
        if not decor['commands']:
            continue
        code.append(f'static const stayc_box_t decor_{i}[] = {{')
        for command in decor['commands']:
            kind, x, y, w, h, color, radius, *_ = command
            if kind != 0:
                raise ValueError('Unexpected decoration command')
            code.append(f'{{{x},{y},{w},{h},{color},{radius}}},')
        code.append('};')
    code.append('const stayc_room_t stayc_decor[4][3] = {')
    for slot in range(4):
        code.append('{' + ','.join(f'{{decor_{slot*3+i},{len(export["decor"][slot*3+i]["commands"])}}}' if i else '{NULL,0}' for i in range(3)) + '},')
    code.append('};')
    code.append('const char *const stayc_decor_labels[4][3] = {')
    for slot in range(4):
        code.append('{' + ','.join(json.dumps(export['decor'][slot*3+i]['label'],ensure_ascii=False) for i in range(3)) + '},')
    code.append('};')
    (destination / 'actions.c').write_text('\n'.join(code) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'assets/images/stayc')
    args = parser.parse_args()
    report = build(args.source, args.output)
    print(json.dumps({k: v for k, v in report.items() if k != 'entries'}))
