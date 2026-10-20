#!/usr/bin/env bash
set -euo pipefail

script_directory=$(cd "$(dirname "$0")" && pwd -P)
engine_root=$(cd "$script_directory/.." && pwd -P)
apple_root="$engine_root/platform/apple"
xcframework="${1:-$apple_root/NuvioEngine.xcframework}"
dependency_root="$apple_root/.deps"
licenses="$apple_root/build/distribution-licenses"

if [[ "$(uname -s)" != Darwin ]]; then
    echo "XCFramework verification requires macOS" >&2
    exit 2
fi

fail() {
    echo "Apple package verification failed: $*" >&2
    exit 1
}

require_file() {
    [[ -f "$1" ]] || fail "missing $1"
}

assert_architectures() {
    local archive=$1
    shift
    local actual
    local expected
    actual=$(xcrun lipo -archs "$archive" | tr ' ' '\n' | sort | tr '\n' ' ')
    expected=$(printf '%s\n' "$@" | sort | tr '\n' ' ')
    [[ "$actual" == "$expected" ]] || \
        fail "$archive has architectures '$actual', expected '$expected'"
}

assert_build_version() {
    local archive=$1
    local architecture=$2
    local platform=$3
    local minimum=$4
    local name=$5
    local thin="$temporary_directory/$name.a"
    local object_directory="$temporary_directory/$name-objects"
    if [[ "$(xcrun lipo -archs "$archive")" == "$architecture" ]]; then
        cp "$archive" "$thin"
    else
        xcrun lipo "$archive" -thin "$architecture" -output "$thin"
    fi
    mkdir -p "$object_directory"
    (
        cd "$object_directory"
        xcrun ar -x "$thin" engine.cpp.o
    )
    local build_version
    build_version=$(xcrun vtool -show-build "$object_directory/engine.cpp.o")
    [[ "$build_version" == *"platform $platform"* ]] || \
        fail "$archive is not marked for $platform"
    [[ "$build_version" == *"minos $minimum"* ]] || \
        fail "$archive does not have minimum OS $minimum"
}

require_file "$xcframework/Info.plist"

macos="$xcframework/macos-arm64_x86_64"
ios="$xcframework/ios-arm64"
simulator="$xcframework/ios-arm64_x86_64-simulator"
for slice in "$macos" "$ios" "$simulator"; do
    require_file "$slice/libCNuvioEngine.a"
    require_file "$slice/Headers/module.modulemap"
    require_file "$slice/Headers/nuvio_engine/nuvio_engine.h"
    require_file "$slice/Headers/nuvio_engine/export.h"
    [[ "$(find "$slice/Headers" -type f | wc -l | tr -d ' ')" == 3 ]] || \
        fail "$slice contains headers outside the stable C module"
    cmp -s \
        "$slice/Headers/module.modulemap" \
        "$apple_root/include/module.modulemap" || fail "module map mismatch in $slice"
    cmp -s \
        "$slice/Headers/nuvio_engine/nuvio_engine.h" \
        "$engine_root/include/nuvio_engine/nuvio_engine.h" || fail "C header mismatch in $slice"
    cmp -s \
        "$slice/Headers/nuvio_engine/export.h" \
        "$engine_root/include/nuvio_engine/export.h" || fail "export header mismatch in $slice"
done

[[ "$(find "$xcframework" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')" == 3 ]] || \
    fail "XCFramework does not contain exactly three platform slices"

assert_architectures "$macos/libCNuvioEngine.a" arm64 x86_64
assert_architectures "$ios/libCNuvioEngine.a" arm64
assert_architectures "$simulator/libCNuvioEngine.a" arm64 x86_64

temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT
assert_build_version "$macos/libCNuvioEngine.a" arm64 MACOS 11.0 macos-arm64
assert_build_version "$ios/libCNuvioEngine.a" arm64 IOS 16.1 ios-arm64
assert_build_version \
    "$simulator/libCNuvioEngine.a" \
    arm64 \
    IOSSIMULATOR \
    16.1 \
    ios-simulator-arm64

api_symbols=(
    _nuvio_engine_api_version
    _nuvio_engine_version_string
    _nuvio_engine_protocol_backend_version
    _nuvio_engine_status_message
    _nuvio_engine_create
    _nuvio_engine_destroy
    _nuvio_engine_add_torrent
    _nuvio_engine_poll_event
    _nuvio_engine_get_file_count
    _nuvio_engine_get_file
    _nuvio_engine_prepare_stream
    _nuvio_engine_remove_torrent
    _nuvio_engine_stop_stream
    _nuvio_engine_get_stats
    _nuvio_engine_get_stream_stats
    _nuvio_engine_stream_stats_init_sized
    _nuvio_engine_reclaim_disk_cache
)
for archive in \
    "$macos/libCNuvioEngine.a" \
    "$ios/libCNuvioEngine.a" \
    "$simulator/libCNuvioEngine.a"; do
    symbols="$temporary_directory/$(basename "$(dirname "$archive")").symbols"
    xcrun nm -gU "$archive" 2>/dev/null | awk '{print $3}' | sort -u > "$symbols"
    for symbol in "${api_symbols[@]}"; do
        rg -Fxq "$symbol" "$symbols" || fail "$archive is missing $symbol"
    done
    rg -Fxq _OPENSSL_init_ssl "$symbols" || fail "$archive does not contain OpenSSL"
    rg -Fxq __Z25libtorrent_version_stringv "$symbols" || \
        fail "$archive does not contain the pinned libtorrent backend"
    strings "$archive" >> "$temporary_directory/archive-strings"
done

if rg -qF "$engine_root" "$temporary_directory/archive-strings"; then
    fail "archives expose the local repository path"
fi
if rg -q '/var/folders/|nuvio-apple-openssl-' "$temporary_directory/archive-strings"; then
    fail "archives expose a temporary build path"
fi

license_pairs=(
    "$licenses/NUVIO-ENGINE-LICENSE.txt:$engine_root/LICENSE"
    "$licenses/THIRD_PARTY_NOTICES.md:$engine_root/THIRD_PARTY_NOTICES.md"
    "$licenses/LIBTORRENT-LICENSE.txt:$dependency_root/sources/libtorrent-2.0.12/LICENSE"
    "$licenses/LIBTORRENT-COPYING.txt:$dependency_root/sources/libtorrent-2.0.12/COPYING"
    "$licenses/TRY_SIGNAL-LICENSE.txt:$dependency_root/sources/libtorrent-2.0.12/deps/try_signal/LICENSE"
    "$licenses/BOOST-LICENSE_1_0.txt:$dependency_root/sources/boost_1_86_0/LICENSE_1_0.txt"
    "$licenses/OPENSSL-LICENSE.txt:$dependency_root/sources/openssl-3.5.7/LICENSE.txt"
)
for pair in "${license_pairs[@]}"; do
    packaged=${pair%%:*}
    source=${pair#*:}
    require_file "$packaged"
    cmp -s "$packaged" "$source" || fail "license sidecar mismatch: $packaged"
done

echo "verified Apple XCFramework slices, ABI, metadata, paths, and licenses"
