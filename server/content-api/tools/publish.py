"""Bounded, resumable HTTP publishing. Never writes the source SQLite files."""
import argparse
from contextlib import contextmanager, closing
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import sqlite3
import sys
import time
import urllib.error
import urllib.request
import uuid

ROOT = Path(__file__).resolve().parents[1]
MAX_BODY = 256 * 1024


class _NoAdminRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, destination):
        # The admin bearer is scoped to the configured endpoint. Even a redirect
        # on that endpoint is a failed operation, never permission to forward it.
        return None


ADMIN_OPENER = urllib.request.build_opener(_NoAdminRedirect())


class PublishError(Exception):
    pass


class DailyBudget(PublishError):
    pass


def encode(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode("utf-8")


def atomic_save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    with temporary.open("wb") as stream:
        stream.write(encode(value))
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


@contextmanager
def publisher_lock(state_dir):
    state_dir.mkdir(parents=True, exist_ok=True)
    path = state_dir / "publisher.lock"
    try:
        descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError as exc:
        raise PublishError("Publisher lock exists; verify no publisher is running before removing it") from exc
    try:
        os.write(descriptor, str(os.getpid()).encode())
        os.close(descriptor)
        yield
    finally:
        path.unlink()


class Client:
    def __init__(self, config):
        self.base = config["baseUrl"].rstrip("/")
        self.token = config["adminToken"]
        if not self.base.startswith("https://") and not self.base.startswith(("http://localhost:", "http://127.0.0.1:")):
            raise PublishError("baseUrl must use HTTPS (or local development HTTP)")

    def request(self, method, path, value=None):
        data = None if value is None else encode(value)
        if data is not None and len(data) > MAX_BODY:
            raise PublishError("Request exceeds 256 KiB")
        request = urllib.request.Request(self.base + path, data=data, method=method,
                                        headers={"Authorization": "Bearer " + self.token, "Content-Type": "application/json",
                                                 "User-Agent": "ColosseumContentPublisher/1.0 (https://getcolosseum.world)"})
        try:
            with ADMIN_OPENER.open(request, timeout=60) as response:
                result = json.loads(response.read(MAX_BODY + 1))
        except urllib.error.HTTPError as exc:
            try:
                result = json.loads(exc.read(MAX_BODY + 1))
            except (ValueError, UnicodeError):
                raise PublishError(f"HTTP {exc.code}: malformed response") from exc
            if exc.code == 429 and isinstance(result, dict) and result.get("error") == "IMPORT_DAILY_BUDGET":
                raise DailyBudget("Server daily import budget reached") from exc
            raise PublishError(f"HTTP {exc.code}: {result.get('error', 'request rejected') if isinstance(result, dict) else 'invalid response'}") from exc
        except (OSError, ValueError, UnicodeError) as exc:
            raise PublishError("Network or malformed JSON response; pending chunk retained") from exc
        if not isinstance(result, dict):
            raise PublishError("Expected JSON object response")
        if method == "POST" and result.get("ok") is not True:
            raise PublishError("Server did not acknowledge request")
        return result


def budget_used(status):
    budget = status.get("budget")
    if not isinstance(budget, list):
        raise PublishError("Missing server budget status")
    today = datetime.now(timezone.utc).date().isoformat()
    total = 0
    for entry in budget:
        if not isinstance(entry, dict):
            raise PublishError("Malformed budget entry")
        if entry.get("day", entry.get("date", today)) != today:
            continue
        amount = entry.get("reserved_writes")
        if not isinstance(amount, int) or amount < 0:
            raise PublishError("Unrecognized server budget value")
        total += amount
    return total


def identifier(name):
    if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name):
        raise PublishError("Unsafe manifest identifier")
    return '"' + name + '"'


def make_chunk(db, table, spec, cursor):
    columns = ",".join(identifier(c) for c in spec["columns"])
    rows = db.execute(f'SELECT rowid,{columns} FROM {identifier(spec["sourceTable"])} WHERE rowid > ? ORDER BY rowid LIMIT 32', (cursor,)).fetchall()
    if not rows:
        return None
    while rows:
        last = rows[-1][0]
        chunk_id = hashlib.sha256(f'{table}:{spec["sourceHash"]}:{cursor}:{last}'.encode()).hexdigest()
        body = {"table": table, "sourceHash": spec["sourceHash"], "chunkId": chunk_id, "rows": [list(r[1:]) for r in rows]}
        try:
            size = len(encode(body))
        except (TypeError, ValueError) as exc:
            raise PublishError("Source row cannot be represented losslessly in JSON") from exc
        if size <= MAX_BODY:
            return {"body": body, "lastRowid": last, "rowCount": len(rows), "cost": len(rows) * spec["writeWeight"] + 10}
        rows.pop()
    raise PublishError("A single source row exceeds request size limit")


def publish(source_dir, schema, manifest, client, progress_path, tables=None, max_writes=45000, max_chunks=None):
    status = client.request("GET", "/admin/status")
    used = budget_used(status)
    remote = {r["name"]: r for r in status.get("tables", [])}
    progress = json.loads(progress_path.read_text("utf-8")) if progress_path.exists() else {"tables": {}}
    selected = list(schema) if tables is None else tables
    if len(set(selected)) != len(selected) or any(t not in schema for t in selected):
        raise PublishError("Unknown or duplicate table selection")
    verified = set()
    chunks = 0
    for table in selected:
        spec = schema[table]
        dataset = spec["dataset"]
        source = source_dir / (dataset + "_catalog.db")
        if dataset not in verified:
            with source.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            if digest != manifest["sources"][dataset]["sha256"] or any(s["sourceHash"] != digest for s in schema.values() if s["dataset"] == dataset):
                raise PublishError(f"Source hash mismatch: {dataset}")
            verified.add(dataset)
        server_table = remote.get(table)
        if server_table is None or server_table.get("source_hash") != spec["sourceHash"] or server_table.get("expected_rows") != spec["rowCount"]:
            raise PublishError(f"Server manifest mismatch: {table}")
        state = progress["tables"].setdefault(table, {"sourceHash": spec["sourceHash"], "lastRowid": -9223372036854775808, "rows": 0})
        if state["sourceHash"] != spec["sourceHash"]:
            raise PublishError("Checkpoint source hash mismatch")
        if server_table.get("ready"):
            continue
        with closing(sqlite3.connect(source.resolve().as_uri() + "?mode=ro", uri=True)) as db:
            while True:
                if max_chunks is not None and chunks >= max_chunks:
                    return {"stopped": "chunk-limit", "chunks": chunks}
                pending = state.get("pending") or make_chunk(db, table, spec, state["lastRowid"])
                if pending is None:
                    if state["rows"] != spec["rowCount"]:
                        raise PublishError("Local imported count does not match manifest")
                    client.request("POST", "/admin/complete", {"table": table, "sourceHash": spec["sourceHash"]})
                    state["complete"] = True
                    atomic_save(progress_path, progress)
                    break
                if used + pending["cost"] > max_writes:
                    return {"stopped": "estimated-write-limit", "chunks": chunks, "estimatedWrites": used}
                state["pending"] = pending
                atomic_save(progress_path, progress)
                try:
                    response = client.request("POST", "/admin/import", pending["body"])
                except DailyBudget:
                    return {"stopped": "server-daily-budget", "chunks": chunks}
                if not isinstance(response.get("replayed"), bool):
                    raise PublishError("Import response missing replay acknowledgement")
                state["lastRowid"] = pending["lastRowid"]
                state["rows"] += pending["rowCount"]
                del state["pending"]
                atomic_save(progress_path, progress)
                if not response["replayed"]:
                    used += pending["cost"]
                chunks += 1
    return {"stopped": "complete", "chunks": chunks, "estimatedWrites": used}


def enrich(kind, mbid, client, state_dir):
    kinds = {"artist", "release", "release-group", "recording", "work", "label"}
    if kind not in kinds or str(uuid.UUID(mbid)) != mbid.lower():
        raise PublishError("Expected a MusicBrainz entity kind and UUID")
    spacing_path = state_dir / "musicbrainz.json"
    previous = json.loads(spacing_path.read_text("utf-8")) if spacing_path.exists() else {}
    time.sleep(max(0, previous.get("lastRequest", 0) + 1.1 - time.time()))
    atomic_save(spacing_path, {"lastRequest": time.time()})
    url = f"https://musicbrainz.org/ws/2/{kind}/{mbid}?fmt=json"
    request = urllib.request.Request(url, headers={"User-Agent": "ColosseumContentPublisher/1.0 (https://getcolosseum.world)", "Accept": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            payload = json.loads(response.read(MAX_BODY + 1))
    except (OSError, ValueError) as exc:
        raise PublishError("MusicBrainz request failed; no automatic retry") from exc
    return client.request("POST", "/admin/enrichment", {"kind": kind, "mbid": mbid.lower(), "fetchedAt": datetime.now(timezone.utc).isoformat(), "sourceUrl": url, "payload": payload})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=ROOT / ".publisher/config.json")
    parser.add_argument("--state-dir", type=Path, default=ROOT / ".publisher")
    commands = parser.add_subparsers(dest="command", required=True)
    upload = commands.add_parser("publish")
    upload.add_argument("--source-dir", type=Path, required=True)
    upload.add_argument("--tables", help="Comma-separated target tables, in priority order")
    upload.add_argument("--max-estimated-writes", type=int, default=45000)
    upload.add_argument("--max-chunks", type=int)
    enrichment = commands.add_parser("enrich")
    enrichment.add_argument("--kind", required=True)
    enrichment.add_argument("--mbid", required=True)
    args = parser.parse_args()
    try:
        with publisher_lock(args.state_dir):
            client = Client(json.loads(args.config.read_text("utf-8")))
            if args.command == "publish":
                if args.max_estimated_writes < 0 or args.max_estimated_writes > 60000 or (args.max_chunks is not None and args.max_chunks < 1):
                    raise PublishError("Invalid bounded publishing limits")
                result = publish(args.source_dir, json.loads((ROOT / "src/source-schema.json").read_text("utf-8")), json.loads((ROOT / "source-manifest.json").read_text("utf-8")), client, args.state_dir / "progress.json", args.tables.split(",") if args.tables else None, args.max_estimated_writes, args.max_chunks)
            else:
                result = enrich(args.kind, args.mbid, client, args.state_dir)
            print(json.dumps(result))
        return 0
    except (PublishError, OSError, ValueError, KeyError, sqlite3.Error) as exc:
        print(f"Publisher stopped: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
