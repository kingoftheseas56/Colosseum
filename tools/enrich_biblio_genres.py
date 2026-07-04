#!/usr/bin/env python3
"""Enrich the Biblio canonical graph with real Goodreads-shelf genres.

Source: goodreads_book_genres_initial.json.gz (UCSD Book Graph) — genre tags extracted from users'
popular shelves by keyword matching, keyed by book_id. Every seed work already carries its Goodreads
book_id in source_assertion.source_key ('book_id:work_id'), so we join on that.

Writes, per work, source='goodreads_shelves':
  - predicate='genre'   value=<primary display genre>       (the one label the detail page leads with)
  - predicate='genres'  value=<comma-joined top display genres>  (for optional chips)
and drops the earlier junk readanybook 'genres' scrape.

Idempotent: clears prior goodreads_shelves genre rows before reinserting. Prints the resulting
distribution so the tagging can be eyeballed before it ships.
"""
from __future__ import annotations

import argparse
import collections
import gzip
import json
import sqlite3
from pathlib import Path

# UCSD's 10 shelf-derived genres → the app's genre vocabulary (aligns with BiblioGenreApi where it can).
UCSD2DISPLAY = {
    "fiction": "Fiction & Literature",
    "fantasy, paranormal": "Sci-Fi & Fantasy",
    "mystery, thriller, crime": "Mysteries & Thrillers",
    "romance": "Romance",
    "young-adult": "Young Adult",
    "history, historical fiction, biography": "History & Biography",
    "comics, graphic": "Comics & Graphic Novels",
    "children": "Children's",
    "non-fiction": "Nonfiction",
    "poetry": "Poetry",
}


def pick_genres(genres: dict[str, int]) -> tuple[str | None, list[str]]:
    """Primary genre + ordered display list. 'fiction' is a near-universal catch-all, so a specific
    genre wins unless fiction dominates it (>2x) — that keeps true literary fiction as Fiction while
    giving genre books their real label."""
    if not genres:
        return None, []
    ordered = sorted(genres.items(), key=lambda kv: -int(kv[1]))
    primary_key = ordered[0][0]
    if primary_key == "fiction" and len(ordered) >= 2:
        second_key, second_ct = ordered[1]
        if int(second_ct) >= 0.5 * int(ordered[0][1]):
            primary_key = second_key
    # display list: primary first, then the rest in count order, deduped, capped at 3
    disp: list[str] = []
    for k, _ in [(primary_key, 0)] + ordered:
        d = UCSD2DISPLAY.get(k, k)
        if d not in disp:
            disp.append(d)
    return UCSD2DISPLAY.get(primary_key, primary_key), disp[:3]


def load_genre_index(gz_path: Path, wanted: set[str]) -> dict[str, dict[str, int]]:
    out: dict[str, dict[str, int]] = {}
    with gzip.open(gz_path, "rt", encoding="utf-8") as fh:
        for line in fh:
            rec = json.loads(line)
            b = str(rec.get("book_id", ""))
            if b in wanted:
                out[b] = rec.get("genres", {})
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", default="tools/biblio_canonical_graph_goodreads_2000.sqlite")
    ap.add_argument("--genres-gz", default="agents/dataset-probes/goodreads_book_genres_initial.json.gz")
    ap.add_argument("--commit", action="store_true", help="write to the DB (default is a dry run)")
    args = ap.parse_args()

    conn = sqlite3.connect(args.db)
    c = conn.cursor()

    # work_id -> book_id, from the graph's own goodreads_seed provenance
    work_book: dict[int, str] = {}
    for wid, skey in c.execute(
        "SELECT subject_id, source_key FROM source_assertion "
        "WHERE source='goodreads_seed' AND predicate='title'"
    ):
        book_id = str(skey or "").split(":")[0]
        if book_id:
            work_book[int(wid)] = book_id
    print(f"works with a goodreads book_id: {len(work_book)}")

    genre_idx = load_genre_index(Path(args.genres_gz), set(work_book.values()))
    print(f"books found in genres file: {len(genre_idx)}")

    dist = collections.Counter()
    tagged = 0
    rows: list[tuple[int, str, str, str]] = []  # (work_id, primary, genres_csv, evidence)
    for wid, book_id in work_book.items():
        primary, disp = pick_genres(genre_idx.get(book_id, {}))
        if not primary:
            continue
        tagged += 1
        dist[primary] += 1
        rows.append((wid, primary, ", ".join(disp), json.dumps(genre_idx.get(book_id, {}))))

    print(f"\nworks tagged: {tagged}/{len(work_book)}")
    print("primary-genre distribution:")
    for g, n in dist.most_common():
        print(f"   {g:26} {n}")

    if not args.commit:
        print("\n(dry run — pass --commit to write)")
        return 0

    with conn:
        # drop the junk readanybook 'genres' scrape and any prior shelf pass
        conn.execute("DELETE FROM source_assertion WHERE predicate IN ('genre','genres') "
                     "AND source IN ('goodreads_shelves','readanybook')")
        for wid, primary, csv_genres, evidence in rows:
            conn.execute(
                "INSERT INTO source_assertion(source, source_key, subject_type, subject_id, predicate, value, confidence, evidence_json) "
                "VALUES ('goodreads_shelves', ?, 'work', ?, 'genre', ?, 0.80, ?)",
                (work_book[wid], wid, primary, evidence),
            )
            conn.execute(
                "INSERT INTO source_assertion(source, source_key, subject_type, subject_id, predicate, value, confidence, evidence_json) "
                "VALUES ('goodreads_shelves', ?, 'work', ?, 'genres', ?, 0.80, ?)",
                (work_book[wid], wid, csv_genres, evidence),
            )
    print(f"\ncommitted {tagged} works × 2 assertions to {args.db}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
