#!/usr/bin/env bash
# Phase 0 Slice 5 — the experiment battery.
#
# Each trial gets a TRUE cold start: engine stopped, cache wiped, engine restarted.
# Without the wipe, run 2 would read run 1's pieces off disk and report a fantasy
# cold-open. House law: one lucky measurement is not a baseline, so every trial
# repeats and we report spread, never a single figure.
set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
LAB="$REPO/native/build-msvc/_t2lab"
SCRIPTS="$SCRIPT_DIR"
OUT="$LAB/logs/slice5"
mkdir -p "$OUT"

SETTINGS_PROD='{"btMaxConnections":200,"btDownloadSpeedSoftLimit":20971520,"btDownloadSpeedHardLimit":41943040}'

cold_start() {
  bash "$LAB/stop-specimen.sh" >/dev/null 2>&1
  sleep 1
  rm -rf "$LAB/cache"; mkdir -p "$LAB/cache"     # TRUE cold: no cached pieces
  bash "$LAB/run-specimen.sh" >/dev/null 2>&1
  for i in $(seq 1 30); do
    sleep 1
    curl -s -m 2 -o /dev/null http://127.0.0.1:11480/settings && return 0
  done
  echo "FAIL: lab did not come up" >&2; return 1
}

trial() {   # trial <tag> <hash> <idx> <secs> <settings-json>
  local tag="$1" hash="$2" idx="$3" secs="$4" settings="$5"
  echo "### $tag"
  cold_start || return 1
  python "$SCRIPTS/watch_run.py" --port 11480 --hash "$hash" --idx "$idx" \
    --secs "$secs" --tag "$tag" --outdir "$OUT" --settings "$settings" \
    2>&1 | grep -E "RESULT_|COLD OPEN|effective|peak KNOWN|peak connected|create ->"
  echo
}

SINTEL=08ada5a7a6183aae1e09d831df6748d566095a10
BBB=dd8255ecdc7ca55fb0bbf81323d87062db1f6d1c

case "${1:-all}" in
  cold)
    for n in 1 2 3; do trial "cold-sintel-$n" "$SINTEL" 5 45 "$SETTINGS_PROD"; done
    ;;
  cold2)
    for n in 1 2 3; do trial "cold-bbb-$n" "$BBB" 1 45 "$SETTINGS_PROD"; done
    ;;
  degraded)
    # NEGATIVE CONTROL: deliberately hobble the engine. If the numbers do NOT
    # get worse, the rig is not measuring what we think and Slice 5 is void.
    trial "degraded-cap5" "$SINTEL" 5 45 '{"btMaxConnections":5,"btDownloadSpeedSoftLimit":20971520,"btDownloadSpeedHardLimit":41943040}'
    ;;
  sustained)
    trial "sustained-bbb" "$BBB" 1 180 "$SETTINGS_PROD"
    ;;
esac
