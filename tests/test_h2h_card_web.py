import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('passport_card_web', ROOT / 'tools/h2h/passport_card_web.py')
card = importlib.util.module_from_spec(spec)
spec.loader.exec_module(card)


class PassportCardWebTests(unittest.TestCase):
    slug = 'a' * 32

    def test_public_card_is_minimal_and_escaped(self):
        status, title, content = card.page(self.slug, lambda _: {
            'visibility': 'public', 'nickname': '<script>alert(1)</script>',
            'avatar_url': 'javascript:alert(1)', 'theme': '<img onerror=alert(1)>',
            'phone': '13800000000', 'wp_email': 'private@example.test',
        })
        self.assertEqual(status, 200)
        self.assertIn('&lt;script&gt;', title)
        self.assertNotIn('<script>', title)
        self.assertNotIn('javascript:', content)
        self.assertNotIn('13800000000', title + content)
        self.assertNotIn('private@example.test', title + content)
        self.assertIn('classic', content)

    def test_private_card_does_not_expose_profile(self):
        status, title, content = card.page(self.slug, lambda _: {
            'visibility': 'private', 'nickname': '私密姓名', 'avatar_url': 'https://www.randomdance.cn/avatar.png',
        })
        self.assertEqual((status, title, content), (200, '这张卡片尚未公开', ''))

    def test_invalid_slug_does_not_call_api(self):
        def forbidden(_):
            self.fail('API request was made for an invalid slug')
        self.assertEqual(card.page('../secrets', forbidden)[0], 404)


if __name__ == '__main__':
    unittest.main()
