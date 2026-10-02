"""AI Passport account site. One Gunicorn worker owns short-lived SSO sessions.

The WordPress authorization code and the apps JWT are never sent to browser JS.
Configuration requires a dedicated SSO client with this site's callback URL.
"""
import hashlib
import html
import hmac
import json
import os
import re
import secrets
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.cookies import SimpleCookie

ORIGIN = 'https://ai-passport.randomdance.cn'
API = 'https://apps.randomdance.cn/api/v1'
AUTHORIZE = 'https://www.randomdance.cn/sso/authorize'
PROVIDER = 'https://www.randomdance.cn/wp-json/wp-user-center/v1/sso'
HEX32 = re.compile(r'^[0-9a-f]{32}$')
HEX64 = re.compile(r'^[0-9a-f]{64}$')
COOKIE = '__Host-passport_session'
TX_COOKIE = '__Host-passport_tx'


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        return None


_opener = urllib.request.build_opener(_NoRedirect)


def _request(url, *, body=None, headers=None):
    data = json.dumps(body, separators=(',', ':')).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, headers={
        'Accept': 'application/json', **({'Content-Type': 'application/json'} if data else {}),
        **(headers or {}),
    })
    with _opener.open(req, timeout=8) as response:
        if response.status != 200:
            raise ValueError('upstream failed')
        result = json.loads(response.read(65536))
    if not isinstance(result, dict):
        raise ValueError('upstream response invalid')
    return result


def _api(path, *, token=None, method='GET', body=None):
    if not path.startswith('/passport/') and path != '/auth/wp-login':
        raise ValueError('invalid API path')
    data = json.dumps(body, separators=(',', ':')).encode() if body is not None else None
    req = urllib.request.Request(API + path, data=data, method=method, headers={
        'Accept': 'application/json',
        **({'Content-Type': 'application/json'} if data is not None else {}),
        **({'Authorization': 'Bearer ' + token} if token else {}),
    })
    with _opener.open(req, timeout=8) as response:
        result = json.loads(response.read(65536))
    if not isinstance(result, dict) or result.get('code') != 0:
        raise ValueError('business API rejected request')
    return result.get('data')


def _cookie(environ, name):
    parsed = SimpleCookie()
    try:
        parsed.load(environ.get('HTTP_COOKIE', ''))
        return parsed[name].value if name in parsed else ''
    except Exception:
        return ''


def _post(environ):
    if environ.get('HTTP_ORIGIN') != ORIGIN:
        raise ValueError('origin mismatch')
    if environ.get('CONTENT_TYPE', '').split(';')[0] != 'application/x-www-form-urlencoded':
        raise ValueError('form required')
    length = int(environ.get('CONTENT_LENGTH', '0'))
    if length < 1 or length > 2048:
        raise ValueError('form too large')
    parsed = urllib.parse.parse_qs(environ['wsgi.input'].read(length).decode('utf-8'), strict_parsing=True)
    if any(len(v) != 1 for v in parsed.values()):
        raise ValueError('repeated form field')
    return {key: value[0] for key, value in parsed.items()}


def _html(title, content):
    return ('<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1">'
            f'<title>{html.escape(title)} · AI 通行证</title>'
            '<style>body{margin:0;background:#dceffa;color:#26466b;font:16px system-ui,sans-serif}'
            'main{box-sizing:border-box;max-width:700px;margin:30px auto;padding:30px;'
            'border-radius:25px;background:#fffcf2;box-shadow:0 12px 40px #26466b22}'
            'h1{font-size:26px}section{padding:18px 0;border-top:1px solid #d9e3e8}'
            'button,.button{display:inline-block;margin:6px 6px 6px 0;padding:10px 16px;border:0;'
            'border-radius:12px;background:#26466b;color:white;text-decoration:none;font:inherit;cursor:pointer}'
            'input,select{font:inherit;padding:8px;border:1px solid #9fb3c7;border-radius:8px}'
            'label{display:block;margin:10px 0}.muted{color:#61738a}code{overflow-wrap:anywhere}'
            '</style><script src="/passport-ble.js" defer></script></head><body><main><p class="muted">RANDOM DANCE · AI PASSPORT</p>'
            f'<h1>{html.escape(title)}</h1>{content}</main></body></html>').encode()


class AccountApp:
    def __init__(self, request=_request, api=_api, clock=time.time):
        self.request = request
        self.api = api
        self.clock = clock
        self.lock = threading.Lock()
        self.transactions = {}
        self.sessions = {}

    def _session(self, environ):
        handle = _cookie(environ, COOKIE)
        if not HEX64.fullmatch(handle):
            return None
        with self.lock:
            session = self.sessions.get(hashlib.sha256(handle.encode()).hexdigest())
            if not session or session['expires'] <= self.clock():
                return None
        result = self.request(PROVIDER + '/introspect', body={
            'client_id': os.environ['PASSPORT_SSO_CLIENT_ID'], 'sid': session['sid'],
        }, headers={'X-WPUC-SSO-Secret': os.environ['PASSPORT_SSO_CLIENT_SECRET']})
        if result.get('active') is not True or result.get('user_id') != session['user_id']:
            with self.lock:
                self.sessions.pop(hashlib.sha256(handle.encode()).hexdigest(), None)
            return None
        return session

    def _response(self, start, status, body=b'', *, location=None, cookies=()):
        reason = {200: 'OK', 303: 'See Other', 400: 'Bad Request', 401: 'Unauthorized',
                  404: 'Not Found', 405: 'Method Not Allowed', 502: 'Bad Gateway',
                  503: 'Service Unavailable'}[status]
        headers = [('Content-Type', 'text/html; charset=utf-8'), ('Content-Length', str(len(body))),
                   ('Cache-Control', 'no-store, private'), ('Referrer-Policy', 'no-referrer'),
                   ('X-Content-Type-Options', 'nosniff'),
                   ('Content-Security-Policy', "default-src 'none'; script-src 'self'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'"),
                   ('Permissions-Policy', 'bluetooth=(self)')]
        if location:
            headers.append(('Location', location))
        headers.extend(('Set-Cookie', c) for c in cookies)
        start(f'{status} {reason}', headers)
        return [body]

    def _error(self, start, code, message):
        return self._response(start, code, _html('无法完成操作', f'<p>{html.escape(message)}</p><a class="button" href="/account">返回账户</a>'))

    def _login(self, environ, start):
        client = os.environ.get('PASSPORT_SSO_CLIENT_ID', '')
        secret = os.environ.get('PASSPORT_SSO_CLIENT_SECRET', '')
        if not client or len(secret) < 32:
            return self._error(start, 503, '网站统一登录尚未配置')
        after = environ.get('QUERY_STRING', '')
        target = urllib.parse.parse_qs(after).get('next', ['/account'])[0]
        if target != '/account' and not re.fullmatch(r'/pair/[0-9a-f]{32}', target):
            target = '/account'
        state, verifier, browser = secrets.token_hex(32), secrets.token_hex(32), secrets.token_hex(32)
        challenge = __import__('base64').urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b'=').decode()
        with self.lock:
            self.transactions = {key: value for key, value in self.transactions.items()
                                 if value['expires'] > self.clock()}
            if len(self.transactions) >= 2048:
                return self._error(start, 503, '登录请求过多，请稍后重试')
            self.transactions[hashlib.sha256(state.encode()).hexdigest()] = {
                'verifier': verifier, 'browser_hash': hashlib.sha256(browser.encode()).hexdigest(),
                'target': target, 'expires': self.clock() + 600,
            }
        query = urllib.parse.urlencode({
            'client_id': client, 'redirect_uri': ORIGIN + '/sso/callback', 'response_type': 'code',
            'state': state, 'code_challenge': challenge, 'code_challenge_method': 'S256',
        })
        return self._response(start, 303, location=AUTHORIZE + '?' + query,
                              cookies=(f'{TX_COOKIE}={browser}; Path=/; Max-Age=600; Secure; HttpOnly; SameSite=Lax',))

    def _callback(self, environ, start):
        query = urllib.parse.parse_qs(environ.get('QUERY_STRING', ''))
        state = query.get('state', [''])[0]
        code = query.get('code', [''])[0]
        browser = _cookie(environ, TX_COOKIE)
        if not HEX64.fullmatch(state) or not HEX64.fullmatch(code) or not HEX64.fullmatch(browser):
            return self._error(start, 400, '登录回调无效或已过期')
        with self.lock:
            tx = self.transactions.pop(hashlib.sha256(state.encode()).hexdigest(), None)
        if not tx or tx['expires'] <= self.clock() or not hmac.compare_digest(tx['browser_hash'], hashlib.sha256(browser.encode()).hexdigest()):
            return self._error(start, 400, '登录校验已过期或已使用')
        try:
            grant = self.request(PROVIDER + '/token', body={
                'client_id': os.environ['PASSPORT_SSO_CLIENT_ID'], 'code': code,
                'code_verifier': tx['verifier'], 'redirect_uri': ORIGIN + '/sso/callback',
            }, headers={'X-WPUC-SSO-Secret': os.environ['PASSPORT_SSO_CLIENT_SECRET']})
            if (not HEX64.fullmatch(str(grant.get('sid', ''))) or
                    not isinstance(grant.get('user_id'), int) or grant['user_id'] <= 0 or
                    not isinstance(grant.get('expires_at'), int) or grant['expires_at'] <= self.clock() or
                    not isinstance(grant.get('wp_token'), str)):
                raise ValueError('SSO response invalid')
            login = self.api('/auth/wp-login', method='POST', body={'wp_token': grant['wp_token']})
            token = login.get('token') if isinstance(login, dict) else None
            if not isinstance(token, str) or len(token) > 4096:
                raise ValueError('account token missing')
            account = login.get('user') if isinstance(login, dict) else None
            if not isinstance(account, dict) or account.get('id') != grant['user_id']:
                raise ValueError('SSO identity mismatch')
        except (KeyError, ValueError, urllib.error.URLError, urllib.error.HTTPError):
            return self._error(start, 502, '统一登录暂不可用，请稍后重试')
        handle = secrets.token_hex(32)
        expires = min(float(grant.get('expires_at', 0)), self.clock() + 86400)
        if expires <= self.clock():
            return self._error(start, 502, '统一登录会话已过期')
        with self.lock:
            self.sessions = {key: value for key, value in self.sessions.items()
                             if value['expires'] > self.clock()}
            if len(self.sessions) >= 2048:
                return self._error(start, 503, '登录会话过多，请稍后重试')
            self.sessions[hashlib.sha256(handle.encode()).hexdigest()] = {
                'token': token, 'sid': grant['sid'], 'user_id': grant['user_id'],
                'csrf': secrets.token_hex(32), 'expires': expires,
            }
        return self._response(start, 303, location=tx['target'], cookies=(
            f'{COOKIE}={handle}; Path=/; Max-Age={int(expires-self.clock())}; Secure; HttpOnly; SameSite=Lax',
            f'{TX_COOKIE}=; Path=/; Max-Age=0; Secure; HttpOnly; SameSite=Lax',
        ))

    def _authorized(self, environ, start, *, target='/account'):
        session = self._session(environ)
        if not session:
            location = '/login?' + urllib.parse.urlencode({'next': target})
            return None, self._response(start, 303, location=location)
        return session, None

    def _account(self, environ, start):
        session, redirect = self._authorized(environ, start)
        if redirect is not None:
            return redirect
        try:
            devices = self.api('/passport/devices', token=session['token'])
            themes = self.api('/passport/themes', token=session['token'])
        except (ValueError, urllib.error.URLError, urllib.error.HTTPError):
            return self._error(start, 502, '暂时无法读取设备信息')
        if not isinstance(devices, list) or not isinstance(themes, list):
            return self._error(start, 502, '设备信息格式错误')
        content = '<p>扫码绑定 AI 通行证后，可以在这里更换主题和管理 NFC 信息卡。</p>'
        for d in devices:
            if not isinstance(d, dict) or not HEX32.fullmatch(str(d.get('id', ''))):
                continue
            ident = d['id']
            csrf = html.escape(session['csrf'])
            options = ''.join(f'<option value="{html.escape(t["id"])}"' + (' selected' if t['id'] == d.get('desired_theme') else '') + f'>{html.escape(str(t.get("title", t["id"])))}</option>'
                              for t in themes if isinstance(t, dict) and re.fullmatch(r'[a-z0-9_-]{1,64}', str(t.get('id', ''))))
            card_url = str(d.get('card_url', ''))
            card_link = f'<a href="{html.escape(card_url, quote=True)}">查看 NFC 信息卡</a>' if card_url.startswith(ORIGIN + '/card/') else ''
            content += (f'<section><h2>设备 {html.escape(ident[-8:])}</h2>'
                        f'<p>设备当前主题：{html.escape(str(d.get("applied_theme", "classic")))}</p>'
                        f'<form method="post" action="/account/{ident}/theme"><input type="hidden" name="csrf" value="{csrf}">'
                        f'<label>选择官方主题 <select name="theme_id">{options}</select></label><button>保存主题选择</button></form>'
                        '<p class="muted">连接卡片蓝牙后，在网页或小程序中手动开始主题下载。</p>'
                        f'<button type="button" data-passport-id="{ident}">蓝牙刷新主题</button><span class="muted" aria-live="polite"></span>'
                        f'<form method="post" action="/account/{ident}/visibility"><input type="hidden" name="csrf" value="{csrf}">'
                        f'<input type="hidden" name="public" value="{0 if d.get("card_public") else 1}">'
                        f'<button>{"关闭" if d.get("card_public") else "公开"} NFC 信息卡</button></form>{card_link}'
                        f'<form method="post" action="/account/{ident}/unbind"><input type="hidden" name="csrf" value="{csrf}">'
                        '<button>解绑设备</button></form></section>')
        if not devices:
            content += '<p>尚未绑定设备。请在卡片上显示绑定二维码，并用手机扫码。</p>'
        content += f'<form method="post" action="/logout"><input type="hidden" name="csrf" value="{html.escape(session["csrf"])}"><button>退出登录</button></form>'
        return self._response(start, 200, _html('我的 AI 通行证', content))

    def _pair(self, environ, start, code):
        if not HEX32.fullmatch(code):
            return self._error(start, 404, '绑定二维码无效')
        session, redirect = self._authorized(environ, start, target='/pair/' + code)
        if redirect is not None:
            return redirect
        if environ['REQUEST_METHOD'] == 'GET':
            return self._response(start, 200, _html('绑定 AI 通行证',
                '<p>确认二维码显示在你手中的卡片上。提交后，还需要在卡片上确认绑定。</p>'
                f'<form method="post" action="/pair/{code}"><input type="hidden" name="csrf" value="{html.escape(session["csrf"])}">'
                '<button>绑定到我的账号</button></form>'))
        fields = _post(environ)
        if not hmac.compare_digest(fields.get('csrf', ''), session['csrf']):
            return self._error(start, 400, '表单校验失败')
        try:
            claim = self.api('/passport/pairings/' + code + '/claim', token=session['token'], method='POST', body={})
        except (ValueError, urllib.error.URLError, urllib.error.HTTPError):
            return self._error(start, 502, '绑定失败，二维码可能已过期')
        account = claim.get('account_id') if isinstance(claim, dict) else None
        if not isinstance(account, int) or account <= 0:
            return self._error(start, 502, '账号校验信息缺失')
        return self._response(start, 200, _html('等待卡片确认',
            f'<p>请核对卡片显示的账号编号 #{account}，再按确定键完成绑定。</p>'
            '<a class="button" href="/account">查看我的设备</a>'))

    def _change(self, environ, start, device_id, action):
        session, redirect = self._authorized(environ, start)
        if redirect is not None:
            return redirect
        fields = _post(environ)
        if not hmac.compare_digest(fields.get('csrf', ''), session['csrf']):
            return self._error(start, 400, '表单校验失败')
        base = '/passport/devices/' + device_id
        if action == 'theme':
            theme = fields.get('theme_id', '')
            if not re.fullmatch(r'[a-z0-9_-]{1,64}', theme):
                raise ValueError('invalid theme')
            path, method, body = base + '/theme', 'PUT', {'theme_id': theme}
        elif action == 'visibility':
            public = fields.get('public', '')
            if public not in ('0', '1'):
                raise ValueError('invalid visibility')
            path, method, body = base + '/card', 'PATCH', {'public': public == '1'}
        else:
            path, method, body = base, 'DELETE', None
        try:
            self.api(path, token=session['token'], method=method, body=body)
        except (ValueError, urllib.error.URLError, urllib.error.HTTPError):
            return self._error(start, 502, '设备操作失败，请稍后重试')
        return self._response(start, 303, location='/account')

    def __call__(self, environ, start):
        path, method = environ.get('PATH_INFO', ''), environ.get('REQUEST_METHOD', 'GET')
        try:
            if path == '/login' and method == 'GET':
                return self._login(environ, start)
            if path == '/sso/callback' and method == 'GET':
                return self._callback(environ, start)
            if path == '/account' and method == 'GET':
                return self._account(environ, start)
            if path == '/logout' and method == 'POST':
                session = self._session(environ)
                fields = _post(environ)
                if not session or not hmac.compare_digest(fields.get('csrf', ''), session['csrf']):
                    return self._error(start, 401, '登录已过期')
                handle = _cookie(environ, COOKIE)
                with self.lock:
                    self.sessions.pop(hashlib.sha256(handle.encode()).hexdigest(), None)
                return self._response(start, 303, location='/account', cookies=(f'{COOKIE}=; Path=/; Max-Age=0; Secure; HttpOnly; SameSite=Lax',))
            pair = re.fullmatch(r'/pair/([0-9a-f]{32})', path)
            if pair and method in ('GET', 'POST'):
                return self._pair(environ, start, pair[1])
            change = re.fullmatch(r'/account/([0-9a-f]{32})/(theme|visibility|unbind)', path)
            if change and method == 'POST':
                return self._change(environ, start, change[1], change[2])
            return self._error(start, 404, '页面不存在')
        except (ValueError, KeyError, UnicodeError):
            return self._error(start, 400, '请求格式错误')
        except (urllib.error.URLError, urllib.error.HTTPError):
            return self._error(start, 503, '统一登录暂不可用，请稍后重试')
