#!/usr/bin/env python3
"""Verify the P08-T4 pause contract prose and reject clause mutations."""

from __future__ import annotations

import argparse
from pathlib import Path


CLAUSES = (
    "Only the current open",
    "generation may change this state; stale generations are rejected",
    "Repeating",
    "the current state is successful and has no additional effect",
    "Pausing gates and defers only new outbound ConnectAction work",
    "Existing",
    "peers, owned requests, metadata exchange, and incoming connections continue",
    "Resuming drains the deferred outbound connects",
    "must not map",
    "this scheduler signal to torrent_handle::pause()",
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
        raise SystemExit("P08-T4 semantic contract missing: " + repr(missing))

    if args.mutations:
        for index, clause in enumerate(CLAUSES, start=1):
            mutated = text.replace(clause, "P08_T4_REMOVED_CLAUSE", 1)
            if not missing_clauses(mutated):
                raise SystemExit(f"P08-T4 semantic mutation {index} escaped")
            print(f"P08T4_SEMANTIC_{index:02d} REJECTED")

    print(f"P08-T4 semantic contract PASS clauses={len(CLAUSES)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
