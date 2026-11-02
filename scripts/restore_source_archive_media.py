#!/usr/bin/env python3
"""Restore optional media excluded from Colosseum's generated source ZIP/TAR.

Original blobs remain tracked in the GitHub repository at the immutable
sourceCommit in source_archive_media_manifest.json. Only manifest-listed
files can be restored. Every byte is verified before atomic installation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import tempfile
from urllib.parse import quote
from urllib.request import Request, urlopen

MANIFEST = Path(__file__).with_name("source_archive_media_manifest.json")
SCOPES = ("docs", "release", "capture", "lanista")


def _target(root, relative):
    if not isinstance(relative, str) or "\\" in relative:
        raise ValueError("Invalid archive media path")
    path = PurePosixPath(relative)
    if path.is_absolute() or any(part in ("", ".", "..") for part in relative.split("/")):
        raise ValueError("Unsafe archive media path: " + relative)
    absolute = root.joinpath(*path.parts)
    if not absolute.resolve().is_relative_to(root.resolve()):
        raise ValueError("Archive media path escapes repository: " + relative)
    return absolute


def _verify(path, entry):
    h = hashlib.sha256()
    length = 0
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            length += len(chunk)
            h.update(chunk)
    return length == entry["bytes"] and h.hexdigest() == entry["sha256"]


def restore(root, *, scope="all", source_dir=None, offline=False, verify_only=False, manifest=None):
    root = Path(root).resolve()
    source = Path(source_dir).resolve() if source_dir is not None else None
    manifest = manifest or json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("version") != 1 or not manifest.get("sourceCommit") or not manifest.get("repository"):
        raise ValueError("Unsupported archive media manifest")
    if scope != "all" and scope not in SCOPES:
        raise ValueError("Unknown media scope")
    summary = {"verified": 0, "restored": 0, "bytesRestored": 0}
    for entry in manifest["files"]:
        if scope != "all" and entry["scope"] != scope:
            continue
        relative = entry["path"]
        destination = _target(root, relative)
        if destination.exists():
            if not destination.is_file() or not _verify(destination, entry):
                raise ValueError("Existing archive media differs from pinned original: " + relative)
            summary["verified"] += 1
            continue
        if verify_only:
            raise FileNotFoundError("Required source archive media missing: " + relative)
        destination.parent.mkdir(parents=True, exist_ok=True)
        fd, temp_name = tempfile.mkstemp(prefix=".archive-media-", dir=destination.parent)
        try:
            with os.fdopen(fd, "wb") as sink:
                local = _target(source, relative) if source is not None else None
                if local is not None and local.is_file():
                    with local.open("rb") as stream:
                        shutil.copyfileobj(stream, sink, length=1024 * 1024)
                else:
                    if offline:
                        raise FileNotFoundError("Offline media unavailable: " + relative)
                    url = ("https://raw.githubusercontent.com/" + manifest["repository"] +
                           "/" + manifest["sourceCommit"] + "/" + quote(relative, safe="/"))
                    with urlopen(Request(url, headers={"User-Agent": "Colosseum-source-archive-media/1"}),
                                 timeout=35) as stream:
                        shutil.copyfileobj(stream, sink, length=1024 * 1024)
            staged = Path(temp_name)
            if not _verify(staged, entry):
                raise ValueError("Restored media checksum/length mismatch: " + relative)
            os.replace(temp_name, destination)
        finally:
            if os.path.exists(temp_name):
                os.unlink(temp_name)
        summary["restored"] += 1
        summary["bytesRestored"] += entry["bytes"]
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scope", choices=("all",) + SCOPES, default="all")
    parser.add_argument("--source-dir", type=Path, help="Full Git checkout or unpacked asset cache")
    parser.add_argument("--offline", action="store_true", help="Never access the network")
    parser.add_argument("--verify-only", action="store_true", help="Only check files already present")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1],
                        help=argparse.SUPPRESS)
    args = parser.parse_args()
    print(json.dumps(restore(args.root, scope=args.scope, source_dir=args.source_dir,
                             offline=args.offline, verify_only=args.verify_only)))


if __name__ == "__main__":
    main()
