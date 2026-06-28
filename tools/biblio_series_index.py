#!/usr/bin/env python3
"""
biblio_series_index.py — distill a Goodreads dump into a compact, offline
TITLE -> {series, position, rating, ...} index for Colosseum's Biblio world.

WHY title, not ISBN: Apple gives a book's ISBN-13, but the Goodreads dump shatters
every book across DOZENS of edition-ISBNs (Catching Fire = 23 ISBNs), and Apple
serves yet another edition's ISBN -- so a single-ISBN join is leaky. Every edition,
however, shares the same title + series, so we match on normalized TITLE (+ author),
with ISBN kept only as a tiebreaker. Goodreads encodes series + position IN the title:
    "Catching Fire (The Hunger Games, #2)"   "...Bree Tanner... (Twilight, #3.5)"
    "The Wee Free Men (Discworld, #30; Tiffany Aching, #1)"   "...Boxset (Harry Potter, #1-7)"
No "(..., #N)" suffix => standalone.

INPUT (auto-detected): *.csv (goodbooks-10k)  |  *.json[.gz] (UCSD ~2.3M)
3rd arg (optional): goodreads_book_authors.json[.gz] to resolve author names.
OUTPUT: a SQLite DB with a deduped `books` table (one row per title), keyed for
title lookup; ships inside Colosseum (Qt has a built-in SQLite driver), queried offline.

Usage:
  python biblio_series_index.py books.csv biblio_series.db
  python biblio_series_index.py goodreads_books.json.gz biblio_series.db goodreads_book_authors.json.gz
"""

import csv, gzip, json, os, re, sqlite3, sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")   # don't die on exotic titles
except Exception: pass

# ---- the one shared brain: pull series + position out of a Goodreads title ----
_SERIES_BLOCK = re.compile(r'\(([^()]*#[^()]*)\)\s*$')
_ONE_SERIES   = re.compile(r'^(.*?)\s*,?\s*#\s*([0-9][0-9.\-]*)\s*$')

def parse_series(title):
    if not title:
        return (title, None, None, None, 0)
    m = _SERIES_BLOCK.search(title)
    if not m:
        return (title.strip(), None, None, None, 0)
    display, parts = title[:m.start()].strip(), []
    for chunk in m.group(1).split(';'):
        sm = _ONE_SERIES.match(chunk.strip())
        if sm and sm.group(1).strip():
            parts.append((sm.group(1).strip(), sm.group(2).strip()))
    if not parts:
        return (title.strip(), None, None, None, 0)
    name, pos = parts[0]
    return (display, name, pos, "; ".join("%s #%s" % (n, p) for n, p in parts), 1 if '-' in pos else 0)

def clean_isbn(v):
    v = (v or "").strip()
    if v.endswith(".0"): v = v[:-2]
    return v if (v.isdigit() and len(v) == 13) else ""
def to_int(v):
    try: return int(float(v))
    except: return None
def to_float(v):
    try: return float(v)
    except: return None
def norm_title(t):
    return re.sub(r'\s+', ' ', (t or "").lower()).strip()

def load_authors(path):
    if not path: return {}
    op = gzip.open if path.endswith(".gz") else open
    m = {}
    with op(path, "rt", encoding="utf-8", errors="replace") as f:
        for line in f:
            try: a = json.loads(line)
            except: continue
            if a.get("author_id"): m[a["author_id"]] = a.get("name", "")
    print("authors loaded: %d" % len(m))
    return m

# ---- readers ----
def read_goodbooks_csv(path, authors):
    with open(path, encoding="utf-8", errors="replace", newline="") as f:
        for row in csv.DictReader(f):
            yield {"isbn13": clean_isbn(row.get("isbn13")),
                   "raw_title": row.get("title") or row.get("original_title") or "",
                   "author": (row.get("authors") or "").split(",")[0].strip(),
                   "rating": to_float(row.get("average_rating")),
                   "ratings_count": to_int(row.get("ratings_count")),
                   "year": to_int(row.get("original_publication_year")),
                   "cover": row.get("image_url") or ""}

def read_ucsd_jsonl(path, authors):
    op = gzip.open if path.endswith(".gz") else open
    with op(path, "rt", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line: continue
            try: b = json.loads(line)
            except: continue
            lang = (b.get("language_code") or "").strip().lower()
            if lang and not lang.startswith("en"):   # English (or untagged) only -> matches Apple's catalog
                continue
            name = ""
            al = b.get("authors") or []
            if al and authors: name = authors.get(al[0].get("author_id"), "")
            yield {"isbn13": clean_isbn(b.get("isbn13")),
                   "raw_title": b.get("title") or b.get("title_without_series") or "",
                   "author": name,
                   "rating": to_float(b.get("average_rating")),
                   "ratings_count": to_int(b.get("ratings_count")),
                   "year": to_int(b.get("publication_year")),
                   "cover": b.get("image_url") or ""}

def reader_for(path, authors):
    low = path.lower()
    if low.endswith(".csv"): return read_goodbooks_csv(path, authors)
    if low.endswith(".json") or low.endswith(".gz"): return read_ucsd_jsonl(path, authors)
    raise SystemExit("Unknown input: %s" % path)

def build(inp, out, authors_path=None):
    authors = load_authors(authors_path)
    if os.path.exists(out): os.remove(out)
    db = sqlite3.connect(out)
    db.execute("PRAGMA journal_mode=OFF"); db.execute("PRAGMA synchronous=OFF")
    db.execute("""CREATE TABLE editions(
        isbn13 TEXT, title TEXT, author TEXT, series TEXT, position TEXT, all_series TEXT,
        is_boxset INTEGER, rating REAL, ratings_count INTEGER, year INTEGER, cover TEXT, title_key TEXT)""")
    n = skipped = 0
    batch = []
    for rec in reader_for(inp, authors):
        tk = norm_title(parse_series(rec["raw_title"])[0])
        if not tk:
            skipped += 1; continue
        disp, series, pos, all_s, boxset = parse_series(rec["raw_title"])
        batch.append((rec["isbn13"], disp, rec["author"], series, pos, all_s, boxset,
                      rec["rating"], rec["ratings_count"] or 0, rec["year"], rec["cover"], tk))
        n += 1
        if len(batch) >= 5000:
            db.executemany("INSERT INTO editions VALUES (?,?,?,?,?,?,?,?,?,?,?,?)", batch); batch.clear()
    if batch: db.executemany("INSERT INTO editions VALUES (?,?,?,?,?,?,?,?,?,?,?,?)", batch)
    db.commit()
    print("editions read: %d   (skipped, no title: %d)" % (n, skipped))

    # Lean: keep only SERIES books (standalone -> "not found" in the app), one canonical row
    # per title (most-rated edition; SQLite's bare-column + MAX(ratings_count) picks that row).
    db.execute("""CREATE TABLE books AS
        SELECT isbn13,title,author,series,position,all_series,is_boxset,rating,
               MAX(ratings_count) AS ratings_count,year,cover,title_key
        FROM editions WHERE series IS NOT NULL GROUP BY title_key""")
    db.execute("DROP TABLE editions")
    db.execute("CREATE UNIQUE INDEX idx_tk ON books(title_key)")
    db.execute("CREATE INDEX idx_series ON books(series)")
    db.execute("CREATE INDEX idx_isbn ON books(isbn13)")
    db.commit()
    db.execute("VACUUM"); db.commit()

    tot = db.execute("SELECT COUNT(*) FROM books").fetchone()[0]
    box = db.execute("SELECT COUNT(*) FROM books WHERE is_boxset=1").fetchone()[0]
    uniq = db.execute("SELECT COUNT(DISTINCT series) FROM books").fetchone()[0]
    auth = db.execute("SELECT COUNT(*) FROM books WHERE author!=''").fetchone()[0]
    print("\n=== built %s  (English, series-only, deduped by title) ===" % out)
    print("series books indexed: %d    distinct series: %d    boxsets: %d" % (tot, uniq, box))
    print("authors resolved: %d    db size: %.1f MB" % (auth, os.path.getsize(out)/1048576))
    return db

def demo(db):
    print("\n=== resolve by TITLE (the reliable join: Apple title+author -> series) ===")
    for t in ["catching fire", "harry potter and the prisoner of azkaban", "throne of glass",
              "the fault in our stars", "fourth wing", "wonder"]:
        r = db.execute("SELECT title,author,series,position,all_series,is_boxset,rating FROM books WHERE title_key=?", (t,)).fetchone()
        if not r: print('  "%s" -> not in series index  (= STANDALONE or unknown)' % t); continue
        title, author, series, pos, all_s, boxset, rating = r
        verdict = "STANDALONE" if not series else (all_s if (all_s and ";" in all_s) else "%s #%s%s" % (series, pos, " [boxset]" if boxset else ""))
        print('  "%s" by %s -> %s  (*%s)' % (title, author or "?", verdict, rating))
    top = db.execute("SELECT series FROM books WHERE series IS NOT NULL AND is_boxset=0 GROUP BY series ORDER BY COUNT(*) DESC LIMIT 1").fetchone()
    if top:
        print("\n=== a whole series in reading order: %s ===" % top[0])
        for pos, title in db.execute("SELECT position,title FROM books WHERE series=? AND is_boxset=0 ORDER BY CAST(position AS REAL)", (top[0],)).fetchall()[:12]:
            print("  #%-5s %s" % (pos, title))

if __name__ == "__main__":
    inp = sys.argv[1] if len(sys.argv) > 1 else "books.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "biblio_series.db"
    authors = sys.argv[3] if len(sys.argv) > 3 else None
    demo(build(inp, out, authors))
