#!/usr/bin/env python3
"""Verify a downloaded Colosseum update manifest, signature, and asset digests."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
import shutil
from pathlib import Path
from typing import Any

REPO = "kingoftheseas56/Colosseum"
VERSION_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")


def fail(message: str) -> None:
    raise ValueError(message)


def openssl_command() -> str:
    candidate = shutil.which("openssl") or __import__("os").environ.get("COLOSSEUM_OPENSSL")
    if candidate:
        return candidate
    windows_git = Path("C:/Program Files/Git/usr/bin/openssl.exe")
    if windows_git.is_file():
        return str(windows_git)
    fail("openssl executable not found")


def digest(path: Path) -> tuple[int, str]:
    h = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            size += len(block)
            h.update(block)
    return size, h.hexdigest()


def version(value: Any, field: str) -> str:
    if not isinstance(value, str) or not VERSION_RE.fullmatch(value):
        fail(f"invalid {field}")
    return value


def verify_signature(manifest: Path, signature: Path, public_der: Path) -> None:
    if len(signature.read_bytes()) != 64:
        fail("signature must be raw 64-byte Ed25519")
    with tempfile.TemporaryDirectory(prefix="colosseum-update-pub-") as temp:
        public_pem = Path(temp) / "public.pem"
        subprocess.run(
            [openssl_command(), "pkey", "-pubin", "-inform", "DER", "-in", str(public_der),
             "-out", str(public_pem)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run(
            [openssl_command(), "pkeyutl", "-verify", "-rawin", "-pubin",
             "-inkey", str(public_pem), "-in", str(manifest), "-sigfile", str(signature)],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )


def verify(manifest_path: Path, signature_path: Path, installer_path: Path,
           public_der: Path, artwork_dir: Path | None = None) -> dict[str, Any]:
    raw = manifest_path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        fail("manifest must be UTF-8 without BOM")
    data = json.loads(raw.decode("utf-8"))
    if data.get("schemaVersion") != 1:
        fail("unsupported schema")
    release_version = version(data.get("version"), "version")
    if data.get("tag") != f"v{release_version}":
        fail("tag/version mismatch")
    expected_notes = f"https://github.com/{REPO}/releases/tag/v{release_version}"
    if data.get("notesUrl") != expected_notes:
        fail("wrong notes URL")
    minimum = version(data.get("minimumUpdaterVersion"), "minimumUpdaterVersion")
    del minimum
    installer = data.get("installer")
    if not isinstance(installer, dict) or installer.get("asset") != installer_path.name:
        fail("installer asset mismatch")
    size, sha = digest(installer_path)
    if installer.get("size") != size or installer.get("sha256", "").lower() != sha:
        fail("installer size/digest mismatch")
    artwork = data.get("artwork", [])
    names: set[str] = set()
    for item in artwork:
        name = item.get("asset") if isinstance(item, dict) else None
        if not isinstance(name, str) or not name or "/" in name or "\\" in name or name in names:
            fail("unsafe or duplicate artwork asset")
        names.add(name)
        if artwork_dir is not None:
            path = artwork_dir / name
            if not path.is_file():
                fail(f"missing artwork: {name}")
            _, actual = digest(path)
            if item.get("sha256", "").lower() != actual:
                fail(f"artwork digest mismatch: {name}")
    for highlight in data.get("highlights", []):
        if any(ref not in names for ref in highlight.get("artworkAssets", [])):
            fail("highlight references missing artwork")
    verify_signature(manifest_path, signature_path, public_der)
    return {"version": release_version, "installerBytes": size, "installerSha256": sha}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--signature", required=True)
    parser.add_argument("--installer", required=True)
    parser.add_argument("--public-key-der", required=True)
    parser.add_argument("--artwork-dir")
    args = parser.parse_args(argv)
    try:
        result = verify(Path(args.manifest), Path(args.signature), Path(args.installer),
                        Path(args.public_key_der), Path(args.artwork_dir) if args.artwork_dir else None)
        print("UPDATE_RELEASE_OK")
        for key, value in result.items():
            print(f"{key.upper()}={value}")
        return 0
    except (OSError, ValueError, json.JSONDecodeError, subprocess.CalledProcessError) as exc:
        print(f"verify_update_release: {exc}", file=__import__("sys").stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
