#!/usr/bin/env bash
set -euo pipefail

script_directory=$(cd "$(dirname "$0")" && pwd -P)
engine_root=$(cd "$script_directory/.." && pwd -P)
xcframework="${1:-$engine_root/platform/apple/NuvioEngine.xcframework}"
source_file="$engine_root/tests/apple_c_api_smoke.cpp"

if [[ "$(uname -s)" != Darwin ]]; then
    echo "Apple smoke linking requires macOS" >&2
    exit 2
fi

temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT

link_smoke() {
    local name=$1
    local sdk=$2
    local architecture=$3
    local minimum_flag=$4
    local slice=$5
    xcrun --sdk "$sdk" clang++ \
        -std=c++20 \
        -arch "$architecture" \
        "$minimum_flag" \
        -I "$xcframework/$slice/Headers" \
        "$source_file" \
        "$xcframework/$slice/libCNuvioEngine.a" \
        -framework Security \
        -framework SystemConfiguration \
        -framework CoreFoundation \
        -Wl,-dead_strip \
        -o "$temporary_directory/$name"
}

link_smoke macos-arm64 macosx arm64 -mmacosx-version-min=11.0 macos-arm64_x86_64
link_smoke macos-x86_64 macosx x86_64 -mmacosx-version-min=11.0 macos-arm64_x86_64
link_smoke ios-arm64 iphoneos arm64 -miphoneos-version-min=16.1 ios-arm64
link_smoke \
    ios-simulator-arm64 \
    iphonesimulator \
    arm64 \
    -mios-simulator-version-min=16.1 \
    ios-arm64_x86_64-simulator
link_smoke \
    ios-simulator-x86_64 \
    iphonesimulator \
    x86_64 \
    -mios-simulator-version-min=16.1 \
    ios-arm64_x86_64-simulator

host_architecture=$(uname -m)
case "$host_architecture" in
    arm64|x86_64) ;;
    *)
        echo "unsupported macOS host architecture: $host_architecture" >&2
        exit 2
        ;;
esac
mkdir -p "$temporary_directory/data" "$temporary_directory/cache"
"$temporary_directory/macos-$host_architecture" \
    "$temporary_directory/data" \
    "$temporary_directory/cache"

echo "linked every Apple architecture and ran the macOS C API smoke test"
