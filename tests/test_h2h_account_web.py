"""SSO transaction and account mutation boundaries for the AI Passport site."""
import importlib.util
import io
import os
import re
import unittest
from pathlib import Path
from unittest.mock import patch
from urllib.parse import urlencode, urlparse, parse_qs

source = Path(__file__).resolve().parents[1] / 'tools/h2h/passport_account_web.py'
spec = importlib.util.spec_from_file_location('passport_account_web', source)
web = importlib.util.module_from_spec(spec)
spec.loader.exec_module(web)


class AccountTests(unittest.TestCase):
    def setUp(self):
        self.calls = []

        def request(url, *, body=None, headers=None):
            self.calls.append(('sso', url, body))
            if url.endswith('/introspect'):
                return {'active': True, 'user_id': 123}
            return {'sid': 'a' * 64, 'wp_token': 'wp-secret', 'user_id': 123,
                    'expires_at': 2000000000}

        def api(path, *, token=None, method='GET', body=None):
            self.calls.append(('api', path, token, method, body))
            if path == '/auth/wp-login':
                return {'token': 'jwt-secret', 'user': {'id': 123}}
            if path == '/passport/devices':
                return [{'id': 'd' * 32, 'card_url': web.ORIGIN + '/card/' + 'c' * 32,
                         'card_public': False, 'desired_theme': 'classic', 'applied_theme': 'classic'}]
            if path == '/passport/themes':
                return [{'id': 'classic', 'title': '经典主题'}]
            if path.endswith('/claim'):
                return {'status': 'awaiting_device_confirmation', 'account_id': 123}
            return {'ok': True}

        self.app = web.AccountApp(request=request, api=api, clock=lambda: 1900000000)

    def call(self, path, *, method='GET', cookie='', form=None, origin=web.ORIGIN, query=''):
        raw = urlencode(form).encode() if form is not None else b''
        env = {'PATH_INFO': path, 'REQUEST_METHOD': method, 'QUERY_STRING': query,
               'HTTP_COOKIE': cookie, 'HTTP_ORIGIN': origin,
               'CONTENT_TYPE': 'application/x-www-form-urlencoded',
               'CONTENT_LENGTH': str(len(raw)), 'wsgi.input': io.BytesIO(raw)}
        result = {}

        def start(status, headers):
            result.update(code=int(status[:3]), headers=dict(headers), cookies=[v for k, v in headers if k == 'Set-Cookie'])

        result['body'] = b''.join(self.app(env, start)).decode()
        return result

    @patch.dict(os.environ, {'PASSPORT_SSO_CLIENT_ID': 'passport', 'PASSPORT_SSO_CLIENT_SECRET': 'x' * 32})
    def sign_in(self, target='/account'):
        begin = self.call('/login', query=urlencode({'next': target}))
        self.assertEqual(begin['code'], 303)
        params = parse_qs(urlparse(begin['headers']['Location']).query)
        browser = begin['headers']['Set-Cookie'].split(';')[0]
        callback = self.call('/sso/callback', cookie=browser, query=urlencode({'state': params['state'][0], 'code': 'b' * 64}))
        self.assertEqual(callback['code'], 303)
        handle = next(value.split(';')[0] for value in callback['cookies'] if value.startswith(web.COOKIE + '='))
        return params, browser, handle

    @patch.dict(os.environ, {'PASSPORT_SSO_CLIENT_ID': 'passport', 'PASSPORT_SSO_CLIENT_SECRET': 'x' * 32})
    def test_sso_pkce_one_time_and_pairing_needs_csrf(self):
        code = 'b' * 32
        params, browser, session_cookie = self.sign_in('/pair/' + code)
        self.assertRegex(params['code_challenge'][0], r'^[A-Za-z0-9_-]{43}$')
        repeated = self.call('/sso/callback', cookie=browser, query=urlencode({'state': params['state'][0], 'code': 'b' * 64}))
        self.assertEqual(repeated['code'], 400)
        self.assertEqual(self.call('/pair/' + code, method='POST', cookie=session_cookie,
                                   form={'csrf': 'bad'})['code'], 400)
        self.assertFalse(any(c[1].endswith('/claim') for c in self.calls if c[0] == 'api'))
        csrf = next(iter(self.app.sessions.values()))['csrf']
        accepted = self.call('/pair/' + code, method='POST', cookie=session_cookie,
                             form={'csrf': csrf})
        self.assertEqual(accepted['code'], 200)
        self.assertIn('#123', accepted['body'])
        self.assertTrue(any(c[1].endswith('/claim') and c[2] == 'jwt-secret' for c in self.calls if c[0] == 'api'))

    @patch.dict(os.environ, {'PASSPORT_SSO_CLIENT_ID': 'passport', 'PASSPORT_SSO_CLIENT_SECRET': 'x' * 32})
    def test_theme_and_visibility_require_session_and_same_origin(self):
        _, _, session_cookie = self.sign_in()
        csrf = next(iter(self.app.sessions.values()))['csrf']
        path = '/account/' + 'd' * 32 + '/visibility'
        self.assertEqual(self.call(path, method='POST', form={'csrf': csrf, 'public': '1'})['code'], 303)
        self.assertEqual(self.call(path, method='POST', cookie=session_cookie,
                                   form={'csrf': csrf, 'public': '1'}, origin='https://evil.test')['code'], 400)
        self.assertEqual(self.call(path, method='POST', cookie=session_cookie,
                                   form={'csrf': csrf, 'public': '1'})['code'], 303)
        self.assertTrue(any(c[1].endswith('/card') and c[4] == {'public': True} for c in self.calls if c[0] == 'api'))

    def test_login_requires_sso_client_configuration(self):
        with patch.dict(os.environ, {'PASSPORT_SSO_CLIENT_ID': '', 'PASSPORT_SSO_CLIENT_SECRET': ''}):
            self.assertEqual(self.call('/login')['code'], 503)


if __name__ == '__main__':
    unittest.main()
