#!/usr/bin/env python3
"""Check the host-only Pocket music source without reading credential values."""
import argparse
import json
import os
from pathlib import Path
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
API_BASE_URL = "https://chunyin.jilllees.top:8443/api"
API_KEY_ENV = "POCKET_MUSIC_API_KEY"
CREDENTIAL_SCOPE = "https://chunyin.jilllees.top:8443"
PROXY_HOST = "apps.randomdance.cn"


def load_policy(path=ROOT / "config/pocket-music-source.json"):
    policy = json.loads(path.read_text())
    expected = {
        "api_base_url": API_BASE_URL,
        "api_key_env": API_KEY_ENV,
        "credential_scope": CREDENTIAL_SCOPE,
        "permitted_proxy_host": PROXY_HOST,
        "proxy_route": None,
    }
    if policy != expected:
        raise ValueError("Use the approved direct source; no music proxy route is verified.")
    return policy


def readiness(policy, environment=None):
    environment = os.environ if environment is None else environment
    present = policy["api_key_env"] in environment
    return {
        **policy,
        "api_key_configured": present,
        "credential_values_read": False,
        "downloads": "not run; authentication contract and source versions need verification",
    }


class NoRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        # No credential or request is forwarded to an unverified route/origin.
        return None


def probe(policy, opener=None):
    """Probe the approved API root without auth, redirects or response-body reads."""
    if policy != load_policy():
        raise ValueError("Probe policy does not match the approved music source.")
    opener = opener or urllib.request.build_opener(NoRedirects())
    request = urllib.request.Request(policy["api_base_url"], method="GET")
    try:
        with opener.open(request, timeout=15) as response:
            return {"origin_reached": True, "http_status": response.status,
                    "authenticated": False}
    except urllib.error.HTTPError as error:
        status = error.code
        error.close()
        return {"origin_reached": True, "http_status": status,
                "authenticated": False}
    except urllib.error.URLError as error:
        tunnel = "Tunnel connection failed: 403" in str(error.reason)
        # Do not serialize arbitrary network exception text or proxy settings.
        return {"origin_reached": False, "http_status": None,
                "authenticated": False,
                "failure": "outbound_proxy_connect_403" if tunnel else "network_or_tls_failure"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", action="store_true",
                        help="check the direct API root without credentials")
    args = parser.parse_args()
    policy = load_policy()
    report = readiness(policy)
    if args.probe:
        report["network"] = probe(policy)
    print(json.dumps(report, indent=2))
    if args.probe and not report["network"]["origin_reached"]:
        return 3
    return 0 if report["api_key_configured"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
