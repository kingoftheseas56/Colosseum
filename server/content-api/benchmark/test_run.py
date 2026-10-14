import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
import threading
import unittest

import run


def fixture():
    statements = ["CREATE TABLE b_items (id INTEGER, title TEXT)", "CREATE INDEX b_items_id ON b_items(id)"]
    tables = [{"name": "b_items", "columns": ["id", "title"], "rows": [[1, "世界"], [2, "Test"]]}]
    query = {"id": "lookup", "sql": "SELECT id,title FROM b_items WHERE id=?", "args": [1], "expectedColumns": ["id", "title"], "expectedRows": [[1, "世界"]]}
    query["expectedDigest"] = run.digest(query["expectedRows"])
    seed = {"version": 1, "schemaStatements": statements, "tables": tables, "queries": [query], "estimatedWrites": 4, "seedRows": 2}
    seed["seedContentDigest"] = hashlib.sha256(run.compact({"schema": statements, "tables": tables}).encode("utf-8")).hexdigest()
    return seed


class FakeTransport:
    def __init__(self, result, status=200):
        self.calls, self.result, self.status, self.payload = 0, result, status, None

    def request(self, url, token, payload):
        self.calls += 1
        self.payload = payload
        return {"status": self.status, "body": run.compact(self.result).encode(), "elapsedMs": 10, "responseBytes": 100}


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.config = {"backend": "sqlite", "databasePath": str(Path(self.temp.name)/"bench.db")}

    def database(self):
        db = run.Database(self.config, writing=True)
        self.addCleanup(db.close)
        return db

    def test_import_read_parity_and_no_overwrite(self):
        db, seed = self.database(), fixture()
        imported = run.import_seed(db, seed, self.config, execute=True)
        self.assertIsNone(imported["reportedRowsWritten"])
        report = run.run_sql(db, seed, repetitions=3)
        self.assertTrue(report["allParityPassed"])
        self.assertEqual(report["results"][0]["repeatSummary"]["count"], 3)
        self.assertFalse(report["coldCacheProven"])
        with self.assertRaisesRegex(run.Failure, "TARGET_NOT_EMPTY"):
            run.import_seed(db, seed, self.config, execute=True)

    def test_corruption_fails_parity_without_repairing_data(self):
        db, seed = self.database(), fixture()
        run.import_seed(db, seed, self.config, execute=True)
        db.execute("UPDATE b_items SET title='changed' WHERE id=1")
        report = run.run_sql(db, seed, repetitions=1)
        self.assertFalse(report["allParityPassed"])
        self.assertEqual(report["results"][0]["error"], "RESULT_PARITY_FAILED")

    def test_tampered_seed_and_import_opt_in(self):
        seed = fixture()
        seed["tables"][0]["rows"][0][1] = "different"
        with self.assertRaisesRegex(run.Failure, "SEED_HASH"):
            run.validate_seed(seed)
        with self.assertRaisesRegex(run.Failure, "EXECUTE"):
            run.import_seed(self.database(), fixture(), self.config)

    def test_turso_types_exact_integer_and_provider_metrics(self):
        transport = FakeTransport({"results": [{"type": "ok", "response": {"type": "execute", "result": {
            "cols": [{"name": "id"}, {"name": "v"}], "rows": [[{"type": "integer", "value": "9007199254740993"}, {"type": "null"}]], "rows_read": 1, "rows_written": 0}}}]})
        from unittest.mock import patch
        with patch.dict(run.os.environ, {"BENCH_TEST_TOKEN": "private"}):
            db = run.Database({"backend": "turso", "url": "libsql://example.turso.io", "tokenEnv": "BENCH_TEST_TOKEN"}, transport=transport)
        result = db.execute("SELECT ?,NULL", [9007199254740993])
        self.assertEqual(result["rows"], [[9007199254740993, None]])
        self.assertEqual(transport.payload["requests"][0]["stmt"]["args"][0]["value"], "9007199254740993")
        self.assertEqual(result["meta"]["rows_read"], 1)
        self.assertEqual(transport.payload["requests"][-1], {"type": "close"})

    def test_d1_column_order_and_error_redaction(self):
        transport = FakeTransport({"success": True, "result": [{"success": True, "results": [{"v": "x", "id": 1}], "meta": {"rows_read": 3}}]})
        from unittest.mock import patch
        with patch.dict(run.os.environ, {"BENCH_TEST_TOKEN": "secret-never-log"}):
            db = run.Database({"backend": "d1", "accountId": "a"*32, "databaseId": "b"*36, "tokenEnv": "BENCH_TEST_TOKEN"}, transport=transport)
        self.assertEqual(db.execute("SELECT id,v", columns=["id", "v"])["rows"], [[1, "x"]])
        transport.result = {"success": False, "errors": [{"message": "secret-never-log"}]}
        with self.assertRaisesRegex(run.Failure, "^D1_QUERY_REJECTED$"):
            db.execute("SELECT id,v")

    def test_local_results_never_select_cloud_winner(self):
        report = run.run_sql(self.seeded_database(), fixture(), repetitions=1)
        result = run.compare(report, report)
        self.assertIsNone(result["winner"])
        self.assertIn("BOTH_HOSTED_BACKENDS_REQUIRED", result["reasons"])
        self.assertEqual(result["decision"], "not-qualified")

    def test_incomplete_reports_never_qualify_comparison(self):
        result = run.compare({"backend": "d1", "allParityPassed": True}, {"backend": "turso", "allParityPassed": True})
        self.assertFalse(result["directSqlComparable"])
        self.assertIn("INVALID_REPORT_LEFT", result["reasons"])
        self.assertIn("INVALID_REPORT_RIGHT", result["reasons"])
        # Simulated hosted envelopes validate the gate only, not hosted performance.
        left = run.run_sql(self.seeded_database(), fixture(), repetitions=1)
        left.update(backend="d1", path="direct-provider-sql-http")
        right = dict(left, backend="turso")
        self.assertTrue(run.compare(left, right)["directSqlComparable"])
        self.assertIsNone(run.compare(left, right)["winner"])

    def seeded_database(self):
        db = self.database()
        run.import_seed(db, fixture(), self.config, execute=True)
        return db

    def test_read_budget_stops_after_observed_overrun(self):
        db = self.seeded_database()
        execute = db.execute
        def with_meter(sql, args=None, columns=None):
            value = execute(sql, args, columns)
            value["meta"] = {"rows_read": 10}
            return value
        db.execute = with_meter
        report = run.run_sql(db, fixture(), repetitions=3, max_reads=15)
        self.assertFalse(report["allParityPassed"])
        self.assertEqual(report["results"][0]["error"], "OBSERVED_READ_BUDGET_EXCEEDED")

    def test_provider_failure_stops_and_partial_import_retains_meters(self):
        db = self.database()
        execute, calls = db.execute, []
        def failure_on_insert(sql, args=None, columns=None):
            calls.append(sql)
            if sql.startswith("INSERT"):
                raise run.Failure("PROVIDER_HTTP_429")
            record = execute(sql, args, columns)
            record["meta"] = {"rows_written": 1, "rows_read": 1}
            return record
        db.execute = failure_on_insert
        with self.assertRaises(run.Failure) as caught:
            run.import_seed(db, fixture(), self.config, execute=True)
        self.assertEqual(caught.exception.report["completedStatements"], 2)
        self.assertEqual(caught.exception.report["reportedRowsWritten"], 2)
        self.assertEqual(caught.exception.report["seedDigest"], fixture()["seedContentDigest"])
        self.assertEqual(len(calls), 3)

    def test_sql_stops_after_provider_error_or_overbudget_mismatch(self):
        db, seed = self.seeded_database(), fixture()
        seed["queries"] *= 2
        original = db.execute
        calls = []
        def reject(sql, args=None, columns=None):
            calls.append(sql)
            if "WHERE" in sql:
                raise run.Failure("PROVIDER_HTTP_429")
            return original(sql, args, columns)
        db.execute = reject
        report = run.run_sql(db, seed, 3)
        self.assertEqual(len(report["results"]), 1)
        self.assertEqual(len(calls), 2)
        def mismatch_with_meter(sql, args=None, columns=None):
            record = original(sql, args, columns)
            if "WHERE" in sql:
                record["rows"] = []
                record["meta"] = {"rows_read": 100}
            return record
        db.execute = mismatch_with_meter
        report = run.run_sql(db, seed, 3, max_reads=50)
        self.assertIsNone(report["reportedRowsRead"])
        self.assertEqual(report["results"][0]["error"], "OBSERVED_READ_BUDGET_EXCEEDED")
        self.assertEqual(len(report["results"]), 1)

    def test_malformed_turso_and_lookalike_system_table(self):
        from unittest.mock import patch
        with patch.dict(run.os.environ, {"BENCH_TEST_TOKEN": "private"}):
            db = run.Database({"backend": "turso", "url": "libsql://example.turso.io", "tokenEnv": "BENCH_TEST_TOKEN"}, transport=FakeTransport({"results": []}))
        with self.assertRaisesRegex(run.Failure, "MALFORMED_PROVIDER_RESPONSE"):
            db.execute("SELECT 1")
        local = self.database()
        local.execute("CREATE TABLE xcf_data(id INTEGER)")
        with self.assertRaisesRegex(run.Failure, "TARGET_NOT_EMPTY"):
            run.import_seed(local, fixture(), self.config, execute=True)


class HttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass
            def do_GET(self):
                if self.path == "/redirect":
                    self.send_response(302); self.send_header("Location", "/json"); self.end_headers(); return
                if self.path == "/fail":
                    self.send_response(503); self.end_headers(); return
                if self.headers.get("If-None-Match") == '"v1"':
                    self.send_response(304); self.end_headers(); return
                body = b'{"ok":true}'
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("ETag", '"v1"')
                self.end_headers(); self.wfile.write(body)
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown(); cls.server.server_close(); cls.thread.join()

    def test_real_local_http_etag_and_failed_next_route(self):
        report = run.run_http({"allowLocalhost": True, "routes": [{"id": "json", "url": self.base+"/json", "kind": "json"}, {"id": "failure", "url": self.base+"/fail"}]}, 1)
        self.assertTrue(report["results"][0]["passed"])
        self.assertEqual(report["results"][0]["conditional"]["status"], 304)
        self.assertFalse(report["results"][1]["passed"])
        self.assertIsNone(report["results"][1]["conditional"])
        self.assertIsNone(report["nativeFirstPaintMs"])

    def test_transport_does_not_redirect_or_exceed_budget(self):
        transport = run.Transport(limit=1)
        response = transport.request(self.base+"/redirect", "sensitive")
        self.assertEqual(response["status"], 302)
        with self.assertRaisesRegex(run.Failure, "REQUEST_BUDGET"):
            transport.request(self.base+"/json")

    def test_body_limit_and_mismatched_asset_hash(self):
        with self.assertRaisesRegex(run.Failure, "BYTE_LIMIT"):
            run.Transport(max_bytes=2).request(self.base+"/json")
        report = run.run_http({"allowLocalhost": True, "routes": [{"id": "hash", "url": self.base+"/json", "sha256": "0"*64}]}, 1)
        self.assertEqual(report["results"][0]["error"], "HTTP_BODY_HASH_MISMATCH")

    def test_auth_required_and_aggregate_transfer_bound(self):
        with self.assertRaisesRegex(run.Failure, "TOKEN_ENV_MISSING"):
            run.run_http({"allowLocalhost": True, "routes": [{"id": "private", "url": self.base+"/json", "authenticated": True}]}, 1)
        transport = run.Transport(total_bytes=15)
        transport.request(self.base+"/json")
        with self.assertRaisesRegex(run.Failure, "TRANSFER_BUDGET_EXHAUSTED"):
            transport.request(self.base+"/json")
        self.assertLessEqual(transport.received_bytes, 16)


if __name__ == "__main__":
    unittest.main()
