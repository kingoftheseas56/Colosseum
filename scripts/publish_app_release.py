"""publish_app_release.py — publish a Colosseum app build as a GitHub release.

The app twin of the data vault's publish_release.py: creates (or reuses) a tagged
release on the PRIVATE app repo (kingoftheseas56/Colosseum) and uploads the packaged
zip. Reuses the vault's token ladder + REST helper — same universal door, different
repo. Safe to re-run: an existing asset with the same name is replaced.

Usage: python scripts/publish_app_release.py <tag> <zip-path> [--title "..."] [--notes "..."]
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "data_vault"))
from vault_common import gh, resolve_token  # noqa: E402

APP_REPO = "kingoftheseas56/Colosseum"


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    tag, zip_path = sys.argv[1], sys.argv[2]
    title = tag
    notes = ""
    args = sys.argv[3:]
    while args:
        a = args.pop(0)
        if a == "--title" and args:
            title = args.pop(0)
        elif a == "--notes" and args:
            notes = args.pop(0)
    if not os.path.exists(zip_path):
        raise SystemExit(f"asset not found: {zip_path}")

    token = resolve_token()

    # create-or-reuse the release
    try:
        rel = gh(f"/repos/{APP_REPO}/releases/tags/{tag}", token)
        print(f"release exists: {rel['html_url']}")
    except Exception:
        rel = gh(f"/repos/{APP_REPO}/releases", token, method="POST", payload={
            "tag_name": tag, "name": title, "body": notes,
            "draft": False, "prerelease": False,
        })
        print(f"release created: {rel['html_url']}")

    # replace-not-duplicate the asset
    name = os.path.basename(zip_path)
    for a in rel.get("assets", []):
        if a["name"] == name:
            gh(f"/repos/{APP_REPO}/releases/assets/{a['id']}", token, method="DELETE")
            print(f"replaced existing asset {name}")
    upload = rel["upload_url"].split("{")[0] + f"?name={name}"
    with open(zip_path, "rb") as f:
        blob = f.read()
    asset = gh("", token, raw_url=upload, method="POST",
               content_type="application/zip", data=blob)
    size_mb = asset["size"] / 1024 / 1024
    print(f"uploaded {name} ({size_mb:.1f} MB): {asset['browser_download_url']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
