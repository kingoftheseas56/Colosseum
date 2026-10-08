#!/usr/bin/env python3
"""Build a portable, content-addressed catalogue manifest for HTTPS object storage.

This command does not upload anything. Upload its four versioned objects first,
then publish the manifest last at the chosen /catalog-vault/manifest.json URL.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sqlite3
import tempfile
from urllib.parse import urlparse, quote

ASSET_NAMES = (
    "mal_catalog.db",
    "tankoban_catalog.db",
    "comics_catalog.db",
    "imdb_catalog.db",
)
SCHEMA = "colosseum.catalog-vault.v1"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

def verify_sqlite(path: Path) -> None:
    if not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f"missing/empty database: {path.name}")
    # Open read-only; never repair or mutate publisher source data.
    db = sqlite3.connect("file:" + path.as_posix() + "?mode=ro", uri=True)
    try:
        result = db.execute("PRAGMA quick_check").fetchone()
        if not result or result[0] != "ok":
            raise ValueError(f"SQLite integrity failed: {path.name}: {result}")
    finally:
        db.close()


def make_manifest(data_dir: Path, base_url: str, tag: str) -> dict:
    parsed = urlparse(base_url)
    if (parsed.scheme != "https" or not parsed.hostname or parsed.username
            or parsed.password or parsed.query or parsed.fragment):
        raise ValueError("public base URL must be HTTPS, without credentials/query/fragment")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,95}", tag):
        raise ValueError("tag must be a safe, non-empty immutable version")
    base = base_url.rstrip("/")
    assets = []
    for name in ASSET_NAMES:
        path = data_dir / name
        verify_sqlite(path)

        digest = sha256_file(path)
        object_key = f"{quote(tag)}/{digest}/{name}"
        assets.append({
            "name": name,
            "size": path.stat().st_size,
            "sha256": digest,
            "url": f"{base}/{object_key}",
        })
    return {"schema": SCHEMA, "tag": tag, "assets": assets}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", required=True, type=Path)
    parser.add_argument("--base-url", required=True,
                        help="HTTPS prefix ending in /catalog-vault")
    parser.add_argument("--tag", required=True, help="immutable snapshot version")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    manifest = make_manifest(args.data_dir, args.base_url, args.tag)
    args.output.parent.mkdir(parents=True, exist_ok=True)

    # The manifest becomes discoverable only after its complete atomic write.
    with tempfile.NamedTemporaryFile("w", encoding="utf-8",
            dir=args.output.parent, prefix=".catalog-vault-", suffix=".json",
            delete=False) as tmp:
        tmp_path = Path(tmp.name)
        json.dump(manifest, tmp, indent=2, ensure_ascii=True)
        tmp.write("\n")
    try:
        os.replace(tmp_path, args.output)
    finally:
        tmp_path.unlink(missing_ok=True)
    print(json.dumps({"schema": SCHEMA, "tag": args.tag,
        "files": len(manifest["assets"]),
        "bytes": sum(a["size"] for a in manifest["assets"]),
        "output": str(args.output)}))


if __name__ == "__main__":
    main()
