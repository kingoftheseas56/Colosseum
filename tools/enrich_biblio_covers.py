#!/usr/bin/env python3
"""Enrich the Biblio canonical graph with cover-art URLs from the Goodreads dump.

The graph carries no cover art, so Top 10 / Top Series / detail pages render blank. Every seed work
keeps its Goodreads book_id in source_assertion.source_key ('book_id:work_id'); goodreads_books.json.gz
carries an `image_url` per book (Goodreads CDN, images.gr-assets.com — reachable). We join on book_id
and write, per work, source='goodreads' predicate='cover' value=<image_url>.

Idempotent: clears prior cover rows before reinserting. Prints coverage so it can be eyeballed.
"""
from __future__ import annotations

import argparse
import gzip
import json
import sqlite3
from pathlib import Path


def load_covers(gz_path: Path, wanted: set[str]) -> dict[str, str]:
    out: dict[str, str] = {}
    with gzip.open(gz_path, "rt", encoding="utf-8") as fh:
        for line in fh:
            try:
                rec = json.loads(line)
            except Exception:
                continue
            b = str(rec.get("book_id", ""))
            if b in wanted and b not in out:
                url = str(rec.get("image_url", "") or "")
                if url and "nophoto" not in url:
                    out[b] = url
            if len(out) == len(wanted):
                break
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", default="tools/biblio_canonical_graph_goodreads_2000.sqlite")
    ap.add_argument("--books-gz", default="agents/dataset-probes/goodreads_books.json.gz")
    ap.add_argument("--commit", action="store_true", help="write to the DB (default is a dry run)")
    args = ap.parse_args()

    conn = sqlite3.connect(args.db)
    c = conn.cursor()

    work_book: dict[int, str] = {}
    for wid, skey in c.execute(
        "SELECT subject_id, source_key FROM source_assertion "
        "WHERE source='goodreads_seed' AND predicate='title'"
    ):
        book_id = str(skey or "").split(":")[0]
        if book_id:
            work_book[int(wid)] = book_id
    print(f"works with a goodreads book_id: {len(work_book)}")

    covers = load_covers(Path(args.books_gz), set(work_book.values()))
    tagged = sum(1 for b in work_book.values() if b in covers)
    print(f"works with a cover URL: {tagged}/{len(work_book)} "
          f"({100.0 * tagged / max(1, len(work_book)):.1f}%)")
    for b in list(covers)[:3]:
        print("   sample:", covers[b])

    if not args.commit:
        print("\n(dry run — pass --commit to write)")
        return 0

    with conn:
        conn.execute("DELETE FROM source_assertion WHERE predicate='cover' AND source='goodreads'")
        for wid, book_id in work_book.items():
            url = covers.get(book_id)
            if not url:
                continue
            conn.execute(
                "INSERT INTO source_assertion(source, source_key, subject_type, subject_id, predicate, value, confidence) "
                "VALUES ('goodreads', ?, 'work', ?, 'cover', ?, 0.85)",
                (book_id, wid, url),
            )
    print(f"\ncommitted {tagged} cover assertions to {args.db}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
