"""Independent, reproducible CC0-only AudioSilo SQLite projection. No network or publication.

python -I projection/audiobooks_bake.py [--fixture] [--source path] [--output path]
The full 2026-11-16 bake stays outside Git. Only --fixture belongs in tests/fixtures.
"""
import argparse
from contextlib import closing
from decimal import Decimal, InvalidOperation
import hashlib
import json
from pathlib import Path
import re
import sqlite3

SNAPSHOT = "data-v2026.11.16-82ea38c-30a9e1e"
TABLES = {
    "works": {"id":["works.id"],"title":["works.title"],"sort_title":["works.title"],"original_language":["works.language"],"first_year":["works.first_published"]},
    "people": {"id":["people.id"],"name":["people.name"]},
    "series": {"id":["series.id"],"name":["series.name"]},
    "work_authors": {"work_id":["work_authors.work_id"],"person_id":["work_authors.person_id"],"ord":["work_authors.ord"]},
    "work_genres": {"work_id":["work_genres.work_id"],"genre":["work_genres.genre"]},
    "narrator_shelves": {"rank":["recording_narrators.person_id","recordings.language"],"person_id":["recording_narrators.person_id"],
                         "name":["people.name"],"english_recordings":["recording_narrators.recording_id","recordings.language"]},
    "genres_summary": {"genre":["work_genres.genre"],"recording_count":["work_genres.work_id","recordings.id"],
                       "covers_json":["recordings.cover_url","recordings.release_date","work_genres.genre"]},
    "recordings": {"id":["recordings.id"],"work_id":["recordings.work_id"],"language":["recordings.language"],
                   "runtime_min":["recordings.runtime_min"],"release_year":["recordings.release_date"],
                   "abridgement":["recordings.abridged"],"asin":["recording_asins.asin"],"isbn":["recording_isbns.isbn"],
                    "release_date":["recordings.release_date"],"publisher":["recordings.publisher"],
                    "cover_url":["recordings.cover_url"],"added_at":["recordings.added_at"],
                    "dramatized":["recordings.publisher","recordings.link_graphicaudio","recording_narrators.person_id","people.name"],
                    "dramatized_signals":["recordings.publisher","recordings.link_graphicaudio","recording_narrators.person_id","people.name"]},
    "recording_narrators": {"work_id":["recording_narrators.work_id"],"recording_id":["recording_narrators.recording_id"],
                            "person_id":["recording_narrators.person_id"],"ord":["recording_narrators.ord"]},
    "work_series": {"work_id":["series_works.work_id"],"series_id":["series_works.series_id"],
                    "number_text":["series_works.position"],"position_sort":["series_works.position"]},
    "recording_asins": {"asin":["recording_asins.asin"],"region":["recording_asins.region"],
                       "work_id":["recording_asins.work_id"],"recording_id":["recording_asins.recording_id"]},
    "recording_isbns": {"isbn":["recording_isbns.isbn"],"work_id":["recording_isbns.work_id"],
                       "recording_id":["recording_isbns.recording_id"]},
    "search_fts": {"kind":["works.id","people.id"],"id":["works.id","people.id"],
                   "title":["works.title","people.name"],"people":["people.name"]},
}
CC0_SOURCES = sorted({source.split(".")[0] for cols in TABLES.values()
                      for origins in cols.values() for source in origins})


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(4 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def year(value):
    match = re.match(r"^([1-9][0-9]{3})(?:$|[^0-9])", str(value or ""))
    return int(match.group(1)) if match else None


def sort_title(value):
    title = str(value or "").strip()
    match = re.match(r"^(the|an|a)\s+(.+)$", title, re.IGNORECASE)
    return (match.group(2) + ", " + match.group(1)) if match else title


def position_sort(value):
    raw = str(value or "").strip()
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+)?", raw):
        return None
    try:
        n = float(Decimal(raw))
        return n if 0 <= n < 1e9 else None
    except (InvalidOperation, OverflowError):
        return None


SNAPSHOT_AS_OF = "2026-11-16"  # Pinned snapshot date; no wall-clock bytes in the bake.

def full_cast_credit(person_id, name):
    # A literal Full Cast credit is evidence, including named ensemble credits.
    # Several ordinary narrators (or "Various") alone are not dramatization.
    return person_id == "full-cast" or bool(re.search(
        r"\bfull[\s-]+cast\b", str(name or ""), re.IGNORECASE))


def drama_signals(publisher, link):
    value = re.sub(r"\s+", "", str(publisher or "").casefold())
    flags = []
    if value in {"graphicaudio", "graphicaudiollc", "blackstonepub&graphicaudio"}:
        flags.append("publisher:graphicaudio")
    if str(link or "").strip():
        flags.append("link:graphicaudio")
    return json.dumps(flags, separators=(",", ":"))


SCHEMA = """
CREATE TABLE works(id TEXT PRIMARY KEY,title TEXT NOT NULL,sort_title TEXT NOT NULL,original_language TEXT,first_year INTEGER);
CREATE TABLE people(id TEXT PRIMARY KEY,name TEXT NOT NULL);
CREATE TABLE series(id TEXT PRIMARY KEY,name TEXT NOT NULL);
CREATE TABLE work_authors(work_id TEXT NOT NULL,person_id TEXT NOT NULL,ord INTEGER NOT NULL,PRIMARY KEY(work_id,person_id,ord));
CREATE TABLE work_genres(work_id TEXT NOT NULL,genre TEXT NOT NULL,PRIMARY KEY(work_id,genre));
CREATE TABLE narrator_shelves(rank INTEGER PRIMARY KEY,person_id TEXT UNIQUE NOT NULL,name TEXT NOT NULL,english_recordings INTEGER NOT NULL);
CREATE TABLE genres_summary(genre TEXT PRIMARY KEY,recording_count INTEGER NOT NULL,covers_json TEXT NOT NULL);
CREATE TABLE recordings(id TEXT NOT NULL,work_id TEXT NOT NULL,language TEXT,runtime_min INTEGER,release_year INTEGER,
  abridgement INTEGER,asin TEXT,isbn TEXT,release_date TEXT,publisher TEXT,cover_url TEXT,added_at TEXT,
  dramatized INTEGER NOT NULL,dramatized_signals TEXT NOT NULL,PRIMARY KEY(work_id,id));
CREATE TABLE recording_narrators(work_id TEXT NOT NULL,recording_id TEXT NOT NULL,person_id TEXT NOT NULL,ord INTEGER NOT NULL,
  PRIMARY KEY(work_id,recording_id,person_id,ord));
CREATE TABLE work_series(work_id TEXT NOT NULL,series_id TEXT NOT NULL,number_text TEXT,position_sort REAL,
  PRIMARY KEY(work_id,series_id));
CREATE TABLE recording_asins(asin TEXT NOT NULL,region TEXT NOT NULL,work_id TEXT NOT NULL,recording_id TEXT NOT NULL,
  PRIMARY KEY(asin,region,work_id,recording_id));
CREATE TABLE recording_isbns(isbn TEXT NOT NULL,work_id TEXT NOT NULL,recording_id TEXT NOT NULL,
  PRIMARY KEY(isbn,work_id,recording_id));
CREATE VIRTUAL TABLE search_fts USING fts5(kind UNINDEXED,id UNINDEXED,title,people,
  tokenize='unicode61 remove_diacritics 2');
"""


def bake(source, output, fixture=False, snapshot_id=SNAPSHOT):
    if not source.is_file():
        raise ValueError("Missing input AudioSilo snapshot: " + str(source))
    if output.exists():
        raise ValueError("Refusing to overwrite output: " + str(output))
    if any(Path(str(source) + suffix).exists() for suffix in ("-wal", "-journal")):
        raise ValueError("Source must be a frozen SQLite snapshot")
    source_hash = sha256(source)
    expected = source.with_suffix(source.suffix + ".sha256")
    if expected.exists() and expected.read_text(encoding="ascii").split()[0].lower() != source_hash:
        raise ValueError("Source snapshot SHA-256 mismatch")
    output.parent.mkdir(parents=True, exist_ok=True)
    manifest_path = output.with_suffix(".manifest.json")
    if manifest_path.exists():
        raise ValueError("Refusing to overwrite manifest")
    try:
        source_uri = source.resolve().as_uri() + "?mode=ro&immutable=1"
        with closing(sqlite3.connect(source_uri, uri=True)) as upstream:
            for table in ("works", "recordings", "series", "people"):
                count = upstream.execute(
                    "SELECT COUNT(*) FROM " + table + " WHERE license IS NULL OR license!='CC0-1.0'"
                ).fetchone()[0]
                if count:
                    raise ValueError("Non-CC0 rows found in " + table + ": " + str(count))
            with closing(sqlite3.connect(output, uri=True)) as db:
                db.execute("PRAGMA page_size=4096")
                db.execute("PRAGMA journal_mode=OFF")
                db.execute("PRAGMA synchronous=OFF")
                db.execute("PRAGMA temp_store=MEMORY")
                db.execute("PRAGMA user_version=2")
                db.create_function("cc0_year", 1, year, deterministic=True)
                db.create_function("cc0_sort_title", 1, sort_title, deterministic=True)
                db.create_function("cc0_drama_signals", 2, drama_signals, deterministic=True)
                db.create_function("cc0_full_cast_credit", 2, full_cast_credit, deterministic=True)
                db.executescript(SCHEMA)
                db.execute("ATTACH DATABASE ? AS upstream", (source_uri,))
                if fixture:
                    db.execute("CREATE TEMP TABLE selected_works(id TEXT PRIMARY KEY)")
                    selected = ["dune", "rhythm-of-war"]
                    regional = db.execute(
                        "SELECT id FROM upstream.works WHERE language IN ('te','ta') ORDER BY id LIMIT 1"
                    ).fetchone()
                    if regional and regional[0] not in selected:
                        selected.append(regional[0])
                    rest = 30 - len(selected)
                    selected += [r[0] for r in db.execute(
                        "SELECT id FROM upstream.works WHERE id NOT IN (" + ",".join("?" for _ in selected) +
                        ") ORDER BY id LIMIT ?", (*selected, rest))]
                    if len(selected) != 30:
                        raise ValueError("Source lacks 30 distinct fixture works")
                    db.executemany("INSERT INTO selected_works VALUES (?)", ((v,) for v in selected))
                work_filter = " WHERE w.id IN (SELECT id FROM selected_works)" if fixture else ""
                db.execute("INSERT INTO works SELECT w.id,w.title,cc0_sort_title(w.title),w.language,cc0_year(w.first_published) FROM upstream.works w" + work_filter + " ORDER BY w.id")
                if fixture:
                    db.execute("""INSERT INTO people SELECT p.id,p.name FROM upstream.people p WHERE p.id IN
                        (SELECT a.person_id FROM upstream.work_authors a JOIN works w ON w.id=a.work_id
                         UNION SELECT n.person_id FROM upstream.recording_narrators n JOIN works w ON w.id=n.work_id)
                        ORDER BY p.id""")
                    db.execute("""INSERT INTO series SELECT s.id,s.name FROM upstream.series s WHERE
                        EXISTS(SELECT 1 FROM upstream.series_works sw JOIN works w ON w.id=sw.work_id WHERE sw.series_id=s.id)
                        ORDER BY s.id""")
                else:
                    db.execute("INSERT INTO people SELECT id,name FROM upstream.people ORDER BY id")
                    db.execute("INSERT INTO series SELECT id,name FROM upstream.series ORDER BY id")
                db.execute("""INSERT INTO recordings
                    SELECT r.id,r.work_id,r.language,r.runtime_min,cc0_year(r.release_date),r.abridged,NULL,NULL,
                    r.release_date,r.publisher,r.cover_url,r.added_at,0,
                    cc0_drama_signals(r.publisher,r.link_graphicaudio)
                    FROM upstream.recordings r JOIN works w ON w.id=r.work_id ORDER BY r.work_id,r.id""")
                db.execute("""INSERT INTO work_authors
                    SELECT a.work_id,a.person_id,a.ord FROM upstream.work_authors a
                    JOIN works w ON w.id=a.work_id JOIN people p ON p.id=a.person_id
                    ORDER BY a.work_id,a.person_id,a.ord""")
                db.execute("""INSERT INTO work_genres
                    SELECT g.work_id,g.genre FROM upstream.work_genres g
                    JOIN works w ON w.id=g.work_id ORDER BY g.work_id,g.genre""")
                db.execute("""INSERT INTO recording_narrators
                    SELECT n.work_id,n.recording_id,n.person_id,n.ord FROM upstream.recording_narrators n
                    JOIN recordings r ON r.work_id=n.work_id AND r.id=n.recording_id
                    JOIN people p ON p.id=n.person_id
                    ORDER BY n.work_id,n.recording_id,n.person_id,n.ord""")
                db.execute("""UPDATE recordings SET dramatized_signals=
                    CASE WHEN dramatized_signals='[]' THEN '["narrator:full-cast"]'
                    ELSE substr(dramatized_signals,1,length(dramatized_signals)-1) || ',"narrator:full-cast"]' END
                    WHERE EXISTS (SELECT 1 FROM recording_narrators n
                    JOIN people p ON p.id=n.person_id WHERE n.work_id=recordings.work_id
                    AND n.recording_id=recordings.id
                    AND cc0_full_cast_credit(n.person_id,p.name))""")
                db.execute("UPDATE recordings SET dramatized=(dramatized_signals!='[]')")
                db.execute("""INSERT INTO recording_asins
                    SELECT a.asin,a.region,a.work_id,a.recording_id FROM upstream.recording_asins a
                    JOIN recordings r ON r.work_id=a.work_id AND r.id=a.recording_id
                    ORDER BY a.asin,a.region,a.work_id,a.recording_id""")
                db.execute("""INSERT INTO recording_isbns
                    SELECT i.isbn,i.work_id,i.recording_id FROM upstream.recording_isbns i
                    JOIN recordings r ON r.work_id=i.work_id AND r.id=i.recording_id
                    ORDER BY i.isbn,i.work_id,i.recording_id""")
                entries = db.execute("""SELECT sw.work_id,sw.series_id,sw.position FROM upstream.series_works sw
                    JOIN works w ON w.id=sw.work_id JOIN series s ON s.id=sw.series_id
                    ORDER BY sw.work_id,sw.series_id""").fetchall()
                db.executemany("INSERT INTO work_series VALUES (?,?,?,?)",
                               ((work, series, pos, position_sort(pos)) for work,series,pos in entries))
                db.executescript("""
                    CREATE INDEX idx_recordings_release ON recordings(release_year DESC,work_id,id);
                     CREATE INDEX idx_recordings_release_date ON recordings(release_date DESC,work_id,id);
                     CREATE INDEX idx_recordings_drama ON recordings(dramatized,release_date DESC,work_id,id);
                     CREATE INDEX idx_work_genres_genre ON work_genres(genre,work_id);
                    CREATE INDEX idx_work_authors_person ON work_authors(person_id,work_id);
                    CREATE INDEX idx_narrators_person ON recording_narrators(person_id,work_id);
                    CREATE INDEX idx_work_series_sort ON work_series(series_id,position_sort,work_id);
                    CREATE INDEX idx_recording_asins_record ON recording_asins(work_id,recording_id);
                    CREATE INDEX idx_recording_isbns_record ON recording_isbns(work_id,recording_id);
                """)
                top = db.execute("""SELECT p.id,p.name,COUNT(DISTINCT n.work_id||char(31)||n.recording_id) AS total
                    FROM recording_narrators n JOIN recordings r
                    ON r.work_id=n.work_id AND r.id=n.recording_id
                    JOIN people p ON p.id=n.person_id WHERE r.language='en'
                    AND lower(trim(p.name)) NOT IN ('various','various narrators','full cast',
                      'full-cast','multiple narrators','unknown','anonymous','ensemble',
                      'cast','narrator','narrators')
                    AND lower(p.name) NOT LIKE '%full cast%'
                    AND lower(p.name) NOT LIKE 'various %'
                    GROUP BY p.id,p.name ORDER BY total DESC,p.name COLLATE NOCASE,p.id LIMIT 3""").fetchall()
                if len(top) != 3:
                    raise ValueError("Cannot select three evidenced English narrators")
                db.executemany("INSERT INTO narrator_shelves VALUES (?,?,?,?)",
                               ((rank,*person) for rank,person in enumerate(top,1)))
                db.execute("""UPDATE recordings SET asin=(SELECT asin FROM recording_asins a
                    WHERE a.work_id=recordings.work_id AND a.recording_id=recordings.id
                    ORDER BY (a.region!='us'),a.region,a.asin LIMIT 1)""")
                db.execute("""UPDATE recordings SET isbn=(SELECT isbn FROM recording_isbns i
                    WHERE i.work_id=recordings.work_id AND i.recording_id=recordings.id
                    ORDER BY i.isbn LIMIT 1)""")
                genre_counts = db.execute("""SELECT g.genre,COUNT(*) FROM work_genres g
                    JOIN recordings r ON r.work_id=g.work_id
                    GROUP BY g.genre ORDER BY COUNT(*) DESC,g.genre""").fetchall()
                cover_map = {}
                for genre, url in db.execute("""SELECT g.genre,r.cover_url FROM work_genres g
                    JOIN recordings r ON r.work_id=g.work_id
                    WHERE r.cover_url LIKE 'http://%' OR r.cover_url LIKE 'https://%'
                    ORDER BY g.genre, r.release_date DESC, r.work_id,r.id"""):
                    if len(cover_map.get(genre, [])) < 4 and url not in cover_map.get(genre, []):
                        cover_map.setdefault(genre, []).append(url)
                db.executemany("INSERT INTO genres_summary VALUES (?,?,?)",
                    ((genre,count,json.dumps(cover_map[genre],ensure_ascii=False,separators=(",",":")))
                     for genre,count in genre_counts if len(cover_map.get(genre, []))==4))
                db.execute("""INSERT INTO search_fts(kind,id,title,people)
                    SELECT 'work',w.id,w.title,TRIM(
                      COALESCE((SELECT group_concat(p.name,' ') FROM work_authors a
                        JOIN people p ON p.id=a.person_id WHERE a.work_id=w.id),'') || ' ' ||
                      COALESCE((SELECT group_concat(p.name,' ') FROM recording_narrators n
                        JOIN people p ON p.id=n.person_id WHERE n.work_id=w.id),''))
                    FROM works w ORDER BY w.id""")
                db.execute("INSERT INTO search_fts(kind,id,title,people) SELECT 'person',id,name,name FROM people ORDER BY id")
                db.commit()
                db.execute("DETACH DATABASE upstream")
                db.execute("VACUUM")
                if db.execute("PRAGMA quick_check").fetchone()[0] != "ok":
                    raise ValueError("Baked SQLite quick_check failed")
                rows = {name: db.execute('SELECT COUNT(*) FROM "' + name + '"').fetchone()[0] for name in TABLES}
                db_counts = {signal:db.execute("SELECT COUNT(*) FROM recordings WHERE instr(dramatized_signals,?)>0",
                                                (signal,)).fetchone()[0]
                             for signal in ("publisher:graphicaudio","link:graphicaudio","narrator:full-cast")}
                shelf_counts = {
                    "new-releases": db.execute("""SELECT COUNT(*) FROM recordings
                        WHERE length(release_date)=10 AND date(release_date) IS NOT NULL
                        AND date(release_date)<=?""",(SNAPSHOT_AS_OF,)).fetchone()[0],
                    "dramatized": db.execute("SELECT COUNT(*) FROM recordings WHERE dramatized=1").fetchone()[0]}
                for person_id,_,_ in top:
                    shelf_counts["narrator:"+person_id] = db.execute("""SELECT COUNT(*) FROM recordings r
                         WHERE r.language='en' AND EXISTS(SELECT 1 FROM recording_narrators n
                         WHERE n.work_id=r.work_id AND n.recording_id=r.id AND n.person_id=?)""",
                         (person_id,)).fetchone()[0]
        manifest = {"format":"colosseum-audiobooks-cc0-v2","schema_version":2,"source_snapshot_id":snapshot_id,
                    "source_sha256":source_hash,"rights":["CC0-1.0"],"cc0_sources":CC0_SOURCES,
                    "fixture":fixture,"sqlite_sha256":sha256(output),"sqlite_bytes":output.stat().st_size,
                     "narrator_picks":[{"id":p[0],"name":p[1],"english_recordings":p[2]} for p in top],
                     "dramatized_signals":db_counts,
                     "shelf_counts":shelf_counts,"shelf_count_as_of":SNAPSHOT_AS_OF,
                    "tables":{table:{"rows":rows[table],"licenses":["CC0-1.0"],"columns":columns}
                              for table,columns in TABLES.items()}}
        manifest_path.write_text(json.dumps(manifest,indent=2,ensure_ascii=False,sort_keys=True)+"\n",encoding="utf-8",newline="\n")
        print(json.dumps({"sqlite":str(output),"sha256":manifest["sqlite_sha256"],"rows":rows,
                          "source_snapshot_id":snapshot_id},sort_keys=True))
    except BaseException:
        output.unlink(missing_ok=True)
        manifest_path.unlink(missing_ok=True)
        raise


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--fixture",action="store_true",help="reproducible 30-work subset")
    parser.add_argument("--snapshot-id",default=SNAPSHOT)
    args=parser.parse_args()
    bake(args.source,args.output,args.fixture,args.snapshot_id)


if __name__=="__main__":
    main()
