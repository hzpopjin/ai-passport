#!/usr/bin/env python3
"""Production WSGI API. Run one Gunicorn worker with threads behind Nginx.
C state is isolated per opaque visitor cookie; SQLite holds validated saves.
Static files are served from the separate public/ directory by Nginx.
"""
import ctypes
import json
import os
import re
import secrets
import sqlite3
import threading
import time
from collections import OrderedDict
from http.cookies import SimpleCookie
from pathlib import Path
import importlib.util

_card_spec=importlib.util.spec_from_file_location('passport_card_web',Path(__file__).with_name('passport_card_web.py'))
passport_card_web=importlib.util.module_from_spec(_card_spec)
_card_spec.loader.exec_module(passport_card_web)
_account_spec=importlib.util.spec_from_file_location('passport_account_web',Path(__file__).with_name('passport_account_web.py'))
passport_account_web=importlib.util.module_from_spec(_account_spec)
_account_spec.loader.exec_module(passport_account_web)
account_app=passport_account_web.AccountApp()

ROOT=Path(__file__).resolve().parents[2]
COOKIE='h2h_visitor'
MAX_SESSIONS=2048
SAVE_SECONDS=3

class WebApp:
    def __init__(self,library=None,data_dir=None,origin=None):
        self.origin=origin or os.environ.get('H2H_ORIGIN','https://ai-passport.randomdance.cn')
        self.lib=ctypes.CDLL(str(library or ROOT/'build/h2h-web.so'))
        self.lib.preview_state.restype=ctypes.c_char_p
        catalog=json.loads((ROOT/'assets/music/h2h/catalog.json').read_text())
        self.catalog=catalog['tracks'];self.song_count=catalog.get('song_count',len(self.catalog))
        self.sessions=OrderedDict();self.lock=threading.Lock();self.cleanup_at=0
        directory=Path(data_dir or os.environ.get('H2H_DATA_DIR',ROOT/'build/web-data'));directory.mkdir(parents=True,exist_ok=True)
        self.db=sqlite3.connect(directory/'progress.sqlite3',check_same_thread=False)
        self.db.execute('PRAGMA journal_mode=WAL')
        self.db.execute('CREATE TABLE IF NOT EXISTS saves (id TEXT PRIMARY KEY, data BLOB NOT NULL, touched REAL NOT NULL)')
        self.db.commit()
    def snapshot(self):
        buffer=ctypes.create_string_buffer(2048);n=self.lib.web_snapshot(buffer)
        if n>len(buffer):raise RuntimeError('Model snapshot exceeds buffer')
        return buffer.raw[:n]
    def save(self):
        buffer=ctypes.create_string_buffer(128);n=self.lib.preview_save(buffer);return buffer.raw[:n]
    def persist(self,sid,session,now):
        data=session['save']
        self.db.execute('INSERT INTO saves VALUES(?,?,?) ON CONFLICT(id) DO UPDATE SET data=excluded.data,touched=excluded.touched',(sid,data,time.time()))
        self.db.commit();session['saved']=data;session['saved_at']=now
    def session(self,sid,now):
        if sid in self.sessions:
            self.sessions.move_to_end(sid);session=self.sessions[sid]
            self.lib.web_restore(session['model'],len(session['model']))
            return sid,session,False
        row=self.db.execute('SELECT data FROM saves WHERE id=?',(sid,)).fetchone() if sid else None
        if not row:sid=secrets.token_hex(32)
        self.lib.web_init(len(self.catalog),self.song_count,secrets.randbits(32))
        if row:self.lib.preview_load(row[0],len(row[0]))
        session={'model':self.snapshot(),'last':now,'saved_at':now,'save':self.save(),'saved':self.save(),'audio_budget':0}
        self.persist(sid,session,now)
        while len(self.sessions)>=MAX_SESSIONS:
            old_id,old=self.sessions.popitem(last=False);self.persist(old_id,old,now)
        self.sessions[sid]=session
        return sid,session,not bool(row)
    @staticmethod
    def integer(data,name,minimum,maximum,default=None):
        value=data.get(name,default)
        if type(value) is not int or not minimum<=value<=maximum:raise ValueError(name)
        return value
    def handle(self,environ):
        path=environ.get('PATH_INFO','');method=environ.get('REQUEST_METHOD','GET')
        if path=='/healthz' and method=='GET':return 200,{'ok':True},None
        if path not in ('/api/state','/api/input','/api/audio'):return 404,{'error':'Not found'},None
        if method!=('GET' if path=='/api/state' else 'POST'):return 405,{'error':'Method not allowed'},None
        data={}
        if method=='POST':
            if environ.get('HTTP_ORIGIN')!=self.origin:return 403,{'error':'Origin rejected'},None
            try:
                length=int(environ.get('CONTENT_LENGTH','0'))
                if not 0<length<=1024:raise ValueError('size')
                if environ.get('CONTENT_TYPE','').split(';')[0]!='application/json':raise ValueError('type')
                data=json.loads(environ['wsgi.input'].read(length))
                if not isinstance(data,dict):raise ValueError('object')
                if path=='/api/input':
                    self.integer(data,'key',0,2);self.integer(data,'event',0,2)
                else:
                    self.integer(data,'generation',0,4294967295)
                    for field,limit in [('ms',2000),('position_ms',86400000),('duration_ms',86400000)]:self.integer(data,field,0,limit,0)
                    for field in ('ended','failed'):
                        if field in data and type(data[field]) is not bool:raise ValueError(field)
            except (ValueError,KeyError,TypeError,UnicodeError):return 400,{'error':'Invalid request'},None
        cookie=SimpleCookie()
        try:cookie.load(environ.get('HTTP_COOKIE',''))
        except Exception:pass
        sid=cookie[COOKIE].value if COOKIE in cookie else ''
        if not re.fullmatch('[0-9a-f]{64}',sid):sid=''
        with self.lock:
            now=time.monotonic()
            if now-self.cleanup_at>600:
                self.db.execute('DELETE FROM saves WHERE touched<?',(time.time()-30*86400,));self.db.commit();self.cleanup_at=now
            sid,s,new=self.session(sid,now)
            elapsed=max(0,min(round((now-s['last'])*1000),60000));s['last']=now
            before=json.loads(self.lib.preview_state())
            s['audio_budget']=min(2000,s['audio_budget']+elapsed) if before['play'] else 0
            self.lib.preview_tick(elapsed)
            if path=='/api/input':self.lib.preview_input(data['key'],data['event'])
            elif path=='/api/audio':
                state=json.loads(self.lib.preview_state());duration=self.catalog[state['track']]['duration_ms']
                self.lib.preview_position(min(data.get('position_ms',0),duration),duration,data['generation'])
                # Rewards cannot grow faster than request wall-clock time.
                credited=min(data.get('ms',0),s['audio_budget']);s['audio_budget']-=credited
                self.lib.preview_audio(credited,data['generation'],data.get('ended',False),data.get('failed',False))
            state=json.loads(self.lib.preview_state());s['model']=self.snapshot();s['save']=self.save()
            if now-s['saved_at']>=SAVE_SECONDS and (s['save']!=s['saved'] or now-s['saved_at']>3600):self.persist(sid,s,now)
            return 200,state,sid if new else None
    def __call__(self,environ,start_response):
        if environ.get('PATH_INFO','').startswith('/card/') and environ.get('REQUEST_METHOD')=='GET':
            return passport_card_web.respond(environ,start_response)
        path=environ.get('PATH_INFO','')
        if path in ('/login','/sso/callback','/account','/logout') or path.startswith(('/pair/','/account/')):
            return account_app(environ,start_response)
        code,data,sid=self.handle(environ);body=json.dumps(data,ensure_ascii=False,separators=(',',':')).encode()
        headers=[('Content-Type','application/json; charset=utf-8'),('Content-Length',str(len(body))),('Cache-Control','no-store'),('X-Content-Type-Options','nosniff')]
        if sid:headers.append(('Set-Cookie',f'{COOKIE}={sid}; Path=/; Max-Age=2592000; HttpOnly; SameSite=Lax'+('; Secure' if self.origin.startswith('https:') else '')))
        start_response(f'{code} '+{200:'OK',400:'Bad Request',403:'Forbidden',404:'Not Found',405:'Method Not Allowed'}[code],headers)
        return [body]

def create_app():return WebApp()
