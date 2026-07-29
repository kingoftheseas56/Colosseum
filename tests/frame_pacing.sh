#!/usr/bin/env bash
# frame_pacing.sh — measure what the EYE sees, not what mpv counts.
#
# WHY (2026-07-29, after Hemanth's report). Hemanth watched plain mpv: visibly smooth. He watches
# Colosseum: visibly stuttering. But tests/drop_ruler.sh proved mpv's dropped-frame counter cannot
# tell the two apart (both swing 0..120 per minute on identical runs). Both observations can be
# true at once, and if they are, the counter is measuring the wrong quantity.
#
# The reason: inside Colosseum, mpv does NOT control when a frame reaches the screen — Qt's scene
# graph does. mpv renders into Qt's framebuffer when Qt asks it to. If QT is late presenting, mpv
# has done its job and counts NO drop, while the viewer sees a hitch. Every number collected today
# was blind to exactly that failure.
#
# WHAT IT MEASURES, plainly: how long each frame took to reach the screen. On a 59 Hz display a
# frame is due every ~17 ms. A frame that takes 50 or 100 ms IS the stutter — one visible hitch.
#
# THE READING: the median tells you nothing interesting (it will look fine). The TAIL is the
# stutter. Count the frames over 33 ms (a doubled frame), over 50 ms, over 100 ms. Those are the
# hitches the eye registers, and their COUNT and CLUSTERING is the thing to drive to zero.
#
# Usage: bash tests/frame_pacing.sh [seconds]     (default 60)

set -u
SECS="${1:-60}"
CLIP="C:\\Users\\Suprabha\\Downloads\\Colosseum\\Tenet - 20260726_184029.mp4"
APP_ROOT="C:/Users/Suprabha/Desktop/Brotherhood/Colosseum/.worktrees/player2-chrome-port"
OUT="$APP_ROOT/tests/pacing-out"
mkdir -p "$OUT"
LOG="$OUT/pacing.log"

busy=$(powershell -NoProfile -Command "(Get-Process cl,link,ninja,cmake,MSBuild -ErrorAction SilentlyContinue | Measure-Object).Count" 2>/dev/null | tr -d '\r')
if [ "${busy:-0}" != "0" ]; then
    echo "REFUSING TO RUN: a build is in progress ($busy processes)."; exit 1
fi

echo "recording frame pacing for ${SECS}s of playback (Colosseum, mpv player)..."
(
    export PATH="/c/Qt/6.11.1/msvc2022_64/bin:/c/tools/mpvqt-feasibility/mpvqt-msvc-install/bin:/c/tools/mpvqt-feasibility/libmpv-prefix/bin:/c/tools/ffmpeg-master-latest-win64-gpl-shared/bin:$PATH"
    export QTFRAMEWORK_BYPASS_LICENSE_CHECK=1
    export QT_FORCE_STDERR_LOGGING=1
    export COLOSSEUM_MPV=1
    export COLOSSEUM_ABBA_CLIP="$CLIP"
    export COLOSSEUM_MPV_DROP_PROBE="10,$SECS"
    # Qt's own per-frame timing. renderloop = the whole frame (sync + render + swap), which is
    # what actually reaches the screen. renderer = the drawing part only.
    export QT_LOGGING_RULES="qt.scenegraph.time.renderloop=true;qt.scenegraph.time.renderer=true"
    cd "$APP_ROOT" && ./native/build-msvc/colosseum.exe qml/Main.qml
) > "$LOG" 2>&1

echo
echo "=================== FRAME PACING (Colosseum) ==================="
# Qt prints lines like:
#   qt.scenegraph.time.renderloop: Frame rendered with 'threaded' renderloop in 18ms, sync=1, render=15, swap=2 ...
grep -oE "renderloop in [0-9]+ms" "$LOG" | grep -oE "[0-9]+" > "$OUT/frametimes.txt" 2>/dev/null
if [ ! -s "$OUT/frametimes.txt" ]; then
    grep -oE "time in renderer: total=[0-9]+" "$LOG" | grep -oE "[0-9]+$" > "$OUT/frametimes.txt" 2>/dev/null
    echo "(renderloop timing unavailable — using renderer time instead; this UNDER-reports, it"
    echo " excludes waiting for the screen. Treat the tail counts as a floor, not a total.)"
fi

n=$(grep -c . "$OUT/frametimes.txt" 2>/dev/null || echo 0)
if [ "${n:-0}" -eq 0 ]; then
    echo "NO FRAME TIMINGS CAPTURED. Qt logging categories may differ in this build."
    echo "Log kept at: $LOG"
    exit 2
fi

sort -n "$OUT/frametimes.txt" > "$OUT/sorted.txt"
med=$(awk -v n="$n" 'NR==int((n+1)/2){print; exit}' "$OUT/sorted.txt")
p99=$(awk -v n="$n" 'NR==int(n*0.99){print; exit}' "$OUT/sorted.txt")
mx=$(tail -1 "$OUT/sorted.txt")

echo "frames measured: $n"
echo "  median frame:        ${med}ms      (a 59Hz screen wants ~17ms)"
echo "  99th percentile:     ${p99}ms"
echo "  worst single frame:  ${mx}ms"
echo
echo "THE TAIL — these are the visible hitches:"
for t in 33 50 100 200; do
    c=$(awk -v t="$t" '$1>t' "$OUT/frametimes.txt" | grep -c . )
    printf "  frames over %4sms : %-6s" "$t" "$c"
    if [ "$n" -gt 0 ]; then awk -v c="$c" -v n="$n" 'BEGIN{printf "(%.2f%% of frames)\n", 100*c/n}'; else echo; fi
done
echo
echo "  a frame over 33ms  = the picture froze for at least two screen refreshes"
echo "  a frame over 100ms = a hitch you would describe as a stutter"
echo
echo "clustering (where the bad frames fall, in order):"
awk '$1>50 {printf "%s ", NR}' "$OUT/frametimes.txt" | head -c 600
echo
echo
echo "mpv's own opinion of the same run (for contrast):"
grep -o 'MPV_DROP_PROBE RESULT[^}]*}' "$LOG" | tail -1
echo
echo "Log: $LOG"
