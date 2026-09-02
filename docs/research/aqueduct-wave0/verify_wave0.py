#!/usr/bin/env python3
from __future__ import annotations

import csv
import json
from pathlib import Path

EXPECTED_COUNTS = {"control": 8, "offline": 13, "live": 11}
EXPECTED_NAMES = {
    "control": {
        "settings-default", "heartbeat", "stats-empty", "favicon", "root-redirect",
        "local-addon-manifest", "settings-update", "settings-effective",
    },
    "offline": {
        "hls-url-master", "probe-url", "subtitles-srt", "proxy-range",
        "zip-create", "zip-head", "zip-range", "tar-create", "tar-head", "tar-range",
        "tgz-create", "tgz-head", "tgz-range",
    },
    "live": {
        "engine-create", "stats-early-0", "metadata-race-range", "media-head",
        "range-open-ended", "range-bounded", "range-seek", "range-invalid",
        "stats-after-ranges", "engine-remove", "stats-after-remove",
    },
}


def validate(root: Path) -> list[str]:
    failures: list[str] = []
    required = [
        root / "UPSTREAM-AUTHORITY.md",
        root / "SOURCE-PORT-MATRIX.csv",
        root / "TANKOBAN2-REGRESSION-CORPUS.md",
        root / "oracle" / "capture_oracle.py",
        root / "oracle" / "fixture_server.py",
        root / "compare_candidate.py",
    ]
    for path in required:
        if not path.is_file():
            failures.append(f"missing artifact: {path.name}")
    matrix = root / "SOURCE-PORT-MATRIX.csv"
    if matrix.is_file():
        with matrix.open(newline="", encoding="utf-8") as handle:
            rows = list(csv.DictReader(handle))
        routes = [row for row in rows if row.get("kind") == "route"]
        internal = [row for row in rows if row.get("kind") == "internal"]
        if len(routes) != 90:
            failures.append(f"source-port matrix route count: expected 90, got {len(routes)}")
        if len(internal) != 18:
            failures.append(f"source-port matrix internal count: expected 18, got {len(internal)}")
        bad = [row for row in rows if row.get("decision") not in {"PORT", "PORT VIA PROVEN EQUIVALENT"}]
        if bad:
            failures.append(f"source-port matrix has {len(bad)} unresolved decisions")
    corpus = root / "TANKOBAN2-REGRESSION-CORPUS.md"
    if corpus.is_file():
        text = corpus.read_text(encoding="utf-8")
        for index in range(1, 11):
            token = f"R{index:02d}"
            if token not in text:
                failures.append(f"regression corpus missing {token}")
    for suite, expected_count in EXPECTED_COUNTS.items():
        path = root / "oracle" / "golden" / f"{suite}.json"
        if not path.is_file():
            failures.append(f"missing golden capture: {suite}")
            continue
        document = json.loads(path.read_text(encoding="utf-8"))
        if document.get("startup_handshake_seen") is not True:
            failures.append(f"{suite}: startup handshake not proven")
        if document.get("shutdown_clean") is not True:
            failures.append(f"{suite}: shutdown cleanup not proven")
        records = document.get("records", [])
        if len(records) != expected_count:
            failures.append(f"{suite}: expected {expected_count} records, got {len(records)}")
        names = {record.get("name") for record in records}
        if names != EXPECTED_NAMES[suite]:
            failures.append(f"{suite}: record-name set differs from contract")
    live = root / "oracle" / "golden" / "live.json"
    if live.is_file():
        records = {r["name"]: r for r in json.loads(live.read_text(encoding="utf-8"))["records"]}
        expected_status = {
            "engine-create": 200,
            "metadata-race-range": 206,
            "media-head": 200,
            "range-open-ended": 206,
            "range-bounded": 206,
            "range-seek": 206,
            "range-invalid": 200,
            "engine-remove": 200,
        }
        for name, status in expected_status.items():
            actual = records.get(name, {}).get("response", {}).get("status")
            if actual != status:
                failures.append(f"live {name}: expected HTTP {status}, got {actual}")
        for name in ("range-open-ended", "range-invalid"):
            if records.get(name, {}).get("response", {}).get("body_truncated") is not True:
                failures.append(f"live {name}: expected bounded capture")
    return failures


def main() -> int:
    root = Path(__file__).resolve().parent
    failures = validate(root)
    if failures:
        print("RED Wave 0 verification")
        for failure in failures:
            print(" -", failure)
        return 1
    print("GREEN Wave 0 verification")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
