#!/bin/sh
# smoke.sh — outer-loop gate for the Rust daemon (crates/daemon).
#
# Boots the daemon on a free port (exported as DAEMON_PORT), then walks the
# full user-facing surface, asserting on response bodies as it goes:
#   1. healthz      GET  /healthz              -> 200 "ok"
#   2. readyz       GET  /readyz               -> 200 "ok"
#   3. search       GET  /catalog/search?q=alpha -> Demo Series Alpha present
#   4. create       POST /v1/accounts          -> 200 session (tokens captured)
#   5. duplicate    POST /v1/accounts (same)   -> 409 username_unavailable
#   6. sign-in      POST /v1/sessions          -> 200 (fresh session)
#   7. refresh      POST /v1/sessions/refresh  -> 200 with a NEW access_token
#   8. replay       POST /v1/sessions/refresh  -> 401 session_invalid
#
# Any leg that fails prints `SMOKE FAIL: <reason>` (plus the daemon log tail)
# and exits nonzero. Success prints the leg trace and closes with SMOKE_OK.
#
# Constraints: POSIX sh only (runs under bash 3.2 /bin/sh — no mapfile, no
# associative arrays, no $RANDOM); no deps beyond curl + python3. The account
# store is in-memory per daemon process, so each run uses a random username
# (date + pid) so repeated runs never collide on the create leg.
set -eu

# ---- leg 0: free port, then boot the daemon ---------------------------------
PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')
export DAEMON_PORT=$PORT
BASE="http://127.0.0.1:$PORT"

TMP=$(mktemp -d "${TMPDIR:-/tmp}/colosseum-smoke.XXXXXX") || { echo "SMOKE FAIL: mktemp failed" >&2; exit 1; }
DAEMON_LOG="$TMP/daemon.log"

# Detach the daemon inside a subshell so it is not a tracked job of this
# shell: bash would otherwise print a "Terminated" job notice to stderr when
# the EXIT trap reaps it. Killing the recorded cargo PID takes the daemon
# child down with it (cargo forwards termination).
(
    cargo run -p daemon --quiet >"$DAEMON_LOG" 2>&1 &
    echo $! >"$TMP/daemon.pid"
)
DAEMON_PID=$(cat "$TMP/daemon.pid")
trap 'kill "$DAEMON_PID" 2>/dev/null; rm -rf "$TMP"' EXIT

# fail <message> — print to stderr with the daemon log tail, exit nonzero.
fail() {
    echo "SMOKE FAIL: $*" >&2
    if [ -n "${DAEMON_LOG:-}" ] && [ -s "$DAEMON_LOG" ]; then
        echo "--- daemon log (tail) ---" >&2
        tail -n 20 "$DAEMON_LOG" >&2 2>/dev/null || true
    fi
    exit 1
}

# json_field <file> <key> — pull a top-level string field out of a JSON body.
json_field() {
    python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))[sys.argv[2]])' "$1" "$2"
}

# ---- leg 1: /healthz up to 200 ---------------------------------------------
echo "[1/8] healthz"
i=0
code=
while kill -0 "$DAEMON_PID" 2>/dev/null; do
    code=$(curl -sS -o /dev/null -w '%{http_code}' "$BASE/healthz" 2>/dev/null) || true
    [ "$code" = 200 ] && break
    i=$((i + 1))
    [ "$i" -ge 500 ] && fail "daemon never became healthy after 100s"
    sleep 0.2
done
[ "$code" = 200 ] || fail "daemon exited before /healthz returned 200"
[ "$(curl -sS "$BASE/healthz")" = "ok" ] || fail "/healthz body was not 'ok'"
echo "      200 ok (port $PORT)"

# ---- leg 2: /readyz ---------------------------------------------------------
echo "[2/8] readyz"
code=$(curl -sS -o "$TMP/readyz.body" -w '%{http_code}' "$BASE/readyz") || fail "/readyz request failed"
[ "$code" = 200 ] || fail "/readyz expected 200, got $code: $(cat "$TMP/readyz.body" 2>/dev/null)"
[ "$(cat "$TMP/readyz.body")" = "ok" ] || fail "/readyz body was not 'ok': $(cat "$TMP/readyz.body")"
echo "      200 ok"

# ---- leg 3: catalog search --------------------------------------------------
echo "[3/8] search"
code=$(curl -sS -o "$TMP/search.body" -w '%{http_code}' "$BASE/catalog/search?q=alpha") || fail "/catalog/search request failed"
[ "$code" = 200 ] || fail "/catalog/search expected 200, got $code"
grep -q 'Demo Series Alpha' "$TMP/search.body" || fail "search body missing 'Demo Series Alpha': $(cat "$TMP/search.body")"
grep -q '"source":"demo"' "$TMP/search.body" || fail "search body missing demo source: $(cat "$TMP/search.body")"
echo "      200: Demo Series Alpha"

# ---- legs 4-8: account create / sign-in / refresh ---------------------------
USERNAME="smoke$(date +%s)$$"
PASSWORD="correct-horse-style"
INSTALL_ID="install-$(date +%s)$$"
printf '{"username":"%s","password":"%s","device_install_id":"%s","device_label":"Smoke Runner","platform":"POSIX"}' \
    "$USERNAME" "$PASSWORD" "$INSTALL_ID" >"$TMP/account.json"
JSON_CT='Content-Type: application/json'

# ---- leg 4: create account --------------------------------------------------
echo "[4/8] create"
code=$(curl -sS -o "$TMP/create.body" -w '%{http_code}' -H "$JSON_CT" \
    --data-binary @"$TMP/account.json" "$BASE/v1/accounts") || fail "account create request failed"
[ "$code" = 200 ] || fail "account create expected 200, got $code: $(cat "$TMP/create.body" 2>/dev/null)"
grep -q '"access_token":"' "$TMP/create.body" || fail "create response missing access_token: $(cat "$TMP/create.body")"
grep -q '"refresh_token":"' "$TMP/create.body" || fail "create response missing refresh_token: $(cat "$TMP/create.body")"
[ "$(json_field "$TMP/create.body" access_token)" = "$(json_field "$TMP/create.body" refresh_token)" ] \
    && fail "create issued identical access/refresh tokens"
[ "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["account"]["username"])' "$TMP/create.body")" = "$USERNAME" ] \
    || fail "create response username does not match request"
echo "      200: session issued for $USERNAME"

# ---- leg 5: duplicate create is a 409 --------------------------------------
echo "[5/8] duplicate create"
code=$(curl -sS -o "$TMP/dup.body" -w '%{http_code}' -H "$JSON_CT" \
    --data-binary @"$TMP/account.json" "$BASE/v1/accounts") || fail "duplicate create request failed"
[ "$code" = 409 ] || fail "duplicate create expected 409, got $code: $(cat "$TMP/dup.body" 2>/dev/null)"
grep -q 'username_unavailable' "$TMP/dup.body" || fail "duplicate create envelope missing username_unavailable: $(cat "$TMP/dup.body")"
echo "      409: username_unavailable"

# ---- leg 6: sign in ---------------------------------------------------------
echo "[6/8] sign-in"
code=$(curl -sS -o "$TMP/signin.body" -w '%{http_code}' -H "$JSON_CT" \
    --data-binary @"$TMP/account.json" "$BASE/v1/sessions") || fail "sign-in request failed"
[ "$code" = 200 ] || fail "sign-in expected 200, got $code: $(cat "$TMP/signin.body" 2>/dev/null)"
ACCESS_SIGNIN=$(json_field "$TMP/signin.body" access_token)
REFRESH_SIGNIN=$(json_field "$TMP/signin.body" refresh_token)
[ -n "$ACCESS_SIGNIN" ] && [ -n "$REFRESH_SIGNIN" ] || fail "sign-in response missing tokens: $(cat "$TMP/signin.body")"
echo "      200"

# ---- leg 7: refresh rotates the access token --------------------------------
echo "[7/8] refresh"
printf '{"refresh_token":"%s"}' "$REFRESH_SIGNIN" >"$TMP/refresh.json"
code=$(curl -sS -o "$TMP/refresh.body" -w '%{http_code}' -H "$JSON_CT" \
    --data-binary @"$TMP/refresh.json" "$BASE/v1/sessions/refresh") || fail "refresh request failed"
[ "$code" = 200 ] || fail "refresh expected 200, got $code: $(cat "$TMP/refresh.body" 2>/dev/null)"
ACCESS_REFRESH=$(json_field "$TMP/refresh.body" access_token)
[ -n "$ACCESS_REFRESH" ] || fail "refresh response missing access_token: $(cat "$TMP/refresh.body")"
[ "$ACCESS_REFRESH" = "$ACCESS_SIGNIN" ] && fail "refresh did not rotate the access token"
echo "      200: access token rotated"

# ---- leg 8: replaying the consumed refresh token is a 401 -------------------
echo "[8/8] refresh replay"
code=$(curl -sS -o "$TMP/replay.body" -w '%{http_code}' -H "$JSON_CT" \
    --data-binary @"$TMP/refresh.json" "$BASE/v1/sessions/refresh") || fail "refresh replay request failed"
[ "$code" = 401 ] || fail "refresh replay expected 401, got $code: $(cat "$TMP/replay.body" 2>/dev/null)"
grep -q 'session_invalid' "$TMP/replay.body" || fail "refresh replay envelope missing session_invalid: $(cat "$TMP/replay.body")"
echo "      401: session_invalid"

echo "SMOKE_OK"
