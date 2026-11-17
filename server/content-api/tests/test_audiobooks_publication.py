"""Offline verification of the AudioSilo-to-D1 SQL publication step."""
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "projection" / "audiobooks_publication.py"
FIXTURE = ROOT / "tests" / "fixtures" / "audiobooks-cc0-fixture.sqlite"
TABLES = ["works","people","series","work_authors","recordings",
          "recording_narrators","work_series","recording_asins","recording_isbns","search_fts"]

class PublicationTests(unittest.TestCase):
    def test_sql_is_deterministic_and_replays_exact_rows(self):
        with tempfile.TemporaryDirectory() as temp:
            outputs = [Path(temp)/f"catalogue-{i}.sql" for i in (1,2)]
            for path in outputs:
                result = subprocess.run([sys.executable,"-I",str(SCRIPT),
                                         "--source",str(FIXTURE),"--output",str(path)],
                                        capture_output=True,text=True,timeout=60)
                self.assertEqual(result.returncode,0,result.stderr)
            first,second=(path.read_bytes() for path in outputs)
            self.assertEqual(hashlib.sha256(first).hexdigest(),hashlib.sha256(second).hexdigest())
            self.assertEqual(first,second)
            report=json.loads(outputs[0].with_suffix(".report.json").read_text(encoding="utf-8"))
            self.assertEqual(report["source_sha256"],hashlib.sha256(FIXTURE.read_bytes()).hexdigest())
            self.assertEqual(report["sql_sha256"],hashlib.sha256(first).hexdigest())
            with sqlite3.connect(FIXTURE) as src,sqlite3.connect(":memory:") as dst:
                dst.executescript(first.decode("utf-8"))
                for table in TABLES:
                    expected=src.execute(f'SELECT COUNT(*) FROM "{table}"').fetchone()[0]
                    actual=dst.execute(f'SELECT COUNT(*) FROM "{table}"').fetchone()[0]
                    self.assertEqual(actual,expected,table)
                self.assertEqual(dst.execute("SELECT title FROM works WHERE id='dune'").fetchone()[0],"Dune")
                self.assertEqual(dst.execute("PRAGMA quick_check").fetchone()[0],"ok")
                self.assertEqual(dst.execute("SELECT COUNT(*) FROM search_fts WHERE search_fts MATCH 'Rhythm'").fetchone()[0],1)

if __name__=="__main__":
    unittest.main()
