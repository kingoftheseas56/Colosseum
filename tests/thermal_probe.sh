#!/usr/bin/env bash
# thermal_probe.sh — is the LAPTOP the cause, not the code?
#
# WHY (2026-07-29). tests/drop_ruler.sh showed that on this machine every playback setup —
# including PLAIN MPV with no Colosseum code anywhere near it — swings between 0 and 120 dropped
# frames per minute across identical runs. Nothing in our software can explain a swing that
# happens when our software is not running. The untested suspect is the hardware itself: a fanless
# Intel UHD 620 decoding 1080p HEVC 10-bit gets hot, and Windows quietly drops the clock when it
# does. That would explain the bursts, the lack of correlation with pause/play, and why every
# software theory evaporated on re-measurement.
#
# WHAT IT DOES, plainly: play the film for 75 seconds while watching how fast the processor is
# actually running, then print the two side by side, second by second.
#
# THE READING:
#   "% of nominal clock" is the throttle indicator. 100 = running at rated speed. Sustained
#   readings well under 100 while the film plays = the chip is being slowed down.
#   If the clock COLLAPSES in the same seconds the drops CLUSTER, the stutter is thermal and no
#   amount of code changes it. If the clock HOLDS while drops still cluster, thermal is ruled out
#   and we move to frame-timing instrumentation instead.
#
# Usage: bash tests/thermal_probe.sh [mpv|app]      (default: mpv — the reference player, so a
#                                                    positive result cannot be blamed on our code)

set -u
ARM="${1:-mpv}"
CLIP="C:\\Users\\Suprabha\\Downloads\\Colosseum\\Tenet - 20260726_184029.mp4"
START_SEC=360
SECONDS_TO_RUN=75
MPV="/c/tools/mpv/mpv.exe"
APP_ROOT="C:/Users/Suprabha/Desktop/Brotherhood/Colosseum/.worktrees/player2-chrome-port"
OUT="$APP_ROOT/tests/thermal-out"
mkdir -p "$OUT"
SAMPLES="$OUT/samples.csv"
PLAYLOG="$OUT/play-$ARM.log"

busy=$(powershell -NoProfile -Command "(Get-Process cl,link,ninja,cmake,MSBuild -ErrorAction SilentlyContinue | Measure-Object).Count" 2>/dev/null | tr -d '\r')
if [ "${busy:-0}" != "0" ]; then
    echo "REFUSING TO RUN: a build is in progress ($busy processes). Measurements would be worthless."
    exit 1
fi

echo "baseline (machine idle, before playback):"
powershell -NoProfile -Command "
  \$p = (Get-Counter '\Processor Information(_Total)\% Processor Performance' -ErrorAction SilentlyContinue).CounterSamples[0].CookedValue
  \$mhz = (Get-CimInstance Win32_Processor).CurrentClockSpeed
  \$max = (Get-CimInstance Win32_Processor).MaxClockSpeed
  '  clock now: {0:N0} MHz of {1:N0} MHz rated   ({2:N0}% of nominal)' -f \$mhz, \$max, \$p
" 2>/dev/null

# ---- sampler: one line per second, for the whole playback ---------------------------------------
powershell -NoProfile -Command "
  \$n = $((SECONDS_TO_RUN + 5))
  'elapsed,pct_of_nominal,mhz,gpu_pct,tempC' | Out-File -Encoding ascii '$SAMPLES'
  for (\$i = 0; \$i -lt \$n; \$i++) {
    \$t0 = Get-Date
    try { \$p = (Get-Counter '\Processor Information(_Total)\% Processor Performance' -ErrorAction Stop).CounterSamples[0].CookedValue } catch { \$p = -1 }
    try { \$mhz = (Get-CimInstance Win32_Processor -ErrorAction Stop).CurrentClockSpeed } catch { \$mhz = -1 }
    try {
      \$g = (Get-Counter '\GPU Engine(*engtype_3D)\Utilization Percentage' -ErrorAction Stop).CounterSamples |
           Measure-Object -Property CookedValue -Sum | Select-Object -ExpandProperty Sum
    } catch { \$g = -1 }
    try {
      \$k = (Get-CimInstance -Namespace root/wmi -ClassName MSAcpi_ThermalZoneTemperature -ErrorAction Stop |
             Select-Object -First 1 -ExpandProperty CurrentTemperature)
      \$c = [math]::Round((\$k / 10) - 273.15, 1)
    } catch { \$c = -1 }
    ('{0},{1:N0},{2:N0},{3:N0},{4}' -f \$i, \$p, \$mhz, \$g, \$c) | Out-File -Encoding ascii -Append '$SAMPLES'
    \$spent = ((Get-Date) - \$t0).TotalMilliseconds
    if (\$spent -lt 1000) { Start-Sleep -Milliseconds ([int](1000 - \$spent)) }
  }
" > /dev/null 2>&1 &
SAMPLER_PID=$!
sleep 1

# ---- playback -----------------------------------------------------------------------------------
if [ "$ARM" = "app" ]; then
    (
        export PATH="/c/Qt/6.11.1/msvc2022_64/bin:/c/tools/mpvqt-feasibility/mpvqt-msvc-install/bin:/c/tools/mpvqt-feasibility/libmpv-prefix/bin:/c/tools/ffmpeg-master-latest-win64-gpl-shared/bin:$PATH"
        export QTFRAMEWORK_BYPASS_LICENSE_CHECK=1 QT_FORCE_STDERR_LOGGING=1 COLOSSEUM_MPV=1
        export COLOSSEUM_ABBA_CLIP="$CLIP"
        export COLOSSEUM_MPV_DROP_PROBE="5,$((SECONDS_TO_RUN - 10))"
        cd "$APP_ROOT" && ./native/build-msvc/colosseum.exe qml/Main.qml
    ) > "$PLAYLOG" 2>&1
else
    "$MPV" --no-config --geometry=1920x1080 --start=00:06:00 --length=$SECONDS_TO_RUN \
        --vo=gpu-next --hwdec=d3d11va-copy --gpu-api=opengl \
        --term-status-msg='STAT t=${=time-pos} vodrop=${frame-drop-count}' \
        "$CLIP" > "$PLAYLOG" 2>&1
fi

wait $SAMPLER_PID 2>/dev/null

# ---- join and print -----------------------------------------------------------------------------
echo
echo "=========== second-by-second: is the chip slowing down when frames drop? ==========="
echo "elapsed  clock%  MHz     GPU%   tempC   drops_this_sec  (playback: $ARM)"
if [ "$ARM" = "mpv" ]; then
    grep -o 'STAT t=[0-9.]* vodrop=[0-9]*' "$PLAYLOG" \
      | sed 's/STAT t=//; s/ vodrop=/ /' \
      | awk -v s=$START_SEC '{ e=int($1-s); d=$2; if (e>=0) last[e]=d } END { for (i=0;i<=90;i++) if (i in last) print i, last[i] }' \
      > "$OUT/drops.txt"
else
    : > "$OUT/drops.txt"
fi

awk -F, 'NR>1 {print $1, $2, $3, $4, $5}' "$SAMPLES" 2>/dev/null | while read -r e pct mhz gpu tc; do
    cur=$(awk -v e="$e" '$1==e {print $2}' "$OUT/drops.txt" 2>/dev/null | tail -1)
    prev=$(awk -v e="$((e-1))" '$1==e {print $2}' "$OUT/drops.txt" 2>/dev/null | tail -1)
    delta=""
    if [ -n "$cur" ] && [ -n "$prev" ]; then delta=$(( cur - prev )); fi
    printf "%5s   %5s  %5s   %4s   %5s   %s\n" "$e" "$pct" "$mhz" "$gpu" "$tc" "${delta:-.}"
done

echo
echo "--- summary ---"
awk -F, 'NR>1 && $2>=0 { n++; s+=$2; if(min==""||$2<min)min=$2; if($2>max)max=$2 }
         END { if(n) printf "clock %% of nominal during playback:  mean %.0f   min %.0f   max %.0f   (n=%d)\n", s/n, min, max, n }' "$SAMPLES"
awk -F, 'NR>1 && $5>0 { n++; s+=$5; if($5>max)max=$5 } END { if(n) printf "CPU temperature:  mean %.1fC   peak %.1fC\n", s/n, max; else print "CPU temperature: not exposed by this laptop (needs admin / vendor driver)" }' "$SAMPLES"
if [ "$ARM" = "mpv" ]; then
    echo -n "total drops over the run: "; tail -1 "$OUT/drops.txt" | awk '{print $2}'
else
    grep -o 'MPV_DROP_PROBE RESULT[^}]*}' "$PLAYLOG" | tail -1
fi
echo
echo "VERDICT GUIDE: clock% sitting far below 100 while playing = throttling (hardware, not code)."
echo "               clock% steady near 100 with drops still bursting = thermal RULED OUT."
