#!/usr/bin/env bash
set -euo pipefail
: "${SOURCE:?}" "${OUT:?}" "${EVIDENCE:?}"
here=$(cd -- "$(dirname -- "$0")" && pwd)
exec > >(tee "$EVIDENCE/clean-runtime.log") 2>&1
trap 'rc=$?; printf "clean_runtime_exit=%s\n" "$rc" > "$EVIDENCE/clean-runtime-status.txt"' EXIT
archive="$OUT/Colosseum-1.1.7-linux-x86_64-candidate.tar.gz"
mkdir "$OUT/clean-extract" "$EVIDENCE/runtime"
(cd "$OUT" && sha256sum -c "$(basename "$archive").sha256")
tar -xzf "$archive" -C "$OUT/clean-extract"
appdir="$OUT/clean-extract/Colosseum-1.1.7-linux-x86_64"
python "$SOURCE/scripts/linux_runtime_dependency_gate.py" --appdir "$appdir"
# Long enough for real decoded-frame checks and a screenshot, not a network asset.
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=64x64:rate=24 \
  -t 120 -c:v libx264 -pix_fmt yuv420p "$OUT/playback-fixture.mp4"
docker build -f "$here/clean-runtime.Dockerfile" -t colosseum-117-runtime "$here"
docker image inspect colosseum-117-runtime > "$EVIDENCE/runtime-image.json"
# Only the extracted package, qualifier and fixture are mounted. No builder
# checkout, SDK, build directory, credentials or inherited environment enters.
printf '%s host: starting clean container\n' "$(date -u +%FT%TZ)" > "$EVIDENCE/runtime/startup.log"
sudo chown -R 1000:1000 "$EVIDENCE/runtime"
# Xvfb deliberately does not send its readiness SIGUSR1 to PID 1. Keep the
# xvfb-run wrapper below Docker's init; no capabilities or sandbox bypass needed.
docker run --rm --init -i --cap-drop=ALL --security-opt=no-new-privileges --shm-size=1g \
  --mount "type=bind,source=$appdir,target=/opt/colosseum,readonly" \
  --mount "type=bind,source=$here/qualify.py,target=/qualify.py,readonly" \
  --mount "type=bind,source=$OUT/playback-fixture.mp4,target=/fixture.mp4,readonly" \
  --mount "type=bind,source=$EVIDENCE/runtime,target=/evidence" \
  colosseum-117-runtime sh -s <<'CONTAINER'
set -eu
log() { printf '%s %s\n' "$(date -u +%FT%TZ)" "$*" | tee -a /evidence/startup.log; }
trap 'rc=$?; log "container: exit=$rc"' EXIT
log "container: entered uid=$(id -u) gid=$(id -g) pid=$$"
log 'xvfb: starting (startup limit 60s; full qualification limit 20m)'
timeout --kill-after=10s 20m xvfb-run -a -e /evidence/xvfb.log \
  -s '-screen 0 1280x900x24' python3 -u /qualify.py \
  --appdir /opt/colosseum --evidence /evidence --fixture /fixture.mp4 &
runner=$!
trap 'kill -TERM "$runner" 2>/dev/null || :; wait "$runner" || :; exit 143' TERM
trap 'kill -TERM "$runner" 2>/dev/null || :; wait "$runner" || :; exit 130' INT
for second in $(seq 1 60); do
  [ ! -f /evidence/qualification.json ] || break
  kill -0 "$runner" 2>/dev/null || break
  sleep 1
done
if [ ! -f /evidence/qualification.json ]; then
  log 'xvfb: qualifier did not initialize within 60s or runner exited; see xvfb.log'
  # Preserve startup process identities even if Xvfb never reaches Python.
  for status in /proc/[0-9]*/status; do
    [ ! -r "$status" ] || cat "$status"
  done > /evidence/startup-processes.txt
  kill -TERM "$runner" 2>/dev/null || :
  wait "$runner" || :
  exit 1
fi
log 'qualifier: initialized; see qualification.json and elf-closure.log'
wait "$runner"
CONTAINER
