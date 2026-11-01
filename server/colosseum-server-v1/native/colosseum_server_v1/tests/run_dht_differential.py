"""Compare native real-UDP receipts with the exact pinned webpack DHT module."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

EXPECTED_SHA256 = "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--oracle", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    actual = hashlib.sha256(args.oracle.read_bytes()).hexdigest()
    if actual != EXPECTED_SHA256:
        raise SystemExit(f"Oracle SHA256 mismatch: {actual}")
    script = Path(__file__).with_name("dht_oracle_probe.cjs")
    oracle = json.loads(subprocess.check_output([args.node, str(script), str(args.oracle)], text=True, timeout=30))
    native = json.loads(subprocess.check_output([str(args.candidate), "--differential"], text=True, timeout=30))
    if native != oracle:
        for key in sorted(native.keys() | oracle.keys()):
            if native.get(key) != oracle.get(key):
                print(f"{key}: native={native.get(key)!r}; pinned={oracle.get(key)!r}")
        raise SystemExit("DHT exact-source differential FAILED")
    print(f"DHT exact-source differential PASS: {len(oracle)} observed fields; oracle SHA256 {actual}")
    print(json.dumps(native, sort_keys=True))


if __name__ == "__main__":
    main()
