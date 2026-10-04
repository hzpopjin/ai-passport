#!/usr/bin/env python3
"""Audit actual embedded data and optional visual A/B capacity; never estimates app size."""
import argparse
import json
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[2]

def audit(app_bytes=None):
    catalog=json.loads((ROOT/'assets/music/h2h/catalog.json').read_text())
    sprites=sum(int(n) for n in re.findall(r'\.data_size=(\d+)',(ROOT/'assets/images/h2h/sprites.c').read_text()))
    icons=sum(int(n) for n in re.findall(r'\.data_size=(\d+)',(ROOT/'assets/images/h2h/icons.c').read_text()))
    font=(ROOT/'assets/fonts/h2h/h2h_font_16.c').read_text()
    bitmap=re.search(r'glyph_bitmap\[\]\s*=\s*\{(.*?)\};',font,re.S).group(1)
    font_bitmap=len(re.findall(r'0x[0-9a-fA-F]+',bitmap))
    audio=catalog['flash_audio_bytes']
    missing=[t['flash_file'] for t in catalog['tracks'] if not (ROOT/'assets/music/h2h'/t['flash_file']).exists()]
    visual=512+sprites+icons
    known=audio+sprites+icons+font_bitmap
    full_lower_bound=0x10000+known+2*(0x1000+visual+audio)
    result={'flash_bytes':0x800000,'default_factory_bytes':0x7f0000,'default_resource_capacity':0,
            'audio_catalog_bytes':audio,'missing_audio_files':missing,'actual_sprite_bytes':sprites,'actual_icon_bytes':icons,
            'actual_font_bitmap_bytes':font_bitmap,'known_embedded_data_lower_bound':known,'visual_payload_bytes':visual,
            'visual_slot_bytes':0x40000,'visual_slot_metadata_bytes':0x1000,'visual_factory_bytes':0x770000,
            'visual_app_limit_with_512k_headroom':0x6f0000,'app_bytes':app_bytes,
            'full_audio_visual_ab_with_embedded_fallback_lower_bound':full_lower_bound,
            'full_audio_visual_ab_excess_lower_bound':full_lower_bound-0x800000,
            'firmware_ota_available':False}
    if app_bytes is not None and app_bytes>0x6f0000:
        raise ValueError('Actual app exceeds visual layout capacity/headroom; reject this layout')
    return result
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--app',type=Path);a=p.parse_args()
    print(json.dumps(audit(a.app.stat().st_size if a.app else None),indent=2))
