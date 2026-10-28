"""Prepare a small read-only projection probe; execute only an approved fixed plan.

The caller owns authenticated transport, disposable database creation and verified
cleanup. This module never reads credentials or targets a database itself.
"""
from __future__ import annotations

from contextlib import closing
import hashlib
import json
import math
import re
import sqlite3
from pathlib import Path

MAX_READS = 10_000
MAX_WRITES = 1_500
NAME = re.compile(r"^proj_[a-z0-9_]+$")


def _numeric_json(value):
    if isinstance(value, float):
        if not math.isfinite(value):
            raise ValueError("Non-finite numeric fact")
        return int(value) if value.is_integer() else value
    if isinstance(value, dict):
        return {key: _numeric_json(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_numeric_json(item) for item in value]
    return value


def digest(value):
    return hashlib.sha256(json.dumps(_numeric_json(value), ensure_ascii=False, separators=(",", ":"),
                                     sort_keys=True).encode("utf-8")).hexdigest()


def quote(name):
    if not NAME.fullmatch(name):
        raise ValueError("Projection name outside allowlist")
    return '"' + name + '"'


class Budget:
    def __init__(self, reads=MAX_READS, writes=MAX_WRITES):
        if not isinstance(reads, int) or not isinstance(writes, int) or not 0 <= reads <= MAX_READS or not 0 <= writes <= MAX_WRITES:
            raise ValueError("Invalid total budget")
        self.max_reads, self.max_writes = reads, writes
        self.reads = self.writes = 0

    def reserve(self, reads, writes):
        if any(type(v) is not int or v < 0 for v in (reads, writes)):
            raise ValueError("Invalid reservation")
        if self.reads + reads > self.max_reads or self.writes + writes > self.max_writes:
            raise ValueError("Next operation exceeds total budget")

    def observe(self, meta):
        if not isinstance(meta, dict):
            raise ValueError("Missing or invalid actual query meters")
        values = [meta.get("rows_read"), meta.get("rows_written")]
        if any(type(v) is not int or v < 0 for v in values):
            raise ValueError("Missing or invalid actual query meters")
        self.reads += values[0]
        self.writes += values[1]
        if self.reads > self.max_reads or self.writes > self.max_writes:
            raise ValueError("Observed total exceeds budget")


def _statement(case_id, sql, params, reads, writes, expected, columns=None):
    return {"id": case_id, "sql": sql, "params": params,
            "maxRowsRead": reads, "maxRowsWritten": writes,
            "columns": columns or [], "expectedRows": expected, "expectedDigest": digest(expected)}


def compose_connector_code(prefix, value, suffix):
    """Insert JSON as a literal, without String.replace dollar substitutions."""
    return prefix + json.dumps(value, ensure_ascii=False, allow_nan=False) + suffix


def prepare_probe(artifact_sqlite, manifest_json, root_limit=10):
    if type(root_limit) is not int or not 1 <= root_limit <= 10:
        raise ValueError("Root sample limit must be an integer from one to ten")
    manifest = json.loads(Path(manifest_json).read_text(encoding="utf-8"))
    if manifest.get("artifactVersion") != 1 or not isinstance(manifest.get("tables"), list):
        raise ValueError("Unsupported projection manifest")
    path = Path(artifact_sqlite).resolve()
    statements, samples, comics = [], {}, None
    with closing(sqlite3.connect(path.as_uri() + "?mode=ro", uri=True)) as db:
        db.execute("PRAGMA query_only=ON")
        for item in manifest["tables"]:
            name = item["name"]
            quoted = quote(name)
            schema = db.execute("SELECT sql FROM sqlite_master WHERE type='table' AND name=?", (name,)).fetchone()
            if not schema or not schema[0] or "VIRTUAL TABLE" in schema[0].upper():
                raise ValueError("Expected ordinary projection table")
            table_info = list(db.execute(f"PRAGMA table_info({quoted})"))
            columns = [row[1] for row in table_info]
            if len(columns) > 100:
                raise ValueError("D1 bound-parameter ceiling exceeded")
            primary_columns = [row[1] for row in sorted(table_info, key=lambda row: row[5]) if row[5]]
            order = ",".join('"' + column.replace('"', '""') + '"' for column in primary_columns) or "rowid"
            expected_columns = item["columns"]
            if columns != expected_columns:
                raise ValueError("Manifest column mismatch")
            indexes = list(db.execute("SELECT name,sql FROM sqlite_master WHERE type='index' AND tbl_name=? AND sql IS NOT NULL ORDER BY name", (name,)))
            for index_name, _ in indexes:
                quote(index_name)
            limit = 20 if any(word in name for word in ("member", "classification", "genre", "format", "tag", "edge")) else root_limit
            payload_columns = [c for c in columns if c == "data" or "payload" in c]
            if payload_columns:
                eligible = " AND ".join('length(CAST("' + c.replace('"', '""') + '" AS BLOB))<=4096' for c in payload_columns)
                rows = [list(row) for row in db.execute(f"SELECT * FROM {quoted} WHERE {eligible} ORDER BY {order} LIMIT 2")]
            else:
                rows = [list(row) for row in db.execute(f"SELECT * FROM {quoted} ORDER BY {order} LIMIT ?", (limit,))]
            safe_rows = []
            payload_rows = 0
            for row in rows:
                if any(isinstance(v, bytes) for v in row):
                    raise ValueError("Binary payload unsupported in query API probe")
                large = any(isinstance(v, str) and len(v.encode("utf-8")) > 4096 for v in row)
                if large:
                    continue
                payload = bool(payload_columns)
                if payload and payload_rows >= 2:
                    continue
                if payload:
                    payload_rows += 1
                    if "data" in columns and "sha256" in columns:
                        encoded = row[columns.index("data")].encode("utf-8")
                        if hashlib.sha256(encoded).hexdigest() != row[columns.index("sha256")]:
                            raise ValueError("Artifact payload digest mismatch")
                safe_rows.append(row)
            if name == "proj_comics_series":
                comics = (columns, safe_rows)
            if payload_columns and rows and not safe_rows:
                raise ValueError("Eligible artifact payload was not represented")
            samples[name] = {"rows": len(safe_rows), "indexCount": len(indexes)}
            statements.append(_statement(f"create:{name}", schema[0], [], 10, 5, []))
            for index_name, index_sql in indexes:
                statements.append(_statement(f"index:{index_name}", index_sql, [], 10, 5, []))
            for number, row in enumerate(safe_rows):
                sql = f"INSERT INTO {quoted} VALUES({','.join('?' for _ in row)})"
                statements.append(_statement(f"insert:{name}:{number}", sql, row, 40, 2 * (1 + len(indexes)), []))
            # Tiny fixture scan: compare every inserted scalar, preserving fixed order.
            sql = f"SELECT * FROM {quoted} ORDER BY {order}"
            statements.append(_statement(f"verify:{name}", sql, [], 4 * len(safe_rows) + 10, 0, safe_rows, columns))
        if not comics:
            raise ValueError("Comics roots required for actual external-content trigram qualification")
        columns, comic_rows = comics
        content = [(row[columns.index("gcd_id")], row[columns.index("title")]) for row in comic_rows]
        match_title = next((t[:24] for _, t in content if isinstance(t, str) and len(t) >= 3), None)
        if match_title is None:
            raise ValueError("Trigram probe requires a sampled comic title of at least three characters")
        fts_sql = "CREATE VIRTUAL TABLE proj_comics_search USING fts5(title,content='proj_comics_series',content_rowid='gcd_id',tokenize='trigram')"
        statements.append(_statement("fts:create", fts_sql, [], 400, 40, []))
        statements.append(_statement("fts:populate", "INSERT INTO proj_comics_search(rowid,title) SELECT gcd_id,title FROM proj_comics_series ORDER BY gcd_id", [], 400, 40 * len(content) + 40, []))
        expected, matches = _local_trigram(content, match_title)
        statements.append(_statement("fts:verify", "SELECT rowid,title FROM proj_comics_search ORDER BY rowid", [], 400, 0, expected, ["rowid", "title"]))
        statements.append(_statement("fts:match", "SELECT rowid,title FROM proj_comics_search WHERE proj_comics_search MATCH ? ORDER BY rowid", ['"' + match_title.replace('"', '""') + '"'], 400, 0, matches, ["rowid", "title"]))
    reservation = {"rowsRead": sum(s["maxRowsRead"] for s in statements),
                   "rowsWritten": sum(s["maxRowsWritten"] for s in statements)}
    Budget().reserve(reservation["rowsRead"], reservation["rowsWritten"])
    plan = {"artifactVersion": 1, "revision": manifest.get("revision"), "sources": manifest.get("sources"), "rootLimit": root_limit,
            "manifestDigest": digest(manifest), "samples": samples, "statements": statements,
            "reservation": reservation, "nonGoals": ["One- and two-character fallback is not FTS proof", "128 KiB payload paging is not covered by the <=4 KiB probe"], "cleanup": "Caller deletes only the verified owned disposable database ID; no SQL cleanup or retries."}
    plan["planDigest"] = digest(plan)
    return plan


def _local_trigram(content, match_title):
    with closing(sqlite3.connect(":memory:")) as db:
        db.execute("CREATE TABLE proj_comics_series(gcd_id INTEGER PRIMARY KEY,title TEXT)")
        db.executemany("INSERT INTO proj_comics_series VALUES(?,?)", content)
        db.execute("CREATE VIRTUAL TABLE proj_comics_search USING fts5(title,content='proj_comics_series',content_rowid='gcd_id',tokenize='trigram')")
        db.execute("INSERT INTO proj_comics_search(rowid,title) SELECT gcd_id,title FROM proj_comics_series ORDER BY gcd_id")
        rows = [list(row) for row in db.execute("SELECT rowid,title FROM proj_comics_search ORDER BY rowid")]
        matches = [list(row) for row in db.execute("SELECT rowid,title FROM proj_comics_search WHERE proj_comics_search MATCH ? ORDER BY rowid", ('"' + match_title.replace('"', '""') + '"',))]
        return rows, matches


def execute_plan(plan, executor, approved_digest, reads=MAX_READS, writes=MAX_WRITES):
    """executor(statement) -> {success, results:[scalar row arrays], meta}.

    No retry on errors. Failed/missing-meter calls retain their reservation as
    unknown liability. Actual meters are recorded even when parity fails.
    """
    unsigned = {k: v for k, v in plan.items() if k != "planDigest"}
    if digest(unsigned) != approved_digest or plan.get("planDigest") != approved_digest:
        raise ValueError("Unapproved or mutated plan")
    budget = Budget(reads, writes)
    budget.reserve(sum(s["maxRowsRead"] for s in plan["statements"]), sum(s["maxRowsWritten"] for s in plan["statements"]))
    receipt = {"planDigest": approved_digest, "operations": [], "ok": False,
               "transport": "Injected authenticated D1 query executor; no Worker/native qualification",
               "cleanupVerified": False}
    for statement in plan["statements"]:
        record = {"id": statement["id"], "reservedReads": statement["maxRowsRead"], "reservedWrites": statement["maxRowsWritten"]}
        receipt["operations"].append(record)
        try:
            budget.reserve(statement["maxRowsRead"], statement["maxRowsWritten"])
            result = executor(statement)
            if not isinstance(result, dict):
                raise ValueError("Provider query failed; metering may be unknown")
            meta = result.get("meta", {})
            record["meta"] = meta
            budget.observe(meta)
            if result.get("success") is not True:
                raise ValueError("Provider query failed")
            if meta["rows_read"] > statement["maxRowsRead"] or meta["rows_written"] > statement["maxRowsWritten"]:
                raise ValueError("Observed operation exceeds its reviewed reservation")
            actual = result.get("results")
            if not isinstance(actual, list):
                raise ValueError("Provider result rows missing")
            record["resultDigest"] = digest(actual)
            if record["resultDigest"] != statement["expectedDigest"]:
                raise ValueError("Exact ordered result digest mismatch")
            record["ok"] = True
        except Exception as error:
            # Do not serialize provider exception strings, SQL parameters or secrets.
            record["ok"] = False
            record["failureType"] = type(error).__name__
            receipt["stoppedAt"] = statement["id"]
            receipt["meteringUnknown"] = not isinstance(record.get("meta"), dict) or any(type(record["meta"].get(k)) is not int or record["meta"][k] < 0 for k in ("rows_read", "rows_written"))
            break
    else:
        receipt["ok"] = True
        receipt["meteringUnknown"] = False
    receipt["actualRowsRead"], receipt["actualRowsWritten"] = budget.reads, budget.writes
    return receipt
