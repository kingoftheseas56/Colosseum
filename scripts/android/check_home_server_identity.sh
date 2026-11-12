#!/usr/bin/env bash
# Run after the normal startup smoke, on its already running test APK.
set -euo pipefail
art="${1:?smoke artifact directory required}"
package="$(cat "$art/package.txt")"
component="$(cat "$art/component.txt")"
[[ "$package" =~ ^[A-Za-z0-9_.]+$ && "$component" == "$package/"* ]]
[[ "$component" =~ ^[A-Za-z0-9_./]+$ ]]
adb_cmd() { timeout 30 "${ADB:-adb}" "$@"; }
identity() { adb_cmd shell run-as "$package" cat no_backup/home-servers/installation-id | tr -d '\r\n'; }
before="$(identity)"
[[ "$before" =~ ^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$ ]]
[[ "$before" != 00000000-0000-0000-0000-000000000000 ]]
old_pid="$(adb_cmd shell pidof "$package" | tr -d '\r')"
[[ "$old_pid" =~ ^[0-9]+$ ]]
adb_cmd shell am force-stop "$package"
adb_cmd shell am start -W -n "$component" > "$art/identity-restart.txt"
for attempt in {1..30}; do
    pid="$(adb_cmd shell pidof "$package" | tr -d '\r' || true)"
    if [[ "$pid" =~ ^[0-9]+$ && "$pid" != "$old_pid" ]] &&
       adb_cmd logcat -d --pid="$pid" -t 1000 | grep -F '[boot] Main.qml loaded' >/dev/null; then
        [[ "$(identity)" == "$before" ]]
        [[ "$(adb_cmd shell pidof "$package" | tr -d '\r')" == "$pid" ]]
        printf '{"success":true,"stableUuid":true,"noBackup":true}\n' > "$art/home-server-identity.json"
        exit 0
    fi
    sleep 1
done
echo 'Home server installation identity restart check failed.' >&2
exit 1
