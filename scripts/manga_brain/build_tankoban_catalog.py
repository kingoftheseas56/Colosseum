"""build_tankoban_catalog.py — bake the offline Tankoban volume catalogue into
data/tankoban_catalog.db.

Catalogue-independence Slice 1 (2026-08-20, Tankoban catalogue independence plan
under docs/superpowers/plans/): the app's provider-free, offline source of
per-series volume counts and covers — the data spine the C++ seam
(native/engine/TankobanCatalog.{h,cpp}) reads and every later slice builds on.
Same doctrine as `scripts/anime_brain/build_mal_db.py` (the MAL catalogue) and
the GCD comics spine: dumps in, one SQLite artifact out, the app reads only the
artifact.

Input 1 (required) — the arc's spine, one JSON object per line:
    {"rank": 1, "malId": 2, "title": "Berserk", "volumes": 0, ...}
`volumes` is MAL's own volume count: 0/null for an ongoing series (the count
gap named in the plan — an ongoing series' true shelf size is unknown until
the BookWalker harvest lands), a positive integer for a completed one.

Input 2 (optional, --covers) — the BookWalker/mymangaindex per-volume cover
harvest, not yet landed as of this slice (arc-side lane, external dependency).
One JSON object per line, either shape:
    {"malId": 2, "number": "3", "coverUrl": "https://...", "name": "..."}
        a single volume's cover/name fact.
    {"malId": 2, "volumeCount": 41}
        a series-level fact: the harvest's own authoritative volume count for
        that series (overrides the spine's MAL count; flips count_basis to
        "bookwalker"). May appear on its own line or merged onto a volume-row
        line — both are read the same way (whichever keys are present apply).
A --covers path that does not exist is a no-op — the db bakes MAL-only, this
slice's own path (fixture data lands later per the plan's dependency note).

Output: data/tankoban_catalog.db
    series(mal_id INTEGER PRIMARY KEY, volume_count INTEGER NOT NULL,
           count_basis TEXT NOT NULL)   -- count_basis: "mal" | "bookwalker"
    volumes(mal_id INTEGER NOT NULL, number TEXT NOT NULL,
            cover_url TEXT NOT NULL DEFAULT '', name TEXT NOT NULL DEFAULT '')
        -- volume numbers are TEXT, never float-collapsed (MangaTankobanTypes
        -- law). A volumes row is emitted ONLY when a cover or name exists for
        -- that number — a plain count with no harvest data emits NO volumes
        -- rows; the C++ seam synthesizes "1".."N" from the count instead.

Usage:
    python scripts/manga_brain/build_tankoban_catalog.py
    python scripts/manga_brain/build_tankoban_catalog.py --covers path/to/harvest.jsonl
    python scripts/manga_brain/build_tankoban_catalog.py --spine path/to/spine.jsonl
"""
from __future__ import annotations

import argparse
import json
import os
import sqlite3
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DB_PATH = os.path.join(REPO, "data", "tankoban_catalog.db")

# The arc's spine, produced outside this repo (Preflight-Architect workspace).
DEFAULT_SPINE = os.environ.get("COLOSSEUM_TANKOBAN_SPINE")


def load_spine(path: str) -> dict:
    """malId -> {"volume_count": int, "count_basis": "mal"}."""
    out = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            obj = json.loads(line)
            mal_id = obj.get("malId")
            if mal_id is None:
                continue
            try:
                mal_id = int(mal_id)
            except (TypeError, ValueError):
                continue
            volumes = obj.get("volumes")
            try:
                volume_count = int(volumes) if volumes else 0
            except (TypeError, ValueError):
                volume_count = 0
            out[mal_id] = {"volume_count": max(0, volume_count), "count_basis": "mal"}
    return out


def load_covers(path: str):
    """Returns (series_overrides, volume_rows).
    series_overrides: malId -> volume_count (int), from harvest-supplied counts.
    volume_rows: list of (malId, number, cover_url, name) with a non-empty
    cover or name — rows with neither are dropped (nothing to overlay)."""
    series_overrides: dict = {}
    volume_rows = []
    if not path or not os.path.exists(path):
        return series_overrides, volume_rows

    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            mal_id = obj.get("malId")
            if mal_id is None:
                continue
            try:
                mal_id = int(mal_id)
            except (TypeError, ValueError):
                continue

            if "volumeCount" in obj and obj.get("volumeCount"):
                try:
                    series_overrides[mal_id] = int(obj["volumeCount"])
                except (TypeError, ValueError):
                    pass

            number = obj.get("number")
            if number is None:
                continue  # a series-count-only line, no volume row to emit
            number = str(number).strip()
            if not number:
                continue
            cover_url = str(obj.get("coverUrl") or "").strip()
            name = str(obj.get("name") or "").strip()
            if not cover_url and not name:
                continue  # nothing to overlay — the C++ seam synthesizes the bare number
            volume_rows.append((mal_id, number, cover_url, name))

    return series_overrides, volume_rows


def bake(spine_path: str, covers_path: str | None) -> int:
    series = load_spine(spine_path)
    series_overrides, volume_rows = load_covers(covers_path)

    for mal_id, count in series_overrides.items():
        entry = series.setdefault(mal_id, {"volume_count": 0, "count_basis": "mal"})
        entry["volume_count"] = max(0, count)
        entry["count_basis"] = "bookwalker"

    # A volume row for a malId the spine never carried still needs a series
    # row to hang off (foreign-key-free schema, but the C++ seam looks up
    # seriesInfo() first) — synthesize one at count 0/"mal" if truly absent.
    for mal_id, _number, _cover, _name in volume_rows:
        series.setdefault(mal_id, {"volume_count": 0, "count_basis": "mal"})

    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    tmp = DB_PATH + ".building"
    if os.path.exists(tmp):
        os.remove(tmp)
    db = sqlite3.connect(tmp)
    db.executescript(
        """
        CREATE TABLE series (
            mal_id INTEGER PRIMARY KEY,
            volume_count INTEGER NOT NULL,
            count_basis TEXT NOT NULL
        );
        CREATE TABLE volumes (
            mal_id INTEGER NOT NULL,
            number TEXT NOT NULL,
            cover_url TEXT NOT NULL DEFAULT '',
            name TEXT NOT NULL DEFAULT ''
        );
        """
    )
    db.executemany(
        "INSERT INTO series (mal_id, volume_count, count_basis) VALUES (?, ?, ?)",
        [(mid, e["volume_count"], e["count_basis"]) for mid, e in series.items()],
    )
    db.executemany(
        "INSERT INTO volumes (mal_id, number, cover_url, name) VALUES (?, ?, ?, ?)",
        volume_rows,
    )
    db.execute("CREATE INDEX volumes_mal_id_idx ON volumes (mal_id)")
    db.commit()
    db.close()

    if os.path.exists(DB_PATH):
        os.remove(DB_PATH)
    os.rename(tmp, DB_PATH)

    bookwalker_count = sum(1 for e in series.values() if e["count_basis"] == "bookwalker")
    print(
        f"baked: {DB_PATH} — {len(series)} series rows "
        f"({bookwalker_count} bookwalker-basis, {len(series) - bookwalker_count} mal-basis), "
        f"{len(volume_rows)} volume rows"
    )
    return len(series)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--spine", default=DEFAULT_SPINE, help="path to spine_top10000.jsonl")
    ap.add_argument("--covers", default=None, help="path to the BookWalker/mymangaindex "
                     "cover harvest jsonl (optional; skipped if absent)")
    args = ap.parse_args()

    if not args.spine:
        print("error: pass --spine or set COLOSSEUM_TANKOBAN_SPINE", file=sys.stderr)
        return 1
    if not os.path.exists(args.spine):
        print(f"error: spine not found at {args.spine}", file=sys.stderr)
        return 1

    row_count = bake(args.spine, args.covers)
    if row_count < 9000:
        print(f"error: only {row_count} series rows baked (< 9000 minimum)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
