#!/usr/bin/env python3
"""Export the generated 5x3 atlas to identical PNG and LVGL RGB565A8 sprites."""
from pathlib import Path
from PIL import Image
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'assets/images/h2h'
sheet = Image.open(OUT / 'ian-atlas-source.png').convert('RGBA')
source = ['/* Generated from ian-atlas-source.png. */', '#include "lvgl.h"']
names = []
for row in range(3):
    for col in range(5):
        cell = sheet.crop((col*sheet.width//5, row*sheet.height//3, (col+1)*sheet.width//5, (row+1)*sheet.height//3))
        # Ignore near-transparent generation noise when measuring character size.
        bounds = cell.getchannel('A').point(lambda a: 255 if a >= 128 else 0).getbbox()
        if bounds is None:
            raise ValueError('Missing sprite')
        character = cell.crop(bounds)
        factor = min(58/character.width, 74/character.height)
        character = character.resize((round(character.width*factor), round(character.height*factor)), Image.Resampling.NEAREST)
        frame = Image.new('RGBA', (64,80))
        frame.paste(character, ((64-character.width)//2, 78-character.height))
        name = f'ian_{row}_{col}'
        frame.save(OUT / f'{name}.png')
        rgb, alpha = bytearray(), bytearray()
        for r,g,b,a in frame.getdata():
            value = ((r>>3)<<11) | ((g>>2)<<5) | (b>>3)
            rgb.extend(value.to_bytes(2,'little')); alpha.append(a)
        payload = rgb + alpha
        source.append(f'static const uint8_t {name}_data[] = {{')
        source.extend(','.join(f'0x{x:02x}' for x in payload[i:i+32])+',' for i in range(0,len(payload),32))
        source.append('};')
        source.append(f'const lv_image_dsc_t {name} = {{.header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8, .w=64, .h=80, .stride=128}}, .data_size={len(payload)}, .data={name}_data}};')
        names.append(name)
source.append('const lv_image_dsc_t *const h2h_sprites[3][5] = {')
for i in range(3): source.append('{' + ','.join('&'+n for n in names[i*5:i*5+5]) + '},')
source.append('};')
(OUT / 'sprites.c').write_text('\n'.join(source)+'\n')
print('15 sprites exported: 64x80, RGB565A8, 230400 bytes total; matching preview PNGs.')
