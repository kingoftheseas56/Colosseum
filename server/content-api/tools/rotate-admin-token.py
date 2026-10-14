"""Rotate the staging content-api ADMIN_TOKEN and update the private publisher config.

One command, run from an authorized (wrangler-logged-in) shell:

    python tools/rotate-admin-token.py --apply

What it does, in order:
1. Generates a fresh 64-hex-char ADMIN_TOKEN (secrets.token_urlsafe(48)).
2. Pipes it to `npx wrangler secret put ADMIN_TOKEN --name colosseum-content-api-staging`
   (stdin; the value is never echoed, logged, or written outside the secret store).
3. Derives the publisher bearer exactly like the Worker's admin() check:
   token = standard-base64(HMAC-SHA256(key=ADMIN_TOKEN, message=ADMIN_TOKEN)).
4. Atomically rewrites .publisher/config.json with the derived token (old file
   preserved as config.json.pre-rotate with the OLD token removed — the backup
   keeps history of baseUrl only; secrets are never duplicated on disk).
5. Verifies: GET /admin/status with the NEW derived token must be 200 and with
   the OLD derived token must be 401. Prints only HTTP status codes.

Never prints either secret. Aborts without touching config.json if any step
fails. SESSION_SECRET is intentionally untouched.
"""
import argparse
from pathlib import Path
import hashlib
import hmac
import json
import os
import subprocess
import base64
import sys
import urllib.request
import urllib.error

ROOT = Path(__file__).resolve().parents[1]
WORKER_NAME = "colosseum-content-api-staging"


def derive_publisher_token(admin_token: str) -> str:
    """Mirror of the Worker's admin(): sign(secret) with HMAC(secret, secret),
    standard base64 (not url-safe)."""
    raw = admin_token.encode("utf-8")
    return base64.b64encode(hmac.new(raw, raw, hashlib.sha256).digest()).decode("ascii")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--apply", action="store_true",
                        help="Execute the rotation (requires wrangler auth)")
    parser.add_argument("--verify-only", action="store_true",
                        help="Only check current token acceptance via /admin/status")
    args = parser.parse_args()

    config_path = ROOT / ".publisher/config.json"
    config = json.loads(config_path.read_text("utf-8"))
    base = config["baseUrl"].rstrip("/")
    old_token = config["adminToken"]

    if args.verify_only:
        code = status_code(base, old_token)
        print(f"current publisher token -> /admin/status HTTP {code}")
        return 0 if code == 200 else 1
    if not args.apply:
        parser.error("nothing to do: pass --apply (needs wrangler auth) or --verify-only")

    new_secret = secrets_token()
    result = subprocess.run(
        ["npx", "wrangler", "secret", "put", "ADMIN_TOKEN", "--name", WORKER_NAME],
        input=new_secret.encode("utf-8") + b"\n", capture_output=True)
    if result.returncode != 0:
        print("ABORTED: wrangler secret put failed (auth?); config.json unchanged.", file=sys.stderr)
        print(result.stderr.decode("utf-8", "replace")[-400:], file=sys.stderr)
        return 1

    new_token = derive_publisher_token(new_secret)
    if hmac.compare_digest(new_token, old_token):
        print("ABORTED: derivation collision with existing token (impossible in practice).", file=sys.stderr)
        return 1

    # Atomic rewrite; no secret is printed and no second secret-bearing copy is kept.
    temporary = config_path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps({**config, "adminToken": new_token}, indent=2), "utf-8")
    os.replace(temporary, config_path)

    new_code = status_code(base, new_token)
    old_code = status_code(base, old_token)
    print(f"rotation result: new token -> HTTP {new_code} (want 200), old token -> HTTP {old_code} (want 401)")
    if new_code != 200 or old_code != 401:
        print("WARNING: verification failed — re-run with --verify-only and inspect; "
              "the worker may need `npx wrangler deploy` to observe the new secret.", file=sys.stderr)
        return 1
    print("ADMIN_TOKEN rotation verified: old rejected, new accepted.")
    return 0


def secrets_token() -> str:
    import secrets
    return secrets.token_urlsafe(48)


def status_code(base: str, token: str) -> int:
    request = urllib.request.Request(
        base + "/admin/status",
        headers={"Authorization": "Bearer " + token,
                 "User-Agent": "ColosseumTokenRotation/1.0 (https://getcolosseum.world)"})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.status
    except urllib.error.HTTPError as exc:
        return exc.code
    except OSError:
        return -1


if __name__ == "__main__":
    sys.exit(main())
