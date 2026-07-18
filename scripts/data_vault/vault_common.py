"""vault_common.py — shared plumbing for the Colosseum data vault (2026-07-18).

The vault is a private GitHub repo (kingoftheseas56/Colosseum-Data) whose weekly
RELEASES carry the baked catalog databases. GitHub's release endpoints are plain
HTTPS — a universal API any machine can call with one token; no dev tooling.

Token ladder (first hit wins):
  1. COLOSSEUM_VAULT_TOKEN env var           (bare player machines)
  2. git credential fill for github.com      (any machine that ever pushed/pulled)
  3. token file beside the repo: data/vault_token.txt   (gitignored, hand-placed)

HOST SWAP (the far-future 2 GB day): set COLOSSEUM_VAULT_HOST to an R2/S3-style
base URL and the pull script fetches <base>/<asset> instead of GitHub releases.
Publish stays GitHub until that day; only pull_data honors the swap.
"""
from __future__ import annotations

import json
import os
import subprocess
import urllib.request

OWNER_REPO = "kingoftheseas56/Colosseum-Data"
API = "https://api.github.com"
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def resolve_token() -> str:
    tok = os.environ.get("COLOSSEUM_VAULT_TOKEN", "").strip()
    if tok:
        return tok
    try:
        out = subprocess.run(["git", "credential", "fill"],
                             input="protocol=https\nhost=github.com\n\n",
                             capture_output=True, text=True, timeout=20).stdout
        for line in out.splitlines():
            if line.startswith("password="):
                return line.split("=", 1)[1].strip()
    except Exception:
        pass
    tf = os.path.join(REPO_ROOT, "data", "vault_token.txt")
    if os.path.exists(tf):
        with open(tf, encoding="utf-8") as f:
            return f.read().strip()
    raise SystemExit("no vault token: set COLOSSEUM_VAULT_TOKEN, sign git into "
                     "github.com, or place data/vault_token.txt")


def gh(path: str, token: str, payload=None, method=None, raw_url=None,
       content_type="application/json", data=None):
    url = raw_url or (API + path)
    body = data if data is not None else (
        json.dumps(payload).encode() if payload is not None else None)
    req = urllib.request.Request(url, data=body, method=method)
    req.add_header("Authorization", "token " + token)
    req.add_header("User-Agent", "colosseum-vault")
    req.add_header("Accept", "application/vnd.github+json")
    if body is not None:
        req.add_header("Content-Type", content_type)
    with urllib.request.urlopen(req, timeout=600) as r:
        raw = r.read()
    return json.loads(raw) if raw[:1] in (b"{", b"[") else raw
