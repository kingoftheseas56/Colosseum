#!/usr/bin/env bash
# Task013 emulator cycle: isolation verify -> play -> timed captures -> seeks ->
# background/foreground -> close. Every action and capture is timestamped.
set -u
OUT="$1"; mkdir -p "$OUT"
A="/root/colosseum-android/android-sdk/platform-tools/adb -s emulator-5554"
P=org.qtproject.example.colosseum.nuvio013cloud
log(){ echo "$(date -u +%H:%M:%S.%3N) $*" | tee -a "$OUT/actions.txt"; }
cap(){ local f="$OUT/$(date -u +%H%M%S)-$1.png"; $A exec-out screencap -p > "$f"; log "capture $(basename "$f")"; }
trig(){ $A shell "run-as $P rm -f files/nuvio013-trigger.json"; sleep 2; echo "$1" | $A shell "run-as $P sh -c 'cat > files/nuvio013-trigger.tmp && mv files/nuvio013-trigger.tmp files/nuvio013-trigger.json'"; log "trigger $1"; }
waitlog(){ local pat="$1" t="${2:-600}" s=$(date +%s); until $A logcat -d | grep -qE "$pat"; do sleep 5; [ $(( $(date +%s)-s )) -gt $t ] && { log "TIMEOUT waiting for $pat"; return 1; }; done; log "seen: $pat"; }
tap(){ $A shell input tap $1 $2; log "tap $1,$2 ($3)"; }
step="${2:-all}"
if [[ $step == all || $step == start ]]; then
  $A logcat -c
  $A shell am force-stop $P; log "force-stop"
  # Fresh task-owned diagnostic roots: the isolated engine refuses saved state,
  # and a cached payload would bypass the seeder.
  $A shell "run-as $P rm -rf files/nuvio/diag013-data cache/nuvio/diag013-cache"
  $A shell "run-as $P sh -c 'ls files/nuvio cache/nuvio 2>&1'" | tr '\n' ' ' | sed 's/^/roots after clean: /' | tee -a "$OUT/actions.txt"; echo
  $A shell am start -n $P/org.qtproject.qt.android.bindings.QtActivity >/dev/null; log "launch"
  waitlog '\[nuvio\] startup engine path' 900
  sleep 20
  trig '{"op":"verify"}'; waitlog 'isolation-verify (PASS|FAIL)' 300
  $A logcat -d | grep -E 'isolation-verify' | tail -1 > "$OUT/isolation.txt"
  trig '{"op":"play","infoHash":"7f13e4dfe42445a675cccb6fda1e7549babad2a8","fileIdx":0,"title":"Nuvio 013 Synthetic Fixture"}'
  waitlog 'streamReady url' 600
  waitlog 'Media3 OES latched frames' 600
  for i in 1 2 3 4 5 6; do sleep 12; cap play$i; done
fi
$A logcat -d > "$OUT/logcat-$step.txt"
if [[ $step == all || $step == rest ]]; then
  $A logcat -d > "$OUT/logcat-start.txt"; $A logcat -c
  # Forward seek: tap video (pause + chrome), seek bar 75% (~45 s), play.
  tap 360 700 "pause+chrome"; sleep 5; tap 526 1388 "seekbar ~45.0s W=09"; sleep 4; tap 360 1488 "play"; sleep 20; cap seekfwd45
  # Back seek: pause + chrome, seek bar 15% (~9.0 s); capture paused target, then play.
  tap 360 700 "pause+chrome"; sleep 5; tap 128 1388 "seekbar ~9.0s W=01"; sleep 15; cap seekback9; tap 360 1488 "play"; sleep 20; cap afterback
  $A logcat -d > "$OUT/logcat-seek.txt"; $A logcat -c
  # Background/foreground across > 30 s.
  log "pid before background $($A shell pidof $P)"; cap prebg
  $A shell am start -a android.settings.SETTINGS >/dev/null; log "background (Settings)"; sleep 45; cap inbg
  $A shell am start -n $P/org.qtproject.qt.android.bindings.QtActivity >/dev/null 2>&1; log "foreground return"
  sleep 3; log "pid after foreground $($A shell pidof $P)"
  waitlog 'streamReady url' 300
  sleep 25; cap fg1; sleep 25; cap fg2; sleep 25; cap fg3
  $A logcat -d > "$OUT/logcat-lifecycle.txt"; $A logcat -c
  # Natural close: back from the player, then observe demand teardown.
  $A shell input keyevent KEYCODE_BACK; log "back (close player)"; sleep 30; cap closed
  log "pid after close $($A shell pidof $P)"
  $A logcat -d > "$OUT/logcat-close.txt"
fi
