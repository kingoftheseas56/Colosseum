#!/usr/bin/env python3
"""Extract the canonical CBZ to the Colosseum page dir and verify byte identity
(spec 4.3): every extracted page's SHA-256 must match the committed manifest.
A mismatch invalidates the run. Also re-verifies the CBZ hash itself, so Max/TB2
(which open the CBZ directly) are covered by the same gate."""

import hashlib
import json
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
CBZ = os.path.join(REPO, "artifacts", "reader-bakeoff", "fixture-longstrip-120.cbz")
PAGES_DIR = os.path.join(REPO, "artifacts", "reader-bakeoff", "pages")
MANIFEST = os.path.join(REPO, "docs", "reader-bakeoff", "fixture-manifest.json")


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    with open(MANIFEST, encoding="utf-8") as handle:
        manifest = json.load(handle)

    cbz_sha = sha256_file(CBZ)
    if cbz_sha != manifest["cbz"]["sha256"]:
        print("VERIFY_FAIL cbz hash mismatch", file=sys.stderr)
        return 1

    os.makedirs(PAGES_DIR, exist_ok=True)
    with zipfile.ZipFile(CBZ) as zf:
        for entry in manifest["entries"]:
            target = os.path.join(PAGES_DIR, entry["entry"])
            if not os.path.exists(target) or os.path.getsize(target) != entry["bytes"]:
                with zf.open(entry["entry"]) as src, open(target, "wb") as dst:
                    dst.write(src.read())
            if sha256_file(target) != entry["sha256"]:
                print("VERIFY_FAIL page hash mismatch: " + entry["entry"], file=sys.stderr)
                return 1

    print("VERIFY_OK cbz=%s pages=%d dir=%s"
          % (cbz_sha[:12], len(manifest["entries"]), PAGES_DIR))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
