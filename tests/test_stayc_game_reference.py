#!/usr/bin/env python3
"""Strict black-box comparison of native V4 gameplay and genuine published v15."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
traces=sorted((ROOT/'tests').glob('stayc_v15_*_trace.json'))
if len(traces)<5:
    raise SystemExit('Missing maintained gameplay regression traces')
total=0
with tempfile.TemporaryDirectory(prefix='stayc-game-differential-') as directory:
    for trace in traces:
        output=Path(directory)/(trace.stem+'.json')
        subprocess.run([sys.executable,str(ROOT/'tools/h2h/diff_oracle.py'),str(trace),
                        '--oracle-adapter',str(ROOT/'tools/h2h/v15_oracle_adapter.py'),
                        '--native-adapter',str(ROOT/'tools/h2h/stayc_native_adapter.py'),
                        '--output',str(output)],check=True,cwd=ROOT)
        report=json.loads(output.read_text())
        assert report['result']=='PASS' and not report['mismatches']
        total+=report['steps']
print(f'Genuine v15 native gameplay: PASS ({len(traces)} traces, {total} complete state/save responses)')
