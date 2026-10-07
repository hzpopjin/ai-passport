#!/usr/bin/env python3
"""Small API-boundary audit, supplementing actual adapter/fault tests."""
import ast
from pathlib import Path
import re
import sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parent
device=(ROOT/'device/main/enrollment_main.c').read_text()
core=(ROOT/'device/main/enrollment_core.c').read_text()
server=(ROOT/'server.cjs').read_text()
host=(ROOT/'host.py').read_text()
assert not re.search(r'\b(?:nvs_erase\w*|nvs_flash_erase\w*|esp_flash_(?:read|write|erase)\w*|esp_partition_(?:read|write|erase)\w*)\s*\(',device+core)
assert re.findall(r'nvs_get_str\([^;]+',device)==['nvs_get_str(readonly_handle,"id",device,&length)==ESP_OK&&pe_hex(device,32)']
assert len(re.findall(r'\bnvs_get_blob\(',device))==1 and 'nvs_get_blob(readonly_handle,"relay_key",NULL,&size)' in device
assert len(re.findall(r'\bnvs_set_\w+\(',device))==1 and 'nvs_set_blob(writer,"relay_key",key,32)' in device
assert 'NVS_READONLY,&readonly_handle' in device and 'nvs_flash_init()==ESP_OK' in device
assert 'nvs_get_str' not in core and 'nvs_get_blob' not in core
assert not re.search(r'(?:UPDATE|DELETE|REPLACE|TRUNCATE)\s+(?:TABLE\s+)?passport_',server,re.I)
assert not re.search(r'SELECT[^\n]+\b(?:state|credentialHash|relay_key)\s*(?:,|FROM)',server,re.I)
assert "SELECT ${COLUMNS}" in server and "device_id,purpose,key_revision,revoked" in server
assert 'StrictHostKeyChecking=yes' in host and 'BatchMode=yes' in host and 'getpass.getpass' in host
assert 'SECURE_LOCAL_TERMINAL_REQUIRED' in host and 'os.O_NOFOLLOW' in host and 'os.O_EXCL' in host
assert 'AESGCM' in host and 'hashlib.scrypt' in host
ast.parse(host)
for p in ROOT.rglob('*.py'):
    if 'build-helper' not in p.parts:
        ast.parse(p.read_text())
print('PASS source API-boundary audit: only existing public ID, key-length metadata and one relay_key insertion; no secret export or erase')
