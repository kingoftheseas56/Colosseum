#!/usr/bin/env bash
# Launch smoke for a Colosseum debug APK on an already-booted emulator/device.
#
# Usage: scripts/android/emulator_smoke.sh <apk> [artifact-dir] [alive-seconds]
#
# Installs and launches the APK, waits for Main.qml and the initial Android world to be ready,
# then monitors that same process for alive-seconds (default 60 s). Cold extraction
# has a separate bounded readiness window (SMOKE_READY_TIMEOUT, default 180 s).
# Captures logcat, screenshots and readable tombstones, including on startup failure.
set -uo pipefail

APK="${1:?usage: emulator_smoke.sh <apk> [artifact-dir] [alive-seconds]}"
ART="${2:-smoke-artifacts}"
ALIVE="${3:-60}"
ADB="${ADB:-adb}"
READY_TIMEOUT="${SMOKE_READY_TIMEOUT:-180}"
for duration in "$ALIVE" "$READY_TIMEOUT"; do
    if [[ ! "$duration" =~ ^[1-9][0-9]*$ ]]; then
        echo "Smoke durations must be positive integer seconds" >&2; exit 2
    fi
done
# Bound every adb operation too: a disconnected device must not hang the job.
adb_cmd() { timeout 120 "$ADB" "$@"; }
now() { date +%s; }
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

adb_cmd wait-for-device
# google_apis emulator images are debuggable; root makes /data/tombstones readable.
adb_cmd root >/dev/null 2>&1 || true
adb_cmd wait-for-device
adb_cmd shell getprop ro.product.cpu.abilist | tee "$ART/device-abi.txt"
adb_cmd shell ls /data/tombstones 2>/dev/null | sort > "$ART/tombstones-before.txt" || true
# A fresh launch and cleared logs prevent a previous run's ready marker passing.
setup_fail=0
adb_cmd shell am force-stop "$PKG" || setup_fail=1
adb_cmd logcat -c || setup_fail=1

fail=$setup_fail
if ! adb_cmd install -r -g "$APK" > "$ART/install.txt" 2>&1; then
    log "INSTALL FAILED"; cat "$ART/install.txt"; fail=1
fi

if [[ $fail == 0 ]]; then
    if ! adb_cmd shell am start -W -n "$PKG/$ACTIVITY" > "$ART/am-start.txt" 2>&1; then
        log "ACTIVITY LAUNCH FAILED"; fail=1
    fi
    cat "$ART/am-start.txt"
    if ! grep -qE '^Status: ok[[:space:]]*$' "$ART/am-start.txt" ||
       grep -qiE '(^|[[:space:]])(Error:|Exception|Failure)' "$ART/am-start.txt"; then
        log "ACTIVITY LAUNCH DID NOT SUCCEED"; fail=1
    fi

    pid=""
    ready_at=""
    launched_at=$(now)
    next_ui_check=$launched_at
    onboarding_tapped=0
    while [[ $fail == 0 ]]; do
        current_pid=$(adb_cmd shell pidof "$PKG" | tr -d '\r')
        # pidof returns 1 until the process starts. Once observed it may never disappear
        # or change; checking only the end would allow a crash/restart to pass.
        if [[ -n "$current_pid" && ! "$current_pid" =~ ^[0-9]+$ ]]; then
            log "AMBIGUOUS PROCESS: $current_pid"; fail=1; break
        fi
        if [[ -n "$pid" && "$current_pid" != "$pid" ]]; then
            log "PROCESS DIED OR RESTARTED ($pid -> ${current_pid:-<none>})"; fail=1; break
        fi
        if [[ -n "$current_pid" && -z "$pid" ]]; then
            pid=$current_pid
            log "launch pid: $pid"
        fi
        if ! adb_cmd logcat -d -v threadtime > "$ART/logcat-poll.txt" 2>&1; then
            log "COULD NOT READ LOGCAT"; fail=1; break
        fi
        if grep -qE 'FATAL EXCEPTION|Fatal signal [0-9]+|QQmlApplicationEngine failed to load component|No functional TLS backend|main\(\) returned -1' "$ART/logcat-poll.txt"; then
            log "STARTUP OR RUNTIME FAILURE in logcat"; fail=1; break
        fi
        current_time=$(now)
        if [[ -z "$ready_at" ]]; then
            # threadtime fields: date time PID TID priority tag message. Only this
            # fresh launch can satisfy readiness; unrelated Qt processes cannot.
            awk -v pid="$pid" '$3 == pid { print }' "$ART/logcat-poll.txt" > "$ART/app-readiness.txt"
            if (( current_time - launched_at > READY_TIMEOUT )); then
                log "READINESS TIMEOUT after ${READY_TIMEOUT} s (pid ${pid:-<none>})"; fail=1; break
            fi
            if [[ -n "$pid" ]] &&
               grep -Fq '[boot] Main.qml loaded' "$ART/app-readiness.txt" &&
               grep -Fq '[boot] android world ready:' "$ART/app-readiness.txt"; then
                ready_at=$current_time
                log "Main.qml loaded and initial world reached after $((ready_at - launched_at)) s; holding ${ALIVE} s"
                adb_cmd exec-out screencap -p > "$ART/screenshot-ready.png" || true
            elif (( current_time - launched_at >= READY_TIMEOUT )); then
                log "READINESS TIMEOUT after ${READY_TIMEOUT} s (pid ${pid:-<none>})"; fail=1; break
            elif [[ $onboarding_tapped == 0 ]] && (( current_time >= next_ui_check )) &&
                 grep -Fq '[boot] Main.qml loaded' "$ART/app-readiness.txt" &&
                 grep -Fq '[boot] local onboarding visible' "$ART/app-readiness.txt"; then
                next_ui_check=$((current_time + 5))
                # Exercise the real first-run local-only choice. Never modify account
                # settings or invoke the controller behind the UI. A fresh successful
                # dump is required, so stale XML cannot authorize a tap.
                if adb_cmd shell uiautomator dump /sdcard/colosseum-smoke-window.xml > "$ART/uiautomator.txt" 2>&1 &&
                   adb_cmd shell cat /sdcard/colosseum-smoke-window.xml > "$ART/ui-window.xml" 2>/dev/null; then
                    display_size=$(adb_cmd shell wm size)
                    coordinates=$(python3 - "$ART/ui-window.xml" "$PKG" "$display_size" <<'PY_UI'
import re
import sys
import xml.etree.ElementTree as ET
try:
    sizes = re.findall(r"(?:Physical|Override) size:\s*(\d+)x(\d+)", sys.argv[3])
    width, height = map(int, sizes[-1])
    choices = []
    for node in ET.parse(sys.argv[1]).iter("node"):
        a = node.attrib
        # Qt appends the accessible description to the name with ", ". The
        # generated QML type number varies, but objectName is a stable identity.
        title = "Continue without account"
        labels = (a.get("text", ""), a.get("content-desc", ""))
        title_matches = any(label == title or
                            (label.startswith(title + ", ") and len(label) > len(title) + 2)
                            for label in labels)
        if (a.get("resource-id", "").rsplit(".", 1)[-1] != "accountWelcomeContinueLocal"
                or a.get("class") != "android.widget.Button"
                or a.get("package") != sys.argv[2] or a.get("enabled") != "true"
                or a.get("visible-to-user", "true") != "true"
                or a.get("clickable") != "true"
                or not title_matches):
            continue
        match = re.fullmatch(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]", a.get("bounds", ""))
        if not match:
            continue
        x1, y1, x2, y2 = map(int, match.groups())
        if 0 <= x1 < x2 <= width and 0 <= y1 < y2 <= height:
            choices.append(((x1 + x2) // 2, (y1 + y2) // 2))
    if len(choices) != 1:
        sys.exit(1)
    print(*choices[0])
except (OSError, ET.ParseError, ValueError, IndexError):
    sys.exit(1)
PY_UI
                    )
                    if [[ "$coordinates" =~ ^[0-9]+\ [0-9]+$ ]]; then
                        read -r tap_x tap_y <<< "$coordinates"
                        if adb_cmd shell input tap "$tap_x" "$tap_y"; then
                            onboarding_tapped=1
                            log "Selected visible Continue without account; waiting for unobscured initial world"
                        else
                            log "ONBOARDING TAP FAILED"; fail=1; break
                        fi
                    fi
                fi
            fi
        elif (( current_time - ready_at >= ALIVE )); then
            log "initial world remained alive for ${ALIVE} s (pid $pid)"
            break
        fi
        sleep 2
    done
fi
# Capture a final screenshot even when install/launch/readiness failed.
adb_cmd exec-out screencap -p > "$ART/screenshot.png" || true

adb_cmd logcat -d -v threadtime > "$ART/logcat.txt" 2>&1 || { log "FINAL LOGCAT CAPTURE FAILED"; fail=1; }
adb_cmd logcat -d -b crash -v threadtime > "$ART/logcat-crash.txt" 2>&1 || { log "CRASH LOGCAT CAPTURE FAILED"; fail=1; }
adb_cmd shell dumpsys activity activities > "$ART/activities.txt" 2>&1 || true
adb_cmd shell ls /data/tombstones 2>/dev/null | sort > "$ART/tombstones-after.txt" || true
new_tombstones=$(comm -13 "$ART/tombstones-before.txt" "$ART/tombstones-after.txt" | grep -v '\.pb$' || true)
for t in $new_tombstones; do
    adb_cmd shell cat "/data/tombstones/$t" > "$ART/$t.txt" 2>/dev/null || true
done

if grep -q "FATAL EXCEPTION" "$ART/logcat.txt"; then
    log "FATAL EXCEPTION in logcat:"; grep -A20 -m1 "FATAL EXCEPTION" "$ART/logcat.txt"; fail=1
fi
if grep -qE 'QQmlApplicationEngine failed to load component|No functional TLS backend|main\(\) returned -1' "$ART/logcat.txt"; then
    log "QML startup or TLS initialization failed:"
    grep -E 'QQmlApplicationEngine failed to load component|No functional TLS backend|main\(\) returned -1' "$ART/logcat.txt" | head -20
    fail=1
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

# Recheck after artifact collection as well, so slow screenshots/logcat cannot hide
# a process exit between the last poll and the successful verdict.
if [[ $fail == 0 ]]; then
    final_pid=$(adb_cmd shell pidof "$PKG" | tr -d '\r')
    if [[ "$final_pid" != "$pid" ]]; then
        log "PROCESS DIED OR RESTARTED ($pid -> ${final_pid:-<none>})"; fail=1
    fi
fi
if [[ $fail != 0 ]]; then log "SMOKE FAILED"; exit 1; fi
log "SMOKE PASSED"
