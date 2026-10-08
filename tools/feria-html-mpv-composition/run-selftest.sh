#!/usr/bin/env bash
# Run on a real desktop session. Offscreen metadata is not picture evidence.
set -euo pipefail
if [[ $# != 4 ]]; then
  echo "usage: $0 ENV_FILE_OR_DASH BUILD_DIR FIXTURE_DIR EVIDENCE_DIR" >&2
  exit 64
fi
ENV_FILE=$1; BUILD_DIR=$(realpath "$2"); FIXTURES=$(realpath "$3")
mkdir -p "$4"; EVIDENCE=$(realpath "$4")
if [[ $ENV_FILE != - ]]; then source "$ENV_FILE"; fi
if [[ -z ${DISPLAY:-} && -z ${WAYLAND_DISPLAY:-} ]]; then
  echo 'A connected graphical desktop session is required.' >&2; exit 69
fi
export HOME="$EVIDENCE/runtime/home"
export XDG_CACHE_HOME="$EVIDENCE/runtime/cache"
export XDG_DATA_HOME="$EVIDENCE/runtime/data"
export XDG_RUNTIME_DIR="$EVIDENCE/runtime/run"
mkdir -p "$HOME" "$XDG_CACHE_HOME" "$XDG_DATA_HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
"$BUILD_DIR/session_tests" | tee "$EVIDENCE/session-tests.log"
set +e
"$BUILD_DIR/feria_html_mpv" --fixtures "$FIXTURES" --evidence "$EVIDENCE" --self-test 2>&1 | tee "$EVIDENCE/runtime.log"
status=${PIPESTATUS[0]}
set -e
printf '%s\n' "$status" > "$EVIDENCE/exit-code.txt"
python3 "$(dirname "$0")/verify_pixels.py" "$EVIDENCE" | tee "$EVIDENCE/pixel-checks.log"
exit "$status"
