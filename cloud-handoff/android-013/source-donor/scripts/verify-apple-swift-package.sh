#!/usr/bin/env bash
set -euo pipefail

script_directory=$(cd "$(dirname "$0")" && pwd -P)
engine_root=$(cd "$script_directory/.." && pwd -P)
apple_root="$engine_root/platform/apple"

if [[ "$(uname -s)" != Darwin ]]; then
    echo "Apple Swift package verification requires macOS" >&2
    exit 2
fi
if [[ ! -f "$apple_root/NuvioEngine.xcframework/Info.plist" ]]; then
    echo "build the local XCFramework before verifying the Swift package" >&2
    exit 2
fi

swift test --package-path "$apple_root"

build_for_destination() {
    local name=$1
    local destination=$2
    local log="$apple_root/build/swift-$name.log"
    if ! (
        cd "$apple_root"
        xcodebuild \
            -scheme NuvioEngine \
            -destination "$destination" \
            -configuration Release \
            -derivedDataPath "build/swift-$name" \
            CODE_SIGNING_ALLOWED=NO \
            build
    ) >"$log" 2>&1; then
        cat "$log" >&2
        exit 1
    fi
}

build_for_destination macos 'generic/platform=macOS'
build_for_destination ios 'generic/platform=iOS'
build_for_destination ios-simulator 'generic/platform=iOS Simulator'

swift package \
    --package-path "$apple_root" \
    dump-symbol-graph \
    --minimum-access-level public \
    >/dev/null
symbol_graph=$(find "$apple_root/.build" -path '*/symbolgraph/NuvioEngine.symbols.json' -print -quit)
if [[ -z "$symbol_graph" ]]; then
    echo "Swift package did not produce a NuvioEngine symbol graph" >&2
    exit 1
fi
if rg -q 'CNuvioEngine|nuvio_engine_' "$symbol_graph"; then
    echo "Swift package exposes its private C module" >&2
    exit 1
fi

echo "verified Swift tests, public API boundary, macOS, iOS, and iOS Simulator builds"
