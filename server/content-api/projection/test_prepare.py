"""Compact projection invariants, using small frozen source databases."""
import hashlib
from contextlib import closing
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest
from unittest.mock import patch

from prepare import build, pack_pages

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = json.loads((ROOT / "src/source-schema.json").read_text("utf-8"))


def make_sources(root):
    integer = {"mal_id", "gcd_id", "series_id", "post_id", "id", "claim_index",
               "gcd_issue_id", "gcd_series_id", "canonical_issue_id", "source_id",
               "curated_edition_id", "available", "official_source_count", "community_source_count",
               "source_count", "canonical_issue_count", "covered_issue_count", "parser_version",
               "total", "volume_count", "downloads", "fan_made", "year", "year_ended",
               "issue_count", "episodes", "votes", "runtimeMin", "isAnime", "explicit",
               "scored_by", "members", "rank", "favorites", "pages", "volumes", "chapters"}
    for dataset in ("imdb", "mal", "comics", "tankoban"):
        with closing(sqlite3.connect(root / (dataset + "_catalog.db"))) as db:
            for name, spec in SCHEMA.items():
                if spec["dataset"] != dataset:
                    continue
                columns = ",".join('"' + c + '" ' + ("INTEGER" if c in integer else "TEXT")
                                   for c in spec["columns"])
                db.execute(f'CREATE TABLE "{spec["sourceTable"]}" ({columns})')

    def insert(dataset, table, values):
        with closing(sqlite3.connect(root / (dataset + "_catalog.db"))) as db:
            columns = [r[1] for r in db.execute('PRAGMA table_info("' + table + '")')]
            row = {c: None for c in columns}
            row.update(values)
            db.execute('INSERT INTO "' + table + '" VALUES (' + ','.join('?' for _ in columns) + ')',
                       [row[c] for c in columns])
            db.commit()

    insert("imdb", "title", {"tt": "tt0000001", "title": "Écho", "norm_title": "echo",
                             "genres": '["Drama"]', "votes": 20, "year": 2020})
    insert("imdb", "genre", {"tt": "tt0000001", "genre": "Drama"})
    for medium in ("anime", "manga"):
        insert("mal", medium, {"mal_id": 13, "title": "Same numeric ID", "norm_title": "same",
                               "norm_title_english": "same", "explicit": 0} if medium == "manga"
               else {"mal_id": 13, "title": "Anime", "norm_title": "anime"})
        insert("mal", "classification", {"medium": medium, "axis": "genre", "value": "Drama", "mal_id": 13})
        insert("mal", "tag", {"medium": medium, "tag": "Drama", "mal_id": 13})
        insert("mal", "classification_count", {"medium": medium, "axis": "genre", "value": "Drama", "total": 1})
        insert("mal", "tag_count", {"medium": medium, "tag": "Drama", "total": 1})
    insert("comics", "series", {"gcd_id": 7, "title": "A 100% Hero", "year": 2001,
                                "publisher": "P", "synopsis": "Keep the full synopsis"})
    insert("comics", "series_stats", {"series_id": 7, "downloads": 3, "kinds": '["issue"]', "latest_post": "2026-01-01"})
    for post in (100, 101, 102):
        insert("comics", "download", {"post_id": post, "series_id": 7, "title": "Post",
                                       "date": "2026-01-01", "link": "https://example.test/p"})
        insert("comics", "download_mirror", {"post_id": post, "host": "H", "label": "mirror",
                                              "url": "https://example.test/" + "é" * 50})
    insert("comics", "curated_series", {"locg_id": "L1", "title": "Curated", "rank": None, "year": 2000})
    insert("comics", "curated_series_coverage", {"locg_id": "L1", "gcd_series_id": 7,
                                                 "coverage_ratio": 0.5, "official_source_count": 1,
                                                 "availability_state": "partial"})
    insert("comics", "curated_genre", {"locg_id": "L1", "genre": "Drama"})
    insert("comics", "curated_edition", {"id": 2, "locg_id": "L1", "format": " Trade ",
                                         "published": "1999–2024", "available": 1, "getcomics_post": "https://example.test/p"})
    insert("comics", "curated_source", {"id": 3, "curated_edition_id": 2, "source_kind": "official"})
    insert("comics", "curated_source_claim", {"source_id": 3, "claim_index": 0, "raw_fragment": "#1–2"})
    insert("comics", "canonical_issue", {"gcd_issue_id": 4, "gcd_series_id": 7, "number": "1A"})
    insert("comics", "curated_source_issue", {"source_id": 3, "canonical_issue_id": 4, "mapping_evidence": "exact"})
    insert("imdb", "meta", {"key": "snapshot", "value": "fixture"})
    return insert


def manifest_for(root):
    return {"sources": {d: {"sha256": hashlib.sha256((root / (d + "_catalog.db")).read_bytes()).hexdigest(),
                             "bytes": (root / (d + "_catalog.db")).stat().st_size}
                         for d in ("imdb", "mal", "comics", "tankoban")}}


class ProjectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.source = self.root / "sources"
        self.source.mkdir()
        self.insert = make_sources(self.source)

    def tearDown(self):
        self.temp.cleanup()

    def run_build(self, output="output", **kwargs):
        return build(self.source, self.root / output, source_manifest=manifest_for(self.source), **kwargs)

    def test_lossless_roots_memberships_payloads_and_source_hashes(self):
        before = manifest_for(self.source)
        report = self.run_build()
        self.assertEqual(before, manifest_for(self.source))
        with closing(sqlite3.connect(self.root / "output/projection.sqlite")) as db:
            db.row_factory = sqlite3.Row
            row = dict(db.execute("SELECT * FROM proj_comics_series").fetchone())
            self.assertEqual(row["downloads"], 3)
            self.assertEqual(row["synopsis"], "Keep the full synopsis")
            self.assertEqual(db.execute("SELECT count(*) FROM proj_mal_classification").fetchone()[0], 2)
            self.assertEqual(db.execute("SELECT count(*) FROM proj_mal_anime").fetchone()[0], 1)
            self.assertEqual(db.execute("SELECT count(*) FROM proj_mal_manga").fetchone()[0], 1)
            curated = dict(db.execute("SELECT * FROM proj_comics_curated").fetchone())
            self.assertIsNone(curated["rank"])
            self.assertEqual((curated["ed_count"], curated["avail_count"], curated["max_year"]), (1, 1, 2024))
            pages = [json.loads(r[0]) for r in db.execute("SELECT data FROM proj_payload WHERE family='comics.downloads' ORDER BY page")]
            downloads = [row for page in pages for row in page["records"]]
            self.assertEqual([r["download"]["post_id"] for r in downloads], [102, 101, 100])
            self.assertEqual(sum(len(r["mirrors"]) for r in downloads), 3)
            families = {r[0] for r in db.execute("SELECT DISTINCT family FROM proj_payload")}
            self.assertTrue({"comics.downloads", "comics.curated", "comics.issues", "mal.facets"} <= families)
            self.assertEqual(db.execute("PRAGMA integrity_check").fetchone()[0], "ok")
        self.assertEqual(report["sources"], before["sources"])
        self.assertEqual(report["hostedQualified"], False)

    def test_page_limit_includes_utf8_envelope_and_continuation(self):
        records = [{"id": n, "text": "é" * 100} for n in range(6)]
        pages = pack_pages("r", "family", "gcd:1", records, 700)
        self.assertGreater(len(pages), 1)
        self.assertEqual([r for p in pages for r in json.loads(p)["records"]], records)
        for i, data in enumerate(pages):
            self.assertLessEqual(len(data.encode("utf-8")), 700)
            page = json.loads(data)
            self.assertEqual(page["pageCount"], len(pages))
            self.assertEqual(page["nextPage"], i + 1 if i + 1 < len(pages) else None)
        with self.assertRaisesRegex(ValueError, "record.*page"):
            pack_pages("r", "family", "id", [{"text": "x" * 1000}], 700)

    def test_revision_and_outputs_are_deterministic(self):
        first = self.run_build("one")
        second = self.run_build("two")
        self.assertEqual(first, second)
        self.assertEqual((self.root / "one/projection.sqlite").read_bytes(), (self.root / "two/projection.sqlite").read_bytes())

    def test_different_or_duplicate_tag_membership_is_not_silently_dropped(self):
        self.insert("mal", "tag", {"medium": "manga", "tag": "Drama", "mal_id": 13})
        with self.assertRaisesRegex(ValueError, "membership"):
            self.run_build()
        self.assertFalse((self.root / "output/projection.sqlite").exists())

    def test_source_mismatch_and_existing_output_fail_before_replacement(self):
        wrong = manifest_for(self.source)
        wrong["sources"]["imdb"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "snapshot"):
            build(self.source, self.root / "bad", source_manifest=wrong)
        self.run_build()
        with self.assertRaisesRegex(ValueError, "exists"):
            self.run_build()

    def test_unresolved_canonical_parent_is_preserved_and_reported(self):
        self.insert("comics", "canonical_issue", {"gcd_issue_id": 5, "gcd_series_id": 999, "number": "2"})
        report = self.run_build()
        self.assertEqual(report["sourceRelationships"]["canonicalIssuesWithoutSearchRoot"], 1)
        with closing(sqlite3.connect(self.root / "output/projection.sqlite")) as db:
            payload = json.loads(db.execute("SELECT data FROM proj_payload WHERE family='comics.issues' AND entity_id='gcd:999'").fetchone()[0])
        self.assertEqual(payload["records"][0]["gcd_issue_id"], 5)

    def test_verifier_rejects_extra_records_in_an_originally_empty_family(self):
        from verify import verify
        with closing(sqlite3.connect(self.source / "comics_catalog.db")) as db:
            db.execute("DELETE FROM curated_source_claim")
            db.commit()
        self.run_build()
        artifact = self.root / "output/projection.sqlite"
        with closing(sqlite3.connect(artifact)) as db:
            row = db.execute("SELECT family,entity_id,page,data FROM proj_payload WHERE family='comics.curated'").fetchone()
            value = json.loads(row[3])
            phantom = {c: None for c in SCHEMA["comics__curated_source_claim"]["columns"]}
            phantom.update(source_id=3, claim_index=1)
            value["records"].append({"table": "curated_source_claim", "row": phantom})
            raw = json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            db.execute("UPDATE proj_payload SET data=?,sha256=?,bytes=? WHERE family=? AND entity_id=? AND page=?",
                       (raw.decode("utf-8"), hashlib.sha256(raw).hexdigest(), len(raw), *row[:3]))
            db.commit()
        report_path = self.root / "output/manifest.json"
        report = json.loads(report_path.read_text("utf-8"))
        report["sqlite"]["sha256"] = hashlib.sha256(artifact.read_bytes()).hexdigest()
        report_path.write_text(json.dumps(report), "utf-8")
        # The query oracle requires production snapshot hashes; this tiny unit
        # fixture exercises independent complete-content verification instead.
        with patch("verify.build_contracts", return_value={"cases": [], "remainingGaps": []}):
            with self.assertRaisesRegex(ValueError, "content or multiplicity"):
                verify(self.source, self.root / "output")

    def test_verifier_rejects_lossless_pages_assigned_to_another_entity(self):
        from verify import verify
        self.run_build()
        artifact = self.root / "output/projection.sqlite"
        with closing(sqlite3.connect(artifact)) as db:
            for family, entity, page, raw in db.execute(
                    "SELECT family,entity_id,page,data FROM proj_payload WHERE family='comics.downloads'").fetchall():
                value = json.loads(raw)
                value["entityId"] = "gcd:999"
                encoded = json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
                db.execute("UPDATE proj_payload SET entity_id=?,data=?,sha256=?,bytes=? WHERE family=? AND entity_id=? AND page=?",
                           ("gcd:999", encoded.decode("utf-8"), hashlib.sha256(encoded).hexdigest(), len(encoded), family, entity, page))
            db.commit()
        report_path = self.root / "output/manifest.json"
        report = json.loads(report_path.read_text("utf-8"))
        report["sqlite"]["sha256"] = hashlib.sha256(artifact.read_bytes()).hexdigest()
        report_path.write_text(json.dumps(report), "utf-8")
        with patch("verify.build_contracts", return_value={"cases": [], "remainingGaps": []}):
            with self.assertRaisesRegex(ValueError, "ownership"):
                verify(self.source, self.root / "output")


if __name__ == "__main__":
    unittest.main()
