#!/usr/bin/env bash
set -euo pipefail
: "${TOOLCHAIN_ROOT:=/workspace/shared/toolchains}"
source "$TOOLCHAIN_ROOT/env.sh"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
: "${EVIDENCE:=/workspace/shared/collections-sharing/evidence/browser-service}"
mkdir -p "$EVIDENCE"
DB="collections_runtime_$(date +%s)_test"
PGDATA="$EVIDENCE/postgres-data"
[[ ! -e "$PGDATA" ]] || { echo 'Use fresh evidence directory'; exit 1; }
initdb -D "$PGDATA" -U collections --auth=trust --no-locale -E UTF8 > "$EVIDENCE/initdb.log"
postgres -D "$PGDATA" -h 127.0.0.1 -p 55432 -c unix_socket_directories= > "$EVIDENCE/postgres.log" 2>&1 & PG_PID=$!
SERVICE_PID=''
cleanup() { [[ -z "$SERVICE_PID" ]] || kill "$SERVICE_PID" 2>/dev/null || true; kill "$PG_PID" 2>/dev/null || true; wait || true; }
trap cleanup EXIT
for i in {1..100}; do pg_isready -h 127.0.0.1 -p 55432 -U collections >/dev/null && break; sleep .1; done
createdb -h 127.0.0.1 -p 55432 -U collections "$DB"
cd "$REPO/server/account-service"
GOMAXPROCS=2 go build -p 2 -o "$EVIDENCE/account-migrate" ./cmd/colosseum-account-migrate
GOMAXPROCS=2 go build -p 2 -o "$EVIDENCE/account-service" ./cmd/colosseum-account-service
DSN="postgres://collections@127.0.0.1:55432/$DB?sslmode=disable"
env -u DATABASE_URL MIGRATION_DATABASE_URL="$DSN" "$EVIDENCE/account-migrate" > "$EVIDENCE/migrate.log" 2>&1
export COLOSSEUM_ACCOUNT_ENV=test HTTP_ADDR=127.0.0.1:55440 DATABASE_URL="$DSN"
for name in RECOVERY_HMAC_KEY ABUSE_HMAC_KEY SESSION_WRAP_KEY SYNC_DATA_KEY; do export "$name=$(python3 -c 'import secrets,base64; print(base64.b64encode(secrets.token_bytes(32)).decode())')"; done
"$EVIDENCE/account-service" > "$EVIDENCE/service.log" 2>&1 & SERVICE_PID=$!
for i in {1..100}; do curl --noproxy '*' -fsS http://127.0.0.1:55440/readyz > "$EVIDENCE/ready.json" 2>/dev/null && break; sleep .1; done
sha256sum "$EVIDENCE/account-service" "$EVIDENCE/account-migrate" > "$EVIDENCE/executables.sha256"
git -C "$REPO" rev-parse HEAD > "$EVIDENCE/source-head.txt"
git -C "$REPO" diff --binary > "$EVIDENCE/source-working-diff.patch"
export SERVICE_URL=http://127.0.0.1:55440
export CHROMIUM_PATH="$TOOLCHAIN_ROOT/chrome-headless/chrome-headless-shell-linux64/chrome-headless-shell"
node internal/httpserver/collectionviewer/viewer.service.test.cjs "$EVIDENCE/browser" 2>&1 | tee "$EVIDENCE/browser-run.log"
