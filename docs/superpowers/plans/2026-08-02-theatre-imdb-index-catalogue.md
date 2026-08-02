# Theatre IMDb Index Catalogue Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild Theatre's Movies/Shows shelves on a bundled IMDb-dataset index so quality, language, and animation shelves are honest — per `docs/superpowers/specs/2026-08-02-theatre-imdb-index-catalogue-design.md`.

**Architecture:** Bake `data/imdb_catalog.db` from IMDb's public dumps (movies/shows twin of `mal_catalog.db`). A new native `ImdbCatalog` (mirror of `MalCatalog`) answers allowlisted, bound, paged queries. `TheatreCatalogRules.js` gets the ratified inventories + threshold dials + pure recipe→query mapping; `TheatreApi.js` swaps the Cinemeta-pool engine for index-first shelves (live rows stay live) and deletes the popularity fallback. A reality probe run through the real `colosseum.exe` prints every shelf's REAL titles — that gate, not fixtures, decides done.

**Tech Stack:** Python 3 (bake), Qt 6.11 C++/QtSql, QML JS libraries, PowerShell + qml.exe offscreen harnesses, colosseum.exe as QML runner for real-context probes.

## Global Constraints

- Work on `master`; no branch or worktree. Preserve unrelated dirty-worktree changes; stage by explicit pathspec only — never `git add .`. `native/CMakeLists.txt` and `native/main.cpp` are shared files: grep-verify after editing and post an additive-line declaration to `agents/chat.md` before committing them.
- Keyless forever: no TMDB, Trakt, accounts, or API keys anywhere in the diff.
- TDD per task: failing test first, observe the failure, minimal implementation, observe green.
- All `*_RATING`/`*_VOTES*` thresholds are named dials in `TheatreCatalogRules.js`; Task 6's real-data calibration decides final values, and fixture tests must NOT pin exact threshold numbers (assert relationships, e.g. `HG_VOTES_MAX < TR_VOTES`).
- Untouched surfaces: Theatre landing, Anime tab engine, Top 10 mechanics, genre pages/mosaic, See-all page component, customization mechanics, hover-rating card, extension placement, explicit-content policy files.
- Build/run cheatsheet (established this arc): build via `cmd //c "<temp>\colosseum_build_target.bat" <target>` (vcvars + Qt CMake); QML harnesses via `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/test_theatre_deep_catalogue.ps1 -Stage <S>`; one build per out/ dir; kill a running `colosseum.exe` before linking.

## File structure

### Create
- `scripts/theatre_brain/build_imdb_db.py` — download 4 IMDb dumps, derive origLang/isAnime, bake `data/imdb_catalog.db` (title + genre + meta tables). `--selftest` runs embedded pure-function fixtures.
- `native/engine/ImdbCatalog.h` / `.cpp` — read-only seam: `ready()`, `titleCatalog(query, offset, limit)`, `titleFacts(ids)`.
- `tests/imdb_catalog_harness.cpp` — C++ contract harness on a temp SQLite fixture.
- `tests/theatre_shelf_reality_probe.qml` — prints every index shelf's REAL titles through the real engine.
- `tests/test_theatre_shelf_reality.ps1` — runs the probe via `colosseum.exe`, asserts the spec's title-level acceptance checks, saves full lists for Hemanth.

### Modify
- `native/CMakeLists.txt` — ImdbCatalog into the app target + new harness target (additive hunks only).
- `native/main.cpp` — register `ImdbCatalog` context property beside `MalCatalog`.
- `qml/TheatreCatalogRules.js` — ratified inventories, `THRESHOLDS`, `indexQueryFor`, daily pool with language guests; delete Cinemeta-era ranking branches.
- `qml/TheatreApi.js` — `mapImdb`, index-first movies/shows engine, facts-filtered live rows, index See-all paging; delete pool/enrichment machinery + popularity fallback.
- `qml/TheatreWorld.qml`, `qml/TheatreCatalogPage.qml`, `qml/TheatreSeeAllPage.qml` — thread `imdbCatalog` like `malCatalog`.
- `tests/theatre_catalog_rules_harness.qml`, `tests/theatre_api_rows_harness.qml`, `tests/theatre_catalog_page_harness.qml`, `tests/test_theatre_anime_parity.ps1` — updated to the new contracts.

## Interfaces (used consistently across tasks)

```text
ImdbCatalog.ready() -> bool
ImdbCatalog.titleCatalog(query, offset=0, limit=24) -> [row]
  query allowlist: type ("movie"|"series"|"mini"), order ("rating"|"votes"|"year"|"episodes"),
                   ratingMin, votesMin, votesMax, yearFrom, yearTo, runtimeMax,
                   genre, lang, notLang, excludeAnime (bool), episodesMin
  "series" matches tvSeries AND tvMiniSeries; "mini" matches tvMiniSeries only.
  Unknown key or order value -> []. limit clamped to [1,100]. Every value bound.
  row: { tt, type ("movie"|"series"|"mini"), title, year, endYear, runtimeMin,
         rating, votes, episodes, origLang, isAnime, genres: [str] }
ImdbCatalog.titleFacts(ids) -> { tt: { rating, votes, isAnime } }   (ids: ["tt..."]; missing ids absent)

TheatreCatalogRules.THRESHOLDS  -> named dials (movie + series blocks; see Task 3)
TheatreCatalogRules.indexQueryFor(recipe) -> titleCatalog query map, or null for live recipes

TheatreApi.loadCatalogPage(pageKey, options, push)   options gains { imdbCatalog }
TheatreApi.loadRowPage(pin, offset, limit, options, done)  unchanged signature
```

---

### Task 1: Bake `data/imdb_catalog.db`

**Files:**
- Create: `scripts/theatre_brain/build_imdb_db.py`
- (verify only) `.gitignore` — `data/` artifacts already ignored (mal_catalog.db precedent)

- [ ] **Step 1: Write the script with a failing `--selftest`**

Create `scripts/theatre_brain/build_imdb_db.py`. The selftest calls the pure functions before they exist — that is the RED state.

```python
"""build_imdb_db.py — bake IMDb's public datasets into data/imdb_catalog.db.

Theatre deep catalogue (spec 2026-08-02): Movies/Shows shelves need real vote
counts, original language, and true miniseries typing — none of which the keyless
live sources carry. Same doctrine as the MAL/GCD/Goodreads bakes: public dumps in
(datasets.imdbws.com, no key, no login), one SQLite artifact out, the app reads
only the artifact (data/*.db is gitignored). Downloads happen at BAKE TIME on the
dev machine; the runtime never touches the dumps.

Usage:  python scripts/theatre_brain/build_imdb_db.py [--selftest]
Output: data/imdb_catalog.db  (tables: title, genre, meta)
"""
from __future__ import annotations

import gzip
import json
import os
import sqlite3
import sys
import urllib.request
from collections import Counter
from datetime import datetime, timezone

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DB_PATH = os.path.join(REPO, "data", "imdb_catalog.db")
CACHE = os.path.join(REPO, "enrichment", "imdb")
BASE = "https://datasets.imdbws.com/"
DUMPS = ["title.ratings.tsv.gz", "title.basics.tsv.gz",
         "title.episode.tsv.gz", "title.akas.tsv.gz"]

VOTE_FLOOR = 1000          # inclusion dial — raise if the artifact runs fat
KEEP_TYPES = {"movie": "movie", "tvSeries": "series", "tvMiniSeries": "mini"}

# script -> language (definitive for non-Latin scripts)
SCRIPT_RANGES = [
    ("ko", [(0xAC00, 0xD7A3), (0x1100, 0x11FF), (0x3130, 0x318F)]),   # Hangul
    ("ja", [(0x3040, 0x309F), (0x30A0, 0x30FF)]),                     # Kana
    ("zh", [(0x4E00, 0x9FFF)]),                                       # CJK (after Kana!)
    ("ru", [(0x0400, 0x04FF)]),
    ("el", [(0x0370, 0x03FF)]),
    ("ar", [(0x0600, 0x06FF)]),
    ("he", [(0x0590, 0x05FF)]),
    ("th", [(0x0E00, 0x0E7F)]),
    ("hi", [(0x0900, 0x097F)]),
]


def detect_script_lang(text: str) -> str:
    """Non-Latin script of a title -> language code; '' when Latin/unknown.
    Kana beats CJK: Japanese titles mix Kanji + Kana, Chinese has no Kana."""
    counts = Counter()
    for ch in text or "":
        cp = ord(ch)
        for lang, ranges in SCRIPT_RANGES:
            if any(lo <= cp <= hi for lo, hi in ranges):
                counts[lang] += 1
                break
    if not counts:
        return ""
    if counts.get("ja"):
        return "ja"                      # any Kana at all -> Japanese
    return counts.most_common(1)[0][0]


def derive_lang(original_title: str, primary_title: str,
                orig_aka_lang: str, matching_aka_langs: list[str],
                has_us_primary_aka: bool) -> str:
    """Spec §3.2 chain: explicit original-aka language, else script detection,
    else Latin tie-break from akas whose title equals the original title,
    else 'en' only when original==primary AND a US aka repeats it verbatim."""
    if orig_aka_lang:
        return orig_aka_lang
    s = detect_script_lang(original_title)
    if s:
        return s
    if matching_aka_langs:
        return Counter(matching_aka_langs).most_common(1)[0][0]
    if original_title == primary_title and has_us_primary_aka:
        return "en"
    return ""


def keep_title(title_type: str, is_adult: str, votes: int) -> bool:
    return (title_type in KEEP_TYPES and is_adult != "1" and votes >= VOTE_FLOOR)


def is_anime(orig_lang: str, genres: list[str]) -> bool:
    return orig_lang == "ja" and "Animation" in genres


def selftest() -> int:
    ok = True

    def check(cond, msg):
        nonlocal ok
        if not cond:
            ok = False
            print("SELFTEST FAIL:", msg)

    check(detect_script_lang("오징어 게임") == "ko", "hangul -> ko")
    check(detect_script_lang("千と千尋の神隠し") == "ja", "kana+kanji -> ja")
    check(detect_script_lang("英雄") == "zh", "pure kanji, no kana -> zh")
    check(detect_script_lang("Брат") == "ru", "cyrillic -> ru")
    check(detect_script_lang("The Godfather") == "", "latin -> unknown")
    check(derive_lang("x", "x", "ko", [], False) == "ko", "explicit aka lang wins")
    check(derive_lang("오징어 게임", "Squid Game", "", [], False) == "ko", "script fallback")
    check(derive_lang("Le fabuleux destin d'Amélie Poulain", "Amélie", "", ["fr", "fr"], False) == "fr",
          "latin tie-break via matching akas")
    check(derive_lang("Ted Lasso", "Ted Lasso", "", [], True) == "en", "US-verbatim -> en")
    check(derive_lang("Somefilm", "Somefilm", "", [], False) == "", "no signal -> unknown, never guessed")
    check(keep_title("movie", "0", 1000) and not keep_title("movie", "1", 99999), "adult excluded")
    check(not keep_title("tvEpisode", "0", 99999) and keep_title("tvMiniSeries", "0", 1000), "type filter")
    check(not keep_title("movie", "0", VOTE_FLOOR - 1), "vote floor")
    check(is_anime("ja", ["Animation", "Action"]) and not is_anime("ja", ["Drama"])
          and not is_anime("en", ["Animation"]), "isAnime = ja AND Animation")
    print("SELFTEST OK" if ok else "SELFTEST FAILED")
    return 0 if ok else 1


def fetch(name: str) -> str:
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, name)
    if not os.path.exists(path):
        print("downloading", name)
        urllib.request.urlretrieve(BASE + name, path)
    return path


def tsv_rows(path: str):
    with gzip.open(path, "rt", encoding="utf-8", errors="replace", newline="") as f:
        header = f.readline().rstrip("\n").split("\t")
        for line in f:
            yield dict(zip(header, line.rstrip("\n").split("\t")))


def bake() -> int:
    # pass 1 — ratings above the floor
    ratings = {}
    for r in tsv_rows(fetch("title.ratings.tsv.gz")):
        v = int(r["numVotes"])
        if v >= VOTE_FLOOR:
            ratings[r["tconst"]] = (float(r["averageRating"]), v)
    print(f"ratings kept: {len(ratings):,}")

    # pass 2 — basics for kept titles
    titles = {}
    for r in tsv_rows(fetch("title.basics.tsv.gz")):
        tt = r["tconst"]
        if tt not in ratings or not keep_title(r["titleType"], r["isAdult"], ratings[tt][1]):
            continue
        genres = [] if r["genres"] in ("\\N", "") else r["genres"].split(",")
        titles[tt] = {
            "type": KEEP_TYPES[r["titleType"]],
            "title": r["primaryTitle"],
            "originalTitle": r["originalTitle"],
            "year": 0 if r["startYear"] == "\\N" else int(r["startYear"]),
            "endYear": 0 if r["endYear"] == "\\N" else int(r["endYear"]),
            "runtimeMin": 0 if r["runtimeMinutes"] == "\\N" else int(r["runtimeMinutes"]),
            "genres": genres,
            "rating": ratings[tt][0],
            "votes": ratings[tt][1],
            "episodes": 0,
            "origAkaLang": "", "matchLangs": [], "usPrimary": False,
        }
    print(f"titles kept: {len(titles):,}")

    # pass 3 — episode counts for kept series
    for r in tsv_rows(fetch("title.episode.tsv.gz")):
        p = r["parentTconst"]
        if p in titles:
            titles[p]["episodes"] += 1

    # pass 4 — language evidence from akas
    for r in tsv_rows(fetch("title.akas.tsv.gz")):
        t = titles.get(r["titleId"])
        if t is None:
            continue
        lang = "" if r["language"] == "\\N" else r["language"]
        if r["isOriginalTitle"] == "1" and lang and not t["origAkaLang"]:
            t["origAkaLang"] = lang
        if lang and r["title"] == t["originalTitle"]:
            t["matchLangs"].append(lang)
        if r["region"] == "US" and r["title"] == t["title"]:
            t["usPrimary"] = True

    # bake
    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    tmp = DB_PATH + ".building"
    if os.path.exists(tmp):
        os.remove(tmp)
    db = sqlite3.connect(tmp)
    db.executescript("""
        CREATE TABLE title (tt TEXT PRIMARY KEY, type TEXT, title TEXT,
            year INTEGER, endYear INTEGER, runtimeMin INTEGER, genres TEXT,
            rating REAL, votes INTEGER, episodes INTEGER,
            origLang TEXT, isAnime INTEGER);
        CREATE TABLE genre (tt TEXT, genre TEXT);
        CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT);
    """)
    n_lang = 0
    for tt, t in titles.items():
        lang = derive_lang(t["originalTitle"], t["title"], t["origAkaLang"],
                           t["matchLangs"], t["usPrimary"])
        anime = 1 if is_anime(lang, t["genres"]) else 0
        n_lang += 1 if lang else 0
        db.execute("INSERT INTO title VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
                   (tt, t["type"], t["title"], t["year"], t["endYear"],
                    t["runtimeMin"], json.dumps(t["genres"]), t["rating"],
                    t["votes"], t["episodes"], lang, anime))
        db.executemany("INSERT INTO genre VALUES (?,?)",
                       [(tt, g) for g in t["genres"]])
    db.executescript("""
        CREATE INDEX title_type_votes ON title (type, votes DESC);
        CREATE INDEX title_type_rating ON title (type, rating DESC, votes DESC);
        CREATE INDEX title_type_year ON title (type, year);
        CREATE INDEX title_type_lang ON title (type, origLang);
        CREATE INDEX genre_lookup ON genre (genre, tt);
    """)
    db.execute("INSERT INTO meta VALUES ('baked_at', ?)",
               (datetime.now(timezone.utc).isoformat(),))
    db.execute("INSERT INTO meta VALUES ('vote_floor', ?)", (str(VOTE_FLOOR),))
    db.commit()
    db.close()
    if os.path.exists(DB_PATH):
        os.remove(DB_PATH)
    os.rename(tmp, DB_PATH)
    print(f"baked: {DB_PATH} ({os.path.getsize(DB_PATH)/1048576:.1f} MB, "
          f"{len(titles):,} titles, {n_lang:,} with known language)")
    return 0


if __name__ == "__main__":
    sys.exit(selftest() if "--selftest" in sys.argv else bake())
```

- [ ] **Step 2: Prove selftest RED, then GREEN**

The functions are defined above, so to honour TDD: first run with a deliberate typo check — simpler and honest here: run the selftest; every `check(...)` line IS the test list. If any prints `SELFTEST FAIL`, fix before proceeding.

Run: `python scripts/theatre_brain/build_imdb_db.py --selftest`
Expected: `SELFTEST OK`, exit 0. (If `SELFTEST FAILED`, the failing line names the broken rule.)

- [ ] **Step 3: Run the real bake** (downloads ~760 MB once into `enrichment/imdb/`; ratings dump is already cached there from the diagnosis session)

Run: `python scripts/theatre_brain/build_imdb_db.py`
Expected: final line like `baked: ...imdb_catalog.db (25–45 MB, 150,000–220,000 titles, ...)`. If > 50 MB, raise `VOTE_FLOOR` to 2000 and re-bake.

- [ ] **Step 4: SQL acceptance probes against the real artifact**

Run:
```bash
python - <<'EOF'
import sqlite3
db = sqlite3.connect("data/imdb_catalog.db")
q = lambda s, *a: db.execute(s, a).fetchone()
rows = lambda s, *a: db.execute(s, a).fetchall()
# known-title truths
print("shawshank:", q("SELECT rating, votes FROM title WHERE tt='tt0111161'"))
print("chernobyl type:", q("SELECT type FROM title WHERE tt='tt7366338'"))
print("one piece:", q("SELECT origLang, isAnime FROM title WHERE tt='tt0388629'"))
print("all of us are dead:", q("SELECT origLang FROM title WHERE tt='tt14169960'"))
print("ted lasso lang:", q("SELECT origLang FROM title WHERE tt='tt10986410'"))
print("amelie lang:", q("SELECT origLang FROM title WHERE tt='tt0211915'"))
print("simpsons:", q("SELECT origLang, isAnime, episodes FROM title WHERE tt='tt0096697'"))
print("adult leaked:", q("SELECT COUNT(*) FROM title t JOIN genre g ON g.tt=t.tt AND g.genre='Adult'"))
print("lang coverage:", rows("SELECT origLang, COUNT(*) FROM title GROUP BY origLang ORDER BY 2 DESC LIMIT 8"))
EOF
```
Expected: Shawshank `(9.3, 3,2xx,xxx)`; Chernobyl `mini`; One Piece `('ja', 1)`; All of Us Are Dead `('ko',)`; Ted Lasso `('en',)`; Amélie `('fr',)`; Simpsons `('en', 0, 7xx)`; adult leaked `(0,)`. If a language probe misses, fix `derive_lang` evidence collection before continuing — this is the foundation every shelf stands on.

- [ ] **Step 5: Commit** (script only — the artifact is gitignored; verify with `git status --short data/` showing nothing)

```bash
git add scripts/theatre_brain/build_imdb_db.py
git commit -m "feat(theatre): bake imdb catalogue index" -- scripts/theatre_brain/build_imdb_db.py
```

---

### Task 2: Native `ImdbCatalog`

**Files:**
- Create: `native/engine/ImdbCatalog.h`, `native/engine/ImdbCatalog.cpp`
- Create: `tests/imdb_catalog_harness.cpp`
- Modify: `native/CMakeLists.txt` (two additive hunks), `native/main.cpp` (one additive hunk)

- [ ] **Step 1: Write the failing C++ harness**

Create `tests/imdb_catalog_harness.cpp` following the exact pattern of `tests/mal_catalog_rows_harness.cpp` (require()/exit-1, temp-file fixture, PASS line):

```cpp
// ImdbCatalog — allowlisted, bound, paged movies/shows query contract (spec 2026-08-02).
// Builds a temp SQLite fixture with the baked schema, opens it read-only through
// ImdbCatalog, and proves: type/order filters, rating/vote bands, year windows, runtime,
// genre join, lang/notLang, excludeAnime, offset paging without overlap, the limit clamp,
// strict allowlisting, bound values (injection inert), and titleFacts batch lookup.
#include "engine/ImdbCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariantList>
#include <QVariantMap>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool c, const char* m) {
    if (!c) { std::cerr << "FAIL: " << m << '\n'; std::exit(1); }
}
QStringList ttsOf(const QVariantList& rows) {
    QStringList out;
    for (const QVariant& v : rows) out << v.toMap().value("tt").toString();
    return out;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString dbPath = QDir::temp().filePath("imdb_catalog_fixture.db");
    QFile::remove(dbPath);
    {
        QSqlDatabase w = QSqlDatabase::addDatabase("QSQLITE", "writer");
        w.setDatabaseName(dbPath);
        require(w.open(), "fixture opens");
        QSqlQuery q(w);
        require(q.exec("CREATE TABLE title (tt TEXT PRIMARY KEY, type TEXT, title TEXT,"
                       " year INTEGER, endYear INTEGER, runtimeMin INTEGER, genres TEXT,"
                       " rating REAL, votes INTEGER, episodes INTEGER,"
                       " origLang TEXT, isAnime INTEGER)"), "title table");
        require(q.exec("CREATE TABLE genre (tt TEXT, genre TEXT)"), "genre table");
        auto ins = [&](const char* tt, const char* type, const char* title, int year,
                       int runtime, double rating, int votes, int episodes,
                       const char* lang, int anime, const char* genresJson) {
            QSqlQuery i(w);
            i.prepare("INSERT INTO title VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
            i.addBindValue(tt); i.addBindValue(type); i.addBindValue(title);
            i.addBindValue(year); i.addBindValue(0); i.addBindValue(runtime);
            i.addBindValue(genresJson); i.addBindValue(rating); i.addBindValue(votes);
            i.addBindValue(episodes); i.addBindValue(lang); i.addBindValue(anime);
            require(i.exec(), "insert title");
        };
        auto tag = [&](const char* tt, const char* g) {
            QSqlQuery i(w);
            i.prepare("INSERT INTO genre VALUES (?,?)");
            i.addBindValue(tt); i.addBindValue(g);
            require(i.exec(), "insert genre");
        };
        //   tt      type      title            year rt   rating votes    eps lang anime genres
        ins("tt1",  "movie",  "Famous Classic", 1994, 142, 9.3, 3000000,  0, "en", 0, "[\"Drama\"]");
        ins("tt2",  "movie",  "Quiet Gem",      2011, 101, 7.9,   45000,  0, "en", 0, "[\"Drama\"]");
        ins("tt3",  "movie",  "Old Cult",       1988,  96, 7.6,   80000,  0, "en", 0, "[\"Horror\"]");
        ins("tt4",  "movie",  "French Film",    2001, 122, 8.3,  790000,  0, "fr", 0, "[\"Comedy\"]");
        ins("tt5",  "movie",  "Anime Film",     2001, 125, 8.6,  900000,  0, "ja", 1, "[\"Animation\"]");
        ins("tt6",  "movie",  "Western Toon",   2010,  90, 7.8,  300000,  0, "en", 0, "[\"Animation\"]");
        ins("tt7",  "series", "Great Series",   2008,   0, 9.5, 2300000, 62, "en", 0, "[\"Crime\",\"Drama\"]");
        ins("tt8",  "series", "Anime Series",   2013,   0, 9.1,  600000, 90, "ja", 1, "[\"Animation\"]");
        ins("tt9",  "mini",   "True Mini",      2019,   0, 9.3,  900000,  5, "en", 0, "[\"Drama\"]");
        ins("tt10", "series", "Long Runner",    1989,   0, 8.7,  450000, 750, "en", 0, "[\"Comedy\"]");
        ins("tt11", "movie",  "Korean Film",    2019, 132, 8.5,  950000,  0, "ko", 0, "[\"Thriller\"]");
        ins("tt12", "movie",  "No Lang",        2015, 110, 8.0,   20000,  0, "",   0, "[\"Drama\"]");
        for (int i = 0; i < 120; ++i)
            ins(("tt9" + QString::number(100 + i)).toUtf8().constData(), "movie",
                "Filler", 2005, 100, 6.0, 1500 + i, 0, "en", 0, "[]");
        tag("tt3", "Horror"); tag("tt4", "Comedy"); tag("tt5", "Animation");
        tag("tt6", "Animation"); tag("tt8", "Animation");
        w.close();
    }
    QSqlDatabase::removeDatabase("writer");

    ImdbCatalog cat(dbPath);
    require(cat.ready(), "opens read-only");

    { // type + rating/vote floor + order
        QVariantMap q{{"type","movie"},{"ratingMin",8.0},{"votesMin",200000},{"order","rating"}};
        const auto r = cat.titleCatalog(q, 0, 24);
        require(ttsOf(r).first() == "tt1", "top rated first is the famous classic");
        require(!ttsOf(r).contains("tt2"), "below vote floor excluded");
    }
    { // vote band (hidden gems shape) excludes the famous title
        QVariantMap q{{"type","movie"},{"ratingMin",7.4},{"votesMin",10000},{"votesMax",100000},{"order","rating"}};
        const auto tts = ttsOf(cat.titleCatalog(q, 0, 24));
        require(tts.contains("tt2") && tts.contains("tt3") && !tts.contains("tt1"),
                "vote band keeps gems, bans the blockbuster");
    }
    { // year window + excludeAnime
        QVariantMap q{{"type","movie"},{"yearTo",1999},{"order","votes"},{"excludeAnime",true}};
        const auto tts = ttsOf(cat.titleCatalog(q, 0, 24));
        require(tts.contains("tt3") && tts.contains("tt1") && !tts.contains("tt5"), "pre-2000 sans anime");
    }
    { // genre join + excludeAnime: western toon in, anime film out
        QVariantMap q{{"type","movie"},{"genre","Animation"},{"excludeAnime",true},{"order","votes"}};
        require(ttsOf(cat.titleCatalog(q, 0, 24)) == QStringList{"tt6"}, "animation minus anime");
    }
    { // lang + notLang
        QVariantMap fr{{"type","movie"},{"lang","fr"},{"order","rating"}};
        require(ttsOf(cat.titleCatalog(fr, 0, 24)) == QStringList{"tt4"}, "lang=fr");
        QVariantMap intl{{"type","movie"},{"notLang","en"},{"order","votes"},{"excludeAnime",true}};
        const auto tts = ttsOf(cat.titleCatalog(intl, 0, 24));
        require(tts.contains("tt4") && tts.contains("tt11") && !tts.contains("tt12") && !tts.contains("tt1"),
                "international = known non-en only");
    }
    { // series includes minis; mini exact; episodes order + floor
        QVariantMap s{{"type","series"},{"order","rating"},{"votesMin",100000},{"excludeAnime",true}};
        const auto tts = ttsOf(cat.titleCatalog(s, 0, 24));
        require(tts.contains("tt7") && tts.contains("tt9") && !tts.contains("tt8"),
                "series = tvSeries+mini, anime excluded");
        QVariantMap m{{"type","mini"},{"order","votes"}};
        require(ttsOf(cat.titleCatalog(m, 0, 24)) == QStringList{"tt9"}, "mini exact");
        QVariantMap lr{{"type","series"},{"episodesMin",100},{"order","episodes"}};
        require(ttsOf(cat.titleCatalog(lr, 0, 24)) == QStringList{"tt10"}, "long-running by episodes");
    }
    { // runtime
        QVariantMap q{{"type","movie"},{"runtimeMax",120},{"votesMin",10000},{"order","votes"},{"excludeAnime",true}};
        const auto tts = ttsOf(cat.titleCatalog(q, 0, 24));
        require(tts.contains("tt2") && !tts.contains("tt1"), "runtime cap");
    }
    { // paging: no overlap; clamp
        QVariantMap q{{"type","movie"},{"order","votes"}};
        const auto a = ttsOf(cat.titleCatalog(q, 0, 3));
        const auto b = ttsOf(cat.titleCatalog(q, 3, 3));
        for (const auto& t : a) require(!b.contains(t), "offset pages disjoint");
        require(cat.titleCatalog(q, 0, 99999).size() == 100, "limit clamps to 100");
    }
    { // allowlist + binding
        QVariantMap bogus{{"type","movie"},{"surprise","x"}};
        require(cat.titleCatalog(bogus, 0, 24).isEmpty(), "unknown key -> empty");
        QVariantMap badOrder{{"type","movie"},{"order","sideways"}};
        require(cat.titleCatalog(badOrder, 0, 24).isEmpty(), "unknown order -> empty");
        QVariantMap evil{{"type","movie"},{"genre","x'); DROP TABLE title;--"}};
        require(cat.titleCatalog(evil, 0, 24).isEmpty(), "injection matches nothing");
        QVariantMap still{{"type","movie"},{"order","votes"}};
        require(!cat.titleCatalog(still, 0, 24).isEmpty(), "table intact after injection");
    }
    { // titleFacts batch
        const auto f = cat.titleFacts({"tt1", "tt8", "ttMISSING"});
        require(f.contains("tt1") && f.value("tt1").toMap().value("votes").toInt() == 3000000, "facts votes");
        require(f.value("tt8").toMap().value("isAnime").toBool(), "facts isAnime");
        require(!f.contains("ttMISSING"), "missing id absent");
    }

    QFile::remove(dbPath);
    std::cout << "PASS ImdbCatalog allowlisted paged query contract\n";
    return 0;
}
```

- [ ] **Step 2: CMake target (additive hunk after `mal_catalog_rows_harness`)**

In `native/CMakeLists.txt`, add beside the mal harness block, and add `engine/ImdbCatalog.cpp` + `engine/ImdbCatalog.h` to the `colosseum` target source list right after the `engine/MalCatalog.*` lines:

```cmake
# ── Deep Theatre catalogue (spec 2026-08-02): imdb index query contract ──
add_executable(imdb_catalog_harness
    ../tests/imdb_catalog_harness.cpp
    engine/ImdbCatalog.cpp
    engine/ImdbCatalog.h)
target_include_directories(imdb_catalog_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(imdb_catalog_harness PRIVATE Qt6::Core Qt6::Sql)
```

Grep-verify both hunks landed and nothing else moved: `git diff native/CMakeLists.txt` shows exactly two additive hunks. Post one additive-declaration line to `agents/chat.md`.

- [ ] **Step 3: Build to observe RED**

Run: `cmd //c "%TEMP%\colosseum_build_target.bat" imdb_catalog_harness`
Expected: `error C1083: Cannot open include file: 'engine/ImdbCatalog.h'` — the contract exists, the class doesn't.

- [ ] **Step 4: Implement `ImdbCatalog`**

`native/engine/ImdbCatalog.h`:

```cpp
#pragma once
// ImdbCatalog — read-only seam onto the baked IMDb index (data/imdb_catalog.db,
// built by scripts/theatre_brain/build_imdb_db.py). The movies/shows twin of
// MalCatalog: QML paints, C++ decides. Allowlisted keys only, every value bound,
// limit clamped; missing db => ready()==false and every accessor returns empty so
// the Theatre pages honestly omit index shelves.
#include <QObject>
#include <QSqlDatabase>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class ImdbCatalog final : public QObject {
    Q_OBJECT
public:
    explicit ImdbCatalog(const QString& dbPath, QObject* parent = nullptr);
    ~ImdbCatalog() override;

    Q_INVOKABLE bool ready() const { return m_ok; }
    // query allowlist: type(movie|series|mini), order(rating|votes|year|episodes),
    // ratingMin, votesMin, votesMax, yearFrom, yearTo, runtimeMax, genre, lang,
    // notLang, excludeAnime, episodesMin. Unknown key/order -> empty. "series"
    // matches tvSeries+mini rows; "mini" only minis. Rows: {tt,type,title,year,
    // endYear,runtimeMin,rating,votes,episodes,origLang,isAnime,genres[]}.
    Q_INVOKABLE QVariantList titleCatalog(const QVariantMap& query,
                                          int offset = 0, int limit = 24) const;
    // batch facts for live-row filtering: {tt: {rating, votes, isAnime}}
    Q_INVOKABLE QVariantMap titleFacts(const QStringList& ids) const;

private:
    QSqlDatabase m_db;
    bool m_ok = false;
    QString m_conn;
};
```

`native/engine/ImdbCatalog.cpp`:

```cpp
// ImdbCatalog.cpp — see header.
#include "ImdbCatalog.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlQuery>

#include <algorithm>

ImdbCatalog::ImdbCatalog(const QString& dbPath, QObject* parent)
    : QObject(parent), m_conn(QStringLiteral("imdb_catalog"))
{
    QString path = dbPath;
    if (!QFileInfo::exists(path)) {
        const QString beside = QCoreApplication::applicationDirPath()
                               + QStringLiteral("/../../") + dbPath;
        if (QFileInfo::exists(beside)) path = beside;
    }
    if (!QFileInfo::exists(path))
        return;                          // shelves omit honestly without the index
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(path);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    m_ok = m_db.open();
}

ImdbCatalog::~ImdbCatalog()
{
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_conn);
}

QVariantList ImdbCatalog::titleCatalog(const QVariantMap& query, int offset, int limit) const
{
    QVariantList out;
    if (!m_ok) return out;

    static const QSet<QString> allowed = {
        QStringLiteral("type"), QStringLiteral("order"), QStringLiteral("ratingMin"),
        QStringLiteral("votesMin"), QStringLiteral("votesMax"), QStringLiteral("yearFrom"),
        QStringLiteral("yearTo"), QStringLiteral("runtimeMax"), QStringLiteral("genre"),
        QStringLiteral("lang"), QStringLiteral("notLang"), QStringLiteral("excludeAnime"),
        QStringLiteral("episodesMin")
    };
    for (auto it = query.constBegin(); it != query.constEnd(); ++it)
        if (!allowed.contains(it.key())) return out;

    const QString order = query.value(QStringLiteral("order")).toString();
    QString orderSql;
    if (order.isEmpty() || order == QStringLiteral("votes")) orderSql = QStringLiteral("t.votes DESC");
    else if (order == QStringLiteral("rating"))   orderSql = QStringLiteral("t.rating DESC, t.votes DESC");
    else if (order == QStringLiteral("year"))     orderSql = QStringLiteral("t.year DESC, t.votes DESC");
    else if (order == QStringLiteral("episodes")) orderSql = QStringLiteral("t.episodes DESC, t.votes DESC");
    else return out;

    QStringList where;
    QVariantList binds;
    const QString type = query.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("movie"))       { where << QStringLiteral("t.type = 'movie'"); }
    else if (type == QStringLiteral("series")) { where << QStringLiteral("t.type IN ('series','mini')"); }
    else if (type == QStringLiteral("mini"))   { where << QStringLiteral("t.type = 'mini'"); }
    else if (!type.isEmpty())                  { return out; }
    const bool joinGenre = query.contains(QStringLiteral("genre"));
    if (joinGenre) { where << QStringLiteral("g.genre = ?");
                     binds << query.value(QStringLiteral("genre")).toString(); }
    if (query.contains(QStringLiteral("ratingMin"))) { where << QStringLiteral("t.rating >= ?");
                     binds << query.value(QStringLiteral("ratingMin")).toDouble(); }
    if (query.contains(QStringLiteral("votesMin")))  { where << QStringLiteral("t.votes >= ?");
                     binds << query.value(QStringLiteral("votesMin")).toInt(); }
    if (query.contains(QStringLiteral("votesMax")))  { where << QStringLiteral("t.votes <= ?");
                     binds << query.value(QStringLiteral("votesMax")).toInt(); }
    if (query.contains(QStringLiteral("yearFrom")))  { where << QStringLiteral("t.year >= ?");
                     binds << query.value(QStringLiteral("yearFrom")).toInt(); }
    if (query.contains(QStringLiteral("yearTo")))    { where << QStringLiteral("t.year <= ? AND t.year > 0");
                     binds << query.value(QStringLiteral("yearTo")).toInt(); }
    if (query.contains(QStringLiteral("runtimeMax"))){ where << QStringLiteral("t.runtimeMin <= ? AND t.runtimeMin > 0");
                     binds << query.value(QStringLiteral("runtimeMax")).toInt(); }
    if (query.contains(QStringLiteral("lang")))      { where << QStringLiteral("t.origLang = ?");
                     binds << query.value(QStringLiteral("lang")).toString(); }
    if (query.contains(QStringLiteral("notLang")))   { where << QStringLiteral("t.origLang != ? AND t.origLang != ''");
                     binds << query.value(QStringLiteral("notLang")).toString(); }
    if (query.value(QStringLiteral("excludeAnime")).toBool())
        where << QStringLiteral("t.isAnime = 0");
    if (query.contains(QStringLiteral("episodesMin"))){ where << QStringLiteral("t.episodes >= ?");
                     binds << query.value(QStringLiteral("episodesMin")).toInt(); }

    QString sql = QStringLiteral(
        "SELECT t.tt, t.type, t.title, t.year, t.endYear, t.runtimeMin, t.genres, "
        "t.rating, t.votes, t.episodes, t.origLang, t.isAnime FROM ");
    sql += joinGenre ? QStringLiteral("genre g JOIN title t ON t.tt = g.tt")
                     : QStringLiteral("title t");
    if (!where.isEmpty()) sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    sql += QStringLiteral(" ORDER BY ") + orderSql + QStringLiteral(" LIMIT ? OFFSET ?");

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const QVariant& b : binds) q.addBindValue(b);
    q.addBindValue(std::clamp(limit, 1, 100));
    q.addBindValue(std::max(0, offset));
    if (!q.exec()) return out;
    while (q.next()) {
        QVariantMap m;
        m.insert(QStringLiteral("tt"), q.value(0).toString());
        m.insert(QStringLiteral("type"), q.value(1).toString());
        m.insert(QStringLiteral("title"), q.value(2).toString());
        m.insert(QStringLiteral("year"), q.value(3).toInt());
        m.insert(QStringLiteral("endYear"), q.value(4).toInt());
        m.insert(QStringLiteral("runtimeMin"), q.value(5).toInt());
        QVariantList genres;
        const QJsonArray arr = QJsonDocument::fromJson(q.value(6).toString().toUtf8()).array();
        for (const auto& v : arr) genres.append(v.toString());
        m.insert(QStringLiteral("genres"), genres);
        m.insert(QStringLiteral("rating"), q.value(7).toDouble());
        m.insert(QStringLiteral("votes"), q.value(8).toInt());
        m.insert(QStringLiteral("episodes"), q.value(9).toInt());
        m.insert(QStringLiteral("origLang"), q.value(10).toString());
        m.insert(QStringLiteral("isAnime"), q.value(11).toInt() != 0);
        out.append(m);
    }
    return out;
}

QVariantMap ImdbCatalog::titleFacts(const QStringList& ids) const
{
    QVariantMap out;
    if (!m_ok || ids.isEmpty()) return out;
    for (int start = 0; start < ids.size(); start += 100) {
        const QStringList chunk = ids.mid(start, 100);
        QStringList marks;
        for (int i = 0; i < chunk.size(); ++i) marks << QStringLiteral("?");
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT tt, rating, votes, isAnime FROM title WHERE tt IN (")
                  + marks.join(QStringLiteral(",")) + QStringLiteral(")"));
        for (const QString& id : chunk) q.addBindValue(id);
        if (!q.exec()) continue;
        while (q.next())
            out.insert(q.value(0).toString(), QVariantMap{
                {QStringLiteral("rating"), q.value(1).toDouble()},
                {QStringLiteral("votes"), q.value(2).toInt()},
                {QStringLiteral("isAnime"), q.value(3).toInt() != 0}});
    }
    return out;
}
```

- [ ] **Step 5: Build + run GREEN**

Run: `cmd //c "%TEMP%\colosseum_build_target.bat" imdb_catalog_harness` then
`PATH="/c/Qt/6.11.1/msvc2022_64/bin:$PATH" native/build-msvc/imdb_catalog_harness.exe`
Expected: `PASS ImdbCatalog allowlisted paged query contract`, exit 0.

- [ ] **Step 6: Register the context property**

In `native/main.cpp`, directly after the `MalCatalog` registration line (grep `MalCatalog` to find it), add:

```cpp
    auto* imdbCatalog = new ImdbCatalog(QStringLiteral("data/imdb_catalog.db"), &app);
    engine.rootContext()->setContextProperty(QStringLiteral("ImdbCatalog"), imdbCatalog);
```

with `#include "engine/ImdbCatalog.h"` beside the MalCatalog include. Grep-verify: `grep -n "ImdbCatalog" native/main.cpp` shows exactly the include + two lines.

- [ ] **Step 7: Build the app, then commit**

Run: `cmd //c "%TEMP%\colosseum_build_target.bat" colosseum` → `BUILD_OK` (kill any running colosseum.exe first).

```bash
git add native/engine/ImdbCatalog.h native/engine/ImdbCatalog.cpp tests/imdb_catalog_harness.cpp
git add -p native/CMakeLists.txt native/main.cpp   # ONLY the ImdbCatalog hunks
git diff --cached --check
git commit -m "feat(theatre): add native imdb catalogue seam" -- native/engine/ImdbCatalog.h native/engine/ImdbCatalog.cpp tests/imdb_catalog_harness.cpp native/CMakeLists.txt native/main.cpp
```

---

### Task 3: Rules — ratified inventories, thresholds, recipe→query mapping

**Files:**
- Modify: `qml/TheatreCatalogRules.js`
- Modify: `tests/theatre_catalog_rules_harness.qml`
- Modify: `tests/test_theatre_anime_parity.ps1` (only if its rules-string greps break — it greps anime titles, which do not change)

- [ ] **Step 1: Rewrite the rules-harness inventory + query sections (failing first)**

In `tests/theatre_catalog_rules_harness.qml`, replace the movie/show inventory assertions and the ranking-fixture assertions (keep: daily determinism, dedupe, applyCustomization, placeExtensions, no-award/no-blurb checks) with:

```qml
// ---- Ratified 2026-08-02 inventories ----
var movies = Rules.defaultRows("movies");
ok(movies[0].key === "top-10" && movies[0].ranked, "Movies Top 10 first");
["recently-released","top-rated","hidden-gems","cult-classics","under-two-hours",
 "documentary-movies","animated-movies","international-cinema","japanese-cinema",
 "korean-cinema","french-cinema","2020s-movies","1970s-movies"]
    .forEach(function(k){ ok(has(movies, k), "movies has " + k); });
ok(!has(movies, "all-time-greats"), "All-Time Greats retired");
ok(!movies.some(function(r){ return /award|in.?theaters|coming.?soon/i.test(r.key + r.title); }),
   "no awards or fabricated freshness");

var shows = Rules.defaultRows("shows");
["top-10","currently-airing","recently-premiered","top-rated","hidden-gems","cult-classics",
 "long-running-series","limited-series","drama-series","comedy-series","crime-and-mystery",
 "science-fiction-and-fantasy","documentary-series","animated-series","korean-drama"]
    .forEach(function(k){ ok(has(shows, k), "shows has " + k); });
ok(!has(shows, "british-television"), "British Television dropped");
ok(!has(shows, "all-time-great-series"), "All-Time Great Series retired");

// ---- Thresholds are dials with sane relationships (never pin exact values) ----
var T = Rules.THRESHOLDS;
ok(T.movie.HG_VOTES_MAX < T.movie.TR_VOTES, "movie gems band sits below the top-rated floor");
ok(T.series.HG_VOTES_MAX < T.series.TR_VOTES, "series gems band below top-rated floor");
ok(T.movie.HG_VOTES_MIN > 0 && T.movie.CC_VOTES_MIN > 0, "bands have lower edges");

// ---- indexQueryFor: pure recipe -> allowlisted query ----
function q(key, page) {
    var defs = Rules.defaultRows(page || "movies");
    for (var i = 0; i < defs.length; i++) if (defs[i].key === key) return Rules.indexQueryFor(defs[i].recipe);
    return undefined;
}
ok(q("top-10") === null, "live recipes map to null (no index query)");
ok(q("recently-released") === null, "recently-released stays live");
ok(q("currently-airing", "shows") === null, "currently-airing stays live");
var tr = q("top-rated");
ok(tr.type === "movie" && tr.order === "rating" && tr.ratingMin === T.movie.TR_RATING
   && tr.votesMin === T.movie.TR_VOTES && tr.excludeAnime === true, "top rated query");
var hg = q("hidden-gems");
ok(hg.votesMin === T.movie.HG_VOTES_MIN && hg.votesMax === T.movie.HG_VOTES_MAX, "gems band");
var cc = q("cult-classics");
ok(cc.yearTo === 1999 && cc.votesMax === T.movie.CC_VOTES_MAX, "cult classics pre-2000 band");
ok(q("under-two-hours").runtimeMax === 120, "runtime query");
ok(q("animated-movies").genre === "Animation" && q("animated-movies").excludeAnime === true,
   "animation minus anime");
ok(q("international-cinema").notLang === "en", "international = non-english");
ok(q("korean-cinema").lang === "ko" && q("japanese-cinema").lang === "ja"
   && q("french-cinema").lang === "fr", "language shelves");
ok(q("2010s-movies").yearFrom === 2010 && q("2010s-movies").yearTo === 2019, "decade window");
ok(q("limited-series", "shows").type === "mini", "limited series is exact mini type");
ok(q("long-running-series", "shows").order === "episodes", "long-running by episodes");
ok(q("korean-drama", "shows").lang === "ko" && q("korean-drama", "shows").type === "series",
   "korean drama is language-based");
// genreAny recipes fan out client-side: mapping returns per-genre queries
var cm = Rules.indexQueriesFor({ kind: "imdbGenreAny", genres: ["Crime","Mystery"], type: "series" });
ok(cm.length === 2 && cm[0].genre === "Crime" && cm[1].genre === "Mystery", "genreAny fans out");
// every index query excludes anime
["top-rated","hidden-gems","cult-classics","animated-movies","korean-cinema"].forEach(function(k){
    ok(q(k).excludeAnime === true, k + " excludes anime");
});

// ---- daily rotation pool includes the language guests, no Holiday recipe ----
var week = {};
for (var d = 0; d < 14; d++)
    Rules.dailyRows(Date.UTC(2026, 7, 1 + d), 6).forEach(function(r){ week[r.key] = r.recipe; });
ok(Object.keys(week).some(function(k){ return /daily-(spanish|italian|german|swedish|danish)/.test(k); }),
   "language guests rotate in across two weeks");
ok(!Object.keys(week).some(function(k){ return /holiday/.test(k); }),
   "no Holiday recipe (IMDb has no honest signal)");
```

Also DELETE the old assertions for: `topRated`/`hiddenGems` Cinemeta ranking fixtures, country/countryExclude/decade/genre/runtimeUnder/status/longRunning/seasonExactly `rankItems` fixtures, and the movies `under-two-hours`/`all-time-greats` inventory checks. KEEP: `recent` ordering fixture, dedupe fixture, explicit-prefilter fixture, applyCustomization block, placeExtensions block, daily determinism block.

- [ ] **Step 2: Run to observe RED**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/test_theatre_deep_catalogue.ps1 -Stage Rules`
Expected: FAILS listing `movies has cult-classics`, `THRESHOLDS` undefined, `indexQueryFor is not a function`, …

- [ ] **Step 3: Implement in `TheatreCatalogRules.js`**

Replace `MOVIE_ROWS`/`SHOW_ROWS`/`MOVIE_DAILY_POOL` and add `THRESHOLDS` + `indexQueryFor` + `indexQueriesFor`. Delete from `rankItems` the branches: `topRated`, `hiddenGems`, `runtimeUnder`, `genre`, `genreAny`, `country`, `countryExclude`, `decade`, `status`, `longRunning`, `seasonExactly` and the `popularityOf` helper (keep: `top` passthrough/cap, `recent`, anime passthrough kinds, `dedupe`, `weighted` stays for anime vote paths).

```javascript
// ── Threshold dials (spec §4.3). Task 6 calibrates against REAL output; tests assert
// relationships only. IMDb votes run ~100× TMDB's (Shawshank: 3.2M).
var THRESHOLDS = {
    movie:  { TR_RATING: 8.0, TR_VOTES: 200000,
              HG_RATING: 7.4, HG_VOTES_MIN: 10000, HG_VOTES_MAX: 100000,
              CC_RATING: 7.2, CC_VOTES_MIN: 10000, CC_VOTES_MAX: 250000 },
    series: { TR_RATING: 8.2, TR_VOTES: 100000,
              HG_RATING: 7.5, HG_VOTES_MIN: 5000, HG_VOTES_MAX: 75000,
              CC_RATING: 7.5, CC_VOTES_MIN: 5000, CC_VOTES_MAX: 150000 },
    RECENT_VOTE_FLOOR: 500,
    GENRE_VOTE_FLOOR: 5000,
    LR_EPISODES: 100
};

function MOVIE_ROWS() {
    var T = THRESHOLDS.movie;
    return [
        house("top-10",             "Top 10",              0,   { kind: "top", limit: 10 }, true),
        house("recently-released",  "Recently Released",   10,  { kind: "recent" }),
        house("top-rated",          "Top Rated",           20,  { kind: "imdbBand", type: "movie", order: "rating", ratingMin: T.TR_RATING, votesMin: T.TR_VOTES }),
        house("hidden-gems",        "Hidden Gems",         30,  { kind: "imdbBand", type: "movie", order: "rating", ratingMin: T.HG_RATING, votesMin: T.HG_VOTES_MIN, votesMax: T.HG_VOTES_MAX }),
        house("cult-classics",      "Cult Classics",       40,  { kind: "imdbBand", type: "movie", order: "rating", ratingMin: T.CC_RATING, votesMin: T.CC_VOTES_MIN, votesMax: T.CC_VOTES_MAX, yearTo: 1999 }),
        house("under-two-hours",    "Under Two Hours",     50,  { kind: "imdbBand", type: "movie", order: "votes", runtimeMax: 120, votesMin: THRESHOLDS.GENRE_VOTE_FLOOR }),
        house("documentary-movies", "Documentary Movies",  60,  { kind: "imdbGenre", type: "movie", genre: "Documentary" }),
        house("animated-movies",    "Animated Movies",     70,  { kind: "imdbGenre", type: "movie", genre: "Animation" }),
        house("international-cinema","International Cinema",80, { kind: "imdbIntl", type: "movie" }),
        house("japanese-cinema",    "Japanese Cinema",     90,  { kind: "imdbLang", type: "movie", lang: "ja" }),
        house("korean-cinema",      "Korean Cinema",       100, { kind: "imdbLang", type: "movie", lang: "ko" }),
        house("french-cinema",      "French Cinema",       110, { kind: "imdbLang", type: "movie", lang: "fr" }),
        house("2020s-movies",       "2020s Movies",        120, { kind: "imdbDecade", type: "movie", from: 2020, to: 2029 }),
        house("2010s-movies",       "2010s Movies",        130, { kind: "imdbDecade", type: "movie", from: 2010, to: 2019 }),
        house("2000s-movies",       "2000s Movies",        140, { kind: "imdbDecade", type: "movie", from: 2000, to: 2009 }),
        house("1990s-movies",       "1990s Movies",        150, { kind: "imdbDecade", type: "movie", from: 1990, to: 1999 }),
        house("1980s-movies",       "1980s Movies",        160, { kind: "imdbDecade", type: "movie", from: 1980, to: 1989 }),
        house("1970s-movies",       "1970s Movies",        170, { kind: "imdbDecade", type: "movie", from: 1970, to: 1979 })
    ];
}

function SHOW_ROWS() {
    var T = THRESHOLDS.series;
    return [
        house("top-10",              "Top 10",                      0,   { kind: "top", limit: 10 }, true),
        house("currently-airing",    "Currently Airing",            10,  { kind: "statusLive", status: "Continuing" }),
        house("recently-premiered",  "Recently Premiered",          20,  { kind: "recent" }),
        house("top-rated",           "Top Rated",                   30,  { kind: "imdbBand", type: "series", order: "rating", ratingMin: T.TR_RATING, votesMin: T.TR_VOTES }),
        house("hidden-gems",         "Hidden Gems",                 40,  { kind: "imdbBand", type: "series", order: "rating", ratingMin: T.HG_RATING, votesMin: T.HG_VOTES_MIN, votesMax: T.HG_VOTES_MAX }),
        house("cult-classics",       "Cult Classics",               50,  { kind: "imdbBand", type: "series", order: "rating", ratingMin: T.CC_RATING, votesMin: T.CC_VOTES_MIN, votesMax: T.CC_VOTES_MAX, yearTo: 1999 }),
        house("long-running-series", "Long-Running Series",         60,  { kind: "imdbBand", type: "series", order: "episodes", episodesMin: THRESHOLDS.LR_EPISODES }),
        house("limited-series",      "Limited Series",              70,  { kind: "imdbBand", type: "mini", order: "votes", votesMin: THRESHOLDS.GENRE_VOTE_FLOOR }),
        house("drama-series",        "Drama Series",                80,  { kind: "imdbGenre", type: "series", genre: "Drama" }),
        house("comedy-series",       "Comedy Series",               90,  { kind: "imdbGenre", type: "series", genre: "Comedy" }),
        house("crime-and-mystery",   "Crime and Mystery",           100, { kind: "imdbGenreAny", type: "series", genres: ["Crime", "Mystery"] }),
        house("science-fiction-and-fantasy", "Science Fiction and Fantasy", 110, { kind: "imdbGenreAny", type: "series", genres: ["Sci-Fi", "Fantasy"] }),
        house("documentary-series",  "Documentary Series",          120, { kind: "imdbGenre", type: "series", genre: "Documentary" }),
        house("animated-series",     "Animated Series",             130, { kind: "imdbGenre", type: "series", genre: "Animation" }),
        house("korean-drama",        "Korean Drama",                140, { kind: "imdbLang", type: "series", lang: "ko" })
    ];
}

var MOVIE_DAILY_POOL = [
    { key: "daily-crime-thrillers", title: "Crime Thrillers",  recipe: { kind: "imdbGenre", type: "movie", genre: "Crime" } },
    { key: "daily-science-fiction", title: "Science Fiction",  recipe: { kind: "imdbGenre", type: "movie", genre: "Sci-Fi" } },
    { key: "daily-family-movies",   title: "Family Movies",    recipe: { kind: "imdbGenre", type: "movie", genre: "Family" } },
    { key: "daily-90-minute",       title: "90-Minute Movies", recipe: { kind: "imdbBand", type: "movie", order: "votes", runtimeMax: 95, votesMin: THRESHOLDS.GENRE_VOTE_FLOOR } },
    { key: "daily-classic-horror",  title: "Classic Horror",   recipe: { kind: "imdbGenre", type: "movie", genre: "Horror", yearTo: 1999 } },
    { key: "daily-war",             title: "War Stories",      recipe: { kind: "imdbGenre", type: "movie", genre: "War" } },
    { key: "daily-westerns",        title: "Westerns",         recipe: { kind: "imdbGenre", type: "movie", genre: "Western" } },
    { key: "daily-mystery",         title: "Mystery",          recipe: { kind: "imdbGenre", type: "movie", genre: "Mystery" } },
    { key: "daily-romance",         title: "Romance",          recipe: { kind: "imdbGenre", type: "movie", genre: "Romance" } },
    { key: "daily-spanish",         title: "Spanish-Language Cinema", recipe: { kind: "imdbLang", type: "movie", lang: "es" } },
    { key: "daily-italian",         title: "Italian Cinema",   recipe: { kind: "imdbLang", type: "movie", lang: "it" } },
    { key: "daily-german",          title: "German Cinema",    recipe: { kind: "imdbLang", type: "movie", lang: "de" } },
    { key: "daily-swedish",         title: "Swedish Cinema",   recipe: { kind: "imdbLang", type: "movie", lang: "sv" } },
    { key: "daily-danish",          title: "Danish Cinema",    recipe: { kind: "imdbLang", type: "movie", lang: "da" } }
];

// ── recipe -> ONE allowlisted ImdbCatalog query; null for live recipes.
function indexQueryFor(recipe) {
    recipe = recipe || {};
    function base(extra) {
        var q = { type: recipe.type, excludeAnime: true };
        for (var k in extra) if (extra[k] !== undefined) q[k] = extra[k];
        return q;
    }
    switch (recipe.kind) {
    case "imdbBand":
        return base({ order: recipe.order || "rating", ratingMin: recipe.ratingMin,
                      votesMin: recipe.votesMin, votesMax: recipe.votesMax,
                      yearTo: recipe.yearTo, runtimeMax: recipe.runtimeMax,
                      episodesMin: recipe.episodesMin });
    case "imdbGenre":
        return base({ order: "votes", genre: recipe.genre, yearTo: recipe.yearTo,
                      votesMin: THRESHOLDS.GENRE_VOTE_FLOOR });
    case "imdbLang":
        return base({ order: "rating", lang: recipe.lang,
                      votesMin: THRESHOLDS.GENRE_VOTE_FLOOR });
    case "imdbIntl":
        return base({ order: "votes", notLang: "en",
                      votesMin: THRESHOLDS.GENRE_VOTE_FLOOR });
    case "imdbDecade":
        return base({ order: "votes", yearFrom: recipe.from, yearTo: recipe.to,
                      votesMin: THRESHOLDS.GENRE_VOTE_FLOOR });
    default:
        return null;    // top / recent / statusLive / imdbGenreAny (fan-out) / extension
    }
}

// ── genreAny recipes fan out to one query per genre; caller merges + dedupes.
function indexQueriesFor(recipe) {
    if (!recipe || recipe.kind !== "imdbGenreAny") {
        var one = indexQueryFor(recipe);
        return one ? [one] : [];
    }
    return (recipe.genres || []).map(function(g) {
        return indexQueryFor({ kind: "imdbGenre", type: recipe.type, genre: g });
    });
}
```

Note: `indexQueryFor` returns `null` for `imdbGenreAny`; `indexQueriesFor` is the API that always answers. The `house()` helper, `dailyRows` seeding, `applyCustomization`, `placeExtensions` stay untouched.

- [ ] **Step 4: Run GREEN + adjacent statics**

Run: `-Stage Rules` → `THEATRE_CATALOG_RULES_OK`.
Run: `tests/test_theatre_top10_genre_boxes.ps1` and `tests/test_theatre_anime_parity.ps1` → both pass (anime titles unchanged; top-10 key intact).

- [ ] **Step 5: Commit**

```bash
git add qml/TheatreCatalogRules.js tests/theatre_catalog_rules_harness.qml
git commit -m "feat(theatre): ratified imdb shelf inventories and query mapping" -- qml/TheatreCatalogRules.js tests/theatre_catalog_rules_harness.qml
```

---

### Task 4: TheatreApi — index-first engine, facts-filtered live rows

**Files:**
- Modify: `qml/TheatreApi.js`
- Modify: `tests/theatre_api_rows_harness.qml`

- [ ] **Step 1: Rewrite the movies section of the api harness (failing first)**

In `tests/theatre_api_rows_harness.qml`, delete the old movie fixtures/enrichment instrumentation (`meta()`, `topMovies()`, `animationMetas()`, `metaFixtureFor`, the pump Timer, metaActive/metaMax/urlCounts, and the whole enrichment/concurrency assertion block). Keep the anime section untouched. Add:

```qml
// ---- fake ImdbCatalog: captures queries, serves deterministic rows -------------------
property var imdbQueries: []
function idxRow(tt, title, extra) {
    var r = { tt: tt, type: "movie", title: title, year: 2015, endYear: 0, runtimeMin: 110,
              rating: 8.0, votes: 50000, episodes: 0, origLang: "en", isAnime: false, genres: ["Drama"] };
    for (var k in (extra || {})) r[k] = extra[k];
    return r;
}
function fakeImdb() {
    return {
        ready: function() { return true; },
        titleCatalog: function(query, offset, limit) {
            harness.imdbQueries.push({ query: query, offset: offset, limit: limit });
            if (query.lang === "fr") return [ harness.idxRow("tt401", "Le Film", { origLang: "fr" }) ];
            if (query.votesMax !== undefined)
                return [ harness.idxRow("tt402", "Quiet Gem", { votes: 40000, rating: 7.9 }) ];
            return [ harness.idxRow("tt403", "Famous Classic", { votes: 3000000, rating: 9.3 }),
                     harness.idxRow("tt404", "Second Classic", { votes: 2000000, rating: 9.2 }) ];
        },
        titleFacts: function(ids) {
            // tt900 is anime, tt777 has 40 votes (shovelware), tt403 is famous
            var out = {};
            if (ids.indexOf("tt900") !== -1) out["tt900"] = { rating: 8.6, votes: 900000, isAnime: true };
            if (ids.indexOf("tt777") !== -1) out["tt777"] = { rating: 6.1, votes: 40, isAnime: false };
            if (ids.indexOf("tt403") !== -1) out["tt403"] = { rating: 9.3, votes: 3000000, isAnime: false };
            return out;
        }
    };
}
// live Cinemeta top fixture: one anime title + one shovelware title that must be filtered
function liveTop() {
    function m(id, name, year) {
        return { id: id, imdb_id: id, type: "movie", name: name, poster: "p/" + id,
                 imdbRating: "7.0", releaseInfo: String(year), genres: ["Drama"] };
    }
    var out = [];
    for (var i = 0; i < 10; i++) out.push(m("tt6" + i, "Live " + i, 2024));
    out.push(m("tt900", "Sneaky Anime", 2024));
    out.push(m("tt777", "Shovelware", 2026));
    return out;
}
function movieAdapter(url, done) {
    if (url.indexOf("/catalog/movie/top") !== -1) { done({ metas: liveTop() }); return; }
    done({ metas: [] });
}

function runMoviesTests() {
    TheatreApi.resetLiveCaches();
    harness.imdbQueries = [];
    TheatreApi.setRequestAdapter(harness.movieAdapter);
    var captured = null;
    TheatreApi.loadCatalogPage("movies", { generation: 5, showExplicit: false,
                                           imdbCatalog: harness.fakeImdb(),
                                           explicitFilter: harness.explicitFilter },
                               function(p) { if (p.generation === 5) captured = p; });
    pageAssert.captured = function() { return captured; };
    pageAssert.start();
}
Timer {
    id: pageAssert; interval: 250; repeat: false
    property var captured: null
    onTriggered: {
        var p = pageAssert.captured();
        harness.ok(p !== null, "movies page published");
        var rows = p.rows;
        harness.ok(rows[0].key === "top-10" && rows[0].ranked, "Top 10 first");
        // index shelves present with mapped items
        var tr = harness.rowByKey(rows, "top-rated");
        harness.ok(tr && tr.items[0].id === "tt403" && tr.items[0].imdbRating === "9.3"
                   && tr.items[0].cover.indexOf("tt403") !== -1,
                   "index row mapped: id, rating string, metahub poster");
        harness.ok(harness.rowByKey(rows, "hidden-gems").items[0].id === "tt402", "gems from band query");
        harness.ok(harness.rowByKey(rows, "french-cinema").items[0].id === "tt401", "language shelf");
        // every index query excluded anime
        harness.ok(harness.imdbQueries.length > 0
                   && harness.imdbQueries.every(function(c){ return c.query.excludeAnime === true; }),
                   "all index queries carry excludeAnime");
        // live Top 10: anime + shovelware filtered by facts, still capped at 10
        var top = harness.rowByKey(rows, "top-10");
        harness.ok(!harness.itemById(top.items, "tt900"), "anime filtered from live Top 10 via facts");
        harness.ok(top.items.length === 10, "Top 10 capped at 10");
        // recently released: shovelware (40 votes) dropped, anime dropped
        var rec = harness.rowByKey(rows, "recently-released");
        harness.ok(rec && !harness.itemById(rec.items, "tt777"), "vote-floor drops shovelware");
        harness.ok(!harness.itemById(rec.items, "tt900"), "anime dropped from recent");
        // no sub/blurb anywhere
        harness.ok(rows.every(function(r){ return r.sub === undefined && r.blurb === undefined; }), "no blurbs");
        // See-all: index pin pages the index with the offset
        harness.imdbQueries = [];
        TheatreApi.loadRowPage({ pageKey: "movies", sourceKind: "house", rowKey: "hidden-gems" },
                               40, 40, { generation: 6, imdbCatalog: harness.fakeImdb() }, function(res) {
            harness.ok(res.generation === 6 && res.items.length > 0, "index See-all serves a page");
            harness.ok(harness.imdbQueries.length === 1 && harness.imdbQueries[0].offset === 40
                       && harness.imdbQueries[0].limit === 40, "index See-all passes offset/limit");
            TheatreApi.loadRowPage({ pageKey: "movies", sourceKind: "house", rowKey: "nope" },
                                   0, 40, { generation: 7 }, function(res2) {
                harness.ok(res2.error.length > 0 && res2.items.length === 0, "unknown row errors honestly");
                harness.runAnimeTests();
            });
        });
    }
}
```

Wire `runMoviesTests()` as the entry point of the existing start Timer (replacing the old movie test start), keeping `runAnimeTests()` → `finish()` as the chain tail.

- [ ] **Step 2: Run to observe RED**

Run: `-Stage ApiRows`
Expected: FAILS — index shelves absent (`top-rated` missing), because the engine still builds Cinemeta pools.

- [ ] **Step 3: Implement the new engine in `TheatreApi.js`**

DELETE: `buildPool`, `enrichPool`, `collectGenres`, `collectNeededFields`, `itemMissing`, `mergeMetaFields`, `loadMetaCached`, `fullMetaUrl`, `MAX_META_WORKERS`, `META_CACHE_TTL_MS`, `ENRICH_CAP`, `metaCache`, `metaInFlight` (trim `resetLiveCaches` accordingly). ADD:

```javascript
function posterFor(tt)      { return "https://live.metahub.space/poster/small/" + tt + "/img"; }
function backgroundFor(tt)  { return "https://live.metahub.space/background/medium/" + tt + "/img"; }

function mapImdb(row, index) {
    var t = tone(index);
    return {
        id: row.tt,
        type: row.type === "movie" ? "movie" : "series",
        caption: row.title, title: row.title,
        blurb: "A featured title.",
        cover: posterFor(row.tt), art: backgroundFor(row.tt),
        ghost: row.type === "movie" ? "T" : "S",
        c1: t[0], c2: t[1], progress: -1,
        imdbRating: row.rating > 0 ? String(row.rating.toFixed ? row.rating.toFixed(1) : row.rating) : "",
        releaseInfo: row.year > 0 ? String(row.year) : "",
        runtime: row.runtimeMin > 0 ? (row.runtimeMin + " min") : "",
        genres: row.genres || [], votes: row.votes || 0,
        origLang: row.origLang || "", source: "IMDb"
    };
}

// merged + deduped index rows for one recipe (imdbGenreAny fans out)
function imdbRowsFor(imdb, recipe, offset, limit) {
    var queries = Rules.indexQueriesFor(recipe);
    if (!queries.length) return [];
    if (queries.length === 1) return imdb.titleCatalog(queries[0], offset, limit) || [];
    var merged = [], seen = {};
    for (var i = 0; i < queries.length; i++) {
        var part = imdb.titleCatalog(queries[i], offset, limit) || [];
        for (var j = 0; j < part.length; j++)
            if (!seen[part[j].tt]) { seen[part[j].tt] = true; merged.push(part[j]); }
    }
    merged.sort(function(a, b) { return (b.votes || 0) - (a.votes || 0); });
    return merged.slice(0, limit);
}

// drop live-row items the index disqualifies: anime always; shovelware on recent shelves.
function filterLiveItems(imdb, items, dropLowVotes) {
    if (!imdb || !imdb.ready || !imdb.ready()) return items;
    var ids = items.map(function(it) { return it.id; })
                   .filter(function(id) { return String(id).indexOf("tt") === 0; });
    var facts = imdb.titleFacts(ids) || {};
    return items.filter(function(it) {
        var f = facts[it.id];
        if (!f) return true;                          // unknown to the index -> keep
        if (f.isAnime) return false;                  // anime lives in the Anime tab
        if (dropLowVotes && f.votes < Rules.THRESHOLDS.RECENT_VOTE_FLOOR) return false;
        return true;
    });
}
```

Replace `loadMoviesShowsDeep` with:

```javascript
function loadMoviesShowsDeep(pageKey, options, push) {
    var type = pageKey === "shows" ? "series" : "movie";
    var generation = options.generation || 0;
    var showExplicit = options.showExplicit === true;
    var explicitFilter = options.explicitFilter || null;
    var imdb = options.imdbCatalog || null;
    var now = options.nowMs || Date.now();

    var defs = Rules.defaultRows(pageKey);
    if (pageKey === "movies") defs = defs.concat(Rules.dailyRows(now, 6));
    defs.sort(function(a, b) { return a.placement - b.placement; });

    var rowData = {};          // key -> items
    var extMainRows = [], extExtensionRows = [];
    var liveDone = false, extDone = false;
    function keep(items) {
        return explicitFilter
            ? items.filter(function(it) { return explicitFilter(it, showExplicit); }) : items;
    }
    function publish() {
        var houseRows = [];
        for (var i = 0; i < defs.length; i++) {
            var def = defs[i];
            var items = keep(rowData[def.key] || []);
            var cap = def.recipe.kind === "top" ? (def.recipe.limit || 10) : PREVIEW_ROW_CAP;
            if (items.length > 0) houseRows.push(rowFromDef(def, items.slice(0, cap)));
        }
        var merged = houseRows.concat(extMainRows);
        merged.sort(function(a, b) { return (a.placement || 0) - (b.placement || 0); });
        push({ pageKey: pageKey, generation: generation,
               rows: merged.concat(extExtensionRows),
               loading: !(liveDone && extDone), error: "" });
    }

    // phase 1 — the index paints every offline shelf synchronously
    if (imdb && imdb.ready && imdb.ready()) {
        for (var i = 0; i < defs.length; i++) {
            var recipe = defs[i].recipe;
            if (Rules.indexQueriesFor(recipe).length === 0) continue;   // live recipe
            var rows = imdbRowsFor(imdb, recipe, 0, PREVIEW_ROW_CAP);
            if (rows.length) rowData[defs[i].key] = rows.map(mapImdb);
        }
    }
    publish();

    // phase 2 — extensions load in parallel (unchanged contract)
    loadExtensionRows(pageKey, type, defs,
        { showExplicit: showExplicit, explicitFilter: explicitFilter },
        function() { publish(); },
        function(main, ext) { extMainRows = main; extExtensionRows = ext; extDone = true; publish(); });

    // phase 3 — live rows from Cinemeta, facts-filtered through the index
    cinemetaCatalog(type, "", function(metas) {
        var mapped = (metas || []).map(mapCinemeta);
        var clean = filterLiveItems(imdb, mapped, false);
        rowData["top-10"] = clean;
        var recentKey = pageKey === "shows" ? "recently-premiered" : "recently-released";
        rowData[recentKey] = Rules.rankItems({ kind: "recent" },
                                             filterLiveItems(imdb, mapped, true), now);
        if (pageKey === "shows")
            rowData["currently-airing"] = clean.filter(function(it) {
                return String(it.status || "") === "Continuing";
            });
        liveDone = true;
        publish();
    });
}
```

(`mapCinemeta` keeps its factual fields from the previous arc — `status` is read straight off the catalog previews, verified live 2026-08-01.)

Replace the house branch of `loadRowPage` with:

```javascript
    var def = findDef(pin.pageKey, pin.rowKey);
    if (!def) { done({ generation: generation, items: [], hasMore: false, error: "unknown row: " + pin.rowKey }); return; }
    var queries = Rules.indexQueriesFor(def.recipe);
    if (queries.length > 0) {                          // index shelf: page the artifact
        var imdb = options.imdbCatalog || null;
        if (!imdb || !imdb.ready || !imdb.ready()) {
            done({ generation: generation, items: [], hasMore: false, error: "catalogue index offline" });
            return;
        }
        var rows = imdbRowsFor(imdb, def.recipe, offset, limit);
        var items = rows.map(mapImdb);
        if (explicitFilter) items = items.filter(function(it) { return explicitFilter(it, showExplicit); });
        done({ generation: generation, items: items, hasMore: rows.length >= limit, error: "" });
        return;
    }
    // live shelf (top-10 / recently-released / recently-premiered / currently-airing)
    var type = pin.pageKey === "shows" ? "series" : "movie";
    cinemetaCatalogPaged(type, "", offset, function(metas) {
        var mapped = (metas || []).map(mapCinemeta);
        var clean = filterLiveItems(options.imdbCatalog || null, mapped,
                                    def.recipe.kind === "recent");
        if (def.recipe.kind === "recent") clean = Rules.rankItems({ kind: "recent" }, clean, now);
        if (def.recipe.kind === "statusLive")
            clean = clean.filter(function(it) { return String(it.status || "") === "Continuing"; });
        if (explicitFilter) clean = clean.filter(function(it) { return explicitFilter(it, showExplicit); });
        done({ generation: generation, items: clean.slice(0, limit),
               hasMore: !!(metas && metas.length >= limit), error: "" });
    });
```

`findDef` keeps concatenating `dailyRows` for movies (unchanged).

- [ ] **Step 4: Run GREEN + full stage sweep**

Run: `-Stage ApiRows` → `THEATRE_API_ROWS_OK` (movies + anime chains).
Run: `-Stage All` → `THEATRE_DEEP_CATALOGUE_OK` (if the Page stage fails on options, Task 5 fixes the wiring — note it and continue only if the failure is exactly the missing `imdbCatalog` passthrough).

- [ ] **Step 5: Commit**

```bash
git add qml/TheatreApi.js tests/theatre_api_rows_harness.qml
git commit -m "feat(theatre): index-first movie and show engine" -- qml/TheatreApi.js tests/theatre_api_rows_harness.qml
```

---

### Task 5: Thread `ImdbCatalog` through World / Page / See-all

**Files:**
- Modify: `qml/TheatreWorld.qml`, `qml/TheatreCatalogPage.qml`, `qml/TheatreSeeAllPage.qml`
- Modify: `tests/theatre_catalog_page_harness.qml`

- [ ] **Step 1: Add the failing page-harness assertion**

In `tests/theatre_catalog_page_harness.qml`, inside the options assertions add:

```qml
ok(h.loaderOptions.imdbCatalog === h.fakeImdbMarker, "imdbCatalog threaded into loader options");
```

with `property var fakeImdbMarker: ({ ready: function() { return false; } })` on the harness root and `imdbCatalog: h.fakeImdbMarker` set on the `UI.TheatreCatalogPage` instantiation.

Run: `-Stage Page` → FAIL `Cannot assign to non-existent property "imdbCatalog"`.

- [ ] **Step 2: Wire the three QML files**

`qml/TheatreCatalogPage.qml`: add `property var imdbCatalog: null` beside `malCatalog`; include `imdbCatalog: page.imdbCatalog` in the `loader(...)` options object in `load()`.

`qml/TheatreWorld.qml`: on the `TheatreCatalogPage` instance add
`imdbCatalog: (typeof ImdbCatalog !== "undefined") ? ImdbCatalog : null`;
on the `TheatreSeeAllPage` instance add the same binding.

`qml/TheatreSeeAllPage.qml`: add `property var imdbCatalog: null`; include `imdbCatalog: seeAll.imdbCatalog` in the `opts` map of `loadPage()`.

- [ ] **Step 3: Run GREEN + boot smoke**

Run: `-Stage Page` → `THEATRE_PAGE_OK`; then `-Stage All` → `THEATRE_DEEP_CATALOGUE_OK`.
Boot smoke (the gate that caught two real bugs last arc): run `colosseum.exe qml/Main.qml` for ~12 s with `QT_FORCE_STDERR_LOGGING=1`, grep stderr for `Cannot assign|non-existent|ReferenceError|TypeError|unavailable` — expect none.

- [ ] **Step 4: Commit**

```bash
git add qml/TheatreWorld.qml qml/TheatreCatalogPage.qml qml/TheatreSeeAllPage.qml tests/theatre_catalog_page_harness.qml
git commit -m "feat(theatre): thread imdb catalogue into the tab pages" -- qml/TheatreWorld.qml qml/TheatreCatalogPage.qml qml/TheatreSeeAllPage.qml tests/theatre_catalog_page_harness.qml
```

---

### Task 6: Reality probe + threshold calibration (the anti-fixture gate)

**Files:**
- Create: `tests/theatre_shelf_reality_probe.qml`
- Create: `tests/test_theatre_shelf_reality.ps1`
- Modify (calibration only): `qml/TheatreCatalogRules.js` `THRESHOLDS`

- [ ] **Step 1: Write the probe**

`tests/theatre_shelf_reality_probe.qml` — run through the REAL `colosseum.exe` (context properties live), REAL baked db, REAL rules — no fixtures anywhere:

```qml
// Prints every INDEX shelf's real top-20 titles through the real engine. Run via
// colosseum.exe (context properties registered), offscreen. Output lines:
//   SHELF <pageKey>/<key> (<n>): Title (year) | Title (year) | ...
import QtQuick
import "../qml/TheatreCatalogRules.js" as Rules

Item {
    Timer {
        interval: 50; running: true; repeat: false
        onTriggered: {
            var imdb = (typeof ImdbCatalog !== "undefined") ? ImdbCatalog : null;
            if (!imdb || !imdb.ready()) { console.log("REALITY: NO DB"); Qt.exit(2); }
            ["movies", "shows"].forEach(function(pageKey) {
                var defs = Rules.defaultRows(pageKey);
                if (pageKey === "movies")
                    defs = defs.concat(Rules.dailyRows(Date.UTC(2026, 7, 2), 6));
                defs.forEach(function(def) {
                    var queries = Rules.indexQueriesFor(def.recipe);
                    if (!queries.length) return;                       // live shelf
                    var rows = [];
                    queries.forEach(function(q) {
                        var part = imdb.titleCatalog(q, 0, 20) || [];
                        for (var i = 0; i < part.length; i++) rows.push(part[i]);
                    });
                    var line = rows.slice(0, 20).map(function(r) {
                        return r.title + " (" + r.year + (r.isAnime ? " ANIME" : "") + ")";
                    }).join(" | ");
                    console.log("SHELF " + pageKey + "/" + def.key + " (" + rows.length + "): " + line);
                });
            });
            console.log("REALITY_PROBE_DONE");
            Qt.exit(0);
        }
    }
}
```

- [ ] **Step 2: Write the assertion wrapper**

`tests/test_theatre_shelf_reality.ps1`:

```powershell
# The anti-fixture gate (spec §7): every index shelf's REAL titles, mechanically checked,
# full lists saved to tests/_reality_shelves.txt for Hemanth's eyes-on.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "native\build-msvc\colosseum.exe"
if (!(Test-Path $exe)) { throw "build colosseum first" }
if (!(Test-Path (Join-Path $root "data\imdb_catalog.db"))) { throw "bake data/imdb_catalog.db first (Task 1)" }
$env:QT_FORCE_STDERR_LOGGING = "1"
$env:QT_QPA_PLATFORM = "offscreen"
$out = cmd /c "`"$exe`" `"$root\tests\theatre_shelf_reality_probe.qml`" 2>&1" | Out-String
$out | Set-Content (Join-Path $root "tests\_reality_shelves.txt")
if ($out -notlike "*REALITY_PROBE_DONE*") { throw "probe did not complete:`n$($out.Substring(0, [Math]::Min(2000, $out.Length)))" }

function Shelf($key) {
    $line = ($out -split "`n") | Where-Object { $_ -like "*SHELF $key *" } | Select-Object -First 1
    if (-not $line) { throw "shelf $key missing from probe output" }
    return $line
}
function AssertIn($key, $needle)  { if ((Shelf $key) -notlike "*$needle*") { throw "$key must contain $needle" } }
function AssertOut($key, $needle) { if ((Shelf $key) -like "*$needle*")   { throw "$key must NOT contain $needle" } }
function AssertDepth($key, $min) {
    if ([int]((Shelf $key) -replace '.*\((\d+)\):.*', '$1') -lt $min) { throw "$key thinner than $min titles" }
}

# spec §7 title-level truths
AssertIn  "movies/top-rated"    "Shawshank"
AssertOut "movies/hidden-gems"  "Shawshank";  AssertOut "movies/hidden-gems" "Godfather"
AssertOut "movies/hidden-gems"  "Dark Knight"
AssertOut "movies/top-rated"    "ANIME";      AssertOut "movies/animated-movies" "ANIME"
AssertIn  "shows/top-rated"     "Breaking Bad"
AssertOut "shows/top-rated"     "Attack on Titan"
AssertIn  "shows/limited-series" "Chernobyl"
AssertOut "shows/korean-drama"  "X-Men";      AssertOut "shows/korean-drama" "Korra"
AssertOut "shows/korean-drama"  "Avatar";     AssertOut "shows/korean-drama" "Solo Leveling"
AssertIn  "shows/korean-drama"  "All of Us Are Dead"
AssertOut "shows/animated-series" "One Piece"
AssertIn  "shows/animated-series" "Simpsons"
AssertIn  "movies/french-cinema" "Am"           # Amélie (accent-safe grep)
foreach ($k in @("movies/top-rated","movies/hidden-gems","movies/cult-classics",
                 "movies/international-cinema","movies/korean-cinema",
                 "shows/top-rated","shows/hidden-gems","shows/cult-classics","shows/korean-drama")) {
    AssertDepth $k 20
}
# cult classics: every year pre-2000
$cc = Shelf "movies/cult-classics"
([regex]::Matches($cc, '\((\d{4})')) | ForEach-Object {
    if ([int]$_.Groups[1].Value -gt 1999) { throw "cult classics leaked a post-1999 year" } }

Write-Host "THEATRE_SHELF_REALITY_OK"
```

- [ ] **Step 3: Run, calibrate, re-run until green**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/test_theatre_shelf_reality.ps1`

Expected first run: likely 1–3 threshold failures (e.g. a 300k-vote title in Hidden Gems, or Cult Classics thin). Calibrate ONLY the `THRESHOLDS` values in `TheatreCatalogRules.js`, re-run `-Stage Rules` (relationship assertions must stay green), re-run the reality gate. Iterate until `THEATRE_SHELF_REALITY_OK`.

- [ ] **Step 4: The eyes-on handoff (spec criterion 14 — hard stop)**

Paste into chat, from `tests/_reality_shelves.txt`, the full top-20 lists for: movies Top Rated · Hidden Gems · Cult Classics · French/Korean/Japanese Cinema · International; shows Top Rated · Hidden Gems · Cult Classics · Korean Drama · Animated Series · Limited Series. **Wait for Hemanth's verdict before Task 7.** Threshold changes he requests happen here.

- [ ] **Step 5: Commit**

```bash
git add tests/theatre_shelf_reality_probe.qml tests/test_theatre_shelf_reality.ps1 qml/TheatreCatalogRules.js
git commit -m "test(theatre): real-data shelf reality gate + calibrated thresholds" -- tests/theatre_shelf_reality_probe.qml tests/test_theatre_shelf_reality.ps1 qml/TheatreCatalogRules.js
```

---

### Task 7: Full verification matrix

**Files:**
- Modify: none expected (fix-forward only if a gate fails)

- [ ] **Step 1: Focused + adjacent regressions**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage All      # Rules·ApiRows·Cards·SeeAll·Preferences·Page·DiscoverRegression
& tests/test_theatre_shelf_reality.ps1
& tests/test_theatre_top10_genre_boxes.ps1
& tests/test_theatre_anime_parity.ps1
& tests/test_theatre_af2_p0.ps1
& tests/test_theatre_search_p0.ps1
& tests/test_explicit_content_policy.ps1
& tests/test_content_preferences.ps1
& tests/test_mal_genre_catalog_p0.ps1
node tests/extension_world_isolation_test.mjs
node tests/extension_reorder_world_test.mjs
```
Plus native: `mal_catalog_rows_harness.exe` and `imdb_catalog_harness.exe`. Expected: all exit 0.

- [ ] **Step 2: Build + boot smoke**

`cmd //c "%TEMP%\colosseum_build_target.bat" colosseum` → `BUILD_OK`; 12-second boot with stderr grep → no QML errors; confirm one `SHELF`-style log absence (probe QML is never loaded by Main).

- [ ] **Step 3: Eyes-on matrix (Hemanth's screen, where environment permits)**

Movies/Shows tabs paint index shelves instantly; Hidden Gems shows unfamiliar titles; Korean Drama all-Korean; no One Piece outside the Anime tab; See-all on Hidden Gems pages past 100; offline relaunch still paints index shelves; Customize move/hide/rename/reset still persists; Explicit off keeps Game of Thrones/Berserk visible.

- [ ] **Step 4: Scope + git hygiene**

`git status --short` (unrelated dirty files untouched, `data/` clean) · `git diff --check` · confirm the six task commits on master · grep the arc's diff for `themoviedb|api_key|trakt` → nothing.

- [ ] **Step 5: Acceptance matrix**

Walk spec §7 criteria 1–14 with MET/PARTIAL/NOT-MET + the exact command or screenshot as evidence. Any PARTIAL/NOT-MET → fix before claiming done. Criterion 14 is Hemanth's verdict from Task 6 Step 4 — it cannot be self-certified.

---

## Self-review (performed at write time)

- **Spec coverage:** §3 bake → Task 1; §3.2 language chain → Task 1 selftest + Step 4 probes; §5 ImdbCatalog/allowlist → Task 2; §4 inventories + §4.3 dials → Task 3; §5 engine swap + live filtering + §4.2 anime-everywhere exclusion → Task 4; wiring → Task 5; §6 states (missing db → omission) → Task 4 phase-1 guard + Task 2 `ready()` + reality wrapper's explicit bake precondition; §7 acceptance → Tasks 6–7; §7.14 eyes-on → Task 6 Step 4 hard stop.
- **Placeholder scan:** no TBDs; every code step carries the code; thresholds intentionally provisional by design (named dials + calibration task), which the spec mandates.
- **Type consistency:** `titleCatalog`/`titleFacts` signatures identical across Tasks 2/4/6; `indexQueryFor`/`indexQueriesFor` defined Task 3, consumed Tasks 4/6; recipe kinds (`imdbBand`/`imdbGenre`/`imdbGenreAny`/`imdbLang`/`imdbIntl`/`imdbDecade`/`top`/`recent`/`statusLive`) match between Rules and TheatreApi; row field names (`tt,type,title,year,...`) match C++ output ↔ `mapImdb` ↔ harness fixtures.
