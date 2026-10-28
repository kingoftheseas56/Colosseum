"""Verify full-population content independently of the projection builder."""
import argparse
from collections import defaultdict
from contextlib import ExitStack, closing
import hashlib
import json
from pathlib import Path
import sqlite3

from contracts import build_contracts, rows_digest
from prepare import SOURCE_SCHEMA, file_hash, quote, validate_sources


class Multiset:
    """Count + two SHA256 accumulators retain duplicate multiplicity.

    This content diagnostic does not replace immutable artifact/source hashes.
    It avoids retaining hundreds of thousands of mirror rows in memory.
    """
    def __init__(self):
        self.count = self.total = self.xor = 0

    def add(self, row):
        raw = json.dumps(row, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")
        digest = int.from_bytes(hashlib.sha256(raw).digest(), "big")
        self.count += 1
        self.total = (self.total + digest) % (1 << 256)
        self.xor ^= digest

    def value(self):
        return [self.count, f"{self.total:064x}", f"{self.xor:064x}"]


def verify(source_dir, artifact_dir):
    source_dir, artifact_dir = Path(source_dir).resolve(), Path(artifact_dir).resolve()
    manifest = json.loads((artifact_dir / "manifest.json").read_text("utf-8"))
    path = artifact_dir / "projection.sqlite"
    if file_hash(path) != manifest["sqlite"]["sha256"]:
        raise ValueError("Artifact checksum mismatch")
    validate_sources(source_dir, manifest)
    expected, actual = defaultdict(Multiset), defaultdict(Multiset)
    with ExitStack() as stack:
        sources = {}
        for dataset in manifest["sources"]:
            db = stack.enter_context(closing(sqlite3.connect((source_dir / (dataset + "_catalog.db")).as_uri() + "?mode=ro", uri=True)))
            db.row_factory = sqlite3.Row
            db.execute("PRAGMA query_only=ON")
            sources[dataset] = db
        projected = stack.enter_context(closing(sqlite3.connect(path.as_uri() + "?mode=ro", uri=True)))
        projected.row_factory = sqlite3.Row
        projected.execute("PRAGMA query_only=ON")
        for key, spec in SOURCE_SCHEMA.items():
            if spec["dataset"] == "tankoban":
                continue  # Existing hosted lane/checkpoint deliberately unchanged.
            expected[key]  # Empty original families must reject invented records.
            for row in sources[spec["dataset"]].execute("SELECT * FROM " + quote(spec["sourceTable"])):
                expected[key].add({c: row[c] for c in spec["columns"]})
        root_mapping = {"imdb__title": "proj_imdb_title", "imdb__genre": "proj_imdb_genre",
                        "mal__anime": "proj_mal_anime", "mal__manga": "proj_mal_manga",
                        "mal__classification": "proj_mal_classification", "comics__series": "proj_comics_series",
                        "comics__curated_series": "proj_comics_curated", "comics__curated_genre": "proj_comics_curated_genre"}
        for key, table in root_mapping.items():
            columns = SOURCE_SCHEMA[key]["columns"]
            for row in projected.execute("SELECT " + ','.join(quote(c) for c in columns) + " FROM " + quote(table)):
                actual[key].add(dict(row))
        stats_ids = {r[0] for r in sources["comics"].execute("SELECT series_id FROM series_stats")}
        coverage_ids = {r[0] for r in sources["comics"].execute("SELECT locg_id FROM curated_series_coverage")}
        for row in projected.execute("SELECT gcd_id,downloads,kinds,latest_post FROM proj_comics_series"):
            if row["gcd_id"] in stats_ids:
                actual["comics__series_stats"].add({"series_id": row["gcd_id"], "downloads": row["downloads"],
                                                    "kinds": row["kinds"], "latest_post": row["latest_post"]})
        coverage_columns = SOURCE_SCHEMA["comics__curated_series_coverage"]["columns"]
        for row in projected.execute("SELECT " + ','.join(quote(c) for c in coverage_columns) + " FROM proj_comics_curated"):
            if row["locg_id"] in coverage_ids:
                actual["comics__curated_series_coverage"].add(dict(row))
        for row in projected.execute("SELECT medium,value,mal_id FROM proj_mal_classification"):
            actual["mal__tag"].add({"medium": row["medium"], "tag": row["value"], "mal_id": row["mal_id"]})
        for row in projected.execute("SELECT dataset,key,value FROM proj_source_meta"):
            actual[row["dataset"] + "__meta"].add({"key": row["key"], "value": row["value"]})
        edition_owners = dict(sources["comics"].execute("SELECT id,locg_id FROM curated_edition"))
        origin_owners = {row[0]: edition_owners.get(row[1]) for row in sources["comics"].execute(
            "SELECT id,curated_edition_id FROM curated_source")}
        page_groups = defaultdict(list)
        for row in projected.execute("SELECT * FROM proj_payload ORDER BY family,entity_id,page"):
            raw = row["data"].encode("utf-8")
            data = json.loads(row["data"])
            if len(raw) != row["bytes"] or len(raw) > manifest["payload"]["capBytes"] or hashlib.sha256(raw).hexdigest() != row["sha256"]:
                raise ValueError("Payload checksum/byte limit mismatch")
            if data.get("apiVersion") != 1 or (data["revision"], data["family"], data["entityId"], data["page"]) != (manifest["revision"], row["family"], row["entity_id"], row["page"]):
                raise ValueError("Payload envelope mismatch")
            page_groups[(row["family"], row["entity_id"])].append((data["page"], data["pageCount"], data["nextPage"]))
            for record in data["records"]:
                if row["family"] == "comics.downloads":
                    if row["entity_id"] != "gcd:" + str(record["download"]["series_id"]):
                        raise ValueError("Download page ownership mismatch")
                    actual["comics__download"].add(record["download"])
                    for mirror in record["mirrors"]:
                        if mirror["post_id"] != record["download"]["post_id"]:
                            raise ValueError("Nested mirror ownership mismatch")
                        actual["comics__download_mirror"].add(mirror)
                elif row["family"] == "comics.issues":
                    if row["entity_id"] != "gcd:" + str(record["gcd_series_id"]):
                        raise ValueError("Issue page ownership mismatch")
                    actual["comics__canonical_issue"].add(record)
                elif row["family"] == "comics.curated":
                    key = "comics__" + record["table"]
                    detail = record["row"]
                    if record["table"] == "curated_edition":
                        owner = detail["locg_id"]
                    elif record["table"] == "curated_source":
                        owner = edition_owners.get(detail["curated_edition_id"])
                    elif record["table"] in ("curated_source_claim", "curated_source_issue"):
                        owner = origin_owners.get(detail["source_id"])
                    else:
                        raise ValueError("Unknown payload record family")
                    if owner is None or row["entity_id"] != "locg:" + str(owner):
                        raise ValueError("Curated page ownership mismatch")
                    actual[key].add(detail)
                elif row["family"] == "mal.facets":
                    if row["entity_id"] != record["medium"]:
                        raise ValueError("MAL facet page ownership mismatch")
                    actual["mal__classification_count"].add(record)
                    actual["mal__tag_count"].add({"medium": record["medium"], "tag": record["value"], "total": record["total"]})
                else:
                    raise ValueError("Unknown payload family")
        for pages in page_groups.values():
            n = len(pages)
            if pages != [(i, n, i + 1 if i + 1 < n else None) for i in range(n)]:
                raise ValueError("Payload pagination discontinuity")
        for key in expected:
            if expected[key].value() != actual[key].value():
                raise ValueError("Source content or multiplicity mismatch: " + key)
        integrity = projected.execute("PRAGMA integrity_check").fetchone()[0]
        if integrity != "ok":
            raise ValueError("Artifact integrity failed")
        # Fixture manifests differ from frozen production hashes. Query contracts
        # are exercised only against the manifest-qualified original population.
        contracts = build_contracts(source_dir)
        results = []
        for case in contracts["cases"]:
            rows = [list(r) for r in projected.execute(case["projectionSql"], case["args"])]
            if rows_digest(rows) != case["expectedDigest"]:
                raise ValueError("Projection query contract mismatch: " + case["id"])
            results.append({"id": case["id"], "rows": len(rows), "digest": case["expectedDigest"]})
    validate_sources(source_dir, manifest)
    return {"ok": True, "revision": manifest["revision"], "sourceHashesUnchanged": True,
            "sourceRecordsVerified": sum(v.count for v in expected.values()),
            "content": {k: v.value() for k, v in expected.items()}, "contracts": results,
            "remainingGaps": contracts["remainingGaps"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--artifact-dir", type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.source_dir, args.artifact_dir)
    (args.artifact_dir / "verification.json").write_text(json.dumps(result, ensure_ascii=False, separators=(",", ":")), "utf-8")
    print(json.dumps({"ok": result["ok"], "sourceRecordsVerified": result["sourceRecordsVerified"],
                      "contracts": len(result["contracts"]), "revision": result["revision"]}))


if __name__ == "__main__":
    main()
