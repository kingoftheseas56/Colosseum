#!/usr/bin/env python3
"""Verify the K05-A2 peer-count contract prose and reject clause mutations."""

from __future__ import annotations

import argparse
from pathlib import Path


CLAUSES = (
    "Returns no value when infoHash is absent or generation is not the",
    "registry's current generation",
    "A current engine with no peers returns",
    "zero counts",
    "Each peer contributes to exactly one lifecycle count",
)


def missing_clauses(text: str) -> list[str]:
    return [clause for clause in CLAUSES if clause not in text]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("header", type=Path)
    parser.add_argument("--mutations", action="store_true")
    args = parser.parse_args()
    text = args.header.read_text(encoding="utf-8")
    missing = missing_clauses(text)
    if missing:
        raise SystemExit("K05-A2 semantic contract missing: " + repr(missing))

    if args.mutations:
        for index, clause in enumerate(CLAUSES, start=1):
            mutated = text.replace(clause, "K05_A2_REMOVED_CLAUSE", 1)
            if not missing_clauses(mutated):
                raise SystemExit(f"K05-A2 semantic mutation {index} escaped")
            print(f"K05A2_SEMANTIC_{index:02d} REJECTED")

    print(f"K05-A2 semantic contract PASS clauses={len(CLAUSES)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
