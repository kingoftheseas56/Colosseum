#!/usr/bin/env bash
# wid_ab.sh — interleaved A/B: Player 1 (render API) vs Player 1.5 (native wid window).
#
# Doctrine: tests/drop_ruler.sh (2026-07-29) — interleave + reverse the order each round,
# refuse to run against a build, report every value + median + range, and a difference only
# counts if the ranges do not overlap. Arms differ ONLY by COLOSSEUM_MPV_WID: the baseline
# arm runs the SAME worktree binary WITHOUT the env, so build/runtime drift is excluded.
#
# Usage:  bash tests/wid_ab.sh [rounds]        (default 4 rounds = 4 samples per arm)
# Needs:  COLOSSEUM_PROBE_CLIP=<local media file>, built native/build-msvc.

set -u
ROUNDS="${1:-4}"
WARMUP=10
MEASURE=60
COOLDOWN=15
CLIP="${COLOSSEUM_PROBE_CLIP:-}"
WT="${COLOSSEUM_WID_WORKTREE:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
APP="$WT/native/build-msvc/colosseum.exe"
[ -n "$CLIP" ] || { echo "Set COLOSSEUM_PROBE_CLIP to a local media file." >&2; exit 2; }
[ -x "$APP" ] || { echo "Missing $APP — build the worktree first." >&2; exit 2; }
OUT="$WT/tests/wid-ab-out"
mkdir -p "$OUT"

busy=$(powershell -NoProfile -Command "(Get-Process cl,link,ninja,cmake,MSBuild -ErrorAction SilentlyContinue | Measure-Object).Count" 2>/dev/null | tr -d '\r')
if [ "${busy:-0}" != "0" ]; then
    echo "REFUSING TO RUN: a build is in progress ($busy processes)."; exit 1
fi

run_arm() {   # run_arm <label> <wid:0|1> <round>
    local label="$1" wid="$2" round="$3"
    local log="$OUT/${label}-r${round}.log"
    local wlog wout wwt
    wlog=$(cygpath -w "$log"); wout=$(cygpath -w "$log.out"); wwt=$(cygpath -w "$WT")
    (
        export PATH="/c/Qt/6.11.1/msvc2022_64/bin:/c/tools/mpvqt-feasibility/mpvqt-msvc-install/bin:/c/tools/mpvqt-feasibility/libmpv-prefix/bin:/c/tools/ffmpeg-master-latest-win64-gpl-shared/bin:$PATH"
        export QTFRAMEWORK_BYPASS_LICENSE_CHECK=1
        export QT_FORCE_STDERR_LOGGING=1
        export COLOSSEUM_MPV=1
        export COLOSSEUM_APPDATA_TAG="widab"
        export COLOSSEUM_ABBA_CLIP="$CLIP"
        export COLOSSEUM_MPV_DROP_PROBE="$WARMUP,$MEASURE"
        export COLOSSEUM_GUI_STALL_PROBE=30
        export QT_LOGGING_RULES="qt.scenegraph.time.renderloop=true"
        if [ "$wid" = "1" ]; then export COLOSSEUM_MPV_WID=1; else unset COLOSSEUM_MPV_WID; fi
        powershell -NoProfile -Command "\$p = Start-Process -FilePath '$APP' -ArgumentList 'qml/Main.qml' -RedirectStandardError '$wlog' -RedirectStandardOutput '$wout' -PassThru -WorkingDirectory '$wwt'; Write-Output \$p.Id" > "$OUT/pid.tmp"
    )
    local appid
    appid=$(tr -d '\r' < "$OUT/pid.tmp")
    sleep $((WARMUP + MEASURE + 12))
    powershell -NoProfile -Command "\$p = Get-Process -Id $appid -ErrorAction SilentlyContinue; if (\$p) { \$null = \$p.CloseMainWindow(); Start-Sleep 5; if (Get-Process -Id $appid -ErrorAction SilentlyContinue) { Stop-Process -Id $appid -Force } }"
    sleep "$COOLDOWN"
    local result over50
    result=$(grep -o 'MPV_DROP_PROBE RESULT[^}]*}' "$log" | tail -1)
    over50=$(grep -oE "frame rendered in [0-9]+ms" "$log" | grep -oE "[0-9]+" | awk '$1>50{c++} END{print c+0}')
    echo "RESULT $label r$round :: $result framesOver50ms=$over50"
}

echo "wid A/B: $ROUNDS rounds, warmup=${WARMUP}s measure=${MEASURE}s, clip=$CLIP"
for r in $(seq 1 "$ROUNDS"); do
    if [ $((r % 2)) -eq 1 ]; then order="p1 wid"; else order="wid p1"; fi   # reverse each round
    for arm in $order; do
        if [ "$arm" = "p1" ]; then run_arm p1 0 "$r"; else run_arm wid 1 "$r"; fi
    done
done

echo
echo "=================== SUMMARY (drops over the ${MEASURE}s window) ==================="
for arm in p1 wid; do
    grep -h "^RESULT $arm " "$OUT/../wid-ab-out/"*.log 2>/dev/null >/dev/null   # no-op; summary reads stdout below
done
python - "$OUT" <<'EOF'
import re, sys, statistics, pathlib
out = pathlib.Path(sys.argv[1])
rows = {"p1": [], "wid": []}
for log in sorted(out.glob("*.log")):
    m = re.match(r"(p1|wid)-r(\d+)\.log", log.name)
    if not m:
        continue
    text = log.read_text(encoding="utf-8", errors="replace")
    r = re.findall(r"MPV_DROP_PROBE RESULT \{\"outputStart\":(\d+),\"outputEnd\":(\d+)\}", text)
    if r:
        start, end = map(int, r[-1])
        rows[m.group(1)].append(end - start)
for arm in ("p1", "wid"):
    v = rows[arm]
    if v:
        print(f"{arm:>4}: values={sorted(v)} median={statistics.median(v)} min={min(v)} max={max(v)} n={len(v)}")
    else:
        print(f"{arm:>4}: NO SAMPLES")
EOF
