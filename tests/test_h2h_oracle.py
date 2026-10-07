#!/usr/bin/env python3
"""Test trace comparisons and pinned oracle identities without an oracle fixture."""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("diff_oracle", ROOT / "tools/h2h/diff_oracle.py")
ORACLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ORACLE)

ADAPTER = '''import json,sys
clock=0
for raw in sys.stdin:
 request=json.loads(raw)
 if request['op']=='identity':
  response={'protocol':'stayc.gameplay-oracle.v1','source_sha256':'%s','engine':'synthetic-test-only'}
 else:
  clock+=request.get('ms',0)
  response={'state':{'clock':clock,'coins':%s,'wardrobe_preview':0,'owned':[True,False]}}
 print(json.dumps(response),flush=True)
'''


class OracleHarnessTest(unittest.TestCase):
    def adapters(self, oracle_digest="1" * 64, native_coins=10):
        temporary = tempfile.TemporaryDirectory(prefix="passport-oracle-harness-")
        self.addCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        result = []
        for role, digest, coins in (("oracle", oracle_digest, 10), ("native", "2" * 64, native_coins)):
            path = directory / (role + ".py")
            path.write_text(ADAPTER % (digest, coins))
            adapter = ORACLE.Adapter(path, 2)
            self.addCleanup(adapter.close)
            result.append(adapter)
        return result

    def trace(self):
        return {"schema": ORACLE.PROTOCOL, "oracle_sha256": "1" * 64,
                "steps": [{"op": "init"}, {"op": "tick", "ms": 500}, {"op": "tick", "ms": 900}]}

    def test_persistent_adapters_compare_every_response(self):
        report = ORACLE.compare_trace(self.trace(), *self.adapters())
        self.assertEqual(report["result"], "PASS")
        self.assertEqual(report["steps"], 3)
        self.assertNotEqual(report["oracle_identity"], report["native_identity"])

    def test_wallet_mismatch_is_reported_at_each_step(self):
        report = ORACLE.compare_trace(self.trace(), *self.adapters(native_coins=9))
        self.assertEqual(report["result"], "FAIL")
        self.assertEqual([m["step"] for m in report["mismatches"]], [0, 1, 2])
        self.assertEqual(report["mismatches"][0]["differences"], [{"path": "$.state.coins", "oracle": 10, "native": 9}])

    def test_unpinned_or_wrong_oracle_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "SHA256 differs"):
            ORACLE.compare_trace(self.trace(), *self.adapters(oracle_digest="3" * 64))
        trace = self.trace()
        del trace["oracle_sha256"]
        with self.assertRaisesRegex(ValueError, "pin the real"):
            ORACLE.compare_trace(trace, None, None)

    def test_extra_missing_fields_array_length_and_type_are_compared(self):
        result = ORACLE.differences({"life_ms": 0, "owned": [False], "coins": 1},
                                    {"life_ms": False, "owned": [], "unknown": 2})
        self.assertEqual(len(result), 4)
        self.assertEqual({x["path"] for x in result}, {"$.life_ms", "$.owned", "$.coins", "$.unknown"})

    def test_duplicate_keys_and_nonfinite_numbers_are_rejected(self):
        for text in ('{"coins":1,"coins":2}', '{"coins":NaN}', '{"clock":Infinity}'):
            with self.assertRaises(ValueError):
                ORACLE.read_json(text)

    def test_invalid_steps_and_fake_identity_are_rejected(self):
        trace = self.trace()
        trace["steps"] = [{"op": "identity"}]
        with self.assertRaisesRegex(ValueError, "invalid trace step"):
            ORACLE.compare_trace(trace, *self.adapters())
        with self.assertRaisesRegex(ValueError, "actual source SHA256"):
            ORACLE.compare_trace(self.trace(), *self.adapters(oracle_digest="fake"))


if __name__ == "__main__":
    unittest.main()
