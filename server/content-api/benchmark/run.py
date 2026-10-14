"""Bounded benchmark runner. Remote writes require a disposable target and --execute.

SQL-over-HTTP timings are diagnostic, not native page-load or Worker-binding timings.
Secrets are read from named environment variables and never included in reports.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sqlite3
import statistics
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import Request, build_opener, HTTPRedirectHandler

from prepare import canonical_rows, compact, digest, quote


class Failure(Exception):
    """Messages must contain codes only: upstream responses can expose credentials."""


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def write_report(path, report):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    temp.replace(path)


def timestamp():
    return datetime.now(timezone.utc).isoformat()


def secret(config, optional=False):
    name = config.get("tokenEnv")
    value = os.environ.get(name, "") if isinstance(name, str) else ""
    if not value and not optional:
        raise Failure("TOKEN_ENV_MISSING")
    return value


def checked_url(url, local=False):
    parts = urlsplit(url)
    allowed_local = local and parts.scheme == "http" and parts.hostname in ("127.0.0.1", "localhost", "::1")
    if (parts.scheme != "https" and not allowed_local) or not parts.hostname or parts.username or parts.password or parts.fragment:
        raise Failure("HTTPS_URL_REQUIRED")
    return url


class Transport:
    def __init__(self, limit=500, max_bytes=8 * 1024 * 1024, timeout=20, total_bytes=64 * 1024 * 1024):
        self.limit, self.max_bytes, self.timeout = limit, max_bytes, timeout
        self.total_bytes, self.received_bytes = total_bytes, 0
        self.calls = 0
        self.opener = build_opener(NoRedirect())

    def request(self, url, token="", payload=None, headers=None):
        if self.calls >= self.limit:
            raise Failure("REQUEST_BUDGET_EXHAUSTED")
        if self.received_bytes >= self.total_bytes:
            raise Failure("TRANSFER_BUDGET_EXHAUSTED")
        self.calls += 1  # Failed and uncertain requests consume the local allowance too.
        request_headers = {"User-Agent": "ColosseumContentBenchmark/1.0 (https://getcolosseum.world)", "Accept-Encoding": "identity"}
        request_headers.update(headers or {})
        if token:
            request_headers["Authorization"] = "Bearer " + token
        data = compact(payload).encode("utf-8") if payload is not None else None
        if data is not None:
            request_headers["Content-Type"] = "application/json"
        started = time.perf_counter()
        try:
            try:
                response = self.opener.open(Request(url, data=data, headers=request_headers), timeout=self.timeout)
            except HTTPError as error:
                response = error
            with response:
                body = response.read(min(self.max_bytes, self.total_bytes - self.received_bytes) + 1)
                self.received_bytes += len(body)
                if self.received_bytes > self.total_bytes:
                    raise Failure("TRANSFER_BUDGET_EXHAUSTED")
                if len(body) > self.max_bytes:
                    raise Failure("RESPONSE_BYTE_LIMIT")
                result = {"status": response.code, "body": body, "elapsedMs": (time.perf_counter()-started)*1000,
                          "responseBytes": len(body), "headers": {k.lower(): v for k, v in response.headers.items()}}
                return result
        except Failure:
            raise
        except (OSError, URLError, TimeoutError):
            raise Failure("NETWORK_FAILURE_NO_AUTOMATIC_RETRY") from None


def typed_arg(value):
    if value is None:
        return {"type": "null"}
    if isinstance(value, bool):
        return {"type": "integer", "value": str(int(value))}
    if isinstance(value, int):
        return {"type": "integer", "value": str(value)}
    if isinstance(value, float) and math.isfinite(value):
        return {"type": "float", "value": value}
    if isinstance(value, str):
        return {"type": "text", "value": value}
    raise Failure("UNSUPPORTED_SQL_VALUE")


def decode_value(value):
    kind = value.get("type")
    if kind == "null":
        return None
    if kind == "integer":
        return int(value["value"])
    if kind == "float":
        number = float(value["value"])
        if not math.isfinite(number):
            raise Failure("NONFINITE_PROVIDER_VALUE")
        return number
    if kind == "text":
        return value["value"]
    # Fixtures do not contain BLOBs; silently coercing them would invalidate parity.
    raise Failure("UNSUPPORTED_PROVIDER_VALUE")


class Database:
    def __init__(self, config, writing=False, transport=None):
        self.config, self.kind = config, config["backend"]
        self.transport = transport or Transport()
        self.connection = None
        self.token = ""
        if self.kind == "sqlite":
            path = Path(config["databasePath"]).resolve()
            if writing:
                path.parent.mkdir(parents=True, exist_ok=True)
                self.connection = sqlite3.connect(path, isolation_level=None)
            else:
                self.connection = sqlite3.connect(path.as_uri() + "?mode=ro", uri=True, isolation_level=None)
        elif self.kind == "d1":
            if not re.fullmatch(r"[a-f0-9]{32}", config.get("accountId", "")) or not re.fullmatch(r"[a-f0-9-]{36}", config.get("databaseId", "")):
                raise Failure("D1_TARGET_INVALID")
            self.url = f'https://api.cloudflare.com/client/v4/accounts/{config["accountId"]}/d1/database/{config["databaseId"]}/query'
            self.token = secret(config)
        elif self.kind == "turso":
            url = config["url"].replace("libsql://", "https://", 1)
            self.url = checked_url(url.rstrip("/") + "/v2/pipeline")
            self.token = secret(config)
        else:
            raise Failure("UNKNOWN_BACKEND")

    def close(self):
        if self.connection:
            self.connection.close()

    def execute(self, sql, args=None, columns=None):
        args = args or []
        if self.kind == "sqlite":
            started = time.perf_counter()
            cursor = self.connection.execute(sql, args)
            names = [col[0] for col in cursor.description] if cursor.description else []
            rows = canonical_rows(cursor.fetchall()) if names else []
            return {"rows": rows, "columns": names, "meta": {}, "elapsedMs": (time.perf_counter()-started)*1000, "responseBytes": None}
        if self.kind == "d1":
            payload = {"sql": sql, "params": args}
        else:
            payload = {"requests": [{"type": "execute", "stmt": {"sql": sql, "args": [typed_arg(v) for v in args]}}, {"type": "close"}]}
        response = self.transport.request(self.url, self.token, payload)
        if response["status"] != 200:
            raise Failure(f'PROVIDER_HTTP_{response["status"]}')
        try:
            result = json.loads(response["body"])
            if self.kind == "d1":
                if not result.get("success") or not result.get("result") or not result["result"][0].get("success"):
                    raise Failure("D1_QUERY_REJECTED")
                item = result["result"][0]
                names = columns or (list(item["results"][0]) if item["results"] else [])
                rows = [[row[name] for name in names] for row in item["results"]]
                meta = item.get("meta", {})
            else:
                item = result["results"][0]
                if item["type"] != "ok" or item["response"]["type"] != "execute":
                    raise Failure("TURSO_QUERY_REJECTED")
                item = item["response"]["result"]
                names = [col["name"] for col in item["cols"]]
                rows = [[decode_value(v) for v in row] for row in item["rows"]]
                meta = {name: item[name] for name in ("rows_read", "rows_written", "affected_row_count") if name in item}
            safe_meta = {name: meta[name] for name in ("rows_read", "rows_written", "duration", "size_after", "served_by_region", "served_by_colo", "served_by_primary") if name in meta}
            return {"rows": canonical_rows(rows), "columns": names, "meta": safe_meta, "elapsedMs": response["elapsedMs"], "responseBytes": response["responseBytes"]}
        except (KeyError, IndexError, AttributeError, TypeError, ValueError):
            raise Failure("MALFORMED_PROVIDER_RESPONSE") from None


def validate_seed(seed):
    content = {"schema": seed["schemaStatements"], "tables": [{"name": t["name"], "columns": t["columns"], "rows": t["rows"]} for t in seed["tables"]]}
    if seed.get("version") != 1 or hashlib.sha256(compact(content).encode("utf-8")).hexdigest() != seed.get("seedContentDigest"):
        raise Failure("SEED_HASH_MISMATCH")
    if not 0 < seed["estimatedWrites"] < 10000:
        raise Failure("SEED_WRITE_BUDGET_INVALID")
    for table in seed["tables"]:
        if not re.fullmatch(r"b_[a-z0-9_]+", table["name"]):
            raise Failure("BENCHMARK_TABLE_PREFIX_REQUIRED")
    for query in seed["queries"]:
        if digest(query["expectedRows"]) != query["expectedDigest"]:
            raise Failure("EXPECTED_RESULT_HASH_MISMATCH")


def summary(values):
    if not values:
        return None
    ordered = sorted(values)
    return {"count": len(values), "p50Ms": statistics.median(values), "p95Ms": ordered[math.ceil(.95*len(ordered))-1], "minMs": ordered[0], "maxMs": ordered[-1]}


def meter_total(records, field):
    values = [r.get("meta", {}).get(field) for r in records]
    return sum(values) if values and all(isinstance(v, (int, float)) for v in values) else None


def observed_total(records, field):
    # A lower bound can stop a run even when other requests omitted their meters.
    return sum(r.get("meta", {}).get(field, 0) for r in records if isinstance(r.get("meta", {}).get(field), (int, float)))


def import_seed(db, seed, config, execute=False, max_writes=9000):
    records = []
    started = time.perf_counter()
    try:
        return _import_seed(db, seed, config, execute, max_writes, records)
    except (Failure, sqlite3.Error) as error:
        failure = error if isinstance(error, Failure) else Failure("LOCAL_SQL_ERROR_PARTIAL_IMPORT")
        failure.report = {"kind": "import", "backend": db.kind, "complete": False, "error": str(failure),
                          "seedDigest": seed.get("seedContentDigest"), "seedRows": seed.get("seedRows"),
                          "completedStatements": len(records), "elapsedMs": (time.perf_counter()-started)*1000,
                          "reportedRowsWritten": meter_total(records, "rows_written"),
                          "observedRowsWrittenLowerBound": observed_total(records, "rows_written"),
                          "reportedRowsRead": meter_total(records, "rows_read"),
                          "providerRequestCount": db.transport.calls, "billingReconciled": False,
                          "timestampUtc": timestamp(), "recovery": "Inspect target and usage; no automatic replay or reset."}
        raise failure from None


def _import_seed(db, seed, config, execute, max_writes, records):
    validate_seed(seed)
    if not execute:
        raise Failure("IMPORT_REQUIRES_EXECUTE_FLAG")
    if db.kind != "sqlite" and (config.get("disposableDatabase") is not True or config.get("freePlanReviewed") is not True or not config.get("usageReviewUtc")):
        raise Failure("REMOTE_DISPOSABLE_FREE_PLAN_REVIEW_REQUIRED")
    if seed["estimatedWrites"] + 100 > max_writes or max_writes > 10000:
        raise Failure("IMPORT_BUDGET_EXCEEDED")
    started = time.perf_counter()
    inventory = db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT GLOB 'sqlite_*' AND name NOT GLOB '_cf_*'", columns=["name"])
    records.append(inventory)
    if inventory["rows"]:
        raise Failure("TARGET_NOT_EMPTY_NO_OVERWRITE")
    # Failed imports remain visible/partial; never reset an existing target or replay blindly.
    records.append(db.execute("CREATE TABLE b_benchmark_manifest (digest TEXT PRIMARY KEY, state TEXT NOT NULL)"))
    records.append(db.execute("INSERT INTO b_benchmark_manifest VALUES (?, 'loading')", [seed["seedContentDigest"]]))
    for statement in seed["schemaStatements"]:
        records.append(db.execute(statement))
    for table in seed["tables"]:
        width = len(table["columns"])
        batch_size = min(32, 100 // width)
        if batch_size < 1:
            raise Failure("TOO_MANY_COLUMNS")
        for start in range(0, len(table["rows"]), batch_size):
            rows = table["rows"][start:start+batch_size]
            marks = "(" + ",".join("?" for _ in range(width)) + ")"
            sql = "INSERT INTO " + quote(table["name"]) + " (" + ",".join(quote(c) for c in table["columns"]) + ") VALUES " + ",".join(marks for _ in rows)
            records.append(db.execute(sql, [v for row in rows for v in row]))
            actual = observed_total(records, "rows_written")
            if actual > max_writes:
                raise Failure("OBSERVED_WRITE_BUDGET_EXCEEDED_PARTIAL_IMPORT")
    for table in seed["tables"]:
        count = db.execute("SELECT COUNT(*) AS n FROM " + quote(table["name"]), columns=["n"])
        records.append(count)
        if count["rows"] != [[len(table["rows"])]]:
            raise Failure("IMPORT_ROW_COUNT_MISMATCH")
    records.append(db.execute("UPDATE b_benchmark_manifest SET state='ready' WHERE digest=?", [seed["seedContentDigest"]]))
    return {"kind": "import", "backend": db.kind, "seedDigest": seed["seedContentDigest"], "seedRows": seed["seedRows"],
            "elapsedMs": (time.perf_counter()-started)*1000, "estimatedWrites": seed["estimatedWrites"], "reportedRowsWritten": meter_total(records, "rows_written"),
            "reportedRowsRead": meter_total(records, "rows_read"), "providerRequestCount": db.transport.calls,
            "billingReconciled": False, "complete": True}


def run_sql(db, seed, repetitions=5, max_reads=200000):
    validate_seed(seed)
    if not 1 <= repetitions <= 30:
        raise Failure("REPETITIONS_OUT_OF_RANGE")
    manifest = db.execute("SELECT digest,state FROM b_benchmark_manifest", columns=["digest", "state"])
    if manifest["rows"] != [[seed["seedContentDigest"], "ready"]]:
        raise Failure("TARGET_SEED_NOT_READY")
    results, all_records = [], [manifest]
    for query in seed["queries"]:
        samples, first, parity, error = [], None, True, None
        try:
            for index in range(repetitions + 1):
                record = db.execute(query["sql"], query["args"], query["expectedColumns"])
                all_records.append(record)
                if observed_total(all_records, "rows_read") > max_reads:
                    raise Failure("OBSERVED_READ_BUDGET_EXCEEDED")
                if record["columns"] != query["expectedColumns"] or digest(record["rows"]) != query["expectedDigest"]:
                    parity, error = False, "RESULT_PARITY_FAILED"
                    break
                measured = {key: value for key, value in record.items() if key not in ("rows", "columns")}
                if index == 0:
                    first = measured
                else:
                    samples.append(measured)
        except Failure as failure:
            parity, error = False, str(failure)
        results.append({"id": query["id"], "parity": parity, "error": error, "firstObserved": first,
                        "repeatSummary": summary([s["elapsedMs"] for s in samples]), "samples": samples})
        if error:
            break
    return {"kind": "sql", "timestampUtc": timestamp(), "backend": db.kind, "seedDigest": seed["seedContentDigest"],
            "queryDigest": digest([[q["id"], q["sql"], compact(q["args"]), q["expectedDigest"]] for q in seed["queries"]]),
            "path": "local-sqlite" if db.kind == "sqlite" else "direct-provider-sql-http",
            "population": "bounded-sample", "repetitions": repetitions, "coldCacheProven": False, "workerCpuMs": None,
            "reportedRowsRead": meter_total(all_records, "rows_read"), "providerRequestCount": db.transport.calls,
            "allParityPassed": len(results) == len(seed["queries"]) and all(r["parity"] for r in results), "results": results,
            "qualification": "diagnostic-only; neither full-scale nor installed-application evidence"}


def run_http(config, repetitions=3):
    """Explicit route mappings only. Measures bytes/HTTP, never Qt decode or first paint."""
    if not 1 <= repetitions <= 30:
        raise Failure("REPETITIONS_OUT_OF_RANGE")
    transport = Transport(limit=200, max_bytes=10*1024*1024)
    routes = config.get("routes", [])
    if not routes or len(routes) > 20:
        raise Failure("EXPLICIT_BOUNDED_HTTP_ROUTES_REQUIRED")
    token = secret(config, optional=not any(r.get("authenticated") for r in routes))
    results = []
    for route in routes:
        url = checked_url(route["url"], config.get("allowLocalhost") is True)
        records, etag, body_hash, failure, conditional = [], None, None, None, None
        try:
            for index in range(repetitions+1):
                response = transport.request(url, token if route.get("authenticated") else "")
                if response["status"] != 200:
                    raise Failure(f'HTTP_STATUS_{response["status"]}')
                if response["headers"].get("content-encoding", "identity") not in ("identity", ""):
                    raise Failure("UNEXPECTED_ENCODING_IDENTITY_REQUESTED")
                current_hash = hashlib.sha256(response["body"]).hexdigest()
                if (route.get("sha256") and route["sha256"] != current_hash) or (body_hash and body_hash != current_hash):
                    raise Failure("HTTP_BODY_HASH_MISMATCH")
                body_hash = current_hash
                if route.get("kind") == "json":
                    json.loads(response["body"])
                if index == 0:
                    etag = response["headers"].get("etag")
                records.append({"elapsedMs": response["elapsedMs"], "responseBytes": response["responseBytes"],
                                "cacheStatus": response["headers"].get("cf-cache-status")})
            conditional = None
            if etag:
                response = transport.request(url, token if route.get("authenticated") else "", headers={"If-None-Match": etag})
                conditional = {"status": response["status"], "responseBytes": response["responseBytes"], "elapsedMs": response["elapsedMs"]}
                if response["status"] != 304 or response["body"]:
                    raise Failure("ETAG_REVALIDATION_FAILED")
            elif route.get("requireEtag", True):
                raise Failure("ETAG_MISSING")
        except (Failure, ValueError) as error:
            failure = str(error) if isinstance(error, Failure) else "INVALID_JSON_RESPONSE"
        results.append({"id": route["id"], "passed": failure is None, "error": failure, "firstObserved": records[0] if records else None,
                        "repeatSummary": summary([r["elapsedMs"] for r in records[1:]]), "bodySha256": body_hash,
                        "conditional": conditional})
        if failure:
            break
    return {"kind": "http", "timestampUtc": timestamp(), "label": config.get("label", "unlabelled"), "results": results,
            "allPassed": len(results) == len(routes) and all(r["passed"] for r in results), "requestCount": transport.calls,
            "receivedBodyBytes": transport.received_bytes, "contentEncoding": "identity",
            "coldCacheProven": False, "nativeFirstPaintMs": None, "imageDecodeMs": None,
            "qualification": "transport-only; no installed-app, production-compression or geographic proof"}


def compare(left, right):
    reasons = []
    for label, report in (("LEFT", left), ("RIGHT", right)):
        valid = (report.get("kind") == "sql" and report.get("path") == "direct-provider-sql-http"
                 and report.get("population") == "bounded-sample"
                 and isinstance(report.get("repetitions"), int) and 1 <= report["repetitions"] <= 30
                 and all(isinstance(report.get(key), str) and re.fullmatch(r"[a-f0-9]{64}", report[key]) for key in ("seedDigest", "queryDigest")))
        observations = report.get("results")
        valid = valid and isinstance(observations, list) and bool(observations)
        if valid:
            valid = all(isinstance(r, dict) and r.get("id") and r.get("parity") is True
                        and isinstance(r.get("firstObserved"), dict)
                        and isinstance(r.get("samples"), list) and len(r["samples"]) == report["repetitions"]
                        for r in observations)
        if not valid:
            reasons.append("INVALID_REPORT_" + label)
    for key in ("seedDigest", "queryDigest", "repetitions", "population", "path"):
        if left.get(key) != right.get(key):
            reasons.append("MISMATCH_" + key)
    if {left.get("backend"), right.get("backend")} != {"d1", "turso"}:
        reasons.append("BOTH_HOSTED_BACKENDS_REQUIRED")
    if not left.get("allParityPassed") or not right.get("allParityPassed"):
        reasons.append("PARITY_NOT_PASSED")
    # Comparable direct SQL timings still cannot decide all product loading behavior.
    reasons.extend(["WORKER_AND_INSTALLED_JOURNEYS_REQUIRED", "FULL_SCALE_IMPORT_AND_FREE_BUDGET_REQUIRED", "ASSET_UNIVERSE_FAILURE_AND_OFFLINE_GATES_REQUIRED"])
    return {"decision": "not-qualified", "winner": None, "reasons": reasons,
            "directSqlComparable": not any(r.startswith(("MISMATCH_", "INVALID_REPORT_")) or r in ("BOTH_HOSTED_BACKENDS_REQUIRED", "PARITY_NOT_PASSED") for r in reasons),
            "note": "Human receiving review must combine matched hosted diagnostics with the acceptance contract; no fastest-sample auto-selection."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("import", "sql", "http", "compare"))
    parser.add_argument("--config", type=Path)
    parser.add_argument("--seed", type=Path, default=Path(__file__).resolve().parents[1]/".publisher/benchmark/seed.json")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--left", type=Path)
    parser.add_argument("--right", type=Path)
    args = parser.parse_args()
    db = None
    try:
        if args.command == "compare":
            report = compare(read_json(args.left), read_json(args.right))
        elif args.command == "http":
            report = run_http(read_json(args.config), args.repetitions)
        else:
            config, seed = read_json(args.config), read_json(args.seed)
            validate_seed(seed)
            if args.command == "import" and not args.execute:
                raise Failure("IMPORT_REQUIRES_EXECUTE_FLAG")
            db = Database(config, writing=args.command == "import")
            report = import_seed(db, seed, config, args.execute) if args.command == "import" else run_sql(db, seed, args.repetitions)
        write_report(args.output, report)
        print(compact({key: report[key] for key in ("kind", "backend", "complete", "allParityPassed", "allPassed", "decision") if key in report}))
        return 2 if report.get("allParityPassed") is False or report.get("allPassed") is False else 0
    except (Failure, OSError, ValueError, KeyError, TypeError, sqlite3.Error) as error:
        code = str(error) if isinstance(error, Failure) else "LOCAL_CONFIGURATION_OR_SQL_ERROR"
        write_report(args.output, getattr(error, "report", {"complete": False, "error": code, "timestampUtc": timestamp(), "providerRequestCount": db.transport.calls if db else 0}))
        print(compact({"complete": False, "error": code}))
        return 2
    finally:
        if db:
            db.close()


if __name__ == "__main__":
    raise SystemExit(main())
