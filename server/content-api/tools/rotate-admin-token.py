"""Rotate the staging content-api ADMIN_TOKEN and update the private publisher config.

The Worker's admin() check accepts exactly ONE bearer: the raw ADMIN_TOKEN
secret value (it signs the secret with itself and verifies that signature
against the supplied token, which holds only for the identical string).
The publisher config therefore stores the raw secret — no derivation.

Run from an authorized (wrangler-logged-in) shell, staging account scoped:

    set CLOUDFLARE_ACCOUNT_ID=69049ce787cd73028c878bf0953946f6
    python tools/rotate-admin-token.py --apply

Order of operations (recovery-safe):
1. Preflight — validate the private config destination and that the intended
   staging Worker exists and currently holds an ADMIN_TOKEN secret
   (`wrangler secret list`). Nothing is mutated.
2. Generate a fresh 64-char secret (never printed).
3. Write `.publisher/rotation-pending.json` holding the new secret BEFORE any
   Cloudflare mutation, so a crash after the secret update can always finish
   with `--recover`. `.publisher/` is gitignored.
4. `wrangler secret put ADMIN_TOKEN` (secret piped via stdin; subprocess
   output captured and redacted before any printing).
5. Atomically rewrite `.publisher/config.json` with the new raw bearer.
6. Verify against the live Worker: new token -> HTTP 200 on /admin/status,
   old token -> HTTP 401. Only status codes are printed.
7. Remove the pending file on full success.

If step 5 or 6 fails after step 4, the tool reports the recovery state and
exits non-zero; re-run `python tools/rotate-admin-token.py --recover` to
complete steps 5-7 from the pending file. Never prints either secret.
SESSION_SECRET is intentionally untouched.
"""
import argparse
from pathlib import Path
import json
import os
import re
import secrets as pysecrets
import shutil
import subprocess
import sys
import urllib.request
import urllib.error

ROOT = Path(__file__).resolve().parents[1]
CONFIG_PATH = ROOT / ".publisher/config.json"
PENDING_PATH = ROOT / ".publisher/rotation-pending.json"
WORKER_NAME = "colosseum-content-api-staging"
EXPECTED_BASE_URL = "https://colosseum-content-api-staging.colosseum-watchparty-relay.workers.dev"
ACCOUNT_ID = "69049ce787cd73028c878bf0953946f6"


def redact(text: str, *secret_values: str) -> str:
    result = text
    for value in secret_values:
        if value:
            result = result.replace(value, "<redacted>")
    return result


def fail(message: str) -> "None":
    raise SystemExit(message)


def wrangler(*arguments: str, stdin: bytes | None = None) -> subprocess.CompletedProcess:
    """Run wrangler with the staging account pinned. Resolves npx explicitly —
    an unqualified 'npx' does not reliably resolve from Python subprocess on
    Windows (npx.cmd vs npx). Output is captured, never streamed, so secret
    material in command output can be redacted before printing."""
    executable = shutil.which("npx")
    if not executable:
        fail("npx not found on PATH; run from a Node-enabled shell")
    environment = dict(os.environ)
    environment["CLOUDFLARE_ACCOUNT_ID"] = ACCOUNT_ID
    return subprocess.run(
        [executable, "wrangler", *arguments],
        input=stdin,
        capture_output=True,
        env=environment,
    )


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


def preflight() -> dict:
    if not CONFIG_PATH.is_file():
        fail(f"private publisher config missing: {CONFIG_PATH}")
    config = json.loads(CONFIG_PATH.read_text("utf-8"))
    if config.get("baseUrl", "").rstrip("/") != EXPECTED_BASE_URL:
        fail(f"config baseUrl is not the staging content API: {config.get('baseUrl')!r}")
    if not config.get("adminToken"):
        fail("config adminToken missing; refusing to rotate a config we cannot restore")

    listing = wrangler("secret", "list", "--name", WORKER_NAME)
    if listing.returncode != 0:
        fail("preflight failed: wrangler secret list returned "
             + redact(listing.stderr.decode("utf-8", "replace")[-300:], config["adminToken"]))
    names = re.findall(r'"name"\s*:\s*"([^"]+)"', listing.stdout.decode("utf-8", "replace"))
    if "ADMIN_TOKEN" not in names:
        fail(f"preflight failed: {WORKER_NAME} reports no ADMIN_TOKEN secret "
             "(wrong Worker or account?)")
    return config


def write_config(config: dict, new_token: str) -> None:
    CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    temporary = CONFIG_PATH.with_suffix(".json.tmp")
    temporary.write_text(json.dumps({**config, "adminToken": new_token}, indent=2), "utf-8")
    os.replace(temporary, CONFIG_PATH)


def main() -> int:
    parser = argparse.ArgumentParser(description="Rotate the staging ADMIN_TOKEN "
                                                 "(raw-secret bearer contract).")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--apply", action="store_true",
                      help="full rotation (requires wrangler auth)")
    mode.add_argument("--recover", action="store_true",
                      help="finish a rotation whose Cloudflare update succeeded "
                           "but local persistence/verification did not")
    mode.add_argument("--verify-only", action="store_true",
                      help="check the current config token against /admin/status")
    args = parser.parse_args()

    config = preflight() if not args.recover else json.loads(CONFIG_PATH.read_text("utf-8"))
    base = config["baseUrl"].rstrip("/")
    old_token = config["adminToken"]

    if args.verify_only:
        code = status_code(base, old_token)
        print(f"current config token -> /admin/status HTTP {code}")
        return 0 if code == 200 else 1

    if args.apply:
        new_secret = pysecrets.token_urlsafe(48)
        PENDING_PATH.write_text(
            json.dumps({"newAdminToken": new_secret}), "utf-8")
        update = wrangler("secret", "put", "ADMIN_TOKEN", "--name", WORKER_NAME,
                          stdin=new_secret.encode("utf-8") + b"\n")
        if update.returncode != 0:
            PENDING_PATH.unlink(missing_ok=True)
            fail("ABORTED before any local change: wrangler secret put failed. "
                 "Config unchanged; live secret unchanged. "
                 + redact(update.stderr.decode("utf-8", "replace")[-300:], new_secret, old_token))
    elif args.recover:
        if not PENDING_PATH.is_file():
            fail("no pending rotation to recover")
        new_secret = json.loads(PENDING_PATH.read_text("utf-8"))["newAdminToken"]
        if status_code(base, new_secret) != 200:
            fail("recovery refused: pending secret is NOT live on the Worker; "
                 "re-run --apply instead")
    else:  # pragma: no cover - argparse enforces a mode
        fail("unreachable")

    write_config(config, new_secret)

    new_code = status_code(base, new_secret)
    old_code = status_code(base, old_token)
    print(f"rotation check: new token -> HTTP {new_code} (want 200), "
          f"old token -> HTTP {old_code} (want 401)")
    if new_code != 200 or old_code != 401:
        print("RECOVERY STATE: the Cloudflare secret was updated and the local "
              "config now carries the new token, but verification did not pass. "
              "Re-run with --verify-only; if it keeps failing, inspect the "
              "Worker (deploy status) before retrying. The pending file is kept.",
              file=sys.stderr)
        return 1

    PENDING_PATH.unlink(missing_ok=True)
    print("ADMIN_TOKEN rotation verified: old rejected, new accepted.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
