#!/bin/sh
# Boot the daemon on a free port, hit both endpoints, exit nonzero on failure.
set -e

PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')
DAEMON_PORT="$PORT" cargo run -p daemon --quiet &
DAEMON_PID=$!
trap 'kill $DAEMON_PID 2>/dev/null' EXIT

i=0
until curl -sf "http://127.0.0.1:$PORT/health" >/dev/null 2>&1; do
  i=$((i+1))
  [ "$i" -gt 50 ] && { echo "daemon never became healthy"; exit 1; }
  sleep 0.2
done

echo "health: $(curl -sf "http://127.0.0.1:$PORT/health")"
BODY=$(curl -sf "http://127.0.0.1:$PORT/catalog/search?q=alpha")
echo "search: $BODY"
echo "$BODY" | grep -q "Demo Series Alpha" || { echo "search body missing expected series"; exit 1; }
echo "SMOKE_OK"
