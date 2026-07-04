#!/usr/bin/env python3
"""
goodreads_seed_builder.py

Build a large local seed CSV from the Goodreads dump files:
  - goodreads_books.json.gz
  - goodreads_book_authors.json.gz
  - goodreads_book_series.json.gz

This is seed generation for the LibGen metadata large pass. Goodreads rows are
treated as test expectations, not canonical Biblio truth.
"""

from __future__ import annotations

import argparse
import csv
import gzip
import html
import json
import re
from pathlib import Path
from typing import Any, Dict, Iterable, Iterator, List, Optional, Tuple


OUTPUT_FIELDS = [
    "title",
    "author",
    "expected_series",
    "expected_position",
    "franchise",
    "source",
    "book_id",
    "work_id",
    "series_id",
    "isbn13",
    "ratings_count",
    "average_rating",
]

ENGLISH_CODES = {"eng", "en", "en-us", "en-gb", "en-ca", "en-au"}

JUNK_TERMS = [
    "summary",
    "study guide",
    "workbook",
    "trivia",
    "unofficial",
    "analysis",
    "notes",
    "companion guide",
    "sparknotes",
    "cliffsnotes",
    "conversation",
    "dailybooks",
]

SERIES_SUFFIX_RE = re.compile(r"\(([^()]*#[^()]*)\)\s*$")
ONE_SERIES_RE = re.compile(r"^(.*?)\s*,?\s*#\s*([0-9][0-9A-Za-z.\-]*)\s*$")


def normalize(text: str) -> str:
    text = html.unescape(text or "").lower()
    text = text.replace("&", " and ")
    text = re.sub(r"[^a-z0-9]+", " ", text)
    return re.sub(r"\s+", " ", text).strip()


def clean_text(text: Any) -> str:
    return re.sub(r"\s+", " ", html.unescape(str(text or ""))).strip()


def to_int(value: Any, default: int = 0) -> int:
    try:
        return int(float(str(value or "").strip()))
    except Exception:
        return default


def open_jsonl_gz(path: Path) -> Iterator[Dict[str, Any]]:
    with gzip.open(path, "rt", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                yield json.loads(line)
            except json.JSONDecodeError:
                continue


def load_authors(path: Path) -> Dict[str, str]:
    authors: Dict[str, str] = {}
    for row in open_jsonl_gz(path):
        author_id = clean_text(row.get("author_id"))
        name = clean_text(row.get("name"))
        if author_id and name:
            authors[author_id] = name
    return authors


def load_series(path: Path) -> Dict[str, str]:
    series: Dict[str, str] = {}
    for row in open_jsonl_gz(path):
        series_id = clean_text(row.get("series_id"))
        title = clean_text(row.get("title"))
        if series_id and title:
            series[series_id] = title
    return series


def parse_title_series(title: str) -> Tuple[str, str, str, bool]:
    """Return display title, series title, ordinal, is_range."""
    title = clean_text(title)
    match = SERIES_SUFFIX_RE.search(title)
    if not match:
        return title, "", "", False

    display_title = title[: match.start()].strip()
    for chunk in match.group(1).split(";"):
        series_match = ONE_SERIES_RE.match(chunk.strip())
        if not series_match:
            continue
        series_name = clean_text(series_match.group(1))
        position = clean_text(series_match.group(2)).lstrip("0") or "0"
        return display_title, series_name, position, "-" in position

    return title, "", "", False


def is_english_or_allowed(language_code: str, include_unknown_language: bool) -> bool:
    code = clean_text(language_code).lower()
    if not code:
        return include_unknown_language
    return code in ENGLISH_CODES or code.startswith("en-")


def is_junk_title(title: str) -> bool:
    normalized = normalize(title)
    return any(term in normalized for term in JUNK_TERMS)


def first_author(row: Dict[str, Any], authors: Dict[str, str]) -> str:
    row_authors = row.get("authors")
    if not isinstance(row_authors, list) or not row_authors:
        return ""
    first = row_authors[0]
    if not isinstance(first, dict):
        return ""
    return authors.get(clean_text(first.get("author_id")), "")


def first_series_id(row: Dict[str, Any]) -> str:
    series = row.get("series")
    if not isinstance(series, list) or not series:
        return ""
    first = series[0]
    if isinstance(first, dict):
        return clean_text(first.get("series_id"))
    return clean_text(first)


def seed_key(row: Dict[str, str]) -> Tuple[str, str]:
    return normalize(row["title"]), normalize(row["author"])


def candidate_from_book(
    row: Dict[str, Any],
    authors: Dict[str, str],
    series_map: Dict[str, str],
    min_ratings: int,
    include_unknown_language: bool,
    allow_ranges: bool,
) -> Optional[Dict[str, str]]:
    ratings_count = to_int(row.get("ratings_count"))
    if ratings_count < min_ratings:
        return None
    if not is_english_or_allowed(clean_text(row.get("language_code")), include_unknown_language):
        return None

    raw_title = clean_text(row.get("title")) or clean_text(row.get("title_without_series"))
    title, parsed_series, position, is_range = parse_title_series(raw_title)
    if not title or not parsed_series or not position:
        return None
    if is_range and not allow_ranges:
        return None
    if is_junk_title(title) or is_junk_title(raw_title):
        return None

    author = first_author(row, authors)
    if not author:
        return None

    series_id = first_series_id(row)
    mapped_series = series_map.get(series_id, "")
    expected_series = parsed_series or mapped_series
    if mapped_series and normalize(mapped_series) != normalize(parsed_series):
        expected_series = mapped_series

    return {
        "title": title,
        "author": author,
        "expected_series": expected_series,
        "expected_position": position,
        "franchise": expected_series,
        "source": "goodreads_dump",
        "book_id": clean_text(row.get("book_id")),
        "work_id": clean_text(row.get("work_id")),
        "series_id": series_id,
        "isbn13": clean_text(row.get("isbn13")),
        "ratings_count": str(ratings_count),
        "average_rating": clean_text(row.get("average_rating")),
    }


def build_seed_csv(
    books_path: Path,
    authors_path: Path,
    series_path: Path,
    out_path: Path,
    limit: int,
    min_ratings: int,
    include_unknown_language: bool = True,
    allow_ranges: bool = False,
) -> int:
    authors = load_authors(authors_path)
    series_map = load_series(series_path)
    best_by_key: Dict[Tuple[str, str], Dict[str, str]] = {}

    for idx, row in enumerate(open_jsonl_gz(books_path), start=1):
        candidate = candidate_from_book(
            row=row,
            authors=authors,
            series_map=series_map,
            min_ratings=min_ratings,
            include_unknown_language=include_unknown_language,
            allow_ranges=allow_ranges,
        )
        if not candidate:
            continue

        key = seed_key(candidate)
        previous = best_by_key.get(key)
        if not previous or to_int(candidate["ratings_count"]) > to_int(previous["ratings_count"]):
            best_by_key[key] = candidate

        if idx % 250000 == 0:
            print(f"scanned={idx:,} candidates={len(best_by_key):,}", flush=True)

    rows = sorted(
        best_by_key.values(),
        key=lambda item: (-to_int(item["ratings_count"]), item["expected_series"], item["expected_position"], item["title"]),
    )[:limit]

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=OUTPUT_FIELDS)
        writer.writeheader()
        writer.writerows(rows)

    return len(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a large Goodreads series seed CSV for LibGen testing.")
    parser.add_argument("--books", default=r"C:\Users\Suprabha\Downloads\goodreads_books.json.gz")
    parser.add_argument("--authors", default=r"C:\Users\Suprabha\Downloads\goodreads_book_authors.json.gz")
    parser.add_argument("--series", default=r"C:\Users\Suprabha\Downloads\goodreads_book_series.json.gz")
    parser.add_argument("--out", required=True)
    parser.add_argument("--limit", type=int, default=2000)
    parser.add_argument("--min-ratings", type=int, default=100)
    parser.add_argument("--english-only", action="store_true", help="Drop rows with unknown language instead of keeping them.")
    parser.add_argument("--allow-ranges", action="store_true", help="Keep range ordinals like #1-2.")
    args = parser.parse_args()

    count = build_seed_csv(
        books_path=Path(args.books),
        authors_path=Path(args.authors),
        series_path=Path(args.series),
        out_path=Path(args.out),
        limit=args.limit,
        min_ratings=args.min_ratings,
        include_unknown_language=not args.english_only,
        allow_ranges=args.allow_ranges,
    )
    print(f"Wrote {count} Goodreads series seeds to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
