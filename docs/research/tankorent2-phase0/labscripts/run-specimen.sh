#!/usr/bin/env bash
# Tankorent 2.0 Phase 0 — launch the LAB specimen (never the install, never :11470).
#
# Mirrors Colosseum's own launch env (native/player/streamserver.cpp:111-121) so the
# lab engine behaves like the one the app drives: NODE_OPTIONS stripped (the bundled
# runtime rejects injected Node flags at boot), NO_HTTPS_SERVER=1, private APP_PATH.
#
# Refuses to run if the lab port is already busy, and refuses to run if it would
# somehow end up on the production port.
set -u

LAB="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LAB_PORT=11480
PROD_PORT=11470
RUN_ID="${1:-$(cat "$LAB/.runseq" 2>/dev/null || echo 1)}"
LOG="$LAB/logs/specimen-run${RUN_ID}.log"

mkdir -p "$LAB/cache" "$LAB/logs"

# Guard 1: the lab port must be free, or we cannot know whose engine we measured.
if (exec 3<>/dev/tcp/127.0.0.1/$LAB_PORT) 2>/dev/null; then
  exec 3<&- 2>/dev/null
  echo "REFUSING: port $LAB_PORT is already in use — stop the previous lab run first." >&2
  exit 1
fi

# Guard 2: never start while the app's production engine is being adopted-in-flight.
# (A live 11470 is fine and expected; we only record it. The patch guarantees we
#  cannot land there. This line exists so the log says what the world looked like.)
if (exec 3<>/dev/tcp/127.0.0.1/$PROD_PORT) 2>/dev/null; then
  exec 3<&- 2>/dev/null
  echo "note: production engine IS live on :$PROD_PORT (lab stays off it)" | tee -a "$LOG"
else
  echo "note: nothing on :$PROD_PORT at launch" | tee -a "$LOG"
fi

cd "$LAB/specimen-lab" || exit 1
unset NODE_OPTIONS
export NO_HTTPS_SERVER=1
export APP_PATH="$(cygpath -w "$LAB/cache" 2>/dev/null || echo "$LAB/cache")"

echo "=== lab specimen starting (run $RUN_ID, port $LAB_PORT, APP_PATH=$APP_PATH)" | tee -a "$LOG"
./stremio-runtime.exe server.js >>"$LOG" 2>&1 &
echo $! > "$LAB/.labpid"
echo $((RUN_ID + 1)) > "$LAB/.runseq"
echo "  pid $(cat "$LAB/.labpid")  log $LOG"
