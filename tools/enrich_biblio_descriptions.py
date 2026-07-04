#!/usr/bin/env python3
"""Enrich the Biblio canonical graph with real Goodreads descriptions.

The canonical detail page shows a generated stub ("X is part of Y (book N)") because the graph has no
blurb. goodreads_books.json.gz carries a real `description` per book; join on the book_id the graph
already stores in source_assertion.source_key ('book_id:work_id'), clean the HTML, clamp to a readable
paragraph, and write source='goodreads' predicate='description'. Idempotent.
"""
from __future__ import annotations

import argparse
import gzip
import json
import re
import sqlite3
from pathlib import Path

TAG = re.compile(r"<[^>]+>")
WS = re.compile(r"\s+")


def clean(desc: str) -> str:
    d = TAG.sub(" ", desc or "")
    d = (d.replace("&amp;", "&").replace("&quot;", '"').replace("&#39;", "'")
         .replace("&#8217;", "’").replace("&#8220;", "“").replace("&#8221;", "”")
         .replace("&#8212;", "—").replace("&hellip;", "…").replace("&nbsp;", " "))
    d = WS.sub(" ", d).strip()
    if len(d) <= 700:
        return d
    cut = d[:700]
    end = max(cut.rfind(". "), cut.rfind("! "), cut.rfind("? "))
    return (cut[:end + 1] if end > 380 else cut.rsplit(" ", 1)[0] + "…").strip()


def load_descriptions(gz_path: Path, wanted: set[str]) -> dict[str, str]:
    out: dict[str, str] = {}
    with gzip.open(gz_path, "rt", encoding="utf-8") as fh:
        for line in fh:
            try:
                rec = json.loads(line)
            except Exception:
                continue
            b = str(rec.get("book_id", ""))
            if b in wanted and b not in out:
                d = clean(str(rec.get("description", "") or ""))
                if len(d) >= 40:
                    out[b] = d
            if len(out) == len(wanted):
                break
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", default="tools/biblio_canonical_graph_goodreads_2000.sqlite")
    ap.add_argument("--books-gz", default="agents/dataset-probes/goodreads_books.json.gz")
    ap.add_argument("--commit", action="store_true")
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
    print(f"works: {len(work_book)}")

    descs = load_descriptions(Path(args.books_gz), set(work_book.values()))
    tagged = sum(1 for b in work_book.values() if b in descs)
    print(f"works with a real description: {tagged}/{len(work_book)} "
          f"({100.0 * tagged / max(1, len(work_book)):.1f}%)")
    for b in list(descs)[:2]:
        print("   sample:", descs[b][:120], "…")

    if not args.commit:
        print("\n(dry run — pass --commit to write)")
        return 0

    with conn:
        conn.execute("DELETE FROM source_assertion WHERE predicate='description' AND source='goodreads'")
        for wid, book_id in work_book.items():
            d = descs.get(book_id)
            if not d:
                continue
            conn.execute(
                "INSERT INTO source_assertion(source, source_key, subject_type, subject_id, predicate, value, confidence) "
                "VALUES ('goodreads', ?, 'work', ?, 'description', ?, 0.85)",
                (book_id, wid, d),
            )
    print(f"\ncommitted {tagged} descriptions to {args.db}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
