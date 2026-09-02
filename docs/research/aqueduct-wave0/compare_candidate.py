#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path

SUITES = ("control", "offline", "live")
DYNAMIC_PREFIXES = ("stats-", "engine-create")


def is_dynamic(name: str) -> bool:
    return name.startswith(DYNAMIC_PREFIXES)


def record_map(document: dict) -> dict[str, dict]:
    return {record["name"]: record for record in document.get("records", [])}


def response_signature(record: dict) -> dict:
    response = record.get("response", {})
    headers = dict(response.get("headers", {}))
    if is_dynamic(record.get("name", "")):
        headers.pop("content-length", None)
        return {"status": response.get("status"), "headers": headers, "body_truncated": response.get("body_truncated")}
    signature = {
        "status": response.get("status"),
        "headers": headers,
    }
    if "json" in response:
        signature["json"] = response["json"]
    elif "text" in response:
        signature["text"] = response["text"]
    else:
        signature["body_sha256"] = response.get("body_sha256")
        signature["body_length"] = response.get("body_length")
    return signature


def compare_documents(golden: dict, candidate: dict) -> list[str]:
    failures: list[str] = []
    if golden.get("suite") != candidate.get("suite"):
        failures.append(f"suite: expected {golden.get('suite')!r}, got {candidate.get('suite')!r}")
    for field in ("startup_handshake_seen", "shutdown_clean"):
        if field in golden and golden.get(field) != candidate.get(field):
            failures.append(f"{field}: expected {golden.get(field)!r}, got {candidate.get(field)!r}")
    expected = record_map(golden)
    actual = record_map(candidate)
    if set(expected) != set(actual):
        missing = sorted(set(expected) - set(actual))
        extra = sorted(set(actual) - set(expected))
        if missing:
            failures.append("missing records: " + ", ".join(missing))
        if extra:
            failures.append("extra records: " + ", ".join(extra))
    for name in sorted(set(expected) & set(actual)):
        left = response_signature(expected[name])
        right = response_signature(actual[name])
        if left != right:
            failures.append(f"{name}: response signature mismatch: expected {left!r}, got {right!r}")
    return failures


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare Colosseum Server captures to Wave-0 Stremio goldens.")
    parser.add_argument("candidate_dir", type=Path)
    parser.add_argument("--golden-dir", type=Path, default=Path(__file__).resolve().parent / "oracle" / "golden")
    args = parser.parse_args()
    failures: list[str] = []
    for suite in SUITES:
        golden_path = args.golden_dir / f"{suite}.json"
        candidate_path = args.candidate_dir / f"{suite}.json"
        if not candidate_path.is_file():
            failures.append(f"{suite}: candidate capture missing: {candidate_path}")
            continue
        failures.extend(f"{suite}: {item}" for item in compare_documents(load_json(golden_path), load_json(candidate_path)))
    if failures:
        print("RED candidate parity")
        for failure in failures:
            print(" -", failure)
        return 1
    print("GREEN candidate parity")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
