#!/usr/bin/env python3
"""Build a first Biblio canonical/assertion SQLite graph from benchmark evidence."""

from __future__ import annotations

import argparse
import csv
import json
import re
import sqlite3
from pathlib import Path
from typing import Any


WEIGHTS = {
    ("goodreads_seed", "work_identity"): 0.90,
    ("goodreads_seed", "series_membership"): 0.85,
    ("goodreads_seed", "series_ordinal"): 0.85,
    ("goodreads_seed", "isbn"): 0.65,
    ("oceanofpdf", "work_identity"): 0.45,
    ("oceanofpdf", "series_membership"): 0.78,
    ("oceanofpdf", "series_ordinal"): 0.80,
    ("oceanofpdf", "isbn"): 0.35,
    ("oceanofpdf", "download_candidate"): 0.40,
    ("libgen", "work_identity"): 0.45,
    ("libgen", "series_membership"): 0.45,
    ("libgen", "series_ordinal"): 0.48,
    ("libgen", "isbn"): 0.78,
    ("libgen", "download_candidate"): 0.85,
    ("zlibrary", "work_identity"): 0.55,
    ("zlibrary", "series_membership"): 0.20,
    ("zlibrary", "series_ordinal"): 0.20,
    ("readanybook", "work_identity"): 0.40,
    ("readanybook", "series_membership"): 0.30,
    ("readanybook", "series_ordinal"): 0.30,
    ("readanybook", "download_candidate"): 0.15,
}


def normalize(text: str) -> str:
    text = (text or "").casefold().replace("&", " and ")
    text = re.sub(r"[^a-z0-9]+", " ", text)
    text = re.sub(r"\bthe\b", " ", text)
    return re.sub(r"\s+", " ", text).strip()


def parse_float(value: Any) -> float | None:
    try:
        text = str(value or "").strip()
        return float(text) if text else None
    except ValueError:
        return None


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def read_json(path: Path) -> list[dict[str, Any]]:
    return json.loads(path.read_text(encoding="utf-8"))


def key(row: dict[str, Any]) -> tuple[str, str]:
    return (normalize(str(row.get("title") or "")), normalize(str(row.get("author") or "")))


SCHEMA = """
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS work (
  id INTEGER PRIMARY KEY,
  canonical_title TEXT NOT NULL,
  canonical_author TEXT NOT NULL,
  normalized_title TEXT NOT NULL,
  normalized_author TEXT NOT NULL,
  source TEXT NOT NULL,
  confidence REAL NOT NULL,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(normalized_title, normalized_author)
);

CREATE TABLE IF NOT EXISTS series (
  id INTEGER PRIMARY KEY,
  canonical_title TEXT NOT NULL,
  normalized_title TEXT NOT NULL UNIQUE,
  source TEXT NOT NULL,
  confidence REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS series_membership (
  id INTEGER PRIMARY KEY,
  work_id INTEGER NOT NULL REFERENCES work(id) ON DELETE CASCADE,
  series_id INTEGER NOT NULL REFERENCES series(id) ON DELETE CASCADE,
  display_position TEXT,
  sort_order REAL,
  confidence REAL NOT NULL,
  source TEXT NOT NULL,
  UNIQUE(work_id, series_id, source)
);

CREATE TABLE IF NOT EXISTS edition (
  id INTEGER PRIMARY KEY,
  work_id INTEGER NOT NULL REFERENCES work(id) ON DELETE CASCADE,
  isbn TEXT,
  language TEXT,
  publication_date TEXT,
  publisher TEXT,
  file_format TEXT,
  source TEXT NOT NULL,
  confidence REAL NOT NULL,
  UNIQUE(work_id, isbn, source)
);

CREATE TABLE IF NOT EXISTS download_candidate (
  id INTEGER PRIMARY KEY,
  work_id INTEGER NOT NULL REFERENCES work(id) ON DELETE CASCADE,
  edition_id INTEGER REFERENCES edition(id) ON DELETE SET NULL,
  source TEXT NOT NULL,
  url TEXT,
  file_hash TEXT,
  file_format TEXT,
  title TEXT,
  author TEXT,
  confidence REAL NOT NULL,
  visible_only INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE IF NOT EXISTS source_assertion (
  id INTEGER PRIMARY KEY,
  source TEXT NOT NULL,
  source_key TEXT,
  subject_type TEXT NOT NULL,
  subject_id INTEGER,
  predicate TEXT NOT NULL,
  value TEXT,
  confidence REAL NOT NULL,
  evidence_json TEXT,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS resolver_alias (
  id INTEGER PRIMARY KEY,
  entity_type TEXT NOT NULL,
  entity_id INTEGER NOT NULL,
  alias TEXT NOT NULL,
  normalized_alias TEXT NOT NULL,
  source TEXT NOT NULL,
  confidence REAL NOT NULL,
  UNIQUE(entity_type, entity_id, normalized_alias, source)
);

CREATE TABLE IF NOT EXISTS quality_flag (
  id INTEGER PRIMARY KEY,
  entity_type TEXT NOT NULL,
  entity_id INTEGER NOT NULL,
  flag TEXT NOT NULL,
  reason TEXT,
  source TEXT,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
"""


def connect(path: Path, replace: bool) -> sqlite3.Connection:
    if replace and path.exists():
        path.unlink()
    conn = sqlite3.connect(path)
    conn.executescript(SCHEMA)
    return conn


def add_assertion(conn: sqlite3.Connection, source: str, source_key: str, subject_type: str, subject_id: int | None, predicate: str, value: Any, confidence: float, evidence: dict[str, Any]) -> None:
    if value is None or value == "":
        return
    conn.execute(
        "INSERT INTO source_assertion(source, source_key, subject_type, subject_id, predicate, value, confidence, evidence_json) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        (source, source_key, subject_type, subject_id, predicate, str(value), confidence, json.dumps(evidence, ensure_ascii=False)),
    )


def add_flag(conn: sqlite3.Connection, entity_type: str, entity_id: int, flag: str, reason: str, source: str) -> None:
    conn.execute(
        "INSERT INTO quality_flag(entity_type, entity_id, flag, reason, source) VALUES (?, ?, ?, ?, ?)",
        (entity_type, entity_id, flag, reason, source),
    )


def get_work(conn: sqlite3.Connection, title: str, author: str, confidence: float) -> int:
    nt = normalize(title)
    na = normalize(author)
    conn.execute(
        "INSERT OR IGNORE INTO work(canonical_title, canonical_author, normalized_title, normalized_author, source, confidence) VALUES (?, ?, ?, ?, 'goodreads_seed', ?)",
        (title, author, nt, na, confidence),
    )
    return int(conn.execute("SELECT id FROM work WHERE normalized_title = ? AND normalized_author = ?", (nt, na)).fetchone()[0])


def get_series(conn: sqlite3.Connection, title: str, source: str, confidence: float) -> int:
    nt = normalize(title)
    conn.execute(
        "INSERT OR IGNORE INTO series(canonical_title, normalized_title, source, confidence) VALUES (?, ?, ?, ?)",
        (title, nt, source, confidence),
    )
    return int(conn.execute("SELECT id FROM series WHERE normalized_title = ?", (nt,)).fetchone()[0])


def add_alias(conn: sqlite3.Connection, entity_type: str, entity_id: int, alias: str, source: str, confidence: float) -> None:
    if not alias:
        return
    conn.execute(
        "INSERT OR IGNORE INTO resolver_alias(entity_type, entity_id, alias, normalized_alias, source, confidence) VALUES (?, ?, ?, ?, ?, ?)",
        (entity_type, entity_id, alias, normalize(alias), source, confidence),
    )


def add_membership(conn: sqlite3.Connection, work_id: int, series_title: str, position: str, source: str, membership_conf: float, ordinal_conf: float) -> None:
    if not series_title:
        return
    sid = get_series(conn, series_title, source, membership_conf)
    confidence = min(membership_conf, ordinal_conf if position else membership_conf)
    conn.execute(
        "INSERT OR IGNORE INTO series_membership(work_id, series_id, display_position, sort_order, confidence, source) VALUES (?, ?, ?, ?, ?, ?)",
        (work_id, sid, position or None, parse_float(position), confidence, source),
    )


def add_edition(conn: sqlite3.Connection, work_id: int, isbn: str, language: str, publication_date: str, publisher: str, fmt: str, source: str, confidence: float) -> int:
    isbn = (isbn or "").split(";")[0].strip()
    conn.execute(
        "INSERT OR IGNORE INTO edition(work_id, isbn, language, publication_date, publisher, file_format, source, confidence) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        (work_id, isbn or None, language or None, publication_date or None, publisher or None, fmt or None, source, confidence),
    )
    row = conn.execute(
        "SELECT id FROM edition WHERE work_id = ? AND COALESCE(isbn, '') = COALESCE(?, '') AND source = ?",
        (work_id, isbn or None, source),
    ).fetchone()
    if row:
        return int(row[0])
    conn.execute(
        "INSERT INTO edition(work_id, isbn, language, publication_date, publisher, file_format, source, confidence) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        (work_id, isbn or None, language or None, publication_date or None, publisher or None, fmt or None, source, confidence),
    )
    return int(conn.execute("SELECT last_insert_rowid()").fetchone()[0])


def build(
    seed_csv: Path,
    libgen_json: Path,
    ocean_json: Path,
    out_db: Path,
    replace: bool,
    zlib_json: Path | None = None,
    readany_json: Path | None = None,
) -> None:
    seeds = read_csv(seed_csv)
    libgen_by_key = {key(row): row for row in read_json(libgen_json)}
    ocean_by_key = {key(row): row for row in read_json(ocean_json)}
    zlib_by_key = {key(row): row for row in read_json(zlib_json)} if zlib_json else {}
    readany_by_key = {key(row): row for row in read_json(readany_json)} if readany_json else {}
    conn = connect(out_db, replace)
    with conn:
        for seed in seeds:
            title = seed.get("title", "")
            author = seed.get("author", "")
            wid = get_work(conn, title, author, WEIGHTS[("goodreads_seed", "work_identity")])
            source_key = f"{seed.get('book_id', '')}:{seed.get('work_id', '')}"

            add_alias(conn, "work", wid, title, "goodreads_seed", 0.90)
            add_assertion(conn, "goodreads_seed", source_key, "work", wid, "title", title, 0.90, seed)
            add_assertion(conn, "goodreads_seed", source_key, "work", wid, "author", author, 0.90, seed)
            add_assertion(conn, "goodreads_seed", source_key, "work", wid, "ratings_count", seed.get("ratings_count"), 0.70, seed)

            expected_series = seed.get("expected_series", "")
            expected_position = seed.get("expected_position", "")
            if expected_series:
                add_membership(conn, wid, expected_series, expected_position, "goodreads_seed", WEIGHTS[("goodreads_seed", "series_membership")], WEIGHTS[("goodreads_seed", "series_ordinal")])
                sid = get_series(conn, expected_series, "goodreads_seed", 0.85)
                add_alias(conn, "series", sid, expected_series, "goodreads_seed", 0.85)
                add_assertion(conn, "goodreads_seed", source_key, "series_membership", wid, "series", expected_series, 0.85, seed)
                add_assertion(conn, "goodreads_seed", source_key, "series_membership", wid, "position", expected_position, 0.85, seed)

            if seed.get("isbn13"):
                add_edition(conn, wid, seed.get("isbn13", ""), "", "", "", "", "goodreads_seed", WEIGHTS[("goodreads_seed", "isbn")])
                add_assertion(conn, "goodreads_seed", source_key, "edition", wid, "isbn13", seed.get("isbn13"), 0.65, seed)

            lrow = libgen_by_key.get(key(seed))
            if lrow:
                lkey = str(lrow.get("libgen_selected_md5") or "")
                add_assertion(conn, "libgen", lkey, "work", wid, "selected_title", lrow.get("libgen_detail_title") or lrow.get("libgen_search_title"), 0.45, lrow)
                add_assertion(conn, "libgen", lkey, "work", wid, "selected_author", lrow.get("libgen_detail_author") or lrow.get("libgen_search_author"), 0.45, lrow)
                if lrow.get("libgen_series_name"):
                    add_assertion(conn, "libgen", lkey, "series_membership", wid, "series", lrow.get("libgen_series_name"), WEIGHTS[("libgen", "series_membership")], lrow)
                    add_assertion(conn, "libgen", lkey, "series_membership", wid, "position", lrow.get("libgen_series_position"), WEIGHTS[("libgen", "series_ordinal")], lrow)
                    if lrow.get("libgen_series_correct"):
                        add_membership(conn, wid, str(lrow.get("libgen_series_name") or ""), str(lrow.get("libgen_series_position") or ""), "libgen", 0.45, 0.48)
                    else:
                        add_flag(conn, "work", wid, "libgen_series_mismatch", f"Expected {expected_series} #{expected_position}; got {lrow.get('libgen_series_raw')}", "libgen")
                else:
                    add_flag(conn, "work", wid, "libgen_missing_series", "No LibGen series field on selected candidate", "libgen")
                eid = add_edition(conn, wid, str(lrow.get("libgen_isbn") or ""), "", str(lrow.get("libgen_year") or ""), str(lrow.get("libgen_publisher") or ""), str(lrow.get("libgen_format") or ""), "libgen", WEIGHTS[("libgen", "isbn")] if lrow.get("libgen_has_isbn") else 0.35)
                if lrow.get("libgen_selected_md5"):
                    conn.execute(
                        "INSERT OR IGNORE INTO download_candidate(work_id, edition_id, source, file_hash, file_format, title, author, confidence, visible_only) VALUES (?, ?, 'libgen', ?, ?, ?, ?, ?, 0)",
                        (wid, eid, str(lrow.get("libgen_selected_md5")).lower(), lrow.get("libgen_format"), lrow.get("libgen_detail_title") or lrow.get("libgen_search_title"), lrow.get("libgen_detail_author") or lrow.get("libgen_search_author"), WEIGHTS[("libgen", "download_candidate")]),
                    )
                if not lrow.get("libgen_search_count"):
                    add_flag(conn, "work", wid, "libgen_no_search_hit", "No LibGen search hit", "libgen")

            orow = ocean_by_key.get(key(seed))
            if orow:
                okey = str(orow.get("ocean_selected_url") or "")
                add_assertion(conn, "oceanofpdf", okey, "work", wid, "selected_title", orow.get("ocean_selected_title"), 0.45, orow)
                add_assertion(conn, "oceanofpdf", okey, "work", wid, "selected_author", orow.get("ocean_selected_author"), 0.45, orow)
                add_alias(conn, "work", wid, str(orow.get("ocean_selected_title") or ""), "oceanofpdf", 0.45)
                if orow.get("ocean_series_name"):
                    add_assertion(conn, "oceanofpdf", okey, "series_membership", wid, "series", orow.get("ocean_series_name"), WEIGHTS[("oceanofpdf", "series_membership")], orow)
                    add_assertion(conn, "oceanofpdf", okey, "series_membership", wid, "position", orow.get("ocean_series_position"), WEIGHTS[("oceanofpdf", "series_ordinal")], orow)
                    if orow.get("ocean_series_correct"):
                        add_membership(conn, wid, str(orow.get("ocean_series_name") or ""), str(orow.get("ocean_series_position") or ""), "oceanofpdf", WEIGHTS[("oceanofpdf", "series_membership")], WEIGHTS[("oceanofpdf", "series_ordinal")])
                    else:
                        add_flag(conn, "work", wid, "ocean_series_mismatch", f"Expected {expected_series} #{expected_position}; got {orow.get('ocean_series_raw')}", "oceanofpdf")
                else:
                    add_flag(conn, "work", wid, "ocean_missing_series", "No OceanofPDF series inferred from selected title", "oceanofpdf")
                eid = add_edition(conn, wid, str(orow.get("ocean_isbn") or ""), str(orow.get("ocean_language") or ""), str(orow.get("ocean_publication_date") or ""), "", "PDF/EPUB" if (orow.get("ocean_has_pdf") and orow.get("ocean_has_epub")) else "PDF" if orow.get("ocean_has_pdf") else "EPUB" if orow.get("ocean_has_epub") else "", "oceanofpdf", WEIGHTS[("oceanofpdf", "isbn")] if orow.get("ocean_has_isbn") else 0.25)
                if orow.get("ocean_has_pdf") or orow.get("ocean_has_epub"):
                    conn.execute(
                        "INSERT OR IGNORE INTO download_candidate(work_id, edition_id, source, url, file_format, title, author, confidence, visible_only) VALUES (?, ?, 'oceanofpdf', ?, ?, ?, ?, ?, 1)",
                        (wid, eid, orow.get("ocean_selected_url"), "PDF/EPUB" if (orow.get("ocean_has_pdf") and orow.get("ocean_has_epub")) else "PDF" if orow.get("ocean_has_pdf") else "EPUB", orow.get("ocean_selected_title"), orow.get("ocean_selected_author"), WEIGHTS[("oceanofpdf", "download_candidate")]),
                    )
                if orow.get("ocean_error"):
                    add_flag(conn, "work", wid, "ocean_error", str(orow.get("ocean_error"))[:500], "oceanofpdf")
                if not orow.get("ocean_search_hit"):
                    add_flag(conn, "work", wid, "ocean_no_search_hit", "No OceanofPDF search hit", "oceanofpdf")

            zrow = zlib_by_key.get(key(seed))
            if zrow:
                zkey = f"zlibrary.sk/search:{title}"
                add_assertion(conn, "zlibrary", zkey, "work", wid, "selected_title", zrow.get("zlib_selected_title"), WEIGHTS[("zlibrary", "work_identity")], zrow)
                add_assertion(conn, "zlibrary", zkey, "work", wid, "nearby_author_text", zrow.get("zlib_selected_nearby"), 0.35, zrow)
                add_assertion(conn, "zlibrary", zkey, "work", wid, "search_hit", zrow.get("zlib_search_hit"), 0.35, zrow)
                add_alias(conn, "work", wid, str(zrow.get("zlib_selected_title") or ""), "zlibrary", WEIGHTS[("zlibrary", "work_identity")])
                if zrow.get("zlib_series_name"):
                    add_assertion(conn, "zlibrary", zkey, "series_membership", wid, "series", zrow.get("zlib_series_name"), WEIGHTS[("zlibrary", "series_membership")], zrow)
                    add_assertion(conn, "zlibrary", zkey, "series_membership", wid, "position", zrow.get("zlib_series_position"), WEIGHTS[("zlibrary", "series_ordinal")], zrow)
                    if zrow.get("zlib_series_correct"):
                        add_membership(conn, wid, str(zrow.get("zlib_series_name") or ""), str(zrow.get("zlib_series_position") or ""), "zlibrary", WEIGHTS[("zlibrary", "series_membership")], WEIGHTS[("zlibrary", "series_ordinal")])
                    else:
                        add_flag(conn, "work", wid, "zlib_series_mismatch", f"Expected {expected_series} #{expected_position}; got {zrow.get('zlib_series_raw')}", "zlibrary")
                elif zrow.get("zlib_search_hit"):
                    add_flag(conn, "work", wid, "zlib_missing_series", "Z-Library public search hit had no inferable series", "zlibrary")
                if not zrow.get("zlib_search_hit"):
                    add_flag(conn, "work", wid, "zlib_no_search_hit", "No Z-Library public search hit", "zlibrary")
                if zrow.get("zlib_error"):
                    add_flag(conn, "work", wid, "zlib_error", str(zrow.get("zlib_error"))[:500], "zlibrary")

            rrow = readany_by_key.get(key(seed))
            if rrow:
                rkey = str(rrow.get("readany_selected_url") or f"readanybook/search:{title}")
                add_assertion(conn, "readanybook", rkey, "work", wid, "selected_title", rrow.get("readany_selected_title"), WEIGHTS[("readanybook", "work_identity")], rrow)
                add_assertion(conn, "readanybook", rkey, "work", wid, "selected_author", rrow.get("readany_selected_author"), WEIGHTS[("readanybook", "work_identity")], rrow)
                add_assertion(conn, "readanybook", rkey, "work", wid, "rating_text", rrow.get("readany_rating_text"), 0.20, rrow)
                add_assertion(conn, "readanybook", rkey, "work", wid, "genres", rrow.get("readany_genres"), 0.20, rrow)
                add_assertion(conn, "readanybook", rkey, "work", wid, "search_hit", rrow.get("readany_search_hit"), 0.25, rrow)
                add_alias(conn, "work", wid, str(rrow.get("readany_selected_title") or ""), "readanybook", WEIGHTS[("readanybook", "work_identity")])
                if rrow.get("readany_series_name"):
                    add_assertion(conn, "readanybook", rkey, "series_membership", wid, "series", rrow.get("readany_series_name"), WEIGHTS[("readanybook", "series_membership")], rrow)
                    add_assertion(conn, "readanybook", rkey, "series_membership", wid, "position", rrow.get("readany_series_position"), WEIGHTS[("readanybook", "series_ordinal")], rrow)
                    if rrow.get("readany_series_correct"):
                        add_membership(conn, wid, str(rrow.get("readany_series_name") or ""), str(rrow.get("readany_series_position") or ""), "readanybook", WEIGHTS[("readanybook", "series_membership")], WEIGHTS[("readanybook", "series_ordinal")])
                    else:
                        add_flag(conn, "work", wid, "readany_series_mismatch", f"Expected {expected_series} #{expected_position}; got {rrow.get('readany_series_raw')}", "readanybook")
                elif rrow.get("readany_search_hit"):
                    add_flag(conn, "work", wid, "readany_missing_series", "ReadAnyBook hit had no usable series metadata", "readanybook")
                if rrow.get("readany_cover_url") or rrow.get("readany_read_url"):
                    conn.execute(
                        "INSERT OR IGNORE INTO download_candidate(work_id, edition_id, source, url, file_format, title, author, confidence, visible_only) VALUES (?, NULL, 'readanybook', ?, ?, ?, ?, ?, 1)",
                        (
                            wid,
                            rrow.get("readany_read_url") or rrow.get("readany_selected_url"),
                            str(rrow.get("readany_formats") or "")[:200] or None,
                            rrow.get("readany_selected_title"),
                            rrow.get("readany_selected_author"),
                            WEIGHTS[("readanybook", "download_candidate")],
                        ),
                    )
                if not rrow.get("readany_search_hit"):
                    add_flag(conn, "work", wid, "readany_no_search_hit", "No ReadAnyBook search hit", "readanybook")
                if rrow.get("readany_error"):
                    add_flag(conn, "work", wid, "readany_error", str(rrow.get("readany_error"))[:500], "readanybook")

    conn.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed-csv", required=True)
    parser.add_argument("--libgen-json", required=True)
    parser.add_argument("--ocean-json", required=True)
    parser.add_argument("--zlib-json")
    parser.add_argument("--readany-json")
    parser.add_argument("--out-db", required=True)
    parser.add_argument("--replace", action="store_true")
    args = parser.parse_args()
    build(
        Path(args.seed_csv),
        Path(args.libgen_json),
        Path(args.ocean_json),
        Path(args.out_db),
        args.replace,
        Path(args.zlib_json) if args.zlib_json else None,
        Path(args.readany_json) if args.readany_json else None,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
