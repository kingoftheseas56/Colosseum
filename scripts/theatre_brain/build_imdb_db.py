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

# Region -> dominant film language, for the last-resort origin fallback (§3.2).
# Monoglot-or-near markets where the local-release region is a reliable origin
# signal. Used ONLY when no aka carried a language code for the title (every
# aka-language code is localization evidence, not origin evidence — verified
# against real data: WALL·E/Lion King/Simpsons all carry `ja` akas from JP
# dubs, so aka-language codes cannot classify origin).
REGION_LANG = {
    "KR": "ko", "JP": "ja", "FR": "fr", "DE": "de", "ES": "es", "IT": "it",
    "CN": "zh", "RU": "ru", "IN": "hi", "TR": "tr", "TH": "th",
    "SE": "sv", "DK": "da", "NO": "no", "FI": "fi", "PL": "pl", "NL": "nl",
    "PT": "pt", "BR": "pt", "MX": "es", "AR": "es", "GR": "el", "EG": "ar",
    "IR": "fa", "IL": "he", "CZ": "cs", "HU": "hu", "RO": "ro", "BG": "bg",
    "HR": "hr", "RS": "sr", "UA": "uk", "VN": "vi", "ID": "id",
}

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
                matching_aka_langs_ci: list[str],
                matching_aka_regions: list[str],
                has_us_primary_aka: bool) -> str:
    """Spec §3.2 chain (calibrated against real IMDb data 2026-08-02):
    1. explicit original-aka language (isOriginalTitle=1 row carrying a code),
    2. script detection on originalTitle (definitive for non-Latin scripts),
    3. tie-break from akas whose title EXACTLY equals the original title,
    3b. tie-break from akas whose title equals the original title
        case-insensitively (IMDb capitalises the same title inconsistently
        across regions — e.g. Amélie's CA-fr aka lowercases 'fabuleux'),
    4. region fallback: akas matching the original title collapse to ONE
       mapped language (last-resort origin evidence for titles whose akas
       carry no language code at all — e.g. All of Us Are Dead's KR aka),
    5. 'en' only when original==primary AND a US aka repeats it verbatim,
    else '' (unknown — never guessed)."""
    if orig_aka_lang:
        return orig_aka_lang
    s = detect_script_lang(original_title)
    if s:
        return s
    if matching_aka_langs:
        return Counter(matching_aka_langs).most_common(1)[0][0]
    if matching_aka_langs_ci:
        return Counter(matching_aka_langs_ci).most_common(1)[0][0]
    if matching_aka_regions:
        langs = {REGION_LANG[r] for r in matching_aka_regions if r in REGION_LANG}
        if len(langs) == 1:           # regions collapse to a single language
            return next(iter(langs))
    if original_title == primary_title and has_us_primary_aka:
        return "en"
    return ""


def keep_title(title_type: str, is_adult: str, votes: int) -> bool:
    return (title_type in KEEP_TYPES and is_adult != "1" and votes >= VOTE_FLOOR)


def is_anime(tt: str, anime_tt: set[str]) -> bool:
    """Anime identity is the Fribb cross-reference set, NOT IMDb language
    (IMDb cannot classify romanised-Japanese origin — see load_anime_tt)."""
    return tt in anime_tt


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
    check(derive_lang("x", "x", "ko", [], [], [], False) == "ko", "explicit aka lang wins")
    check(derive_lang("오징어 게임", "Squid Game", "", [], [], [], False) == "ko", "script fallback")
    check(derive_lang("Le fabuleux destin d'Amélie Poulain", "Amélie", "", ["fr", "fr"], [], [], False) == "fr",
          "exact-match tie-break")
    # IMDb capitalises the same title inconsistently across regions; the
    # case-insensitive fallback catches it when no exact match fired.
    check(derive_lang("Le Fabuleux Destin", "Amélie", "", [], ["fr", "fr"], [], False) == "fr",
          "case-insensitive tie-break")
    # All-Of-Us-Are-Dead shape: no aka carried a language code, but the
    # original-matching akas' regions collapse to a single mapped language.
    check(derive_lang("Jigeum uri hakgyoneun", "All of Us Are Dead", "", [], [], ["\\N", "KR"], False) == "ko",
          "region fallback when akas carry no lang code")
    # conflicting regions must NOT guess (CA unmapped + KR ko would be ko by
    # accident; here two mapped langs collide -> defer to unknown, not guess)
    check(derive_lang("X", "X", "", [], [], ["KR", "FR"], False) == "",
          "region fallback only when regions collapse to one lang")
    check(derive_lang("Ted Lasso", "Ted Lasso", "", [], [], [], True) == "en", "US-verbatim -> en")
    check(derive_lang("Somefilm", "Somefilm", "", [], [], [], False) == "", "no signal -> unknown, never guessed")
    check(keep_title("movie", "0", 1000) and not keep_title("movie", "1", 99999), "adult excluded")
    check(not keep_title("tvEpisode", "0", 99999) and keep_title("tvMiniSeries", "0", 1000), "type filter")
    check(not keep_title("movie", "0", VOTE_FLOOR - 1), "vote floor")
    check(is_anime("tt0388629", {"tt0388629"}) and not is_anime("tt0096697", {"tt0388629"}),
          "isAnime from the anime-identity set")
    print("SELFTEST OK" if ok else "SELFTEST FAILED")
    return 0 if ok else 1


def fetch(name: str) -> str:
    """Cache a dump by bare filename (IMDb TSVs) OR by full URL (Fribb JSON).

    A bare name downloads from datasets.imdbws.com; a full http(s) URL is
    cached under its basename. Public, keyless sources only."""
    os.makedirs(CACHE, exist_ok=True)
    is_url = name.startswith("http://") or name.startswith("https://")
    cache_name = name.rsplit("/", 1)[-1] if is_url else name
    src = name if is_url else BASE + name
    path = os.path.join(CACHE, cache_name)
    if not os.path.exists(path):
        print("downloading", cache_name)
        urllib.request.urlretrieve(src, path)
    return path


# Definitive anime-identity source (same dataset the Anime tab uses): a public,
# keyless JSON of every anime with an IMDb cross-reference. IMDb's own akas
# cannot classify Japanese-origin animation (it romanises originalTitles AND
# localises Western toons into Kana), so isAnime comes from THIS set, not
# IMDb language. Spec amendment 2026-08-02.
FRIBB_URL = "https://raw.githubusercontent.com/Fribb/anime-lists/master/anime-list-full.json"


def load_anime_tt() -> set[str]:
    """Every tt id the Fribb anime-identity dataset cross-references to IMDb.
    `imdb_id` is a list of tt strings per entry (sometimes a single string,
    sometimes empty); we flatten all into one set."""
    with open(fetch(FRIBB_URL), "rt", encoding="utf-8", errors="replace") as f:
        data = json.load(f)
    out: set[str] = set()
    for e in data:
        raw = e.get("imdb_id")
        if isinstance(raw, list):
            out.update(s for s in raw if isinstance(s, str) and s.startswith("tt"))
        elif isinstance(raw, str) and raw.startswith("tt"):
            out.add(raw)
    return out


def tsv_rows(path: str):
    with gzip.open(path, "rt", encoding="utf-8", errors="replace", newline="") as f:
        header = f.readline().rstrip("\n").split("\t")
        for line in f:
            yield dict(zip(header, line.rstrip("\n").split("\t")))


def bake() -> int:
    # anime identity (definitive; loaded once, reused at insert time)
    anime_tt = load_anime_tt()
    print(f"anime tt ids (Fribb): {len(anime_tt):,}")

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
            "origAkaLang": "", "matchLangs": [], "matchLangsCI": [],
            "matchRegions": [], "usPrimary": False,
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
        region = r.get("region", "\\N")
        if r["isOriginalTitle"] == "1" and lang and not t["origAkaLang"]:
            t["origAkaLang"] = lang
        ot_lower = (t["originalTitle"] or "").lower()
        title_lower = (r["title"] or "").lower()
        if lang and r["title"] == t["originalTitle"]:
            t["matchLangs"].append(lang)
        elif lang and title_lower == ot_lower:
            t["matchLangsCI"].append(lang)
        if title_lower == ot_lower:
            t["matchRegions"].append(region)
        if region == "US" and r["title"] == t["title"]:
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
    n_anime = 0
    for tt, t in titles.items():
        lang = derive_lang(t["originalTitle"], t["title"], t["origAkaLang"],
                           t["matchLangs"], t["matchLangsCI"],
                           t["matchRegions"], t["usPrimary"])
        anime = 1 if is_anime(tt, anime_tt) else 0
        n_anime += anime
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
          f"{len(titles):,} titles, {n_lang:,} with known language, "
          f"{n_anime:,} anime)")
    return 0


if __name__ == "__main__":
    sys.exit(selftest() if "--selftest" in sys.argv else bake())
