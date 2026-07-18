"""pull_data.py — fetch the latest baked databases from the Colosseum-Data vault.

THE universal side: runs on any machine with Python and a token — the dev
laptop, the old laptop running a bare exe, anywhere. Downloads every catalog
asset from the newest release (or --tag <t> for a specific snapshot) into
data/, atomically (tmp file, then swap) so a half-download never wedges the app.

Host swap: COLOSSEUM_VAULT_HOST=<base-url> skips GitHub entirely and fetches
<base-url>/<asset> — the Cloudflare R2 path for the day assets outgrow 2 GB.

Usage: python scripts/data_vault/pull_data.py [--tag data-YYYY-MM-DD]
"""
from __future__ import annotations

import os
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vault_common import OWNER_REPO, REPO_ROOT, gh, resolve_token


def fetch_to(url: str, dest: str, headers: dict) -> None:
    req = urllib.request.Request(url)
    for k, v in headers.items():
        req.add_header(k, v)
    tmp = dest + ".downloading"
    with urllib.request.urlopen(req, timeout=1800) as r, open(tmp, "wb") as f:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
    if os.path.exists(dest):
        os.remove(dest)
    os.rename(tmp, dest)


def main() -> int:
    tag = None
    if len(sys.argv) >= 3 and sys.argv[1] == "--tag":
        tag = sys.argv[2]
    data_dir = os.path.join(REPO_ROOT, "data")
    os.makedirs(data_dir, exist_ok=True)

    host = os.environ.get("COLOSSEUM_VAULT_HOST", "").strip().rstrip("/")
    if host:                                   # R2/S3-style flat host (the 2 GB day)
        for name in ("comics_catalog.db", "mal_catalog.db"):
            print("pulling", name, "from", host)
            fetch_to(f"{host}/{name}", os.path.join(data_dir, name), {})
            print("  ok")
        return 0

    token = resolve_token()
    path = (f"/repos/{OWNER_REPO}/releases/tags/{tag}" if tag
            else f"/repos/{OWNER_REPO}/releases/latest")
    rel = gh(path, token)
    assets = rel.get("assets", [])
    if not assets:
        print("release", rel.get("tag_name"), "has no assets")
        return 1
    print("release:", rel.get("tag_name"))
    for a in assets:
        name = a["name"]
        print(f"pulling {name} ({a['size'] / 1048576:.1f} MB)...")
        # asset download: the API asset url + octet-stream Accept follows to the blob
        fetch_to(a["url"], os.path.join(data_dir, name),
                 {"Authorization": "token " + token,
                  "Accept": "application/octet-stream",
                  "User-Agent": "colosseum-vault"})
        print("  ok ->", os.path.join("data", name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
