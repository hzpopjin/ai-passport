#!/usr/bin/env python3
"""Require 512 KiB headroom inside the configured application partition."""
import argparse,json,struct
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path);a=p.parse_args()
table=(a.build/'partition_table/partition-table.bin').read_bytes(); partitions=[]
for offset in range(0,len(table),32):
    record=table[offset:offset+32]
    if len(record)!=32 or record[:2]!=b'\xaa\x50': break
    _,kind,subtype,start,size,label,flags=struct.unpack('<HBBII16sI',record)
    if kind==0:partitions.append((start,size,label.rstrip(b'\0').decode()))
args=json.loads((a.build/'flasher_args.json').read_text())
flash_files=args['flash_files']
for start,size,label in partitions:
    filename=next((path for addr,path in flash_files.items() if int(addr,0)==start),None)
    if filename:
        actual=(a.build/filename).stat().st_size; free=size-actual
        if free<512*1024: raise SystemExit(f'Capacity FAILED: {label} has only {free} bytes free')
        print(f'H2H capacity: app {actual} bytes, headroom {free} bytes (minimum 524288): PASS')
        break
else: raise SystemExit('No flashed application partition found')
full=a.build/'FoloToy-AI-Passport-full.bin'
if full.stat().st_size>8*1024*1024: raise SystemExit('Merged image exceeds 8 MiB')
