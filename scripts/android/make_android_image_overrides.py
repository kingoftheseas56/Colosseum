#!/usr/bin/env python3
"""Regenerate the Android APK's smaller copies of large bundled art.

The Android package stages `assets/` into the APK. A few tribute images are
multi-megabyte PNG paintings; the APK carries WebP copies instead, written
under packaging/android/asset-overrides/ with the SAME relative path and file
name (Qt's image reader recognises WebP by content, so QML paths stay as they
are). Desktop packages keep the original PNGs.

The manifest records each source PNG's SHA-256 so a test can flag an override
that no longer matches its source art.

Usage:  python scripts/android/make_android_image_overrides.py
Requires Pillow.
"""
import hashlib
import io
import json
import pathlib
import sys

from PIL import Image

REPO = pathlib.Path(__file__).resolve().parents[2]
OVERRIDE_ROOT = REPO / "packaging" / "android" / "asset-overrides"
MANIFEST = OVERRIDE_ROOT / "manifest.json"
SOURCES = ["assets/universes/one-piece"]
MAX_WIDTH = 2560
QUALITY = 85


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    entries = {}
    total_in = total_out = 0
    for root in SOURCES:
        for src in sorted((REPO / root).rglob("*.png")):
            rel = src.relative_to(REPO).as_posix()
            raw = src.read_bytes()
            image = Image.open(io.BytesIO(raw))
            image = image.convert("RGBA" if image.mode in ("RGBA", "LA", "P") else "RGB")
            if image.width > MAX_WIDTH:
                image = image.resize((MAX_WIDTH, round(image.height * MAX_WIDTH / image.width)),
                                     Image.LANCZOS)
            out = io.BytesIO()
            image.save(out, "WEBP", quality=QUALITY, method=6)
            data = out.getvalue()
            if len(data) >= len(raw):
                continue  # not worth overriding
            dest = OVERRIDE_ROOT / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
            entries[rel] = {"source_sha256": sha256(raw), "override_sha256": sha256(data)}
            total_in += len(raw)
            total_out += len(data)
    MANIFEST.write_text(json.dumps({"format": "webp", "quality": QUALITY, "max_width": MAX_WIDTH,
                                    "files": entries}, indent=2) + "\n", encoding="utf-8")
    print(f"{len(entries)} overrides: {total_in / 1e6:.1f} MB PNG -> {total_out / 1e6:.1f} MB WebP")
    return 0


if __name__ == "__main__":
    sys.exit(main())
