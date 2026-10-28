import hashlib,json,sqlite3,tempfile,unittest
from pathlib import Path
from contextlib import closing
from unittest.mock import patch
import packed

class PackedTests(unittest.TestCase):
 def fixture(self,folder,rows):
  source=folder/'source';source.mkdir();dbpath=source/'imdb_catalog.db'
  with closing(sqlite3.connect(dbpath)) as db:
   db.executescript('CREATE TABLE title(tt TEXT PRIMARY KEY,n INTEGER,r REAL,note TEXT);CREATE TABLE genre(tt TEXT,genre TEXT);')
   db.executemany('INSERT INTO title VALUES (?,?,?,?)',rows);db.executemany('INSERT INTO genre VALUES (?,?)',[(r[0],'Drama')for r in rows]);db.commit()
  manifest={'sources':{'imdb':{'bytes':dbpath.stat().st_size,'sha256':hashlib.sha256(dbpath.read_bytes()).hexdigest()}}}
  (folder/'source-manifest.json').write_text(json.dumps(manifest));return source
 def test_typed_full_rows_nulls_quotes_and_membership(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);rows=[('tt1',None,1.5,"l'été"),('tt2',4,None,'日本語')];source=self.fixture(root,rows)
   before=(source/'imdb_catalog.db').read_bytes()
   with patch.object(packed,'ROOT',root):report=packed.build(source,root/'out',64)
   with closing(sqlite3.connect(root/'out/packed.sqlite'))as db:
    self.assertEqual(db.execute('SELECT * FROM lib_imdb_title').fetchall(),rows)
    self.assertEqual(db.execute('SELECT __membership FROM den_imdb_title').fetchone()[0],'["Drama"]')
   self.assertEqual(report['logicalRows'],4);self.assertEqual(report['integrityCheck'],'ok');self.assertFalse(report['hostedQualified'])
   self.assertEqual(before,(source/'imdb_catalog.db').read_bytes())
 def test_adaptive_statement_byte_limit(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);source=self.fixture(root,[('tt'+str(i),i,None,"'"*10000)for i in range(12)])
   with patch.object(packed,'ROOT',root):packed.build(source,root/'out',128)
   self.assertTrue(all(len(line.encode())<95000 for line in (root/'out/packed.sql').read_text().splitlines()))
 def test_source_hash_mismatch_rejected(self):
  with tempfile.TemporaryDirectory()as tmp:
   root=Path(tmp);source=self.fixture(root,[]);(source/'imdb_catalog.db').write_bytes(b'corrupt')
   with patch.object(packed,'ROOT',root),self.assertRaisesRegex(ValueError,'Source hash mismatch'):packed.build(source,root/'out')
if __name__=='__main__':unittest.main()
