import importlib.util
import unittest
from pathlib import Path

file = Path(__file__).resolve().parents[1] / 'tools/h2h/nfc_ndef.py'
spec = importlib.util.spec_from_file_location('nfc_ndef', file)
nfc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(nfc)


class NFCNdefTests(unittest.TestCase):
    def test_uri_record_fits_ntag213_and_contains_only_public_url(self):
        url = nfc.ORIGIN + 'a' * 32
        memory = nfc.ndef_user_memory(url)
        self.assertEqual(len(memory), 144)
        self.assertEqual(memory[0], 0x03)
        record = memory[2:2 + memory[1]]
        self.assertEqual(record[:4], b'\xd1\x01' + bytes((len(url) - len('https://') + 1,)) + b'U')
        self.assertEqual(record[4], 0x04)
        self.assertEqual('https://' + record[5:].decode(), url)
        self.assertEqual(memory[2 + memory[1]], 0xfe)

    def test_rejects_tokens_and_other_hosts(self):
        for value in ('https://evil.test/card/' + 'a' * 32,
                      nfc.ORIGIN + 'a' * 32 + '?token=secret',
                      nfc.ORIGIN + 'A' * 32):
            with self.assertRaises(ValueError):
                nfc.ndef_user_memory(value)


if __name__ == '__main__':
    unittest.main()
