#!/usr/bin/env python3
"""Build a GitHub-compatible metadata envelope for first-party update discovery.

The updater's actual trust root is the signed colosseum-update-v1.json manifest,
not this unsigned discovery envelope. Verify signature and installer before
writing stable.json. No network, upload or private-key access occurs here.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from urllib.parse import quote, urlsplit, urlunsplit

from verify_update_release import verify

_MANIFEST = "colosseum-update-v1.json"
_SIGNATURE = "colosseum-update-v1.json.sig"
_GITLAB_RAW_ROOT = "/kingoftheseas56/colosseum-site/-/raw/"
_GITLAB_PREFIX = _GITLAB_RAW_ROOT + "release-assets/"
_TAG = re.compile(r"^v(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)$")


def file_digest(path: Path) -> tuple[int, str]:
    length = 0
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            length += len(chunk)
            digest.update(chunk)
    return length, digest.hexdigest()


def safe_name(name: str) -> bool:
    return isinstance(name, str) and bool(name) and len(name) <= 128 and name not in {".", ".."} \
        and "/" not in name and "\\" not in name and not any(ord(ch) < 32 for ch in name)


def validated_base(base_url: str, tag: str) -> str:
    """Require our own public GitLab release-assets branch, not an arbitrary mirror."""
    parts = urlsplit(base_url)
    expected_path = _GITLAB_PREFIX + tag + "/releases"
    if (parts.scheme != "https" or parts.hostname != "gitlab.com"
        or parts.port is not None or parts.username is not None
        or parts.password is not None or parts.fragment
        or parts.path.rstrip("/") != expected_path
        or parts.query not in {"", "ref_type=heads"}):
        raise ValueError("asset base must be the exact versioned Colosseum GitLab raw releases path")
    # GitLab must receive the slash inside the ref as part of one segment.
    # Unencoded refs can resolve metadata/ranges while full LFS GETs fail404.
    return _GITLAB_RAW_ROOT + quote("release-assets/" + tag, safe="") + "/releases"


def asset_url(base_path: str, name: str) -> str:
    if not safe_name(name):
        raise ValueError("unsafe release asset name")
    return urlunsplit(("https", "gitlab.com", base_path + "/" + quote(name, safe=""),
                       "ref_type=heads", ""))


def build_stable_feed(*, manifest: Path, signature: Path, installer: Path,
                      public_key_der: Path, asset_base_url: str,
                      artwork_dir: Path | None = None) -> bytes:
    """Return deterministic feed bytes only for a verified release."""
    checked = verify(manifest, signature, installer, public_key_der, artwork_dir)
    release = json.loads(manifest.read_bytes())
    tag = release.get("tag")
    if (not isinstance(tag, str) or not _TAG.fullmatch(tag)
        or tag != "v" + checked["version"]):
        raise ValueError("invalid stable tag")
    base_path = validated_base(asset_base_url, tag)
    art = release.get("artwork", [])
    if not isinstance(art, list) or len(art) > 16:
        raise ValueError("invalid artwork list")
    if art and artwork_dir is None:
        raise ValueError("artwork directory is required for published artwork")

    named_files = [
        (_MANIFEST, manifest),
        (_SIGNATURE, signature),
        (installer.name, installer),
    ]
    names = set()
    for item in art:
        if not isinstance(item, dict) or not safe_name(item.get("asset")):
            raise ValueError("invalid artwork entry")
        name = item["asset"]
        if name in names:
            raise ValueError("duplicate artwork entry")
        names.add(name)
        named_files.append((name, artwork_dir / name))

    result_assets = []
    seen = set()
    for name, path in named_files:
        if name in seen or not safe_name(name):
            raise ValueError("duplicate or unsafe release asset")
        seen.add(name)
        size, digest = file_digest(path)
        if not size:
            raise ValueError("zero-byte release asset")
        if name == _MANIFEST and size > 512 * 1024:
            raise ValueError("manifest exceeds updater size limit")
        if name == _SIGNATURE and size != 64:
            raise ValueError("signature must be exactly 64 bytes")
        result_assets.append({
            "name": name,
            "browser_download_url": asset_url(base_path, name),
            "size": size,
            "digest": "sha256:" + digest,
        })

    # Reuse the existing release JSON contract. The client checks the signature
    # and installer SHA-256 before allowing an automatic Windows installation.
    payload = {
        "tag_name": tag,
        "draft": False,
        "prerelease": False,
        "assets": result_assets,
    }
    encoded = (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
    if len(encoded) > 2 * 1024 * 1024:
        raise ValueError("stable feed exceeds updater API size limit")
    return encoded


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--signature", type=Path, required=True)
    parser.add_argument("--installer", type=Path, required=True)
    parser.add_argument("--public-key-der", type=Path, required=True)
    parser.add_argument("--asset-base-url", required=True,
                        help="https://gitlab.com/kingoftheseas56/colosseum-site/-/raw/"
                             "release-assets/vX.Y.Z/releases?ref_type=heads")
    parser.add_argument("--artwork-dir", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        data = build_stable_feed(
            manifest=args.manifest, signature=args.signature, installer=args.installer,
            public_key_der=args.public_key_der, asset_base_url=args.asset_base_url,
            artwork_dir=args.artwork_dir,
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        # The operator must explicitly remove an existing draft to avoid silent
        # overwrites of an already-vetted stable feed.
        with args.output.open("xb") as out:
            out.write(data)
        print(f"STABLE_FEED={args.output}")
        print(f"STABLE_FEED_SHA256={hashlib.sha256(data).hexdigest()}")
        print(f"STABLE_FEED_BYTES={len(data)}")
        return 0
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        parser.exit(1, f"create_stable_feed: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
