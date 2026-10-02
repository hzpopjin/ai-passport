import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path

file = Path(__file__).resolve().parents[1] / 'tools/h2h/build_theme_catalog.py'
spec = importlib.util.spec_from_file_location('build_theme_catalog', file)
themes = importlib.util.module_from_spec(spec)
spec.loader.exec_module(themes)


class ThemeCatalogTests(unittest.TestCase):
    def test_official_packs_reference_embedded_skins_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / 'themes'
            catalog = themes.build(target)
            self.assertEqual(catalog, json.loads((target / 'catalog.json').read_text()))
            self.assertEqual({item['id'] for item in catalog}, {'sky', 'lemon', 'pink'})
            for item in catalog:
                data = (target / item['file']).read_bytes()
                magic, version, outfit, room, flags, reserved = struct.unpack(themes.FORMAT, data)
                self.assertEqual((magic, version, flags, reserved), (themes.MAGIC, 1, 0, 0))
                self.assertEqual(outfit, room)
                self.assertIn(outfit, range(3))
                self.assertEqual(len(data), item['size'])
                self.assertTrue(item['preview_url'].endswith(f'ian_{outfit}_0.png'))


if __name__ == '__main__':
    unittest.main()
