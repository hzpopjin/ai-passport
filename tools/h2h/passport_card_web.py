"""Public NFC landing page. The tag stores only a stable opaque URL.

The business API decides whether an account holder has published a card. This
site never embeds an account token or private profile field in an NFC payload.
"""
import html
import json
import re
import urllib.error
import urllib.request
from urllib.parse import urlparse

SLUG = re.compile(r'^[0-9a-f]{32}$')
API = 'https://apps.randomdance.cn/api/v1/passport/cards/'
AVATAR_HOSTS = {'www.randomdance.cn', 'randomdance.cn', 'apps.randomdance.cn'}


def _avatar_url(value):
    if not isinstance(value, str):
        return ''
    parsed = urlparse(value)
    if parsed.scheme != 'https' or parsed.hostname not in AVATAR_HOSTS:
        return ''
    if parsed.username or parsed.password or parsed.fragment:
        return ''
    return html.escape(value, quote=True)


def _fetch(slug):
    request = urllib.request.Request(API + slug, headers={'Accept': 'application/json'})
    with urllib.request.urlopen(request, timeout=5) as response:
        if response.status != 200:
            raise ValueError('profile response failed')
        body = response.read(16_384)
    decoded = json.loads(body)
    if not isinstance(decoded, dict) or decoded.get('code') != 0:
        raise ValueError('profile response invalid')
    return decoded.get('data')


def page(slug, fetch=_fetch):
    if not SLUG.fullmatch(slug):
        return 404, '卡片不存在', ''
    try:
        card = fetch(slug)
    except (urllib.error.URLError, ValueError, json.JSONDecodeError):
        return 503, '暂时无法读取卡片', ''
    if not isinstance(card, dict):
        return 503, '暂时无法读取卡片', ''
    if card.get('visibility') != 'public':
        return 200, '这张卡片尚未公开', ''
    nickname = str(card.get('nickname') or '随机舞蹈用户')[:80]
    avatar = _avatar_url(card.get('avatar_url'))
    portrait = f'<img class="avatar" src="{avatar}" alt="用户头像">' if avatar else '<div class="avatar empty" aria-hidden="true">♥</div>'
    theme = str(card.get('theme') or 'classic')
    if not re.fullmatch(r'[a-z0-9_-]{1,64}', theme):
        theme = 'classic'
    return 200, html.escape(nickname), portrait + f'<p class="theme">当前主题：{html.escape(theme)}</p>'


def respond(environ, start_response, fetch=_fetch):
    slug = environ.get('PATH_INFO', '').removeprefix('/card/')
    code, title, content = page(slug, fetch)
    body = ('<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1">'
            '<title>AI 通行证 · 随机舞蹈</title>'
            '<style>body{margin:0;min-height:100vh;display:grid;place-items:center;'
            'background:#dceffa;color:#26466b;font-family:system-ui,sans-serif}'
            'main{box-sizing:border-box;width:min(90vw,420px);padding:36px 28px;'
            'border-radius:28px;background:#fffcf2;text-align:center;box-shadow:0 18px 55px #26466b22}'
            '.avatar{display:grid;place-items:center;width:112px;height:112px;'
            'margin:0 auto 20px;border-radius:50%;object-fit:cover;background:#f7cdd9;'
            'font-size:48px}.theme{color:#61738a}h1{font-size:25px}small{color:#61738a}'
            '</style></head><body><main><small>RANDOM DANCE · AI PASSPORT</small>'
            f'{content}<h1>{title}</h1><p>轻触卡片，分享此刻的自己。</p>'
            '</main></body></html>').encode('utf-8')
    status = {200: '200 OK', 404: '404 Not Found', 503: '503 Service Unavailable'}[code]
    start_response(status, [
        ('Content-Type', 'text/html; charset=utf-8'),
        ('Content-Length', str(len(body))),
        ('Cache-Control', 'no-store'),
        ('Referrer-Policy', 'no-referrer'),
        ('X-Content-Type-Options', 'nosniff'),
        ('Content-Security-Policy', "default-src 'none'; img-src https://www.randomdance.cn https://randomdance.cn https://apps.randomdance.cn; style-src 'unsafe-inline'; base-uri 'none'; frame-ancestors 'none'"),
    ])
    return [body]
