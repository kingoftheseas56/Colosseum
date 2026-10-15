#!/usr/bin/env bash
# drop_ruler.sh — a trustworthy ruler for dropped frames.
#
# WHY THIS EXISTS (2026-07-29). Every stutter conclusion on this project so far was drawn from a
# SINGLE 60-second run. On this laptop that is not a measurement. Two runs of the IDENTICAL mpv
# configuration (same file, same scene, same window, same decode) produced 13 and 400 dropped
# frames. The noise is larger than every effect anyone has claimed to see, so the claims were
# noise-reading: the Continue-row cascade, the "mpv drops 1 per 5 minutes" floor, the copy-back
# structural theory, and the interpolation theory ALL died on re-measurement.
#
# WHAT IT IS, plainly: run each setup several times, alternating between them, and print the
# middle number and the spread. That is the whole idea. No statistics beyond a median and a range.
#
# THE THREE RULES IT ENFORCES (each one is a mistake we actually made):
#   1. INTERLEAVE + REVERSE. Arms run A,B then B,A then A,B... Running A always before B on a
#      fanless laptop measures thermal state, not the setting. Our first A/B did exactly that.
#   2. QUIET MACHINE. Refuses to start if a compiler/build is running. Several of today's runs
#      were racing my own builds.
#   3. SPREAD, NOT A POINT. Prints every value plus median/min/max. A difference only counts if
#      the ranges DO NOT OVERLAP. Every "finding" today would have failed that test.
#
# Usage:  bash tests/drop_ruler.sh [rounds]     (default 5 rounds = 5 samples per arm)
#
# Add or edit arms in the run_arm() case block below. Each arm must echo a single integer:
# the number of OUTPUT (VO) dropped frames observed over the measure window.

set -u

ROUNDS="${1:-5}"
CLIP="${COLOSSEUM_PROBE_CLIP:-}"
START="00:06:00"        # a real action scene: the opening credits are low-motion and flatter every arm
MEASURE=60              # seconds of measured playback
WARMUP=15               # seconds before counting, so startup cost never lands in the number
COOLDOWN=15             # seconds idle between runs, so heat does not carry from one arm to the next
MPV="/c/tools/mpv/mpv.exe"
APP_ROOT="${COLOSSEUM_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
[ -n "$CLIP" ] || { echo "Set COLOSSEUM_PROBE_CLIP to a local media file." >&2; exit 2; }
OUT="$APP_ROOT/tests/ruler-out"
mkdir -p "$OUT"

QT_BIN="/c/Qt/6.11.1/msvc2022_64/bin"
RUNTIME_PATH="$QT_BIN:/c/tools/mpvqt-feasibility/mpvqt-msvc-install/bin:/c/tools/mpvqt-feasibility/libmpv-prefix/bin:/c/tools/ffmpeg-master-latest-win64-gpl-shared/bin"

# ---- rule 2: refuse to measure on a busy machine ------------------------------------------------
busy=$(powershell -NoProfile -Command "(Get-Process cl,link,ninja,cmake,MSBuild -ErrorAction SilentlyContinue | Measure-Object).Count" 2>/dev/null | tr -d '\r')
if [ "${busy:-0}" != "0" ]; then
    echo "REFUSING TO RUN: a build is in progress ($busy compiler processes)."
    echo "Measurements taken against a running build are worthless. Wait, then re-run."
    exit 1
fi

# ---- the arms ------------------------------------------------------------------------------------
# Each prints ONE integer: output/VO drops over the measure window.

mpv_run() {   # mpv_run <label> <extra mpv flags...>
    local label="$1"; shift
    local log="$OUT/${label}-$(date +%s%N 2>/dev/null || echo run).log"
    "$MPV" --no-config --geometry=1920x1080 \
        --start="$START" --length=$((WARMUP + MEASURE)) \
        --vo=gpu-next --term-status-msg='STAT t=${=time-pos} vodrop=${frame-drop-count}' \
        "$@" "$CLIP" > "$log" 2>&1
    # Count only what happened AFTER the warm-up: the counter at the end minus the counter at the
    # warm-up mark. Startup drops are real but they are not what we are chasing.
    local warm_mark
    warm_mark=$(awk -F'[= ]' -v w="$WARMUP" '/STAT t=/{ for(i=1;i<=NF;i++) if($i=="t") t=$(i+1)+0; for(i=1;i<=NF;i++) if($i=="vodrop") d=$(i+1)+0; if (t>=360+w && s=="") { print d; s="done"; exit } }' "$log")
    local final
    final=$(grep -o 'vodrop=[0-9]*' "$log" | tail -1 | cut -d= -f2)
    echo $(( ${final:-0} - ${warm_mark:-0} ))
}

app_run() {
    local log="$OUT/app-$(date +%s%N 2>/dev/null || echo run).log"
    (
        export PATH="$RUNTIME_PATH:$PATH"
        export QTFRAMEWORK_BYPASS_LICENSE_CHECK=1
        export QT_FORCE_STDERR_LOGGING=1
        export COLOSSEUM_MPV=1
        export COLOSSEUM_ABBA_CLIP="$CLIP"
        export COLOSSEUM_MPV_DROP_PROBE="$WARMUP,$MEASURE"
        cd "$APP_ROOT" && ./native/build-msvc/colosseum.exe qml/Main.qml
    ) > "$log" 2>&1
    # The in-app probe prints: MPV_DROP_PROBE RESULT {"outputStart":N,"outputEnd":M,...}
    local json
    json=$(grep -o 'MPV_DROP_PROBE RESULT[[:space:]]*{[^}]*}' "$log" | tail -1)
    if [ -z "$json" ]; then echo "NA"; return; fi
    local s e
    s=$(echo "$json" | grep -o '"outputStart":[0-9]*' | cut -d: -f2)
    e=$(echo "$json" | grep -o '"outputEnd":[0-9]*' | cut -d: -f2)
    if [ -z "$s" ] || [ -z "$e" ]; then echo "NA"; return; fi
    echo $(( e - s ))
}

run_arm() {
    case "$1" in
        mpv-plain)  mpv_run mpv-plain  --hwdec=d3d11va-copy --gpu-api=opengl --video-sync=audio --interpolation=no ;;
        mpv-interp) mpv_run mpv-interp --hwdec=d3d11va-copy --gpu-api=opengl --video-sync=display-resample --interpolation=yes ;;
        app)        app_run ;;
        *)          echo "NA" ;;
    esac
}

ARMS=(mpv-plain mpv-interp app)
declare -A RESULTS
for a in "${ARMS[@]}"; do RESULTS[$a]=""; done

echo "drop ruler — $ROUNDS rounds, ${MEASURE}s measured per run (after ${WARMUP}s warm-up), scene $START"
echo "arms: ${ARMS[*]}"
echo

for r in $(seq 1 "$ROUNDS"); do
    # rule 1: reverse the order every other round so run-order/heat cancels out
    order=("${ARMS[@]}")
    if [ $((r % 2)) -eq 0 ]; then
        rev=(); for ((i=${#ARMS[@]}-1; i>=0; i--)); do rev+=("${ARMS[$i]}"); done; order=("${rev[@]}")
    fi
    for a in "${order[@]}"; do
        v=$(run_arm "$a")
        RESULTS[$a]="${RESULTS[$a]} $v"
        printf "  round %s  %-11s -> %s\n" "$r" "$a" "$v"
        sleep "$COOLDOWN"
    done
done

echo
echo "================ RESULT (output/VO drops per ${MEASURE}s) ================"
for a in "${ARMS[@]}"; do
    vals=$(echo "${RESULTS[$a]}" | tr ' ' '\n' | grep -E '^[0-9]+$' | sort -n)
    n=$(echo "$vals" | grep -c .)
    if [ "$n" -eq 0 ]; then printf "%-11s  no valid samples\n" "$a"; continue; fi
    min=$(echo "$vals" | head -1)
    max=$(echo "$vals" | tail -1)
    med=$(echo "$vals" | awk -v n="$n" 'NR==int((n+1)/2){print}')
    printf "%-11s  median %-6s range %s..%s   samples: %s\n" "$a" "$med" "$min" "$max" "$(echo ${RESULTS[$a]})"
done
echo
echo "READ IT THIS WAY: a difference between two arms is only real if their ranges DO NOT overlap."
echo "If the ranges overlap, the arms are indistinguishable on this machine — say so, do not pick"
echo "the flattering number. That mistake is what produced every wrong conclusion on 2026-07-29."
