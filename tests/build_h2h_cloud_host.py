"""Build actual cloud/main C files for host tests; never builds a firmware image.

The test SDK adapter uses host OpenSSL and pinned upstream cJSON. These are
standard test dependencies, not replacement board assets or production trust roots.
"""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
HASHES = {
    "cJSON.c": "298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d",
    "cJSON.h": "25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee",
}

def build(output, dependency_dir):
    dependency_dir.mkdir(parents=True, exist_ok=True)
    for name, digest in HASHES.items():
        target = dependency_dir / name
        if not target.exists():
            data = urllib.request.urlopen(
                f"https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.19/{name}", timeout=30
            ).read()
            if hashlib.sha256(data).hexdigest() != digest:
                raise RuntimeError(f"cJSON integrity mismatch: {name}")
            target.write_bytes(data)
        if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f"Existing cJSON integrity mismatch: {target}")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-DCONFIG_ESP_MAIN_TASK_STACK_SIZE=12288",
        "-Itests/pocket_stubs", "-Icomponents/pocket_crypto", f"-I{dependency_dir}", "-Imain/h2h",
        "tests/test_h2h_cloud_host.c", "main/h2h/h2h_pocket.c",
        "main/h2h/h2h_ownership.c", "main/h2h/h2h_model.c",
        "main/h2h/h2h_resource.c", "main/h2h/h2h_resource_runtime.c",
        "components/pocket_crypto/monocypher.c", "components/pocket_crypto/monocypher-ed25519.c",
        str(dependency_dir / "cJSON.c"), "-lcrypto", "-lm", "-o", str(output),
    ], cwd=ROOT, check=True)
    return output

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("/tmp/test_h2h_cloud_host"))
    parser.add_argument("--dependencies", type=Path, default=Path("/tmp/h2h-pocket-cjson-v1.7.19"))
    args = parser.parse_args()
    print(build(args.output, args.dependencies))
