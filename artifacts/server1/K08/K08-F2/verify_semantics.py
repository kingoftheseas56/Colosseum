#!/usr/bin/env python3
"""Verify the public K08-F2 lifecycle clauses and reject semantic mutations."""

from __future__ import annotations

import argparse
from pathlib import Path


CLAUSES = (
    "The first non-empty failure wins",
    "it closes the reader, cancels live reads, clears pending/completed work",
    "releases its scheduler selection",
    "makes the reason available exactly",
    "once through takeError()",
    "queued for takeData() remain valid",
    "late source completions are ignored",
    "Empty reasons and calls after EOF, close, or an earlier failure are no-ops",
    "void fail(std::string error);",
)


def verify(text: str) -> list[str]:
    return [clause for clause in CLAUSES if clause not in text]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("header", type=Path)
    parser.add_argument("--mutations", action="store_true")
    args = parser.parse_args()
    text = args.header.read_text(encoding="utf-8")
    missing = verify(text)
    if missing:
        raise SystemExit("K08-F2 semantic contract missing: " + repr(missing))

    if args.mutations:
        for index, clause in enumerate(CLAUSES, start=1):
            mutated = text.replace(clause, "K08_F2_REMOVED_CLAUSE", 1)
            if not verify(mutated):
                raise SystemExit(f"K08-F2 semantic mutation {index} escaped")
            print(f"K08F2_SEMANTIC_{index:02d} REJECTED")

    print(f"K08-F2 semantic contract PASS clauses={len(CLAUSES)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
