import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

SPEC = importlib.util.spec_from_file_location("publisher", Path(__file__).resolve().parents[1] / "tools/publish.py")
p = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(p)


class FakeClient:
    def __init__(self, spec):
        self.spec = spec
        self.bodies = []
        self.seen = set()
        self.fail = False
        self.quota = False
        self.used = 0
        self.completed = False

    def request(self, method, path, body=None):
        if path == "/admin/status":
            return {"tables": [{"name": "test__items", "source_hash": self.spec["sourceHash"], "expected_rows": self.spec["rowCount"], "imported_rows": 0, "ready": False}], "budget": [{"reserved_writes": self.used}]}
        if path == "/admin/complete":
            self.completed = True
            return {"ok": True}
        self.bodies.append(body)
        if self.quota:
            raise p.DailyBudget()
        replayed = body["chunkId"] in self.seen
        self.seen.add(body["chunkId"])
        if not replayed:
            self.used += len(body["rows"]) * self.spec["writeWeight"] + 10
        if self.fail:
            self.fail = False
            raise p.PublishError("lost acknowledgement")
        return {"ok": True, "replayed": replayed}


class PublisherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        source = self.root / "test_catalog.db"
        with sqlite3.connect(source) as db:
            db.execute("CREATE TABLE items (id INTEGER PRIMARY KEY, title TEXT)")
            db.executemany("INSERT INTO items VALUES (?,?)", [(i, "title" + str(i)) for i in range(1, 66)])
        db.close()
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        self.spec = {"dataset": "test", "sourceTable": "items", "columns": ["id", "title"], "rowCount": 65, "sourceHash": digest, "writeWeight": 2}
        self.schema = {"test__items": self.spec}
        self.manifest = {"sources": {"test": {"sha256": digest}}}
        self.client = FakeClient(self.spec)
        self.progress = self.root / "progress.json"

    def tearDown(self):
        self.temp.cleanup()

    def run_publish(self, **kwargs):
        return p.publish(self.root, self.schema, self.manifest, self.client, self.progress, **kwargs)

    def test_uncertain_ack_replays_identical_chunk_and_counts_once(self):
        self.client.fail = True
        with self.assertRaises(p.PublishError):
            self.run_publish()
        pending = json.loads(self.progress.read_text())["tables"]["test__items"]["pending"]["body"]
        self.assertEqual(self.run_publish()["stopped"], "complete")
        self.assertEqual(self.client.bodies[1], pending)
        self.assertEqual(self.client.used, 160)
        self.assertEqual(json.loads(self.progress.read_text())["tables"]["test__items"]["rows"], 65)
        self.assertTrue(self.client.completed)

    def test_smoke_resume_keyset(self):
        self.assertEqual(self.run_publish(max_chunks=1)["stopped"], "chunk-limit")
        self.assertFalse(self.client.completed)
        self.run_publish()
        self.assertEqual([b["rows"][0][0] for b in self.client.bodies], [1, 33, 65])

    def test_server_budget_survives_invocations(self):
        self.client.used = 44950
        self.assertEqual(self.run_publish()["stopped"], "estimated-write-limit")
        self.assertEqual(self.client.bodies, [])

    def test_daily_quota_preserves_pending(self):
        self.client.quota = True
        self.assertEqual(self.run_publish()["stopped"], "server-daily-budget")
        self.assertIn("pending", json.loads(self.progress.read_text())["tables"]["test__items"])

    def test_hash_mismatch_never_uploads(self):
        self.manifest["sources"]["test"]["sha256"] = "bad"
        with self.assertRaisesRegex(p.PublishError, "hash mismatch"):
            self.run_publish()
        self.assertEqual(self.client.bodies, [])

    def test_http_malformed_and_specific_quota(self):
        client = p.Client({"baseUrl": "https://example.com", "adminToken": "test"})
        for code, body, error in [(200, b"not-json", p.PublishError), (429, b'{"error":"IMPORT_DAILY_BUDGET"}', p.DailyBudget), (429, b'{"error":"other"}', p.PublishError)]:
            if code == 200:
                response = unittest.mock.MagicMock()
                response.__enter__.return_value.read.return_value = body
                context = patch.object(p.urllib.request, "urlopen", return_value=response)
            else:
                context = patch.object(p.urllib.request, "urlopen", side_effect=urllib.error.HTTPError("https://example.com", code, "error", {}, io.BytesIO(body)))
            with context, self.assertRaises(error):
                client.request("POST", "/admin/import", {})

    def test_client_identifies_publisher(self):
        response = unittest.mock.MagicMock()
        response.__enter__.return_value.read.return_value = b'{"tables":[],"budget":[]}'
        with patch.object(p.urllib.request, "urlopen", return_value=response) as fetch:
            p.Client({"baseUrl": "https://example.com", "adminToken": "test"}).request("GET", "/admin/status")
        self.assertEqual(fetch.call_args.args[0].get_header("User-agent"), "ColosseumContentPublisher/1.0 (https://getcolosseum.world)")

    def test_lock_exclusive(self):
        with p.publisher_lock(self.root):
            with self.assertRaises(p.PublishError):
                with p.publisher_lock(self.root):
                    pass

    def test_chunk_shrinks_to_body_limit_and_rejects_oversized_row(self):
        with sqlite3.connect(":memory:") as db:
            db.execute("CREATE TABLE items (id INTEGER PRIMARY KEY, title TEXT)")
            db.executemany("INSERT INTO items VALUES (?,?)", [(1, "x" * 150000), (2, "y" * 150000)])
            chunk = p.make_chunk(db, "test__items", self.spec, 0)
            self.assertEqual(chunk["rowCount"], 1)
            self.assertLessEqual(len(p.encode(chunk["body"])), p.MAX_BODY)
            db.execute("UPDATE items SET title=? WHERE id=1", ("z" * p.MAX_BODY,))
            with self.assertRaisesRegex(p.PublishError, "single source row"):
                p.make_chunk(db, "test__items", self.spec, 0)

    def test_enrichment_one_request_with_spacing_and_identifying_agent(self):
        mbid = "12345678-1234-1234-1234-123456789abc"
        p.atomic_save(self.root / "musicbrainz.json", {"lastRequest": 100})
        response = unittest.mock.MagicMock()
        response.__enter__.return_value.read.return_value = b'{"id":"known"}'
        client = unittest.mock.Mock()
        with patch.object(p.time, "time", return_value=100.5), patch.object(p.time, "sleep") as sleep, patch.object(p.urllib.request, "urlopen", return_value=response) as fetch:
            p.enrich("artist", mbid, client, self.root)
        self.assertAlmostEqual(sleep.call_args.args[0], .6)
        fetch.assert_called_once()
        self.assertIn("getcolosseum.world", fetch.call_args.args[0].get_header("User-agent"))
        body = client.request.call_args.args[2]
        self.assertEqual(body["mbid"], mbid)
        self.assertEqual(body["payload"], {"id": "known"})


if __name__ == "__main__":
    unittest.main()
