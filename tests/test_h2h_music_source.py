"""Verify Pocket music source scope and credential-free readiness/probes."""
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
import urllib.error

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("music_source", ROOT / "tools/h2h/music_source.py")
source = importlib.util.module_from_spec(spec)
spec.loader.exec_module(source)


class PresenceOnly:
    def __contains__(self, name):
        return name == "POCKET_MUSIC_API_KEY"

    def __getitem__(self, name):
        raise AssertionError("Credential values must never be read")


class FakeOpener:
    def __init__(self, outcome):
        self.outcome = outcome
        self.request = None

    def open(self, request, timeout):
        self.request = request
        raise self.outcome


class MusicSource(unittest.TestCase):
    def test_no_retired_music_host_in_product_sources(self):
        for directory in ("main", "tools", "config", "docs"):
            for path in (ROOT / directory).rglob("*"):
                if not path.is_file():
                    continue
                try:
                    content = path.read_text()
                except UnicodeError:
                    continue
                self.assertNotIn("chaojifather.me", content.lower(), str(path))

    def test_only_dedicated_credential_presence_is_inspected(self):
        policy = source.load_policy()
        self.assertTrue(source.readiness(policy, PresenceOnly())["api_key_configured"])
        self.assertFalse(source.readiness(policy, {})["api_key_configured"])
        self.assertFalse(source.readiness(policy, {"FA_API_KEY": "fixture"})["api_key_configured"])
        self.assertFalse(source.readiness(policy, PresenceOnly())["credential_values_read"])

    def test_unverified_sources_proxies_and_credential_scopes_are_rejected(self):
        policy = source.load_policy()
        for field, value in [
            ("api_base_url", "https://other.example/api"),
            ("api_base_url", source.API_BASE_URL + "?credential=fixture"),
            ("api_key_env", "FA_API_KEY"),
            ("credential_scope", "https://apps.randomdance.cn"),
            ("permitted_proxy_host", "other.example"),
            ("proxy_route", "/unverified-music-route"),
        ]:
            with self.subTest(field=field), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "source.json"
                path.write_text(json.dumps({**policy, field: value}))
                with self.assertRaises(ValueError):
                    source.load_policy(path)

    def test_proxy_rejection_is_distinct_from_origin_auth_failure(self):
        policy = source.load_policy()
        blocked = FakeOpener(urllib.error.URLError("Tunnel connection failed: 403 Forbidden"))
        result = source.probe(policy, blocked)
        self.assertFalse(result["origin_reached"])
        self.assertEqual(result["failure"], "outbound_proxy_connect_403")
        self.assertEqual(blocked.request.full_url, source.API_BASE_URL)
        self.assertEqual(blocked.request.header_items(), [])
        origin = FakeOpener(urllib.error.HTTPError(source.API_BASE_URL, 401, "Unauthorized", {}, io.BytesIO()))
        self.assertEqual(source.probe(policy, origin),
                         {"origin_reached": True, "http_status": 401, "authenticated": False})

    def test_probe_never_follows_redirects(self):
        self.assertIsNone(source.NoRedirects().redirect_request(
            None, None, 302, "Found", {}, "https://other.example/api"))


if __name__ == "__main__":
    unittest.main()
