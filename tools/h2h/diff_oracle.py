#!/usr/bin/env python3
"""Compare two persistent JSON-line gameplay adapters with the same trace.

Adapters normalize engine-specific APIs into public state, not private device
identity or cloud credentials. Every state field is compared; there is no ignore
list. An adapter's identity response binds the actual source/oracle SHA256.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import queue
import re
import subprocess
import sys
import threading
from pathlib import Path

PROTOCOL = "stayc.gameplay-oracle.v1"
SHA256 = re.compile(r"[a-f0-9]{64}")


def json_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def read_json(text):
    return json.loads(text, object_pairs_hook=json_object,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError(f"invalid JSON number {value}")))


class Adapter:
    def __init__(self, path: Path, timeout: float):
        self.path, self.timeout = path.resolve(strict=True), timeout
        if self.path.suffix != ".py" or not self.path.is_file():
            raise ValueError("adapter must be a Python script")
        self.process = subprocess.Popen([sys.executable, str(self.path)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                        encoding="utf-8", bufsize=1)
        self.responses = queue.Queue()
        self.errors = []

        def read_output():
            try:
                for line in self.process.stdout:
                    if len(line) > 1024 * 1024:
                        self.responses.put(ValueError("adapter response exceeds 1 MiB"))
                        return
                    self.responses.put(line)
            finally:
                self.responses.put(EOFError("adapter ended without a response"))

        def read_errors():
            for line in self.process.stderr:
                self.errors.append(line.rstrip()[:2048])
                del self.errors[:-8]

        threading.Thread(target=read_output, daemon=True).start()
        threading.Thread(target=read_errors, daemon=True).start()

    def request(self, message):
        if self.process.poll() is not None:
            raise ValueError(f"adapter exited with code {self.process.returncode}: {self.path.name}")
        self.process.stdin.write(json.dumps(message, ensure_ascii=False, allow_nan=False) + "\n")
        self.process.stdin.flush()
        try:
            response = self.responses.get(timeout=self.timeout)
        except queue.Empty as error:
            raise ValueError(f"adapter timed out: {self.path.name}") from error
        if isinstance(response, Exception):
            raise ValueError(f"adapter protocol failed: {self.path.name}: {response}")
        response = read_json(response)
        if not isinstance(response, dict):
            raise ValueError("adapter response must be an object")
        return response

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()


def differences(expected, actual, path="$"):
    """Compare JSON values strictly, including field presence, array length and type."""
    if type(expected) is not type(actual):
        return [{"path": path, "oracle": expected, "native": actual}]
    if isinstance(expected, dict):
        results = []
        for key in sorted(expected.keys() | actual.keys()):
            child = f"{path}.{key}"
            if key not in expected or key not in actual:
                results.append({"path": child, "oracle_present": key in expected,
                                "native_present": key in actual})
            else:
                results.extend(differences(expected[key], actual[key], child))
        return results
    if isinstance(expected, list):
        if len(expected) != len(actual):
            return [{"path": path, "oracle_length": len(expected), "native_length": len(actual)}]
        return [difference for i, (left, right) in enumerate(zip(expected, actual))
                for difference in differences(left, right, f"{path}[{i}]")]
    return [] if expected == actual else [{"path": path, "oracle": expected, "native": actual}]


def compare_trace(trace, oracle: Adapter, native: Adapter):
    if not isinstance(trace, dict) or trace.get("schema") != PROTOCOL:
        raise ValueError("unsupported trace schema")
    digest = trace.get("oracle_sha256", "")
    if not isinstance(digest, str) or not SHA256.fullmatch(digest):
        raise ValueError("trace must pin the real oracle SHA256")
    steps = trace.get("steps")
    if not isinstance(steps, list) or not steps or len(steps) > 100000:
        raise ValueError("trace must contain 1..100000 steps")
    identities = [adapter.request({"op": "identity", "protocol": PROTOCOL}) for adapter in (oracle, native)]
    for identity in identities:
        source_hash = identity.get("source_sha256", "")
        if identity.get("protocol") != PROTOCOL or not isinstance(source_hash, str) or not SHA256.fullmatch(source_hash):
            raise ValueError("adapter identity must report protocol and actual source SHA256")
    if identities[0]["source_sha256"] != digest:
        raise ValueError("oracle adapter SHA256 differs from pinned trace")
    results = []
    for index, step in enumerate(steps):
        if not isinstance(step, dict) or not isinstance(step.get("op"), str) or step["op"] == "identity":
            raise ValueError(f"invalid trace step {index}")
        expected, actual = (adapter.request(step) for adapter in (oracle, native))
        mismatch = differences(expected, actual)
        if mismatch:
            results.append({"step": index, "request": step, "differences": mismatch})
    return {"schema": PROTOCOL, "result": "FAIL" if results else "PASS",
            "steps": len(steps), "oracle_identity": identities[0], "native_identity": identities[1],
            "mismatches": results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--oracle-adapter", required=True, type=Path)
    parser.add_argument("--native-adapter", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=10)
    args = parser.parse_args()
    adapters = []
    try:
        if not 0 < args.timeout <= 60:
            raise ValueError("timeout must be in (0,60] seconds")
        trace_bytes = args.trace.read_bytes()
        for path in (args.oracle_adapter, args.native_adapter):
            adapters.append(Adapter(path, args.timeout))
        report = compare_trace(read_json(trace_bytes), *adapters)
        report["trace_sha256"] = hashlib.sha256(trace_bytes).hexdigest()
        report["adapter_sha256"] = {role: hashlib.sha256(adapter.path.read_bytes()).hexdigest()
                                    for role, adapter in zip(("oracle", "native"), adapters)}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + "\n")
        print(f"Gameplay differential: {report['result']} ({report['steps']} steps, {len(report['mismatches'])} mismatching responses)")
        return int(report["result"] != "PASS")
    except (OSError, UnicodeError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2
    finally:
        for adapter in adapters:
            adapter.close()


if __name__ == "__main__":
    raise SystemExit(main())
