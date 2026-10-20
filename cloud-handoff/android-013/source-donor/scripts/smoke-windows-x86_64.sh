#!/usr/bin/env bash
set -euo pipefail

script_directory=$(cd "$(dirname "$0")" && pwd -P)
engine_root=$(cd "$script_directory/.." && pwd -P)
windows_root="$engine_root/platform/windows"
archive="$windows_root/dist/nuvio-engine-windows-x86_64.zip"
image=nuvio-engine-windows-wine-smoke:ubuntu-22.04
container="nuvio-engine-windows-wine-smoke-$$"
timeout_seconds=${NUVIO_WINE_TIMEOUT_SECONDS:-120}

if [[ ! -f "$archive" ]]; then
    echo "build the Windows x86_64 package before running its smoke test" >&2
    exit 2
fi
if ! docker info >/dev/null 2>&1; then
    echo "Docker must be running for the Windows x86_64 smoke test" >&2
    exit 2
fi
if [[ ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ]]; then
    echo "NUVIO_WINE_TIMEOUT_SECONDS must be a positive integer" >&2
    exit 2
fi

docker build \
    --platform linux/amd64 \
    --file "$windows_root/Dockerfile.wine" \
    --tag "$image" \
    "$engine_root"

cleanup() {
    docker rm --force "$container" >/dev/null 2>&1 || true
}
trap cleanup EXIT

docker run \
    --name "$container" \
    --platform linux/amd64 \
    --volume "$windows_root/dist:/packages:ro" \
    "$image" \
    /bin/bash -euc '
        package=/tmp/nuvio-engine
        unzip -q /packages/nuvio-engine-windows-x86_64.zip -d "$package"
        distribution="$package/nuvio-engine-windows-x86_64"
        export WINEDEBUG=-all
        export WINEPREFIX=/tmp/nuvio-wine-prefix
        export WINEDLLOVERRIDES=mscoree,mshtml=
        cd "$distribution/bin"
        /usr/lib/wine/wine64 ../tools/nuvio_engine_smoke.exe
    ' &
run_pid=$!
deadline=$((SECONDS + timeout_seconds))
while kill -0 "$run_pid" 2>/dev/null; do
    if (( SECONDS >= deadline )); then
        echo "Windows x86-64 Wine smoke test timed out after ${timeout_seconds}s" >&2
        kill "$run_pid" 2>/dev/null || true
        wait "$run_pid" 2>/dev/null || true
        exit 2
    fi
    sleep 1
done
wait_status=0
wait "$run_pid" || wait_status=$?
if (( wait_status != 0 )); then
    exit "$wait_status"
fi
cleanup
trap - EXIT

echo "ran the packaged Windows x86_64 C API smoke test under Wine"
