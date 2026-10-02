#!/usr/bin/env python3
"""Rebuild and verify a font subset covering UI strings and every packed title."""
from pathlib import Path
import json, subprocess
from fontTools.ttLib import TTFont
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'assets/fonts/h2h'
text=''.join(p.read_text() for p in (ROOT/'main/h2h').glob('*.c'))
for t in json.loads((ROOT/'assets/music/h2h/catalog.json').read_text())['tracks']:
    text+=t['title']+t['artist']
chars=set(chr(c) for c in range(32,127)) | {c for c in text if ord(c)>=127 and c.isprintable()}
font=TTFont(OUT/'NotoSansSC.ttf'); coverage=font.getBestCmap()
missing=[c for c in sorted(chars) if ord(c) not in coverage]
if missing: raise SystemExit('Font is missing: '+repr(missing))
symbols=''.join(sorted(chars))
(OUT/'glyphs.txt').write_text(symbols+'\n')
converter=ROOT/'build/font-tools/node_modules/.bin/lv_font_conv'
subprocess.run([str(converter),'--font','assets/fonts/h2h/NotoSansSC.ttf','--symbols',symbols,'--size','16','--bpp','4','--format','lvgl','--no-compress','--lv-font-name','h2h_font_16','--lv-include','lvgl.h','--output','assets/fonts/h2h/h2h_font_16.c'],cwd=ROOT,check=True)
print(f'Font source coverage: {len(chars)} glyphs PASS; converter 1.5.3, 16 px/4 bpp.')

from fontTools import subset
preview_font=TTFont(OUT/'NotoSansSC.ttf')
subsetter=subset.Subsetter(); subsetter.populate(text=symbols); subsetter.subset(preview_font)
preview_font.save(ROOT/'preview/h2h/font.ttf')
