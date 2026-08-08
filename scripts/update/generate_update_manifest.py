#!/usr/bin/env python3
"""Generate and sign Colosseum's deterministic update manifest v1."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile
import shutil
from pathlib import Path
from typing import Any

REPO = "kingoftheseas56/Colosseum"
VERSION_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
TAG_RE = re.compile(r"^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
DER_PREFIX = bytes.fromhex("302a300506032b6570032100")
ROOT_KEYS = {"eyebrow", "title", "summary", "notes_url", "highlights", "artwork"}
HIGHLIGHT_KEYS = {
    "kind", "section", "title", "body", "value", "context",
    "before_caption", "after_caption", "artwork_assets",
}
ARTWORK_KEYS = {"asset", "path"}


def fail(message: str) -> None:
    raise ValueError(message)


def openssl_command() -> str:
    candidate = shutil.which("openssl") or os.environ.get("COLOSSEUM_OPENSSL")
    if candidate:
        return candidate
    windows_git = Path("C:/Program Files/Git/usr/bin/openssl.exe")
    if windows_git.is_file():
        return str(windows_git)
    fail("openssl executable not found")


def canonical_version(value: str) -> str:
    if not VERSION_RE.fullmatch(value):
        fail(f"non-canonical version: {value}")
    return value


def canonical_tag(value: str) -> str:
    if not TAG_RE.fullmatch(value):
        fail(f"non-canonical tag: {value}")
    return value


def sha256_file(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            size += len(block)
            digest.update(block)
    return size, digest.hexdigest()


def text(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value or len(value) > 4096:
        fail(f"invalid text: {field}")
    return value


def asset_name(value: Any, field: str) -> str:
    name = text(value, field)
    if name in {".", ".."} or "/" in name or "\\" in name:
        fail(f"unsafe asset name: {field}")
    return name


def load_presentation(path: Path, version: str) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or set(data) - ROOT_KEYS:
        fail("unknown presentation key")
    tag = f"v{version}"
    notes_url = data.get("notes_url")
    expected_url = f"https://github.com/{REPO}/releases/tag/{tag}"
    if notes_url != expected_url:
        fail("wrong notes URL")

    highlights: list[dict[str, Any]] = []
    raw_highlights = data.get("highlights", [])
    if not isinstance(raw_highlights, list) or len(raw_highlights) > 8:
        fail("invalid highlight list")
    for raw in raw_highlights:
        if not isinstance(raw, dict) or set(raw) - HIGHLIGHT_KEYS:
            fail("unknown highlight key")
        kind = raw.get("kind", "feature")
        if kind not in {"feature", "statistic", "beforeAfter", "milestone"}:
            fail(f"unknown highlight kind: {kind}")
        item: dict[str, Any] = {
            "kind": kind,
            "section": text(raw.get("section"), "highlight.section"),
            "title": text(raw.get("title"), "highlight.title"),
            "body": text(raw.get("body"), "highlight.body"),
        }
        for source, target in (
            ("value", "value"), ("context", "context"),
            ("before_caption", "beforeCaption"), ("after_caption", "afterCaption"),
        ):
            if source in raw:
                item[target] = text(raw[source], f"highlight.{source}")
        refs = raw.get("artwork_assets", [])
        if not isinstance(refs, list):
            fail("invalid highlight artwork list")
        item["artworkAssets"] = [asset_name(ref, "highlight.artwork_assets") for ref in refs]
        highlights.append(item)

    artwork: list[dict[str, str]] = []
    raw_artwork = data.get("artwork", [])
    if not isinstance(raw_artwork, list) or len(raw_artwork) > 16:
        fail("invalid artwork list")
    seen: set[str] = set()
    for raw in raw_artwork:
        if not isinstance(raw, dict) or set(raw) != ARTWORK_KEYS:
            fail("artwork requires asset and path")
        name = asset_name(raw["asset"], "artwork.asset")
        if name in seen:
            fail(f"duplicate artwork asset: {name}")
        source = Path(text(raw["path"], "artwork.path"))
        if not source.is_file():
            fail(f"missing artwork: {source}")
        size, digest = sha256_file(source)
        if size <= 0:
            fail(f"empty artwork: {source}")
        artwork.append({"asset": name, "sha256": digest})
        seen.add(name)

    return {
        "schemaVersion": 1,
        "version": version,
        "tag": tag,
        "eyebrow": text(data.get("eyebrow"), "eyebrow"),
        "title": text(data.get("title"), "title"),
        "summary": text(data.get("summary"), "summary"),
        "installer": {},
        "minimumUpdaterVersion": canonical_version(data.get("minimum_updater_version", "1.1.0")),
        "notesUrl": notes_url,
        "highlights": highlights,
        "artwork": artwork,
    }


def manifest_bytes(version: str, installer: Path, presentation: Path) -> bytes:
    version = canonical_version(version)
    expected_name = f"Colosseum-{version}-setup.exe"
    if installer.name != expected_name:
        fail(f"installer filename drift: {installer.name} != {expected_name}")
    if not installer.is_file():
        fail(f"installer not found: {installer}")
    size, digest = sha256_file(installer)
    manifest = load_presentation(presentation, version)
    manifest["installer"] = {"asset": installer.name, "size": size, "sha256": digest}
    artwork_names = {item["asset"] for item in manifest["artwork"]}
    for highlight in manifest["highlights"]:
        if any(name not in artwork_names for name in highlight["artworkAssets"]):
            fail("highlight references missing artwork")
    return (json.dumps(manifest, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")


def sign_raw(manifest: bytes, private_key: Path) -> bytes:
    if not private_key.is_file():
        fail(f"private key not found: {private_key}")
    with tempfile.TemporaryDirectory(prefix="colosseum-update-sign-") as temp:
        root = Path(temp)
        message = root / "manifest.json"
        signature = root / "manifest.sig"
        message.write_bytes(manifest)
        subprocess.run(
            [openssl_command(), "pkeyutl", "-sign", "-rawin", "-inkey", str(private_key),
             "-in", str(message), "-out", str(signature)],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        raw = signature.read_bytes()
    if len(raw) != 64:
        fail(f"unexpected Ed25519 signature length: {len(raw)}")
    return raw


def cpp_header_key(path: Path) -> bytes:
    values = re.findall(r"0x([0-9a-fA-F]{2})", path.read_text(encoding="utf-8"))
    if len(values) != 32:
        fail("C++ public-key header does not contain exactly 32 bytes")
    return bytes.fromhex("".join(values))


def check_cpp_header(public_der: Path, cpp_header: Path) -> None:
    der = public_der.read_bytes()
    if len(der) != 44 or der[:12] != DER_PREFIX:
        fail("unexpected Ed25519 SPKI DER")
    if der[12:] != cpp_header_key(cpp_header):
        fail("external public key does not match C++ header")
    print("UPDATE_PUBLIC_KEY_MATCH=1")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version")
    parser.add_argument("--installer")
    parser.add_argument("--presentation")
    parser.add_argument("--private-key")
    parser.add_argument("--output-dir")
    parser.add_argument("--public-key-der")
    parser.add_argument("--check-cpp-header")
    args = parser.parse_args(argv)
    try:
        if args.public_key_der or args.check_cpp_header:
            if not (args.public_key_der and args.check_cpp_header):
                fail("--public-key-der and --check-cpp-header are paired")
            check_cpp_header(Path(args.public_key_der), Path(args.check_cpp_header))
            if not args.version:
                return 0
        required = (args.version, args.installer, args.presentation, args.private_key, args.output_dir)
        if any(value is None for value in required):
            parser.error("generation requires --version, --installer, --presentation, --private-key, and --output-dir")
        manifest = manifest_bytes(args.version, Path(args.installer), Path(args.presentation))
        signature = sign_raw(manifest, Path(args.private_key))
        output = Path(args.output_dir)
        output.mkdir(parents=True, exist_ok=True)
        (output / "colosseum-update-v1.json").write_bytes(manifest)
        (output / "colosseum-update-v1.json.sig").write_bytes(signature)
        print(f"MANIFEST_PATH={output / 'colosseum-update-v1.json'}")
        print(f"SIGNATURE_PATH={output / 'colosseum-update-v1.json.sig'}")
        print(f"MANIFEST_SHA256={hashlib.sha256(manifest).hexdigest()}")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"generate_update_manifest: {exc}", file=__import__("sys").stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
