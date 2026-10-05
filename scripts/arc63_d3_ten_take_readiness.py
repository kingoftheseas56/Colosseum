#!/usr/bin/env python3
"""Arc 63-D3 hosted ten-take readiness reproducibility proof.

Runs the accepted D2 production-component readiness suites ten consecutive times.
The Shelves suite emits one deterministic [D2BSHOT] JSON witness per run. D3
requires every take to be green and every normalized witness to be identical.

This is a readiness/reproducibility proof, not a substitute for the later
assembled-app visual/capture replay.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


TOTALS_RE = re.compile(
    r"Totals:\s+(?P<passed>\d+) passed,\s+(?P<failed>\d+) failed,\s+"
    r"(?P<skipped>\d+) skipped"
)
WITNESS_RE = re.compile(r"\[D2BSHOT\]\s+(\{.*\})")


class ProofError(RuntimeError):
    pass


def run_test(runner: Path, repo: Path, test_path: str, timeout: int) -> tuple[str, dict[str, int]]:
    cmd = [str(runner), "-input", str(repo / test_path)]
    proc = subprocess.run(
        cmd,
        cwd=repo,
        env=os.environ.copy(),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        check=False,
    )
    output = proc.stdout or ""
    sys.stdout.write(output)
    if proc.returncode != 0:
        raise ProofError(f"{test_path} exited {proc.returncode}")

    totals = TOTALS_RE.findall(output)
    if len(totals) != 1:
        raise ProofError(f"{test_path} emitted {len(totals)} Totals lines, expected 1")
    passed, failed, skipped = map(int, totals[0])
    if failed != 0 or skipped != 0:
        raise ProofError(
            f"{test_path} not clean: passed={passed} failed={failed} skipped={skipped}"
        )
    return output, {"passed": passed, "failed": failed, "skipped": skipped}


def normalize_witness(raw: dict[str, Any]) -> dict[str, Any]:
    members = raw.get("members")
    if not isinstance(members, list):
        raise ProofError("D2BSHOT witness has no members list")

    normalized_members: list[dict[str, Any]] = []
    for index, member in enumerate(members):
        if not isinstance(member, dict):
            raise ProofError(f"D2BSHOT member {index} is not an object")
        normalized_members.append(
            {
                "kind": str(member.get("kind", "")),
                "key": str(member.get("key", "")),
                "source": str(member.get("source", "")),
                "status": str(member.get("status", "")),
            }
        )

    try:
        sweep_bottom = round(float(raw["sweepBottom"]), 2)
    except (KeyError, TypeError, ValueError) as exc:
        raise ProofError("D2BSHOT witness has invalid sweepBottom") from exc

    witness = {
        "required": int(raw.get("required", -1)),
        "ready": int(raw.get("ready", -1)),
        "failed": int(raw.get("failed", -1)),
        "endObject": str(raw.get("endObject", "")),
        "sweepBottom": sweep_bottom,
        "members": normalized_members,
    }

    if witness["required"] != 15 or witness["ready"] != 15 or witness["failed"] != 0:
        raise ProofError(
            "AOT readiness witness is not 15 required / 15 ready / 0 failed: "
            f"{witness['required']}/{witness['ready']}/{witness['failed']}"
        )
    if witness["endObject"] != "shelvesMedium_films":
        raise ProofError(
            f"AOT readiness witness ends at {witness['endObject']!r}, expected shelvesMedium_films"
        )
    if abs(witness["sweepBottom"] - 1812.25) > 0.01:
        raise ProofError(
            f"AOT readiness sweepBottom={witness['sweepBottom']}, expected 1812.25"
        )
    if len(witness["members"]) != 15:
        raise ProofError(
            f"AOT readiness witness carries {len(witness['members'])} members, expected 15"
        )
    if any(member["status"] != "ready" for member in witness["members"]):
        raise ProofError("AOT readiness witness contains a non-ready member")
    return witness


def witness_from_output(output: str) -> dict[str, Any]:
    matches = WITNESS_RE.findall(output)
    if len(matches) != 1:
        raise ProofError(f"Shelves run emitted {len(matches)} D2BSHOT witnesses, expected 1")
    try:
        raw = json.loads(matches[0])
    except json.JSONDecodeError as exc:
        raise ProofError("D2BSHOT witness is not valid JSON") from exc
    return normalize_witness(raw)


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qmltestrunner", required=True)
    parser.add_argument("--repo", default=".")
    parser.add_argument("--takes", type=int, default=10)
    parser.add_argument("--output", default="")
    parser.add_argument("--timeout-seconds", type=int, default=90)
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    runner = Path(args.qmltestrunner).resolve()
    if args.takes != 10:
        raise ProofError("canonical Arc 63-D3 proof requires exactly 10 takes")
    if not runner.is_file():
        raise ProofError(f"qmltestrunner not found: {runner}")

    universes_test = "tests/qml/tst_arc63_d2_universes_readiness.qml"
    shelves_test = "tests/qml/tst_arc63_d2_shelves_readiness.qml"
    for rel in (universes_test, shelves_test):
        if not (repo / rel).is_file():
            raise ProofError(f"required D2 readiness suite missing: {rel}")

    take_records: list[dict[str, Any]] = []
    canonical_witness: dict[str, Any] | None = None
    canonical_hash = ""

    for take in range(1, args.takes + 1):
        print(f"\n=== ARC63_D3_TAKE {take:02d}/{args.takes:02d} ===", flush=True)
        universes_output, universes_totals = run_test(
            runner, repo, universes_test, args.timeout_seconds
        )
        shelves_output, shelves_totals = run_test(
            runner, repo, shelves_test, args.timeout_seconds
        )

        if universes_totals["passed"] != 7:
            raise ProofError(
                f"take {take}: Universes readiness passed={universes_totals['passed']}, expected 7"
            )
        if shelves_totals["passed"] != 10:
            raise ProofError(
                f"take {take}: Shelves readiness passed={shelves_totals['passed']}, expected 10"
            )

        witness = witness_from_output(shelves_output)
        digest = hashlib.sha256(canonical_bytes(witness)).hexdigest()
        if canonical_witness is None:
            canonical_witness = witness
            canonical_hash = digest
        elif witness != canonical_witness:
            raise ProofError(
                f"take {take}: readiness witness drifted from take 1 "
                f"({digest} != {canonical_hash})"
            )

        record = {
            "take": take,
            "universes": universes_totals,
            "shelves": shelves_totals,
            "witnessSha256": digest,
        }
        take_records.append(record)
        print(
            f"ARC63_D3_TAKE_OK take={take:02d} "
            f"universes={universes_totals['passed']}/7 shelves={shelves_totals['passed']}/10 "
            f"witness={digest}",
            flush=True,
        )

    assert canonical_witness is not None
    result = {
        "schema": "colosseum.arc63.d3.ten-take.v1",
        "takes": args.takes,
        "status": "pass",
        "witnessSha256": canonical_hash,
        "witness": canonical_witness,
        "takeResults": take_records,
    }

    if args.output:
        output = Path(args.output)
        if not output.is_absolute():
            output = repo / output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"ARC63_D3_RESULT={output}")

    print(f"ARC63_D3_TEN_TAKE_OK takes={args.takes} witness={canonical_hash}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ProofError, subprocess.TimeoutExpired) as exc:
        print(f"ARC63_D3_TEN_TAKE_FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
