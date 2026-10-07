#!/usr/bin/env python3
"""Host-only bridge to the actual compiled native V4 gameplay module."""
import ctypes
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FILES = [ROOT / 'tools/h2h/stayc_game_bridge.c'] + [ROOT / 'main/h2h' / name for name in (
    'stayc_game.c', 'stayc_game_motion.c', 'stayc_game_life.c',
    'stayc_game_reaction.c', 'stayc_game_decor.c')]
HEADERS = sorted((ROOT / 'main/h2h').glob('stayc_game*.h'))
digest = hashlib.sha256()
for path in FILES + HEADERS:
    digest.update(str(path.relative_to(ROOT)).encode() + b'\0' + path.read_bytes())
with tempfile.TemporaryDirectory(prefix='stayc-native-') as directory:
    library = Path(directory) / 'game.so'
    subprocess.run(['cc', '-shared', '-fPIC', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(ROOT / 'main/h2h'), *map(str, FILES), '-o', str(library)], check=True)
    engine = ctypes.CDLL(str(library))
    engine.preview_state.restype = ctypes.c_char_p
    transfer = ctypes.create_string_buffer(128)
    saved = ctypes.create_string_buffer(128)
    engine.preview_init(12, 6)

    def uint(request, key, default=None):
        value = request.get(key, default)
        if type(value) is not int or not 0 <= value <= 0xffffffff:
            raise ValueError('Invalid uint32 ' + key)
        return value

    def fill(request):
        encoded = request['hex']
        if not isinstance(encoded, str) or len(encoded) > 256 or len(encoded) % 2:
            raise ValueError('Invalid transfer hex')
        data = bytes.fromhex(encoded)
        ctypes.memset(transfer, 0, 128)
        ctypes.memmove(transfer, data, len(data))
        return len(data)

    for line in sys.stdin:
        request = json.loads(line)
        op = request['op']
        if op == 'identity':
            response = {'protocol': 'stayc.gameplay-oracle.v1', 'source_sha256': digest.hexdigest(),
                        'engine': 'native-v4-explicit-port'}
        else:
            result, extra = None, {}
            if op == 'init': engine.preview_init(uint(request, 'tracks', 12), uint(request, 'songs', 6))
            elif op == 'input': engine.preview_input(uint(request, 'key'), uint(request, 'event'))
            elif op == 'tick': engine.preview_tick(uint(request, 'ms'))
            elif op == 'audio': engine.preview_audio(uint(request, 'ms', 0), uint(request, 'generation'),
                                                     int(request.get('ended', False)), int(request.get('failed', False)))
            elif op == 'position': engine.preview_position(uint(request, 'position_ms'), uint(request, 'duration_ms'), uint(request, 'generation'))
            elif op == 'load': result = bool(engine.preview_load(transfer, fill(request)))
            elif op == 'save': result = 92
            elif op == 'shop_prepare':
                result = engine.preview_shop_prepare(transfer)
                extra['candidate_hex'] = transfer.raw[:result].hex()
            elif op in ('shop_validate', 'shop_finish'):
                length = fill(request) if 'hex' in request else uint(request, 'length', 92)
                args = (transfer, length)
                if op == 'shop_finish': args += (uint(request, 'result'),)
                args += (uint(request, 'generation'), uint(request, 'debug_epoch'))
                value = getattr(engine, 'preview_' + op)(*args)
                result = bool(value) if op == 'shop_validate' else int(value)
            else: raise ValueError('Unknown operation ' + op)
            size = engine.preview_save(saved)
            if size != 92: raise ValueError('Invalid native save size')
            response = {'result': result, **extra, 'state': json.loads(engine.preview_state()),
                        'save_hex': saved.raw[:size].hex()}
        print(json.dumps(response, ensure_ascii=False), flush=True)
