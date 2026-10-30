#!/usr/bin/env bash
set -euo pipefail
: "${TOOLCHAIN_ROOT:=/workspace/shared/toolchains}"
source "$TOOLCHAIN_ROOT/env.sh"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
: "${EVIDENCE:=/workspace/shared/collections-sharing/evidence/native-service}"
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
python3 "$REPO/tests/collections_sharing_runtime/seed_service.py" "$EVIDENCE"
: "${BUILD:=/workspace/shared/collections-sharing/build}"
export XDG_DATA_HOME="$EVIDENCE/xdg/data" XDG_CONFIG_HOME="$EVIDENCE/xdg/config" XDG_CACHE_HOME="$EVIDENCE/xdg/cache" XDG_RUNTIME_DIR="$EVIDENCE/xdg/runtime"
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
export COLOSSEUM_APPDATA_TAG=collections-fixture-sharing-runtime
export COLOSSEUM_COLLECTIONS_TEST_FIXTURES=1 COLOSSEUM_COLLECTIONS_RUNTIME_RUN=first
export COLOSSEUM_LANISTA_DRIVE=1 COLOSSEUM_LANISTA_PIPE=CollectionsIntegration-sharing-runtime
export COLOSSEUM_ACCOUNT_SERVICE_URL=http://127.0.0.1:55440
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QSG_RHI_BACKEND=software QML_DISABLE_DISK_CACHE=1
export COLOSSEUM_DISABLE_BOOT_PREFETCH=1 COLOSSEUM_AUTOMATION_FOCUS_SEAMS=0
ROOT="$XDG_DATA_HOME/Brotherhood/Colosseum-dltest-$COLOSSEUM_APPDATA_TAG"
mkdir -p "$ROOT/runtime-first"
printf 'collections-synthetic-fixture-v1\n' > "$ROOT/.collections-synthetic-fixture"
printf '%s\n' "$ROOT" > "$EVIDENCE/native-root.txt"
sha256sum "$BUILD/colosseum" > "$EVIDENCE/native-executable.sha256"
cd "$REPO"
for PHASE in first offline owner; do
    export COLOSSEUM_COLLECTIONS_RUNTIME_RUN="$PHASE"
    mkdir -p "$ROOT/runtime-$PHASE"
    if [[ "$PHASE" == offline ]]; then
        # No receiving-service access during restart qualification. This does not
        # claim a system-wide network outage or provider playback test.
        export COLOSSEUM_ACCOUNT_SERVICE_URL=http://127.0.0.1:55441
    else
        export COLOSSEUM_ACCOUNT_SERVICE_URL=http://127.0.0.1:55440
    fi
    printf '%s\n' "$PHASE" > "$EVIDENCE/active-native-phase.txt"
    "$BUILD/colosseum" > "$EVIDENCE/native-$PHASE.stdout.log" 2> "$EVIDENCE/native-$PHASE.stderr.log"
    printf '%s\n' "$?" > "$EVIDENCE/native-$PHASE-exit-code.txt"
done
