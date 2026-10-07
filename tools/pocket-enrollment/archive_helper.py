#!/usr/bin/env python3
"""Reuse the board's segmented verifier with the distinct helper project name."""
import argparse
import json
from pathlib import Path
import sys
sys.dont_write_bytecode=True
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=('create','verify'))
    p.add_argument('directory',type=Path)
    p.add_argument('--board-repo',type=Path,required=True)
    p.add_argument('--archive-root',type=Path,default=Path('build/enrollment-segmented'))
    a=p.parse_args()
    sys.path.insert(0,str(a.board_repo.resolve()/'tools'))
    import archive_segmented_firmware as verifier
    import archive_firmware
    # Only this process's verifier constants change; no repository file changes.
    verifier.APP='Pocket-Enrollment'
    archive_firmware.APP='Pocket-Enrollment'
    archive_firmware.REQUIRED_IMAGES=tuple(x.replace('FoloToy-AI-Passport','Pocket-Enrollment') for x in archive_firmware.REQUIRED_IMAGES)
    verifier.ARTIFACTS=tuple(x.replace('FoloToy-AI-Passport','Pocket-Enrollment') for x in verifier.ARTIFACTS)
    verifier.EXPECTED_OFFSETS={k.replace('FoloToy-AI-Passport','Pocket-Enrollment'):v for k,v in verifier.EXPECTED_OFFSETS.items()}
    dest=verifier.create_archive(a.directory,a.archive_root) if a.command=='create' else a.directory
    manifest=verifier.verify_archive(dest)
    if manifest['app_descriptor']['project_name']!='Pocket-Enrollment' or manifest['app_descriptor']['idf_version']!='v5.5.3':
        raise ValueError('helper project/IDF identity differs')
    print(json.dumps({'archive':str(dest.resolve()),'app_sha256':manifest['app_sha256'],'app_elf_sha256':manifest['app_elf_sha256'],'app_headroom_bytes':manifest['app_headroom_bytes'],'flash_device_written':False},indent=2))
if __name__=='__main__':
    main()
