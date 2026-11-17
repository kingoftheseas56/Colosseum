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
            outputs = [Path(work) / f"bake{i}.sqlite" for i in (1, 2)]
            for target in outputs:
                p = subprocess.run([sys.executable, "-I", str(BAKE), "--source", str(SOURCE),
                                    "--output", str(target), "--fixture"],
                                   capture_output=True, text=True, timeout=180)
                self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            self.assertEqual(HASH(outputs[0]), HASH(outputs[1]))
            self.assertEqual(outputs[0].read_bytes(), outputs[1].read_bytes())
            self.assertEqual(HASH(outputs[0]), HASH(FIXTURE))

if __name__ == "__main__":
    unittest.main()
