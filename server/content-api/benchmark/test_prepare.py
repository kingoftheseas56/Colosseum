import importlib.util
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location("prepare_benchmark",Path(__file__).parent/"prepare.py")
p=importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)


class PreparationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        for dataset in ("imdb","mal","comics","tankoban"):
            db=sqlite3.connect(self.root/(dataset+"_catalog.db"))
            for name,(ds,source,key,columns) in p.DEFINITIONS.items():
                if ds==dataset:
                    declaration=",".join(p.quote(c)+(" INTEGER" if c in {"mal_id","gcd_id","series_id","post_id","members","scored_by","explicit","volume_count","downloads","year","votes"} else " REAL" if c=="score" or c=="rating" else " TEXT") for c in columns)
                    db.execute(f'CREATE TABLE {p.quote(source)} ({declaration})')
            if dataset=="imdb":
                db.executemany("INSERT INTO title VALUES(?,?,?,?,?,?)",[("tt1","Stár Hero","star hero",2000,8.0,100),("tt2","Null Year","null year",None,None,1)])
                db.execute("INSERT INTO genre VALUES('tt1','Action')")
            elif dataset=="mal":
                db.executemany("INSERT INTO manga VALUES(?,?,?,?,?,?,?,?)",[(1,"Hero","hero",100,8,5000,0,"2000-01-01"),(2,"Other","other",50,9,50,0,"2001-01-01")])
                db.execute("INSERT INTO anime VALUES(3,'Anime','anime',50,8)")
                db.executemany("INSERT INTO classification VALUES(?,?,?,?)",[("manga",axis,value,mal_id) for mal_id in (1,2) for axis,value in (("genre","Romance"),("demographic","Seinen"))]+[("anime","genre","Action",3)])
            elif dataset=="comics":
                db.execute("INSERT INTO series VALUES(1,'Alpha Hero',2000)")
                db.execute("INSERT INTO series_stats VALUES(1,1)")
                db.execute("INSERT INTO download VALUES(1,1,'Download','2000-01-01')")
                db.execute("INSERT INTO download_mirror VALUES(1,'https://example.com','example','door')")
            else:
                db.execute("INSERT INTO series VALUES(1,3,'known')")
                db.executemany("INSERT INTO volumes VALUES(?,?,?,?)",[(1,"1","cover1","first"),(1,"5","cover5","extra")])
            db.commit()
            db.close()
        self.hashes={ds:p.file_hash(self.root/(ds+"_catalog.db")) for ds in ("imdb","mal","comics","tankoban")}
        (self.root/"source-manifest.json").write_text(json.dumps({"sources":{ds:{"sha256":h} for ds,h in self.hashes.items()}}))

    def tearDown(self):
        self.temp.cleanup()

    def test_end_to_end_reproducible_closure_and_source_preservation(self):
        with patch.object(p,"ROOT",self.root):
            first=p.prepare(self.root,self.root/"out")
            second=p.prepare(self.root,self.root/"out2")
        self.assertEqual(first["seedContentDigest"],second["seedContentDigest"])
        self.assertLess(first["estimatedWrites"],9000)
        queries={q["id"]:q for q in first["queries"]}
        self.assertEqual(queries["tankoban_overlay"]["expectedRows"],[["1","cover1","first"],["2","",""],["3","",""],["5","cover5","extra"]])
        # Mean=8.5; low-vote score 9 remains slightly above the mean, while score 8 stays below.
        self.assertEqual([r[0] for r in queries["mal_scoped_bayesian"]["expectedRows"]],[2,1])
        self.assertTrue(queries["imdb_null"]["expectedRows"])
        self.assertTrue(queries["imdb_unicode"]["expectedRows"])
        for q in first["queries"]:
            self.assertEqual(q["expectedDigest"],p.digest(q["expectedRows"]))
            self.assertTrue(q["expectedColumns"])
            self.assertIsNone(q["sourceQueryPlan"]["actualRowsScanned"])
        for ds,h in self.hashes.items():
            self.assertEqual(p.file_hash(self.root/(ds+"_catalog.db")),h)

    def test_budget_rejected_before_artifacts(self):
        with patch.object(p,"ROOT",self.root),self.assertRaisesRegex(ValueError,"budget"):
            p.prepare(self.root,self.root/"out",1)
        self.assertFalse((self.root/"out").exists())

    def test_hash_mismatch_rejected(self):
        (self.root/"source-manifest.json").write_text(json.dumps({"sources":{ds:{"sha256":"wrong"} for ds in self.hashes}}))
        with patch.object(p,"ROOT",self.root),self.assertRaisesRegex(ValueError,"snapshot mismatch"):
            p.prepare(self.root,self.root/"out")

    def test_utf8_boundaries_include_wildcard(self):
        cases=p.boundary_cases()
        self.assertEqual([q["patternBytes"] for q in cases],[49,50,51,49,50,51])
        for q in cases:
            self.assertEqual(len(q["args"][0].encode()),q["patternBytes"])
            self.assertTrue(q["args"][0].endswith("%"))

    def test_cross_runtime_canonical_numbers(self):
        self.assertEqual(p.digest([[1.0,0.0,None,"é"]]),p.digest([[1,0,None,"é"]]))


if __name__=="__main__":
    unittest.main()
