"""build_mal_db.py — bake the Kaggle MyAnimeList dump into data/mal_catalog.db.

The genre-page revival (Hemanth, 2026-07-18): Jikan's /anime?genres= and
/manga?genres= filter endpoints 504 chronically, so genre browsing now reads a
LOCAL catalog baked from the weekly-updated public dump
(kaggle: andreuvallhernndez/myanimelist). kagglehub downloads it ANONYMOUSLY —
no account, no token — so the standing keyless law holds even at build time.
Same doctrine as the GCD comics spine and the Goodreads dump: dumps in, one
SQLite artifact out, the app reads only the artifact (data/*.db is gitignored).

Rows are stored JIKAN-SHAPED where it matters (type/status strings match what
api.jikan.moe returns) so the QML card mappers consume them unchanged.

Usage:  python scripts/anime_brain/build_mal_db.py
Output: data/mal_catalog.db  (tables: anime, manga, tag, tag_count,
        classification, classification_count, meta)

Tankoban Discover (spec 2026-08-01): manga is no longer SFW-only. Non-SFW manga
rows are RETAINED and marked explicit=1 (the app gates them behind the Explicit
Content preference); the flattened tag/tag_count tables are preserved unchanged,
and axis-aware classification/classification_count tables back the paged discovery
queries (MalCatalog.discoverPage / discoverFilters). Anime stays SFW-only.
"""
from __future__ import annotations

import ast
import csv
import io
import json
import os
import re
import sqlite3
import sys
import zipfile
from datetime import datetime, timezone

_POSSESSIVE = re.compile(r"['’]s")
_NON_ALNUM = re.compile(r"[^a-z0-9]+")
_WHITESPACE = re.compile(r"\s+")
_LEADING_ARTICLE = re.compile(r"^(the|a|an) ")


def normalized_title(raw):
    """Match VaultKit::normalizedTitle for catalogue-side title keys."""
    s = (raw or "").lower()
    s = _POSSESSIVE.sub("", s)
    s = _NON_ALNUM.sub(" ", s)
    s = _WHITESPACE.sub(" ", s).strip()
    return _LEADING_ARTICLE.sub("", s, count=1)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DB_PATH = os.path.join(REPO, "data", "mal_catalog.db")
DATASET = "andreuvallhernndez/myanimelist"

# keep the browsable slice lean: pages show 24 cards; nothing below this many
# members ever surfaces on a ranked genre page. Counts are computed BEFORE the
# slice, so the hero's "N titles" line stays the true catalog total.
KEEP_TOP_BY_MEMBERS = 20000
SYNOPSIS_CAP = 320

ANIME_TYPE = {"tv": "TV", "movie": "Movie", "ova": "OVA", "ona": "ONA",
              "special": "Special", "music": "Music", "tv_special": "TV Special",
              "cm": "CM", "pv": "PV"}
ANIME_STATUS = {"finished_airing": "Finished Airing",
                "currently_airing": "Currently Airing",
                "not_yet_aired": "Not yet aired"}
MANGA_TYPE = {"manga": "Manga", "light_novel": "Light Novel", "one_shot": "One-shot",
              "novel": "Novel", "manhwa": "Manhwa", "manhua": "Manhua",
              "doujinshi": "Doujinshi"}
MANGA_STATUS = {"currently_publishing": "Publishing", "finished": "Finished",
                "on_hiatus": "On Hiatus", "discontinued": "Discontinued",
                "not_yet_published": "Not yet published"}


def fetch_csv(name: str):
    import kagglehub
    path = kagglehub.dataset_download(DATASET, path=name)
    # kagglehub serves single files zip-wrapped under the csv's own name
    with open(path, "rb") as f:
        magic = f.read(2)
    if magic == b"PK":
        z = zipfile.ZipFile(path)
        return io.TextIOWrapper(z.open(name), encoding="utf-8", errors="replace", newline="")
    return open(path, encoding="utf-8", errors="replace", newline="")


def listify(raw: str):
    """Dump list columns are python-repr strings: "['Action', 'Drama']"."""
    s = (raw or "").strip()
    if not s or s == "[]":
        return []
    try:
        v = ast.literal_eval(s)
        return [str(x).strip() for x in v if str(x).strip()] if isinstance(v, list) else []
    except (ValueError, SyntaxError):
        return []


def credit_names(raw: str):
    """Credits columns differ by medium: anime `studios` is a list of name STRINGS
    (['Bones']), but manga `authors` is a list of OBJECTS
    ([{'first_name': 'Kentarou', 'last_name': 'Miura', 'role': ...}]). Stringifying
    an author object dumped the raw dict onto the card (the 2026-07-19 bug). Extract
    the name in Jikan's "Last, First" shape so the card's flipName renders "First Last";
    a studio (or a comma-less author) passes through unflipped."""
    s = (raw or "").strip()
    if not s or s == "[]":
        return []
    try:
        v = ast.literal_eval(s)
    except (ValueError, SyntaxError):
        return []
    if not isinstance(v, list):
        return []
    out = []
    for x in v:
        if isinstance(x, dict):
            ln = str(x.get("last_name", "") or "").strip()
            fn = str(x.get("first_name", "") or "").strip()
            name = (ln + ", " + fn) if (ln and fn) else (ln or fn
                    or str(x.get("name", "") or "").strip())
        else:
            name = str(x).strip()
        if name:
            out.append(name)
    return out


def num(raw, cast=float):
    try:
        v = cast(float(raw))
        return v
    except (TypeError, ValueError):
        return None


def year_of(row, *cols):
    for c in cols:
        s = (row.get(c) or "").strip()
        if len(s) >= 4 and s[:4].isdigit():
            return int(s[:4])
    return None


def clean_synopsis(raw: str) -> str:
    s = " ".join((raw or "").split())
    s = s.replace("[Written by MAL Rewrite]", "").strip()
    return s[:SYNOPSIS_CAP]


def load_rows(fname, medium):
    out = []
    with fetch_csv(fname) as f:
        for row in csv.DictReader(f):
            sfw = (row.get("sfw") or "").strip().lower()
            # Anime stays SFW-only (its lane has no explicit gate). Manga now RETAINS
            # non-SFW rows and marks them explicit=1 (Tankoban Discover, 2026-08-01).
            if medium == "anime":
                if sfw != "true":
                    continue
                if (row.get("approved") or "").strip().lower() == "false":
                    continue
            mal_id = num(row.get(medium + "_id"), int)
            title = (row.get("title") or "").strip()
            if not mal_id or not title:
                continue
            # axis-aware classifications from the SEPARATE source columns, and the
            # legacy flattened tag list (genres+themes+demographics, unchanged order).
            axes = {
                "genre": listify(row.get("genres")),
                "demographic": listify(row.get("demographics")),
                "theme": listify(row.get("themes")),
            }
            explicit = int(sfw == "false" or
                           any(x.lower() in {"hentai", "erotica", "pornography"}
                               for values in axes.values() for x in values))
            tags = axes["genre"] + axes["theme"] + axes["demographic"]
            out.append({
                "mal_id": int(mal_id),
                "title": title,
                "title_english": (row.get("title_english") or "").strip(),
                "norm_title": normalized_title(title),
                "norm_title_english": normalized_title(row.get("title_english") or ""),
                "type": (ANIME_TYPE if medium == "anime" else MANGA_TYPE)
                        .get((row.get("type") or "").strip().lower(),
                             (row.get("type") or "").strip()),
                "score": num(row.get("score")),
                "scored_by": int(num(row.get("scored_by"), int) or 0),
                "members": int(num(row.get("members"), int) or 0),
                "status": (ANIME_STATUS if medium == "anime" else MANGA_STATUS)
                          .get((row.get("status") or "").strip().lower(),
                               (row.get("status") or "").strip()),
                "episodes": int(num(row.get("episodes"), int) or 0),
                "volumes": int(num(row.get("volumes"), int) or 0),
                "chapters": int(num(row.get("chapters"), int) or 0),
                "year": year_of(row, "start_year", "start_date", "real_start_date"),
                "cover": (row.get("main_picture") or "").strip(),
                "synopsis": clean_synopsis(row.get("synopsis")),
                "credits": json.dumps(
                    credit_names(row.get("studios" if medium == "anime" else "authors"))[:3]),
                "tags": tags,
                "axes": axes,
                "explicit": explicit,
                "start_date": (row.get("start_date") or "").strip(),
                "favorites": int(num(row.get("favorites"), int) or 0),
            })
    return out


def bake():
    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    tmp = DB_PATH + ".building"
    if os.path.exists(tmp):
        os.remove(tmp)
    db = sqlite3.connect(tmp)
    db.executescript("""
        CREATE TABLE anime (mal_id INTEGER PRIMARY KEY, title TEXT, title_english TEXT,
            norm_title TEXT NOT NULL, norm_title_english TEXT NOT NULL,
            type TEXT, score REAL, scored_by INTEGER, members INTEGER, status TEXT,
            episodes INTEGER, year INTEGER, cover TEXT, synopsis TEXT,
            credits TEXT, tags TEXT);
        CREATE TABLE manga (mal_id INTEGER PRIMARY KEY, title TEXT, title_english TEXT,
            norm_title TEXT NOT NULL, norm_title_english TEXT NOT NULL,
            type TEXT, score REAL, scored_by INTEGER, members INTEGER, status TEXT,
            volumes INTEGER, chapters INTEGER, year INTEGER, cover TEXT, synopsis TEXT,
            credits TEXT, tags TEXT,
            explicit INTEGER NOT NULL DEFAULT 0, start_date TEXT NOT NULL DEFAULT '',
            favorites INTEGER NOT NULL DEFAULT 0);
        CREATE TABLE tag (medium TEXT, tag TEXT, mal_id INTEGER);
        CREATE TABLE tag_count (medium TEXT, tag TEXT, total INTEGER,
            PRIMARY KEY (medium, tag));
        CREATE TABLE classification (medium TEXT, axis TEXT, value TEXT, mal_id INTEGER);
        CREATE TABLE classification_count (medium TEXT, axis TEXT, value TEXT, total INTEGER,
            PRIMARY KEY (medium, axis, value));
        CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT);
    """)

    for medium, fname in (("anime", "anime.csv"), ("manga", "manga.csv")):
        rows = load_rows(fname, medium)
        # true totals per tag BEFORE the browsable slice
        totals = {}
        for r in rows:
            for t in r["tags"]:
                totals[t] = totals.get(t, 0) + 1
        db.executemany("INSERT INTO tag_count VALUES (?,?,?)",
                       [(medium, t, n) for t, n in totals.items()])
        # axis-aware totals BEFORE the slice, keyed by (axis, value)
        class_totals = {}
        for r in rows:
            for axis, values in r["axes"].items():
                for v in values:
                    class_totals[(axis, v)] = class_totals.get((axis, v), 0) + 1
        db.executemany("INSERT INTO classification_count VALUES (?,?,?,?)",
                       [(medium, axis, v, n) for (axis, v), n in class_totals.items()])

        rows.sort(key=lambda r: r["members"], reverse=True)
        kept = rows[:KEEP_TOP_BY_MEMBERS]
        for r in kept:
            cols = ["mal_id", "title", "title_english", "norm_title", "norm_title_english",
                    "type", "score", "scored_by",
                    "members", "status"]
            cols += ["episodes"] if medium == "anime" else ["volumes", "chapters"]
            cols += ["year", "cover", "synopsis", "credits"]
            if medium == "manga":
                cols += ["explicit", "start_date", "favorites"]
            db.execute(
                f"INSERT INTO {medium} ({','.join(cols)}, tags) VALUES "
                f"({','.join('?' * len(cols))}, ?)",
                [r[c] for c in cols] + [json.dumps(r["tags"])])
            db.executemany("INSERT INTO tag VALUES (?,?,?)",
                           [(medium, t, r["mal_id"]) for t in r["tags"]])
            db.executemany("INSERT INTO classification VALUES (?,?,?,?)",
                           [(medium, axis, v, r["mal_id"])
                            for axis, values in r["axes"].items() for v in values])
        explicit_kept = sum(1 for r in kept if r.get("explicit"))
        print(f"{medium}: {len(rows)} rows -> kept top {len(kept)} by members, "
              f"{len(totals)} tags, {len(class_totals)} classifications, "
              f"{explicit_kept} explicit")

    db.execute("CREATE INDEX idx_tag ON tag (medium, tag, mal_id)")
    db.execute("CREATE INDEX anime_norm_title_idx ON anime (norm_title)")
    db.execute("CREATE INDEX anime_norm_title_english_idx ON anime (norm_title_english)")
    db.execute("CREATE INDEX manga_norm_title_idx ON manga (norm_title)")
    db.execute("CREATE INDEX manga_norm_title_english_idx ON manga (norm_title_english)")
    # Deep Theatre catalogue (spec 2026-08-01): indexes that back MalCatalog.animeCatalog's
    # members/score/status/type/year paging. (The tag lookup MalCatalog.animeCatalog needs is
    # already served by idx_tag above, so no separate tag index is baked.) Rebuilding the .db
    # is a data-vault/deploy step — the runtime works without these indexes, just slower.
    db.execute("CREATE INDEX anime_members_idx ON anime (members DESC)")
    db.execute("CREATE INDEX anime_score_votes_idx ON anime (score DESC, scored_by DESC)")
    db.execute("CREATE INDEX anime_status_type_year_idx ON anime (status, type, year)")
    # Tankoban Discover paging: manga members/score/date orders + the facet lookup.
    db.execute("CREATE INDEX manga_members_idx ON manga (members DESC)")
    db.execute("CREATE INDEX manga_score_votes_idx ON manga (score DESC, scored_by DESC)")
    db.execute("CREATE INDEX manga_start_date_idx ON manga (start_date DESC)")
    db.execute("CREATE INDEX idx_classification ON classification (medium, axis, value, mal_id)")
    db.execute("INSERT INTO meta VALUES ('baked_at', ?)",
               (datetime.now(timezone.utc).isoformat(),))
    db.execute("INSERT INTO meta VALUES ('dataset', ?)", (DATASET,))
    db.commit()
    db.close()
    if os.path.exists(DB_PATH):
        os.remove(DB_PATH)
    os.rename(tmp, DB_PATH)
    print("baked:", DB_PATH, f"({os.path.getsize(DB_PATH) / 1048576:.1f} MB)")


if __name__ == "__main__":
    sys.exit(bake())
