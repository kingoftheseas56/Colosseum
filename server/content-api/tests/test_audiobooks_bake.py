"""Deterministic and license-constrained AudioSilo bake acceptance tests."""
from contextlib import closing
import hashlib
import json
import os
import runpy
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BAKE = ROOT / "projection" / "audiobooks_bake.py"
SOURCE = Path(os.environ.get("AUDIOSILO_SOURCE", "C:/Colosseum-CatalogBake/audiosilo-20261116/meta.sqlite"))
FIXTURE = ROOT / "tests" / "fixtures" / "audiobooks-cc0-fixture.sqlite"
EXCLUDED = {"work_descriptions", "characters", "character_aliases", "recaps", "recap_summaries", "chapters"}
# Independent field-level redistribution boundary from AUDIOSILO-QUALIFICATION.md.
# Do not derive this allowlist from the baker or its emitted manifest.
CC0_FIELDS = {
    "works": {"id", "title", "language", "first_published"},
    "people": {"id", "name"}, "series": {"id", "name"},
    "work_authors": {"work_id", "person_id", "ord"},
    "work_genres": {"work_id", "genre"},
    "recordings": {"id", "work_id", "language", "runtime_min", "release_date", "abridged",
                   "publisher", "cover_url", "added_at", "link_graphicaudio", "librivox_id"},
    "recording_narrators": {"work_id", "recording_id", "person_id", "ord"},
    "recording_asins": {"asin", "region", "work_id", "recording_id"},
    "recording_isbns": {"isbn", "work_id", "recording_id"},
    "series_works": {"work_id", "series_id", "position"},
}
HASH = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()

class AudiobooksBakeTests(unittest.TestCase):
    def test_cc0_output_schema_and_manifest_provenance(self):
        self.assertTrue(FIXTURE.exists())
        manifest = json.loads(FIXTURE.with_suffix(".manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["sqlite_sha256"], HASH(FIXTURE))
        self.assertEqual(manifest["source_snapshot_id"], "data-v2026.11.16-82ea38c-30a9e1e")
        self.assertEqual(manifest["rights"], ["CC0-1.0"])
        with sqlite3.connect(FIXTURE) as db:
            names = {r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type='table'")}
            self.assertTrue(EXCLUDED.isdisjoint(names))
            for table, info in manifest["tables"].items():
                self.assertIn(table, names)
                self.assertEqual(info["licenses"], ["CC0-1.0"])
                self.assertEqual(info["rows"], db.execute(f'SELECT COUNT(*) FROM "{table}"').fetchone()[0])
                self.assertEqual(set(info["columns"]),
                                 {r[1] for r in db.execute(f'PRAGMA table_info("{table}")')})
                for sources in info["columns"].values():
                    self.assertTrue(sources)
                    for source in sources:
                        source_table, column = source.split(".")
                        self.assertIn(source_table, CC0_FIELDS)
                        self.assertIn(column, CC0_FIELDS[source_table])
            self.assertEqual(set(manifest["cc0_sources"]), set(CC0_FIELDS))
            self.assertEqual(db.execute("SELECT COUNT(*) FROM works").fetchone()[0],30)

    def test_explicit_named_full_cast_credits_are_evidence_not_narrator_count(self):
        bake = runpy.run_path(str(BAKE))
        credit = bake["full_cast_credit"]
        for name in ("Full Cast", "Full Cast featuring Joan Walker",
                     "Jared Reinfeldt full cast", "Full-Cast"):
            self.assertTrue(credit("some-person", name), name)
        self.assertTrue(credit("full-cast", ""))
        for name in ("Various", "Various Narrators", "Scott Brick", "Castro"):
            self.assertFalse(credit("some-person", name), name)

    @staticmethod
    def make_source(source):
        with closing(sqlite3.connect(source)) as db, db:
            db.executescript("""
                CREATE TABLE works(id,title,language,first_published,license,description);
                CREATE TABLE people(id,name,license,description);
                CREATE TABLE series(id,name,license);
                CREATE TABLE recordings(id,work_id,language,runtime_min,release_date,abridged,
                    publisher,cover_url,added_at,link_graphicaudio,license,librivox_id);
                CREATE TABLE work_authors(work_id,person_id,ord);
                CREATE TABLE work_genres(work_id,genre);
                CREATE TABLE recording_narrators(work_id,recording_id,person_id,ord);
                CREATE TABLE recording_asins(asin,region,work_id,recording_id);
                CREATE TABLE recording_isbns(isbn,work_id,recording_id);
                CREATE TABLE series_works(work_id,series_id,position);
                CREATE TABLE work_descriptions(work_id,text,license);
                INSERT INTO work_descriptions VALUES('w0','DO NOT COPY','CC-BY-SA-4.0');
            """)
            for n, (person_id, name) in enumerate((("cast", "Full Cast featuring Joan Walker"),
                    ("scott-brick", "Scott Brick"), ("simon-vance", "Simon Vance"),
                    ("robin-miles", "Robin Miles"))):
                db.execute("INSERT INTO works VALUES(?,?,'en','2020','CC0-1.0','DO NOT COPY')",
                           (f"w{n}", f"Work {n}"))
                db.execute("INSERT INTO people VALUES(?,?,'CC0-1.0','DO NOT COPY')", (person_id, name))
                db.execute("INSERT INTO recordings VALUES('r',?,'en',60,'2020-01-01',0,NULL,NULL,NULL,NULL,'CC0-1.0',NULL)",
                           (f"w{n}",))
                db.execute("INSERT INTO recording_narrators VALUES(?,'r',?,0)", (f"w{n}", person_id))
            # Extra credits and non-commercial/non-English records must not inflate counts.
            db.execute("INSERT INTO recording_narrators VALUES('w1','r','scott-brick',1)")
            for rid, language, librivox in (("lv", "en", 123), ("zero", "en", 0),
                                          ("foreign", "fr", None), ("empty", "en", "")):
                db.execute("INSERT INTO recordings VALUES(?,'w1',?,60,'2020-01-01',0,NULL,NULL,NULL,NULL,'CC0-1.0',?)",
                           (rid, language, librivox))
                db.execute("INSERT INTO recording_narrators VALUES('w1',?,'scott-brick',0)", (rid,))

    def test_bake_preserves_cast_evidence_and_rejects_non_cc0_rows(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "source.sqlite"
            target = Path(folder) / "baked.sqlite"
            self.make_source(source)
            command = [sys.executable, "-I", str(BAKE), "--source", str(source), "--output", str(target)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            with closing(sqlite3.connect(target)) as db:
                self.assertEqual(db.execute("SELECT rank,person_id,name,english_recordings FROM narrator_shelves ORDER BY rank").fetchall(),
                                 [(1, "scott-brick", "Scott Brick", 2), (2, "simon-vance", "Simon Vance", 1),
                                  (3, "robin-miles", "Robin Miles", 1)])
                self.assertEqual(db.execute("SELECT dramatized,dramatized_signals FROM recordings WHERE work_id='w0'").fetchone(),
                                 (1, '["narrator:full-cast"]'))
                self.assertEqual(db.execute("SELECT COUNT(*) FROM recordings WHERE dramatized=1").fetchone()[0], 1)
                self.assertNotIn("work_descriptions", {r[0] for r in db.execute("SELECT name FROM sqlite_master")})
                for table in ("works", "people"):
                    self.assertNotIn("description", {r[1] for r in db.execute(f"PRAGMA table_info({table})")})
                self.assertEqual(db.execute("SELECT COUNT(*) FROM narrator_shelves WHERE name LIKE '%Full Cast%'").fetchone()[0], 0)
            with closing(sqlite3.connect(source)) as db, db:
                db.execute("UPDATE works SET license='CC-BY-SA-4.0' WHERE id='w0'")
            command[-1] = str(Path(folder) / "rejected.sqlite")
            rejected = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("Non-CC0 rows found in works", rejected.stderr)
            self.assertFalse(Path(command[-1]).exists())

    def test_missing_or_empty_editorial_narrator_fails_loudly(self):
        for person_id in ("scott-brick", "simon-vance", "robin-miles"):
            for defect in ("missing", "librivox-only", "non-english-only", "no-recordings"):
                with self.subTest(person_id=person_id, defect=defect), tempfile.TemporaryDirectory() as folder:
                    source, target = Path(folder) / "source.sqlite", Path(folder) / "baked.sqlite"
                    self.make_source(source)
                    with closing(sqlite3.connect(source)) as db, db:
                        if defect == "missing":
                            db.execute("DELETE FROM people WHERE id=?", (person_id,))
                        else:
                            where = " WHERE (work_id,id) IN (SELECT work_id,recording_id FROM recording_narrators WHERE person_id=?)"
                            if defect == "librivox-only":
                                db.execute("UPDATE recordings SET librivox_id=123" + where, (person_id,))
                            elif defect == "non-english-only":
                                db.execute("UPDATE recordings SET language='fr'" + where, (person_id,))
                            else:
                                db.execute("DELETE FROM recordings" + where, (person_id,))
                    result = subprocess.run([sys.executable, "-I", str(BAKE), "--source", str(source),
                                             "--output", str(target)], capture_output=True, text=True, timeout=30)
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertIn(person_id, result.stderr)
                    self.assertIn("missing from people" if defect == "missing" else "0 commercial English recordings", result.stderr)
                    self.assertFalse(target.exists())
                    self.assertFalse(target.with_suffix(".manifest.json").exists())

    @unittest.skipUnless(SOURCE.exists(), "pinned source snapshot unavailable")
    def test_two_independent_runs_have_identical_bytes(self):
        with tempfile.TemporaryDirectory() as work:
            # The checked-in fixture is run one. Independently regenerate from the
            # pinned 2.1 GB source: a second run must reproduce it byte-for-byte.
            target = Path(work) / "rebaked.sqlite"
            p = subprocess.run([sys.executable, "-I", str(BAKE), "--source", str(SOURCE),
                                "--output", str(target), "--fixture"],
                               capture_output=True, text=True, timeout=900)
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            self.assertEqual(HASH(target), HASH(FIXTURE))
            self.assertEqual(target.read_bytes(), FIXTURE.read_bytes())

    def test_discover_row_provenance_and_editorial_narrators(self):
        manifest = json.loads(FIXTURE.with_suffix(".manifest.json").read_text(encoding="utf-8"))
        self.assertIn("narrator_picks", manifest)
        self.assertEqual([(p["id"], p["name"]) for p in manifest["narrator_picks"]],
                         [("scott-brick", "Scott Brick"), ("simon-vance", "Simon Vance"), ("robin-miles", "Robin Miles")])
        self.assertEqual(manifest["narrator_selection"], {"type": "editorial", "by": "Hemanth", "date": "2026-11-17"})
        self.assertIn("dramatized_signals", manifest)
        with sqlite3.connect(FIXTURE) as db:
            self.assertIn("work_genres",{t for t in manifest["tables"]})
            self.assertIn("genres_summary",{t for t in manifest["tables"]})
            self.assertIn("narrator_shelves",{t for t in manifest["tables"]})
            for col in ("release_date","publisher","cover_url","added_at","dramatized","dramatized_signals"):
                self.assertIn(col,{row[1] for row in db.execute("PRAGMA table_info(recordings)")})
            picks = db.execute("SELECT rank,person_id,name,english_recordings FROM narrator_shelves ORDER BY rank").fetchall()
            self.assertEqual([(r[0],r[1],r[2]) for r in picks],
                             [(1,"scott-brick","Scott Brick"),(2,"simon-vance","Simon Vance"),(3,"robin-miles","Robin Miles")])
            for rank, person_id, name, count in picks:
                actual = db.execute("""SELECT COUNT(*) FROM recordings r WHERE r.language='en'
                    AND (r.librivox_id IS NULL OR r.librivox_id='') AND EXISTS
                    (SELECT 1 FROM recording_narrators n WHERE n.work_id=r.work_id AND n.recording_id=r.id AND n.person_id=?)""",
                    (person_id,)).fetchone()[0]
                self.assertGreater(count, 0)
                self.assertEqual(count, actual)
                self.assertEqual(manifest["shelf_counts"]["narrator:"+person_id], count)
                self.assertEqual(manifest["narrator_picks"][rank-1]["english_recordings"], count)
            self.assertGreater(db.execute("SELECT COUNT(*) FROM work_genres").fetchone()[0],0)
            for r in db.execute("SELECT covers_json FROM genres_summary"):
                urls=json.loads(r[0]);self.assertEqual(len(urls),4)
                self.assertTrue(all(url.startswith(("http://","https://")) for url in urls))
            for v in db.execute("SELECT dramatized,dramatized_signals FROM recordings"):
                signals=json.loads(v[1]);self.assertEqual(bool(v[0]),bool(signals))
            for row in manifest["narrator_picks"]:
                self.assertNotIn(row["name"].lower(),("full cast","various"))

if __name__ == "__main__":
    unittest.main()
