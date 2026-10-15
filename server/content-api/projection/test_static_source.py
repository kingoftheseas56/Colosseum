import hashlib
import json
import sqlite3
import tempfile
import unittest
from contextlib import closing
from pathlib import Path
from unittest.mock import patch
import packed
import static_source


class StaticSourceTests(unittest.TestCase):
    def fixture(self, root):
        source = root / 'source'
        source.mkdir()
        path = source / 'imdb_catalog.db'
        with closing(sqlite3.connect(path)) as db:
            db.executescript('CREATE TABLE title(tt TEXT PRIMARY KEY,n INTEGER,r REAL,note TEXT);CREATE TABLE genre(tt TEXT,genre TEXT);')
            db.executemany('INSERT INTO title VALUES(?,?,?,?)', [('tt1', None, 1.5, "l'été"), ('tt2', 4, None, '日本語')])
            db.execute("INSERT INTO genre VALUES('tt1','Drama')")
            db.commit()
        (root / 'source-manifest.json').write_text(json.dumps({'sources': {'imdb': {'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}}}))
        return source, path

    def test_local_oracle_preserves_types_rows_membership_and_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, path = self.fixture(root)
            before = path.read_bytes()
            with patch.object(packed, 'ROOT', root):
                report = static_source.build(source, root / 'out')
            with closing(sqlite3.connect(root / 'out/static-source.sqlite')) as db:
                self.assertEqual(db.execute('SELECT * FROM lib_imdb_title').fetchall(), [('tt1', None, 1.5, "l'été"), ('tt2', 4, None, '日本語')])
                self.assertEqual(db.execute('SELECT __membership FROM den_imdb_title ORDER BY rowid').fetchall(), [('["Drama"]',), ('[]',)])
                self.assertEqual(db.execute("SELECT COUNT(*) FROM sqlite_master WHERE name='_library_state'").fetchone()[0], 0)
            self.assertEqual(path.read_bytes(), before)
            self.assertEqual(report['format'], 'static-source-v1')
            self.assertEqual(report['sqliteSha256'], hashlib.sha256((root / 'out/static-source.sqlite').read_bytes()).hexdigest())
            self.assertFalse((root / 'out/000-schema.sql').exists())
            self.assertFalse((root / 'out/oracle/packed.sql').exists())

    def test_changed_source_and_existing_output_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, path = self.fixture(root)
            path.write_bytes(b'changed')
            with patch.object(packed, 'ROOT', root), self.assertRaisesRegex(ValueError, 'Source hash mismatch'):
                static_source.build(source, root / 'out')
            with self.assertRaises(FileExistsError):
                static_source.build(source, root / 'out')


if __name__ == '__main__':
    unittest.main()
