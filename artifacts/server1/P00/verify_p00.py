"""Fail-closed evidence validator for the Server 1.0 P00 packet."""

from __future__ import annotations

import argparse
import hashlib
import re
from pathlib import Path, PurePosixPath


class ValidationError(ValueError):
    pass


_ROW = re.compile(r"^(?P<sha>[0-9a-fA-F]{64})  (?P<size>[0-9]+)  (?P<path>.+)$")
_VERSION = re.compile(
    r'name:\s*["\']stremio-server["\']\s*,\s*version:\s*["\'](?P<version>[^"\']+)',
    re.DOTALL,
)


def _safe_path(root: Path, relative: str) -> Path:
    pure = PurePosixPath(relative)
    if pure.is_absolute() or ".." in pure.parts:
        raise ValidationError(f"escaping manifest path: {relative}")
    candidate = (root / Path(*pure.parts)).resolve(strict=False)
    if not candidate.is_relative_to(root.resolve()):
        raise ValidationError(f"escaping manifest path: {relative}")
    return candidate


def _read_rows(manifest: Path):
    rows = []
    seen = set()
    for line_number, line in enumerate(manifest.read_text(encoding="utf-8").splitlines(), 1):
        match = _ROW.fullmatch(line)
        if not match:
            raise ValidationError(f"invalid manifest row at line {line_number}")
        relative = match.group("path")
        if relative in seen:
            raise ValidationError(f"duplicate manifest path: {relative}")
        seen.add(relative)
        rows.append((relative, int(match.group("size")), match.group("sha").lower()))
    return rows


def validate_manifest(root: Path, manifest: Path) -> dict:
    root = root.resolve()
    manifest = manifest.resolve()
    rows = _read_rows(manifest)
    listed = set()
    for relative, expected_size, expected_sha in rows:
        candidate = _safe_path(root, relative)
        listed.add(PurePosixPath(relative).as_posix())
        if not candidate.is_file():
            raise ValidationError(f"missing manifest payload: {relative}")
        data = candidate.read_bytes()
        actual_size = len(data)
        actual_sha = hashlib.sha256(data).hexdigest()
        if actual_size != expected_size or actual_sha != expected_sha:
            raise ValidationError(
                f"payload mismatch: {relative} size={actual_size}/{expected_size} sha={actual_sha}/{expected_sha}"
            )

    actual = {
        p.relative_to(root).as_posix()
        for p in root.rglob("*")
        if p.is_file() and p.resolve() != manifest
    }
    missing = sorted(listed - actual)
    extra = sorted(actual - listed)
    if missing:
        raise ValidationError(f"missing manifest payload: {missing[0]}")
    if extra:
        raise ValidationError(f"unmanifested payload path: {extra[0]}")
    return {"manifest": str(manifest), "rows": len(rows), "payload_files": len(actual)}


def validate_identity(
    oracle: Path,
    cdn_version: str,
    embedded_claim: str,
    expected_sha256: str,
    expected_bytes: int,
) -> dict:
    data = oracle.read_bytes()
    actual_sha256 = hashlib.sha256(data).hexdigest()
    if len(data) != expected_bytes or actual_sha256 != expected_sha256.lower():
        raise ValidationError(
            f"oracle identity mismatch: bytes={len(data)}/{expected_bytes} sha={actual_sha256}/{expected_sha256.lower()}"
        )
    if cdn_version != "v4.21.1":
        raise ValidationError(f"unexpected CDN version claim: {cdn_version}")
    match = _VERSION.search(data.decode("utf-8"))
    if not match:
        raise ValidationError("embedded stremio-server version was not found")
    embedded_version = match.group("version")
    if embedded_claim != embedded_version:
        raise ValidationError(
            f"embedded version mismatch: claim={embedded_claim} actual={embedded_version}"
        )
    return {
        "cdn_version": cdn_version,
        "embedded_version": embedded_version,
        "bytes": len(data),
        "sha256": actual_sha256,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, action="append", required=True)
    parser.add_argument("--oracle", type=Path, required=True)
    parser.add_argument("--cdn-version", required=True)
    parser.add_argument("--embedded-version", required=True)
    parser.add_argument("--oracle-sha256", required=True)
    parser.add_argument("--oracle-bytes", type=int, required=True)
    args = parser.parse_args()
    try:
        identity = validate_identity(
            args.oracle,
            args.cdn_version,
            args.embedded_version,
            args.oracle_sha256,
            args.oracle_bytes,
        )
        manifests = [validate_manifest(path.parent, path) for path in args.manifest]
    except ValidationError as error:
        print(f"REJECTED: {error}")
        return 1
    print(f"ACCEPTED: identity={identity} manifests={manifests}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
