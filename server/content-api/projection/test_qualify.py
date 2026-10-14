from contextlib import closing
import json
import sqlite3
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from qualify import Budget, compose_connector_code, digest, execute_plan, prepare_probe


class QualificationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.db_path = Path(self.directory.name) / "projection.sqlite"
        self.manifest_path = Path(self.directory.name) / "manifest.json"
        with closing(sqlite3.connect(self.db_path)) as db:
            db.execute("CREATE TABLE proj_titles(id INTEGER PRIMARY KEY,title TEXT)")
            db.execute("CREATE INDEX proj_titles_title_idx ON proj_titles(title)")
            db.executemany("INSERT INTO proj_titles VALUES(?,?)", [(i, f"Title {i}") for i in range(30)])
            db.execute("CREATE TABLE proj_payloads(id INTEGER PRIMARY KEY,data TEXT)")
            db.executemany("INSERT INTO proj_payloads VALUES(?,?)", [(1, "small"), (2, "x" * 4097), (3, "third"), (4, "fourth")])
            db.execute("CREATE TABLE proj_comics_series(gcd_id INTEGER PRIMARY KEY,title TEXT)")
            db.executemany("INSERT INTO proj_comics_series VALUES(?,?)", [(9, "Comic Alpha"), (2, "Comic Beta")])
            db.commit()
        self.manifest = {"artifactVersion": 1, "revision": "fixture-v1", "sources": {}, "schemaSql": [],
                         "tables": [{"name": "proj_titles", "columns": ["id", "title"], "indexCount": 1, "rows": 30, "sha256": "fixture"},
                                    {"name": "proj_payloads", "columns": ["id", "data"], "indexCount": 0, "rows": 4, "sha256": "fixture"}]}
        self.manifest["tables"].append({"name":"proj_comics_series", "columns":["gcd_id","title"], "indexCount":0, "rows":2, "sha256":"fixture"})
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        self.plan = prepare_probe(self.db_path, self.manifest_path)

    def test_probe_preserves_artifact_and_caps_roots_and_payloads(self):
        before = self.db_path.read_bytes()
        again = prepare_probe(self.db_path, self.manifest_path)
        self.assertEqual(before, self.db_path.read_bytes())
        self.assertEqual(self.plan, again)
        self.assertEqual(self.plan["samples"]["proj_titles"]["rows"], 10)
        self.assertEqual(self.plan["samples"]["proj_payloads"]["rows"], 2)
        self.assertLessEqual(self.plan["reservation"]["rowsWritten"], 1500)
        self.assertLessEqual(self.plan["reservation"]["rowsRead"], 10000)
        self.assertTrue(any(s["id"] == "fts:match" for s in self.plan["statements"]))

    def test_complete_local_scalar_parity_roundtrip(self):
        with closing(sqlite3.connect(":memory:")) as db:
            def executor(statement):
                rows = [list(r) for r in db.execute(statement["sql"], statement["params"]).fetchall()]
                return {"success": True, "results": rows, "meta": {"rows_read": len(rows), "rows_written": 0}}
            result = execute_plan(self.plan, executor, self.plan["planDigest"])
        self.assertTrue(result["ok"])
        self.assertFalse(result["cleanupVerified"])

    def test_rejects_mutated_plan_before_executor(self):
        self.plan["statements"][0]["sql"] = "DROP TABLE unrelated"
        with self.assertRaises(ValueError):
            execute_plan(self.plan, lambda _: self.fail("Executor must not run"), self.plan["planDigest"])

    def test_budget_rejects_next_reservation_and_counts_indexes(self):
        meter = Budget(reads=10, writes=5)
        meter.observe({"rows_read": 8, "rows_written": 4})
        with self.assertRaises(ValueError):
            meter.reserve(3, 1)
        with self.assertRaises(ValueError):
            meter.reserve(1, 2)
        with self.assertRaises(ValueError):
            Budget(writes=1501)

    def test_missing_meta_stops_without_retry_or_secret_serialization(self):
        calls = []
        def executor(statement):
            calls.append(statement["id"])
            return {"success": True, "results": [], "meta": {}}
        result = execute_plan(self.plan, executor, self.plan["planDigest"])
        self.assertFalse(result["ok"])
        self.assertTrue(result["meteringUnknown"])
        self.assertEqual(len(calls), 1)
        self.assertGreater(result["operations"][0]["reservedWrites"], 0)

    def test_failure_retains_observed_cost_before_parity_rejection(self):
        calls = []
        def executor(statement):
            calls.append(statement["id"])
            return {"success": True, "results": [["wrong"]], "meta": {"rows_read": 0, "rows_written": 2}}
        result = execute_plan(self.plan, executor, self.plan["planDigest"])
        self.assertFalse(result["ok"])
        self.assertFalse(result["meteringUnknown"])
        self.assertEqual((result["actualRowsRead"], result["actualRowsWritten"]), (0, 2))
        self.assertEqual(len(calls), 1)

    def test_failed_provider_response_halts_without_leaking_exception(self):
        def executor(_):
            raise RuntimeError("sensitive provider details")
        result = execute_plan(self.plan, executor, self.plan["planDigest"])
        self.assertNotIn("sensitive provider", json.dumps(result))
        self.assertTrue(result["meteringUnknown"])

    def test_provider_failure_with_meta_is_still_counted(self):
        result = execute_plan(self.plan, lambda _: {"success": False, "meta": {"rows_read": 9, "rows_written": 2}}, self.plan["planDigest"])
        self.assertFalse(result["ok"])
        self.assertFalse(result["meteringUnknown"])
        self.assertEqual((result["actualRowsRead"], result["actualRowsWritten"]), (9, 2))

    def test_operation_reservation_overrun_halts_after_metering(self):
        calls = []
        def executor(statement):
            calls.append(statement["id"])
            return {"success": True, "results": [], "meta": {"rows_read": 0, "rows_written": 6}}
        result = execute_plan(self.plan, executor, self.plan["planDigest"])
        self.assertFalse(result["ok"])
        self.assertEqual(result["actualRowsWritten"], 6)
        self.assertEqual(len(calls), 1)

    def test_manifest_name_is_restricted_before_artifact_sql(self):
        self.manifest["tables"][0]["name"] = "existing_production_table"
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        with self.assertRaises(ValueError):
            prepare_probe(self.db_path, self.manifest_path)

    def test_without_rowid_membership_uses_primary_key_order(self):
        with closing(sqlite3.connect(self.db_path)) as db:
            db.execute("CREATE TABLE proj_membership(a INTEGER,b TEXT,PRIMARY KEY(a,b)) WITHOUT ROWID")
            db.executemany("INSERT INTO proj_membership VALUES(?,?)", [(i, "m") for i in range(30, 0, -1)])
            db.commit()
        self.manifest["tables"].append({"name": "proj_membership", "columns": ["a", "b"], "indexCount": 0, "rows": 30, "sha256": "fixture"})
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        plan = prepare_probe(self.db_path, self.manifest_path)
        verify = next(s for s in plan["statements"] if s["id"] == "verify:proj_membership")
        self.assertEqual(len(verify["expectedRows"]), 20)
        self.assertEqual(verify["expectedRows"][0], [1, "m"])

    def test_small_payload_is_selected_beyond_initial_root_window(self):
        with closing(sqlite3.connect(self.db_path)) as db:
            db.execute("DELETE FROM proj_payloads")
            db.executemany("INSERT INTO proj_payloads VALUES(?,?)", [(i, "x" * 4097) for i in range(1, 21)])
            db.execute("INSERT INTO proj_payloads VALUES(21,'eligible')")
            db.commit()
        plan = prepare_probe(self.db_path, self.manifest_path)
        verify = next(s for s in plan["statements"] if s["id"] == "verify:proj_payloads")
        self.assertEqual(verify["expectedRows"], [[21, "eligible"]])

    def test_actual_trigram_external_content_is_the_probed_schema(self):
        create = next(s for s in self.plan["statements"] if s["id"] == "fts:create")
        populate = next(s for s in self.plan["statements"] if s["id"] == "fts:populate")
        self.assertIn("tokenize='trigram'", create["sql"])
        self.assertIn("content='proj_comics_series'", create["sql"])
        self.assertIn("SELECT gcd_id,title FROM proj_comics_series", populate["sql"])
        verify = next(s for s in self.plan["statements"] if s["id"] == "fts:verify")
        self.assertEqual(verify["expectedRows"], [[2, "Comic Beta"], [9, "Comic Alpha"]])

    def test_null_meta_returns_failed_receipt_not_an_unhandled_error(self):
        result = execute_plan(self.plan, lambda _: {"success": True, "meta": None}, self.plan["planDigest"])
        self.assertFalse(result["ok"])
        self.assertTrue(result["meteringUnknown"])

    def test_artifact_payload_digest_mismatch_rejected_before_hosted_work(self):
        with closing(sqlite3.connect(self.db_path)) as db:
            db.execute("CREATE TABLE proj_checked_payload(id INTEGER PRIMARY KEY,data TEXT,sha256 TEXT)")
            db.execute("INSERT INTO proj_checked_payload VALUES(1,'small','wrong')")
            db.commit()
        self.manifest["tables"].append({"name": "proj_checked_payload", "columns": ["id", "data", "sha256"], "indexCount": 0, "rows": 1, "sha256": "fixture"})
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        with self.assertRaises(ValueError):
            prepare_probe(self.db_path, self.manifest_path)

    def test_numeric_json_equivalence_preserves_other_scalar_facts(self):
        self.assertEqual(digest({"nested": [[6.0, -0.0, 1.5]]}), digest({"nested": [[6, 0, 1.5]]}))
        self.assertNotEqual(digest([True]), digest([1]))
        self.assertNotEqual(digest(["6.0"]), digest(["6"]))
        self.assertNotEqual(digest(['{"value":6.0}']), digest(['{"value":6}']))
        self.assertNotEqual(digest([None]), digest([0]))
        self.assertNotEqual(digest([1.5]), digest([1]))
        with self.assertRaises(ValueError):
            digest([float("inf")])

    def test_integral_real_facts_survive_json_number_transport(self):
        with closing(sqlite3.connect(self.db_path)) as db:
            db.execute("CREATE TABLE proj_real(id INTEGER PRIMARY KEY,score REAL)")
            db.execute("INSERT INTO proj_real VALUES(1,6.0)")
            db.commit()
        self.manifest["tables"].append({"name": "proj_real", "columns": ["id", "score"], "indexCount": 0, "rows": 1, "sha256": "fixture"})
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        plan = prepare_probe(self.db_path, self.manifest_path)
        with closing(sqlite3.connect(":memory:")) as db:
            def executor(statement):
                rows = [[int(v) if isinstance(v, float) and v.is_integer() else v for v in r]
                        for r in db.execute(statement["sql"], statement["params"]).fetchall()]
                return {"success": True, "results": rows, "meta": {"rows_read": len(rows), "rows_written": 0}}
            self.assertTrue(execute_plan(plan, executor, plan["planDigest"])["ok"])

    def test_measured_create_table_read_one_write_three_fits_reservation(self):
        statement = self.plan["statements"][0]
        self.assertEqual((statement["maxRowsRead"], statement["maxRowsWritten"]), (10, 5))
        tiny = {"statements": [statement]}
        tiny["planDigest"] = digest(tiny)
        result = execute_plan(tiny, lambda _: {"success": True, "results": [], "meta": {"rows_read": 1, "rows_written": 3}}, tiny["planDigest"], reads=9999, writes=1497)
        self.assertTrue(result["ok"])
        self.assertEqual((result["actualRowsRead"], result["actualRowsWritten"]), (1, 3))

    def test_composed_connector_literal_preserves_dollars_and_utf8(self):
        values = ["$$60", "$&", "$`", "$'", "Unicode 雪😀", "literal\\backslash\\n", "quote\" and newline\n"]
        prefix, suffix = "const payload=", ";"
        code = compose_connector_code(prefix, {"params": values}, suffix)
        self.assertEqual(json.loads(code[len(prefix):-len(suffix)]), {"params": values})
        self.assertIn("$$60", code)
        self.assertNotIn("Authorization", code)
        node = shutil.which("node")
        self.assertIsNotNone(node, "Node required to verify the actual JavaScript transport literal")
        runtime_code = compose_connector_code(prefix, {"params": values}, ";console.log(JSON.stringify(payload));")
        result = subprocess.run([node, "-e", runtime_code], capture_output=True, text=True,
                                encoding="utf-8", check=True, timeout=5)
        self.assertEqual(json.loads(result.stdout), {"params": values})

    def test_root_eight_preserves_membership_and_payload_caps(self):
        plan = prepare_probe(self.db_path, self.manifest_path, root_limit=8)
        self.assertEqual(plan["rootLimit"], 8)
        self.assertEqual(plan["samples"]["proj_titles"]["rows"], 8)
        self.assertEqual(plan["samples"]["proj_payloads"]["rows"], 2)
        for invalid in (0, 11, True, 1.5):
            with self.assertRaises(ValueError):
                prepare_probe(self.db_path, self.manifest_path, root_limit=invalid)


if __name__ == "__main__":
    unittest.main()
