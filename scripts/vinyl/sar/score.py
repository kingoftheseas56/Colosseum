"""Score a local item-affinity vector against the exported Vinyl SAR graph."""

import argparse
from collections import defaultdict
from contextlib import closing
import json
import math
from pathlib import Path
import sqlite3

LEVELS = ("recording", "artist", "release_group")


def score(database, level, affinity, top_n=10):
    if level not in LEVELS:
        raise ValueError("invalid item level")
    if top_n < 1:
        raise ValueError("top_n must be positive")
    if not isinstance(affinity, dict) or any(
        not isinstance(seed, str) or not seed.strip() or type(weight) not in (int, float)
        or not math.isfinite(weight) or weight < 0 for seed, weight in affinity.items()
    ):
        raise ValueError("affinity must be an object mapping item IDs to finite nonnegative weights")

    contributions = defaultdict(list)
    with closing(sqlite3.connect(database.resolve().as_uri() + "?mode=ro", uri=True)) as db:
        for seed, weight in sorted(affinity.items()):
            if not weight:
                continue
            for item, similarity in db.execute(
                "SELECT neighbour, similarity FROM neighbours WHERE level = ? AND item = ?",
                (level, seed),
            ):
                if item not in affinity:  # remove all seeds, not just self-edges
                    contribution = weight * similarity
                    if contribution > 0:
                        contributions[item].append({"seed": seed, "contribution": contribution})
    results = []
    for item, parts in contributions.items():
        total = sum(part["contribution"] for part in parts)
        if not math.isfinite(total):
            raise ValueError("score overflow; reduce affinity weights")
        results.append({"item": item, "score": total, "contributions": parts})
    return sorted(results, key=lambda row: (-row["score"], row["item"]))[:top_n]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", type=Path)
    parser.add_argument("affinity", type=Path, help="JSON object mapping item IDs to weights")
    parser.add_argument("--level", choices=LEVELS, required=True)
    parser.add_argument("--top-n", type=int, default=10)
    args = parser.parse_args()
    try:
        with args.affinity.open(encoding="utf-8") as f:
            affinity = json.load(f)
        results = score(args.database, args.level, affinity, args.top_n)
    except (OSError, ValueError, OverflowError, sqlite3.Error) as exc:
        parser.exit(1, f"score: {exc}\n")
    print(json.dumps(results, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
