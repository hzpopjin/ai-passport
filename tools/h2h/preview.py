#!/usr/bin/env python3
"""Local screen preview running the firmware's C state model. No device access."""
import argparse, ctypes, json, mimetypes, subprocess, time, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit
ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build'; BUILD.mkdir(exist_ok=True)
LIB=BUILD/'h2h-preview.so'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC','-I'+str(ROOT/'main/h2h'),str(ROOT/'main/h2h/h2h_model.c'),str(ROOT/'main/h2h/h2h_motion.c'),str(ROOT/'tools/h2h/preview_bridge.c'),'-o',str(LIB)],check=True)
lib=ctypes.CDLL(str(LIB)); lib.preview_state.restype=ctypes.c_char_p
catalog=json.loads((ROOT/'assets/music/h2h/catalog.json').read_text())
lib.preview_init(len(catalog['tracks']),catalog.get('song_count',len(catalog['tracks'])))
savefile=BUILD/'h2h-preview-save.bin'
if savefile.exists():
    data=savefile.read_bytes(); lib.preview_load(data,len(data))
last=time.monotonic(); saved_at=last
state_lock=threading.RLock()
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args): pass
    def send(self,data,mime='application/json',status=200):
        self.send_response(status); self.send_header('Content-Type',mime); self.send_header('Content-Length',str(len(data))); self.send_header('Cache-Control','no-store'); self.end_headers(); self.wfile.write(data)
    def update(self):
        global last,saved_at
        now=time.monotonic(); lib.preview_tick(min(round((now-last)*1000),60000)); last=now
        if now-saved_at>=3:
            data=ctypes.create_string_buffer(128); size=lib.preview_save(data); temp=savefile.with_suffix('.tmp'); temp.write_bytes(data.raw[:size]); temp.replace(savefile); saved_at=now
    def do_GET(self):
        route=unquote(urlsplit(self.path).path)
        if route=='/account':
            body=('<!doctype html><html lang="zh-CN"><meta charset="utf-8">'
                  '<meta name="viewport" content="width=device-width,initial-scale=1">'
                  '<title>AI 通行证 · 本地预览</title>'
                  '<style>body{margin:0;min-height:100vh;display:grid;place-items:center;'
                  'background:#dceffa;color:#26466b;font:16px system-ui}'
                  'main{max-width:460px;margin:24px;padding:32px;border-radius:24px;'
                  'background:#fffcf2}a{color:#26466b}</style>'
                  '<main><h1>AI 通行证 · 本地预览</h1>'
                  '<p>此处只模拟卡片画面和按键。账号登录、蓝牙配网、扫码绑定与 NFC 轻触需要服务端和实体设备联调。</p>'
                  '<p><a href="/">返回卡片交互预览</a></p></main></html>').encode()
            self.send(body,'text/html; charset=utf-8');return
        if route=='/api/state':
            with state_lock:
                self.update(); response=lib.preview_state()
            self.send(response); return
        if route=='/': route='/preview/h2h/index.html'
        allowed=('/preview/h2h/','/assets/images/h2h/','/assets/music/h2h/')
        target=(ROOT/route.lstrip('/')).resolve()
        if not route.startswith(allowed) or not target.is_relative_to(ROOT) or not target.is_file():
            self.send(b'Not found','text/plain',404); return
        self.send(target.read_bytes(),mimetypes.guess_type(target.name)[0] or 'application/octet-stream')
    def do_POST(self):
        origin=self.headers.get('Origin')
        if origin and origin not in (f'http://127.0.0.1:{self.server.server_port}',f'http://localhost:{self.server.server_port}'):
            self.send(b'Forbidden','text/plain',403); return
        try:
            size=int(self.headers.get('Content-Length',0))
            if not 0<size<=1024: raise ValueError()
            data=json.loads(self.rfile.read(size))
            with state_lock:
                self.update()
                if self.path=='/api/input': lib.preview_input(int(data['key']),int(data['event']))
                elif self.path=='/api/audio':
                    lib.preview_position(int(data.get('position_ms',0)),int(data.get('duration_ms',0)),int(data['generation']))
                    lib.preview_audio(max(0,min(int(data.get('ms',0)),2000)),int(data['generation']),bool(data.get('ended')),bool(data.get('failed')))
                else: self.send(b'Not found','text/plain',404); return
                response=lib.preview_state()
            self.send(response)
        except (ValueError,KeyError,TypeError): self.send(b'Invalid request','text/plain',400)
parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('--port',type=int,default=8877); args=parser.parse_args()
print(f'H2H preview: http://127.0.0.1:{args.port} (same C model as firmware)',flush=True)
class PreviewServer(ThreadingHTTPServer):
    request_queue_size=32
PreviewServer(('127.0.0.1',args.port),Handler).serve_forever()
