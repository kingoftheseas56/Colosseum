#!/usr/bin/env bash
# Launch smoke for a Colosseum debug APK on an already-booted emulator/device.
#
# Usage: scripts/android/emulator_smoke.sh <apk> [artifact-dir] [alive-seconds]
#
# Installs the APK, launches its launcher activity, waits (default 60 s), then asserts the
# process is still alive. Always captures logcat, a screenshot, and tombstones (when readable)
# into artifact-dir. Fails on: install failure, process death, "FATAL EXCEPTION", a native
# crash signature in logcat, or a new tombstone.
set -uo pipefail

APK="${1:?usage: emulator_smoke.sh <apk> [artifact-dir] [alive-seconds]}"
ART="${2:-smoke-artifacts}"
ALIVE="${3:-60}"
ADB="${ADB:-adb}"
mkdir -p "$ART"
log() { printf '[smoke] %s\n' "$*"; }

aapt=$(ls "${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"/build-tools/*/aapt 2>/dev/null | sort | tail -1)
if [[ -n "$aapt" ]]; then
    PKG=$("$aapt" dump badging "$APK" | sed -nE "s/^package: name='([^']+)'.*/\1/p")
    ACTIVITY=$("$aapt" dump badging "$APK" | sed -nE "s/^launchable-activity: name='([^']+)'.*/\1/p")
fi
PKG="${PKG:-org.qtproject.example}"
ACTIVITY="${ACTIVITY:-org.qtproject.qt.android.bindings.QtActivity}"
log "package=$PKG activity=$ACTIVITY"

"$ADB" wait-for-device
# google_apis emulator images are debuggable; root makes /data/tombstones readable.
"$ADB" root >/dev/null 2>&1 || true
"$ADB" wait-for-device
"$ADB" shell getprop ro.product.cpu.abilist | tee "$ART/device-abi.txt"
"$ADB" shell ls /data/tombstones 2>/dev/null | sort > "$ART/tombstones-before.txt" || true
"$ADB" logcat -c || true

fail=0
if ! "$ADB" install -r -g "$APK" > "$ART/install.txt" 2>&1; then
    log "INSTALL FAILED"; cat "$ART/install.txt"; fail=1
fi

if [[ $fail == 0 ]]; then
    "$ADB" shell am start -W -n "$PKG/$ACTIVITY" | tee "$ART/am-start.txt"
    sleep 5
    pid=$("$ADB" shell pidof "$PKG" | tr -d '\r')
    log "pid after 5 s: ${pid:-<none>}"
    # Screenshot while the UI is up (early), and again at the end of the alive window.
    sleep 20
    "$ADB" exec-out screencap -p > "$ART/screenshot-25s.png" || true
    sleep $((ALIVE > 25 ? ALIVE - 25 : 0))
    pid_end=$("$ADB" shell pidof "$PKG" | tr -d '\r')
    "$ADB" exec-out screencap -p > "$ART/screenshot.png" || true
    if [[ -z "$pid_end" ]]; then
        log "PROCESS NOT ALIVE after ${ALIVE} s"; fail=1
    else
        log "alive after ${ALIVE} s (pid $pid_end)"
        [[ -n "$pid" && "$pid" != "$pid_end" ]] && { log "PROCESS RESTARTED ($pid -> $pid_end)"; fail=1; }
    fi
fi

"$ADB" logcat -d -v threadtime > "$ART/logcat.txt" 2>&1 || true
"$ADB" logcat -d -b crash -v threadtime > "$ART/logcat-crash.txt" 2>&1 || true
"$ADB" shell dumpsys activity activities > "$ART/activities.txt" 2>&1 || true
"$ADB" shell ls /data/tombstones 2>/dev/null | sort > "$ART/tombstones-after.txt" || true
new_tombstones=$(comm -13 "$ART/tombstones-before.txt" "$ART/tombstones-after.txt" | grep -v '\.pb$' || true)
for t in $new_tombstones; do
    "$ADB" shell cat "/data/tombstones/$t" > "$ART/$t.txt" 2>/dev/null || true
done

if grep -q "FATAL EXCEPTION" "$ART/logcat.txt"; then
    log "FATAL EXCEPTION in logcat:"; grep -A20 -m1 "FATAL EXCEPTION" "$ART/logcat.txt"; fail=1
fi
if grep -qE "Fatal signal [0-9]+|\*\*\* \*\*\* \*\*\* \*\*\* \*\*\* \*\*\*|backtrace:" "$ART/logcat.txt" "$ART/logcat-crash.txt"; then
    log "native crash in logcat:"
    grep -hE -A25 -m1 "Fatal signal [0-9]+|\*\*\* \*\*\* \*\*\*" "$ART/logcat.txt" "$ART/logcat-crash.txt" | head -60
    fail=1
fi
if [[ -n "$new_tombstones" ]]; then
    log "new tombstones: $new_tombstones"; fail=1
fi

# Short app-side summary for the job log (never the whole logcat).
grep -E "\[boot\]|qml: |QML|libcolosseum|E Qt|F Qt| W Qt|qt\.qml" "$ART/logcat.txt" | head -80 > "$ART/app-summary.txt" || true
tail -40 "$ART/app-summary.txt"

if [[ $fail != 0 ]]; then log "SMOKE FAILED"; exit 1; fi
log "SMOKE PASSED"
