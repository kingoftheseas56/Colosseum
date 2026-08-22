"""publish_release.py — push this week's baked databases to the Colosseum-Data vault.

Creates (or reuses) a dated release on the private vault repo and uploads every
catalog artifact present in data/. Run after the weekly bakes; safe to re-run —
an existing asset with the same name is replaced, not duplicated.

Usage: python scripts/data_vault/publish_release.py [--tag data-YYYY-MM-DD]
"""
from __future__ import annotations

import os
import sys
from datetime import datetime, timezone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vault_common import OWNER_REPO, REPO_ROOT, gh, resolve_token

ARTIFACTS = ["comics_catalog.db", "mal_catalog.db", "tankoban_catalog.db",
             "imdb_catalog.db"]


def main() -> int:
    tag = None
    if len(sys.argv) >= 3 and sys.argv[1] == "--tag":
        tag = sys.argv[2]
    if not tag:
        tag = "data-" + datetime.now(timezone.utc).strftime("%Y-%m-%d")
    token = resolve_token()

    present = [a for a in ARTIFACTS
               if os.path.exists(os.path.join(REPO_ROOT, "data", a))]
    if not present:
        print("nothing to publish: no catalog dbs in data/")
        return 1

    # find or create the release for this tag
    try:
        rel = gh(f"/repos/{OWNER_REPO}/releases/tags/{tag}", token)
    except Exception:
        rel = gh(f"/repos/{OWNER_REPO}/releases", token, payload={
            "tag_name": tag, "target_commitish": "main", "name": tag,
            "body": "Weekly catalog snapshot (auto-published by publish_release.py)."})
        print("created release", tag)

    existing = {a["name"]: a["id"] for a in rel.get("assets", [])}
    upload_base = rel["upload_url"].split("{")[0]

    for name in present:
        path = os.path.join(REPO_ROOT, "data", name)
        if name in existing:                      # replace, never duplicate
            gh(f"/repos/{OWNER_REPO}/releases/assets/{existing[name]}", token,
               method="DELETE")
        size_mb = os.path.getsize(path) / 1048576
        print(f"uploading {name} ({size_mb:.1f} MB)...")
        with open(path, "rb") as f:
            gh("", token, raw_url=f"{upload_base}?name={name}", method="POST",
               content_type="application/octet-stream", data=f.read())
        print("  uploaded", name)

    print("published:", tag, "->", ", ".join(present))
    return 0


if __name__ == "__main__":
    sys.exit(main())
