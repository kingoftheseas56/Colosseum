#!/usr/bin/env python3
"""Build the canonical long-strip bakeoff fixture (spec 4.2-4.4).

Takes a deterministic 120-page front slice of the locally downloaded source
volume, packs it into ONE canonical CBZ under artifacts/reader-bakeoff/, and
writes the byte-identity manifest: CBZ SHA-256, ordered entries, per-page
SHA-256 / size / format / dimensions, and spread classification by one shared
threshold. Per Hemanth's 2026-07-15 amendment to spec 4.1 the source is a
local commercial volume: the CBZ stays local-only (gitignored) and the
manifest records hashes and dimensions, not provenance URLs.

Repetition rule (spec 4.2) is not needed: the source has 380 pages.
"""

import glob
import hashlib
import json
import os
import struct
import sys
import zipfile

PAGES = 120
# width/height above this ratio = double-width spread (recorded in manifest)
SPREAD_RATIO = 1.0

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT_DIR = os.path.join(REPO, "artifacts", "reader-bakeoff")
CBZ_PATH = os.path.join(OUT_DIR, "fixture-longstrip-120.cbz")
MANIFEST_PATH = os.path.join(REPO, "docs", "reader-bakeoff", "fixture-manifest.json")

SOURCE_GLOB = os.path.expanduser(
    "~/AppData/Roaming/Brotherhood/Colosseum/comics/gc_batman/*e021b95696"
)


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def jpeg_dimensions(data):
    """Width/height straight from the JPEG stream (no PIL dependency at runtime)."""
    i = 2
    while i < len(data) - 9:
        if data[i] != 0xFF:
            i += 1
            continue
        marker = data[i + 1]
        if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7,
                      0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            height, width = struct.unpack(">HH", data[i + 5 : i + 9])
            return width, height
        if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7:
            i += 2
            continue
        (seg_len,) = struct.unpack(">H", data[i + 2 : i + 4])
        i += 2 + seg_len
    raise ValueError("no SOF marker found")


def main():
    sources = glob.glob(SOURCE_GLOB)
    if not sources:
        print("FIXTURE_FAIL source volume not found", file=sys.stderr)
        return 1
    src_dir = sources[0]
    pages = sorted(glob.glob(os.path.join(src_dir, "page_*.jpg")))
    if len(pages) < PAGES:
        print("FIXTURE_FAIL only %d source pages" % len(pages), file=sys.stderr)
        return 1
    pages = pages[:PAGES]

    os.makedirs(OUT_DIR, exist_ok=True)
    os.makedirs(os.path.dirname(MANIFEST_PATH), exist_ok=True)

    entries = []
    spreads = 0
    # STORED (no compression): byte-identical entries, cheap extraction, and
    # deterministic output independent of zlib version.
    with zipfile.ZipFile(CBZ_PATH, "w", compression=zipfile.ZIP_STORED) as zf:
        for index, path in enumerate(pages):
            with open(path, "rb") as handle:
                data = handle.read()
            width, height = jpeg_dimensions(data)
            name = "page_%03d.jpg" % index
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            zf.writestr(info, data)
            is_spread = (width / float(height)) > SPREAD_RATIO
            spreads += 1 if is_spread else 0
            entries.append(
                {
                    "entry": name,
                    "sha256": sha256_bytes(data),
                    "bytes": len(data),
                    "format": "jpeg",
                    "width": width,
                    "height": height,
                    "spread": is_spread,
                }
            )

    with open(CBZ_PATH, "rb") as handle:
        cbz_sha = sha256_bytes(handle.read())

    manifest = {
        "spec": "docs/superpowers/specs/2026-07-15-reader-long-strip-bakeoff-design.md",
        "amendment": (
            "Hemanth 2026-07-15: local commercial volume replaces the spec-4.1 "
            "public-domain source; CBZ is local-only and this manifest carries "
            "byte identity, not provenance"
        ),
        "cbz": {
            "file": "artifacts/reader-bakeoff/fixture-longstrip-120.cbz",
            "sha256": cbz_sha,
            "pages": len(entries),
            "spread_ratio_threshold": SPREAD_RATIO,
            "spread_count": spreads,
        },
        "entries": entries,
    }
    with open(MANIFEST_PATH, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")

    print("FIXTURE_OK cbz=%s pages=%d spreads=%d sha=%s"
          % (os.path.basename(CBZ_PATH), len(entries), spreads, cbz_sha[:12]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
