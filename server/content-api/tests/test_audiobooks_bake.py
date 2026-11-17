"""Deterministic and license-constrained AudioSilo bake acceptance tests."""
import hashlib
import json
import os
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
                for dest, sources in info["columns"].items():
                    self.assertIn(dest, {r[1] for r in db.execute(f'PRAGMA table_info("{table}")')})
                    self.assertTrue(sources)
                    self.assertTrue(all(table_name in manifest["cc0_sources"] for table_name in
                                        (source.split(".")[0] for source in sources)))
            self.assertEqual(db.execute("SELECT COUNT(*) FROM works").fetchone()[0],30)

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

    def test_discover_row_provenance_and_baked_narrator_ranking(self):
        manifest = json.loads(FIXTURE.with_suffix(".manifest.json").read_text(encoding="utf-8"))
        self.assertIn("narrator_picks", manifest)
        self.assertEqual(len(manifest["narrator_picks"]),3)
        self.assertIn("dramatized_signals", manifest)
        with sqlite3.connect(FIXTURE) as db:
            self.assertIn("work_genres",{t for t in manifest["tables"]})
            self.assertIn("genres_summary",{t for t in manifest["tables"]})
            self.assertIn("narrator_shelves",{t for t in manifest["tables"]})
            for col in ("release_date","publisher","cover_url","added_at","dramatized","dramatized_signals"):
                self.assertIn(col,{row[1] for row in db.execute("PRAGMA table_info(recordings)")})
            self.assertEqual(db.execute("SELECT COUNT(*) FROM narrator_shelves").fetchone()[0],3)
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
