#!/usr/bin/env python3
"""Focused synthetic gate; requires installed IDF Python, Node and host compiler."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode=True
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--idf',type=Path,required=True)
    p.add_argument('--openssl',type=Path,required=True)
    a=p.parse_args();root=Path(__file__).resolve().parent
    def run(command):
        subprocess.run([str(x) for x in command],cwd=root,check=True)
    with tempfile.TemporaryDirectory(prefix='pocket-enrollment-tests-') as tmp:
        tmp=Path(tmp);flags=['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g']
        run(flags+['-Idevice/main','device/main/enrollment_core.c','tests/test_core.c','-o',tmp/'core'])
        run([tmp/'core'])
        # Upstream cJSON's sprintf triggers macOS deprecation diagnostics;
        # suppress only this vendor object, keep our code's full warning gate.
        cj=a.idf/'components/json/cJSON'
        run(flags+['-Wno-deprecated-declarations','-I'+str(cj),'-c',cj/'cJSON.c','-o',tmp/'cjson.o'])
        run(flags+['-Itests/stubs','-Idevice/main','-I'+str(cj),'-I'+str(a.openssl/'include'),'tests/test_device_adapter.c','device/main/enrollment_core.c',tmp/'cjson.o','-L'+str(a.openssl/'lib'),'-lcrypto','-o',tmp/'adapter'])
        run([tmp/'adapter'])
        run(['node','--test','tests/test_server.cjs'])
        run([sys.executable,'-B','tests/test_host.py'])
        run([sys.executable,'-B','audit_source.py'])
    print('PASS focused gate (synthetic only; no real key, registration, USB write or firmware flash)')
if __name__=='__main__':
    main()
