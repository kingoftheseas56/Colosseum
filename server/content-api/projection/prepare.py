"""Build a frozen compact catalogue artifact; never writes publisher inputs.

This artifact is preparation, not a deployed API or a qualified full import.
Query rows preserve source fields; detail children are grouped into JSON pages.
"""
import argparse
from collections import Counter
from contextlib import ExitStack, closing
import hashlib
from itertools import groupby
import json
from pathlib import Path
import re
import sqlite3

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SCHEMA = json.loads((ROOT / "src/source-schema.json").read_text("utf-8"))
VERSION = "compact-v1"


def encode(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False)


def file_hash(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def quote(name):
    if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name):
        raise ValueError("Unsafe schema identifier")
    return '"' + name + '"'


def pack_pages(revision, family, entity_id, records, page_cap=131072):
    """The cap includes the complete UTF-8 envelope, including continuation."""
    if not 512 <= page_cap <= 131072:
        raise ValueError("Page cap must be between 512 and 131072 bytes")

    def envelope(rows, page, count, next_page):
        return {"apiVersion": 1, "revision": revision, "family": family,
                "entityId": str(entity_id), "page": page, "pageCount": count,
                "nextPage": next_page, "records": rows}

    groups, pending = [], []
    for record in records:
        trial = pending + [record]
        # Reserve worst-case 32-bit pagination fields; final envelope is smaller.
        if len(encode(envelope(trial, 2147483647, 2147483647, 2147483647)).encode("utf-8")) > page_cap:
            if not pending:
                raise ValueError(f"One {family} record exceeds the page cap")
            groups.append(pending)
            pending = [record]
            if len(encode(envelope(pending, 2147483647, 2147483647, 2147483647)).encode("utf-8")) > page_cap:
                raise ValueError(f"One {family} record exceeds the page cap")
        else:
            pending = trial
    if pending or not groups:
        groups.append(pending)
    return [encode(envelope(rows, i, len(groups), i + 1 if i + 1 < len(groups) else None))
            for i, rows in enumerate(groups)]


def validate_sources(source_dir, manifest):
    for dataset, spec in manifest["sources"].items():
        path = source_dir / (dataset + "_catalog.db")
        if any(Path(str(path) + suffix).exists() for suffix in ("-wal", "-journal")):
            raise ValueError(f"Source snapshot is not frozen: {dataset}")
        if path.stat().st_size != spec["bytes"] or file_hash(path) != spec["sha256"]:
            raise ValueError(f"Source snapshot mismatch: {dataset}")


def validate_relationships(db):
    pairs = [("download_mirror", "post_id", "download", "post_id"),
             ("download", "series_id", "series", "gcd_id"),
             ("series_stats", "series_id", "series", "gcd_id"),
             ("curated_edition", "locg_id", "curated_series", "locg_id"),
             ("curated_genre", "locg_id", "curated_series", "locg_id"),
             ("curated_series_coverage", "locg_id", "curated_series", "locg_id"),
             ("curated_source", "curated_edition_id", "curated_edition", "id"),
             ("curated_source_claim", "source_id", "curated_source", "id"),
             ("curated_source_issue", "source_id", "curated_source", "id"),
             ("curated_source_issue", "canonical_issue_id", "canonical_issue", "gcd_issue_id")]
    for child, fk, parent, pk in pairs:
        count = db.execute(f'SELECT count(*) FROM {quote(child)} c WHERE NOT EXISTS '
                           f'(SELECT 1 FROM {quote(parent)} p WHERE p.{quote(pk)}=c.{quote(fk)})').fetchone()[0]
        if count:
            raise ValueError(f"Unresolved source relationship: {child}.{fk} ({count})")
    # The frozen source contains canonical issues whose parents are absent from
    # the searchable series population. Preserve their identity-qualified pages.
    return {"canonicalIssuesWithoutSearchRoot": db.execute(
        "SELECT count(*) FROM canonical_issue i WHERE NOT EXISTS "
        "(SELECT 1 FROM series s WHERE s.gcd_id=i.gcd_series_id)").fetchone()[0]}


def validate_memberships(db):
    tags = Counter(tuple(r) for r in db.execute("SELECT medium,tag,mal_id FROM tag"))
    classifications = Counter(tuple(r) for r in db.execute("SELECT medium,value,mal_id FROM classification"))
    if tags != classifications:
        raise ValueError("MAL tag/classification membership multiplicity differs")
    counts = Counter(tuple(r) for r in db.execute("SELECT medium,tag,total FROM tag_count"))
    classified_counts = Counter(tuple(r) for r in db.execute("SELECT medium,value,total FROM classification_count"))
    if counts != classified_counts:
        raise ValueError("MAL baked membership counts differ")
    for medium in ("anime", "manga"):
        orphan = db.execute(f"SELECT count(*) FROM classification c WHERE medium=? AND NOT EXISTS "
                            f"(SELECT 1 FROM {medium} e WHERE e.mal_id=c.mal_id)", (medium,)).fetchone()[0]
        if orphan:
            raise ValueError(f"Unresolved {medium} classification membership")
    if db.execute("SELECT count(*) FROM classification WHERE medium NOT IN ('anime','manga') OR medium IS NULL").fetchone()[0]:
        raise ValueError("Unknown MAL membership medium")


def max_year(value):
    return max((int(y) for y in re.findall(r"\d{4}", value or "")
                if 1900 <= int(y) <= 2100), default=0)


def build(source_dir, output_dir, source_manifest=None, page_cap=131072):
    source_dir, output_dir = Path(source_dir).resolve(), Path(output_dir).resolve()
    manifest = source_manifest or json.loads((ROOT / "source-manifest.json").read_text("utf-8"))
    if set(manifest["sources"]) != {"imdb", "mal", "comics", "tankoban"}:
        raise ValueError("Expected the four frozen catalogue snapshots")
    validate_sources(source_dir, manifest)
    output_dir.mkdir(parents=True, exist_ok=True)
    artifact, report_path = output_dir / "projection.sqlite", output_dir / "manifest.json"
    pending = output_dir / "projection.pending.sqlite"
    pending_report = output_dir / "manifest.pending.json"
    if any(p.exists() for p in (artifact, report_path, pending, pending_report)):
        raise ValueError("Projection output exists; use a fresh directory")
    revision = hashlib.sha256(encode({"version": VERSION, "sources": manifest["sources"],
                                     "builder": file_hash(__file__), "pageCap": page_cap}).encode("utf-8")).hexdigest()
    schema_sql, table_keys, derived = [], {}, []
    try:
        with ExitStack() as stack:
            source = {}
            for dataset in manifest["sources"]:
                db = stack.enter_context(closing(sqlite3.connect(
                    (source_dir / (dataset + "_catalog.db")).as_uri() + "?mode=ro", uri=True)))
                db.row_factory = sqlite3.Row
                db.execute("PRAGMA query_only=ON")
                source[dataset] = db
            validate_memberships(source["mal"])
            relationships = validate_relationships(source["comics"])
            output = stack.enter_context(closing(sqlite3.connect(pending)))
            output.row_factory = sqlite3.Row

            def ddl(sql):
                output.execute(sql)
                schema_sql.append(sql)

            def copy(name, dataset, table, keys, indexes=(), select=None, extra=()):
                spec = SOURCE_SCHEMA[dataset + "__" + table]
                info = {r[1]: r[2] for r in source[dataset].execute(f'PRAGMA table_info({quote(table)})')}
                columns = spec["columns"]
                declarations = [quote(c) + " " + (info[c] or "TEXT") for c in columns]
                declarations += [quote(c) + " " + kind for c, kind in extra]
                without = " WITHOUT ROWID" if len(keys) > 1 else ""
                ddl(f'CREATE TABLE {quote(name)} (' + ','.join(declarations)
                    + ',PRIMARY KEY(' + ','.join(quote(k) for k in keys) + '))' + without)
                rows = source[dataset].execute(select or
                    f'SELECT ' + ','.join(quote(c) for c in columns) + f' FROM {quote(table)} ORDER BY rowid')
                count = len(columns) + len(extra)
                output.executemany(f'INSERT INTO {quote(name)} VALUES(' + ','.join('?' for _ in range(count)) + ')',
                                   (tuple(r) for r in rows))
                for i, expression in enumerate(indexes):
                    ddl(f'CREATE INDEX {quote(name + "_idx" + str(i))} ON {quote(name)}({expression})')
                table_keys[name] = keys

            copy("proj_imdb_title", "imdb", "title", ("tt",),
                 ("norm_title", "votes DESC", "rating DESC,votes DESC", "year DESC,votes DESC",
                  "episodes DESC,votes DESC", "type,origLang,votes DESC"))
            copy("proj_imdb_genre", "imdb", "genre", ("genre", "tt"))
            for medium in ("anime", "manga"):
                indexes = ["norm_title", "norm_title_english", "members DESC", "score DESC,scored_by DESC"]
                if medium == "manga":
                    indexes.append("explicit,start_date DESC")
                copy("proj_mal_" + medium, "mal", medium, ("mal_id",), indexes)
            copy("proj_mal_classification", "mal", "classification", ("medium", "axis", "value", "mal_id"),
                 ("medium,mal_id,axis,value",))
            columns = SOURCE_SCHEMA["comics__series"]["columns"]
            series_select = "SELECT " + ','.join('s.' + quote(c) for c in columns) + \
                ",t.downloads,t.kinds,t.latest_post FROM series s LEFT JOIN series_stats t ON t.series_id=s.gcd_id ORDER BY s.gcd_id"
            copy("proj_comics_series", "comics", "series", ("gcd_id",),
                 ("title COLLATE NOCASE", "downloads DESC,year DESC"), series_select,
                 (("downloads", "INTEGER"), ("kinds", "TEXT"), ("latest_post", "TEXT")))

            comics = source["comics"]
            comics.create_function("projection_max_year", 1, max_year)
            columns = SOURCE_SCHEMA["comics__curated_series"]["columns"]
            coverage = [c for c in SOURCE_SCHEMA["comics__curated_series_coverage"]["columns"] if c != "locg_id"]
            coverage_types = {r[1]: r[2] for r in comics.execute('PRAGMA table_info(curated_series_coverage)')}
            curated_select = "SELECT " + ','.join('s.' + quote(c) for c in columns)
            curated_select += ',' + ','.join('c.' + quote(c) for c in coverage)
            curated_select += ",COALESCE((SELECT group_concat(g.genre) FROM curated_genre g WHERE g.locg_id=s.locg_id),'')"
            curated_select += ",(SELECT count(*) FROM curated_edition e WHERE e.locg_id=s.locg_id)"
            curated_select += ",(SELECT count(*) FROM curated_edition e WHERE e.locg_id=s.locg_id AND e.available=1 AND e.getcomics_post!='')"
            curated_select += ",COALESCE((SELECT max(projection_max_year(e.published)) FROM curated_edition e WHERE e.locg_id=s.locg_id),0)"
            curated_select += " FROM curated_series s LEFT JOIN curated_series_coverage c ON c.locg_id=s.locg_id ORDER BY s.locg_id"
            extra = [(c, coverage_types[c]) for c in coverage] + [("genres", "TEXT"), ("ed_count", "INTEGER"),
                    ("avail_count", "INTEGER"), ("max_year", "INTEGER")]
            copy("proj_comics_curated", "comics", "curated_series", ("locg_id",),
                 ("norm_title", "rank", "publisher", "max_year DESC"), curated_select, extra)
            # Derive only raw availability ratio, never request-scoped houseScore.
            ddl('ALTER TABLE proj_comics_curated ADD COLUMN avail_frac REAL')
            output.execute("UPDATE proj_comics_curated SET avail_frac=CASE WHEN ed_count>0 THEN 1.0*avail_count/ed_count ELSE 0 END")
            copy("proj_comics_curated_genre", "comics", "curated_genre", ("genre", "locg_id"))
            ddl('CREATE TABLE proj_comics_curated_format (locg_id TEXT,format TEXT,PRIMARY KEY(format,locg_id)) WITHOUT ROWID')
            output.executemany("INSERT INTO proj_comics_curated_format VALUES(?,?)", comics.execute(
                "SELECT DISTINCT locg_id,lower(trim(format)) FROM curated_edition WHERE format IS NOT NULL AND trim(format)!='' ORDER BY locg_id,lower(trim(format))"))
            table_keys["proj_comics_curated_format"] = ("format", "locg_id")

            ddl('CREATE TABLE proj_payload (family TEXT,entity_id TEXT,page INTEGER,data TEXT,sha256 TEXT,bytes INTEGER,PRIMARY KEY(family,entity_id,page))')
            table_keys["proj_payload"] = ("family", "entity_id", "page")

            def payload(family, entity_id, records):
                for page, data in enumerate(pack_pages(revision, family, entity_id, records, page_cap)):
                    raw = data.encode("utf-8")
                    output.execute("INSERT INTO proj_payload VALUES(?,?,?,?,?,?)",
                                   (family, str(entity_id), page, data, hashlib.sha256(raw).hexdigest(), len(raw)))

            # Indexed source joins are evaluated locally once, not as API reads.
            for series_id, rows in groupby(comics.execute("SELECT * FROM download ORDER BY series_id,date DESC,post_id DESC"),
                                          key=lambda r: r["series_id"]):
                records = []
                for row in rows:
                    mirrors = [dict(m) for m in comics.execute("SELECT * FROM download_mirror WHERE post_id=? ORDER BY rowid", (row["post_id"],))]
                    records.append({"download": dict(row), "mirrors": mirrors})
                payload("comics.downloads", "gcd:" + str(series_id), records)
            for series_id, rows in groupby(comics.execute("SELECT * FROM canonical_issue ORDER BY gcd_series_id,gcd_issue_id"),
                                          key=lambda r: r["gcd_series_id"]):
                payload("comics.issues", "gcd:" + str(series_id), [dict(r) for r in rows])
            for (locg_id,) in comics.execute("SELECT locg_id FROM curated_series ORDER BY locg_id"):
                records = []
                for row in comics.execute("SELECT * FROM curated_edition WHERE locg_id=? ORDER BY id", (locg_id,)):
                    records.append({"table": "curated_edition", "row": dict(row)})
                    for origin in comics.execute("SELECT * FROM curated_source WHERE curated_edition_id=? ORDER BY id", (row["id"],)):
                        records.append({"table": "curated_source", "row": dict(origin)})
                        for table, order in (("curated_source_claim", "claim_index"), ("curated_source_issue", "canonical_issue_id")):
                            for detail in comics.execute(f'SELECT * FROM {quote(table)} WHERE source_id=? ORDER BY {quote(order)}', (origin["id"],)):
                                records.append({"table": table, "row": dict(detail)})
                payload("comics.curated", "locg:" + str(locg_id), records)
            for medium in ("anime", "manga"):
                records = [dict(r) for r in source["mal"].execute(
                    "SELECT * FROM classification_count WHERE medium=? ORDER BY axis,value", (medium,))]
                payload("mal.facets", medium, records)
            ddl('CREATE TABLE proj_source_meta (dataset TEXT,key TEXT,value TEXT,PRIMARY KEY(dataset,key)) WITHOUT ROWID')
            for dataset in ("imdb", "mal"):
                output.executemany("INSERT INTO proj_source_meta VALUES(?,?,?)",
                                   ((dataset, r[0], r[1]) for r in source[dataset].execute("SELECT key,value FROM meta ORDER BY key")))
            table_keys["proj_source_meta"] = ("dataset", "key")
            ddl("CREATE VIRTUAL TABLE proj_comics_search USING fts5(title,content='proj_comics_series',content_rowid='gcd_id',tokenize='trigram')")
            before_fts = output.total_changes
            output.execute("INSERT INTO proj_comics_search(rowid,title) SELECT gcd_id,title FROM proj_comics_series ORDER BY gcd_id")
            derived.append({"name": "proj_comics_search", "rows": output.execute("SELECT count(*) FROM proj_comics_series").fetchone()[0],
                            "localSqliteChanges": output.total_changes - before_fts,
                            "statement": "INSERT INTO proj_comics_search(rowid,title) SELECT gcd_id,title FROM proj_comics_series ORDER BY gcd_id",
                            "hostedWriteCost": None})
            output.commit()
            integrity = output.execute("PRAGMA integrity_check").fetchone()[0]
            if integrity != "ok":
                raise ValueError("Projection integrity check failed")
            tables = []
            for name, keys in table_keys.items():
                info = output.execute(f'PRAGMA table_info({quote(name)})').fetchall()
                digest = hashlib.sha256()
                count = 0
                for row in output.execute(f'SELECT * FROM {quote(name)} ORDER BY ' + ','.join(quote(k) for k in keys)):
                    digest.update(encode(list(row)).encode("utf-8") + b"\n")
                    count += 1
                indexes = output.execute(f'PRAGMA index_list({quote(name)})').fetchall()
                declaration = output.execute("SELECT sql FROM sqlite_master WHERE name=?", (name,)).fetchone()[0]
                without_rowid = "WITHOUT ROWID" in declaration.upper()
                # A WITHOUT ROWID primary key is the base keyed structure, not
                # a second physical index. This is a model, not D1 metering.
                extra_indexes = sum(not (without_rowid and i[3] == "pk") for i in indexes)
                tables.append({"name": name, "columns": [r[1] for r in info], "primaryKey": list(keys),
                               "rows": count, "indexCount": len(indexes), "withoutRowid": without_rowid,
                               "modeledWriteWeight": 1 + extra_indexes,
                               "sha256": digest.hexdigest()})
            payload_sizes = output.execute("SELECT count(*),coalesce(sum(bytes),0),coalesce(max(bytes),0) FROM proj_payload").fetchone()
            if payload_sizes[2] > page_cap:
                raise ValueError("Encoded payload exceeded page cap")
            pages = output.execute("PRAGMA page_count").fetchone()[0]
            page_size = output.execute("PRAGMA page_size").fetchone()[0]
            output.execute("VACUUM")
            pages = output.execute("PRAGMA page_count").fetchone()[0]
        validate_sources(source_dir, manifest)
        report = {"artifactVersion": 1, "projectionVersion": VERSION, "revision": revision,
                  "sources": manifest["sources"], "tables": tables, "schemaSql": schema_sql, "derived": derived,
                  "sourceRelationships": relationships, "sqlite": {"bytes": pending.stat().st_size,
                  "pages": pages, "pageSize": page_size, "sha256": file_hash(pending), "integrityCheck": integrity},
                  "payload": {"pages": payload_sizes[0], "bytes": payload_sizes[1], "maxBytes": payload_sizes[2], "capBytes": page_cap},
                  "hostedQualified": False,
                  "writeModel": {"dataAndIndexesOnly": sum(t["rows"] * t["modeledWriteWeight"] for t in tables),
                                 "excludes": ["FTS maintenance", "receipts", "schema work", "verification", "cleanup"],
                                 "isProviderMeasurement": False}}
        pending_report.write_text(encode(report) + "\n", encoding="utf-8")
        pending.rename(artifact)
        pending_report.rename(report_path)
        return report
    except Exception:
        # These exact temporary files belong to this invocation; no directory cleanup.
        pending.unlink(missing_ok=True)
        pending_report.unlink(missing_ok=True)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    report = build(args.source_dir, args.output_dir)
    print(encode({"revision": report["revision"], "sqlite": report["sqlite"],
                  "payload": report["payload"], "writeModel": report["writeModel"],
                  "rows": sum(t["rows"] for t in report["tables"])}))


if __name__ == "__main__":
    main()
