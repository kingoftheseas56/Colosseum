#!/usr/bin/env bash
set -euo pipefail

ios_deployment_target=16.1
macos_deployment_target=11.0

script_directory=$(cd "$(dirname "$0")" && pwd -P)
engine_root=$(cd "$script_directory/.." && pwd -P)
apple_root="$engine_root/platform/apple"
dependency_root="$apple_root/.deps"
build_root="$apple_root/build"
output="$apple_root/NuvioEngine.xcframework"

if [[ "$(uname -s)" != Darwin ]]; then
    echo "XCFramework builds require macOS" >&2
    exit 2
fi

"$script_directory/prepare-native-dependencies.sh" "$dependency_root"
"$script_directory/build-apple-openssl.sh" "$dependency_root"
mkdir -p "$build_root"

boost_source="$dependency_root/sources/boost_1_86_0"
libtorrent_source="$dependency_root/sources/libtorrent-2.0.12"
compiler_engine_root=${engine_root// /\\ }
compiler_dependency_root=${dependency_root// /\\ }
reproducible_flags="-ffile-prefix-map=$compiler_engine_root=/nuvio-engine/source -ffile-prefix-map=$compiler_dependency_root=/nuvio-engine/dependencies"

configure_and_build() {
    local name=$1
    local sdk=$2
    local deployment_target=$3
    local architectures=$4
    local openssl_slice=$5
    local system_name=$6
    local build_directory="$build_root/$name"
    local openssl_root="$dependency_root/openssl/install/$openssl_slice"
    local configure_log="$build_directory/configure.log"
    local build_log="$build_directory/build.log"
    mkdir -p "$build_directory"

    local arguments=(
        -S "$engine_root"
        -B "$build_directory"
        -G Ninja
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_FLAGS="$reproducible_flags"
        -DCMAKE_CXX_FLAGS="$reproducible_flags"
        -DNUVIO_ENGINE_BUILD_TESTS=OFF
        -DNUVIO_ENGINE_ENABLE_LIBTORRENT=ON
        -DNUVIO_ENGINE_BUILD_SHARED=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_OSX_SYSROOT="$sdk"
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target"
        -DCMAKE_OSX_ARCHITECTURES="$architectures"
        -DOPENSSL_ROOT_DIR="$openssl_root"
        -DOPENSSL_INCLUDE_DIR="$openssl_root/include"
        -DOPENSSL_SSL_LIBRARY="$openssl_root/lib/libssl.a"
        -DOPENSSL_CRYPTO_LIBRARY="$openssl_root/lib/libcrypto.a"
        -DOPENSSL_USE_STATIC_LIBS=TRUE
        -DFETCHCONTENT_SOURCE_DIR_NUVIO_BOOST="$boost_source"
        -DFETCHCONTENT_SOURCE_DIR_NUVIO_LIBTORRENT="$libtorrent_source"
    )
    if [[ -n "$system_name" ]]; then
        arguments+=(
            -DCMAKE_SYSTEM_NAME="$system_name"
            -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
        )
    fi
    if ! cmake "${arguments[@]}" >"$configure_log" 2>&1; then
        cat "$configure_log" >&2
        exit 1
    fi
    if ! cmake --build "$build_directory" --parallel "${NUVIO_BUILD_JOBS:-8}" \
        >"$build_log" 2>&1; then
        cat "$build_log" >&2
        exit 1
    fi
}

configure_and_build \
    macos \
    macosx \
    "$macos_deployment_target" \
    'arm64;x86_64' \
    macos \
    ''
configure_and_build \
    ios \
    iphoneos \
    "$ios_deployment_target" \
    arm64 \
    ios-arm64 \
    iOS
configure_and_build \
    ios-simulator \
    iphonesimulator \
    "$ios_deployment_target" \
    'arm64;x86_64' \
    ios-simulator \
    iOS

merge_slice() {
    local name=$1
    local openssl_slice=$2
    local destination="$build_root/combined/$name"
    local build_directory="$build_root/$name"
    local openssl_root="$dependency_root/openssl/install/$openssl_slice"
    local merge_log="$destination/merge.log"
    cmake -E remove_directory "$destination"
    mkdir -p "$destination"
    if ! xcrun libtool -static \
        -o "$destination/libCNuvioEngine.a" \
        "$build_directory/libnuvio_engine.a" \
        "$build_directory/_deps/nuvio_libtorrent-build/libtorrent-rasterbar.a" \
        "$openssl_root/lib/libssl.a" \
        "$openssl_root/lib/libcrypto.a" \
        >"$merge_log" 2>&1; then
        cat "$merge_log" >&2
        exit 1
    fi
}

merge_slice macos macos
merge_slice ios ios-arm64
merge_slice ios-simulator ios-simulator

headers="$build_root/headers"
cmake -E remove_directory "$headers"
mkdir -p "$headers/nuvio_engine"
cmake -E copy \
    "$engine_root/include/nuvio_engine/nuvio_engine.h" \
    "$headers/nuvio_engine/nuvio_engine.h"
cmake -E copy \
    "$engine_root/include/nuvio_engine/export.h" \
    "$headers/nuvio_engine/export.h"
cmake -E copy \
    "$apple_root/include/module.modulemap" \
    "$headers/module.modulemap"

licenses="$build_root/distribution-licenses"
cmake -E remove_directory "$licenses"
mkdir -p "$licenses"
cmake -E copy "$engine_root/LICENSE" "$licenses/NUVIO-ENGINE-LICENSE.txt"
cmake -E copy "$engine_root/THIRD_PARTY_NOTICES.md" "$licenses/THIRD_PARTY_NOTICES.md"
cmake -E copy "$libtorrent_source/LICENSE" "$licenses/LIBTORRENT-LICENSE.txt"
cmake -E copy "$libtorrent_source/COPYING" "$licenses/LIBTORRENT-COPYING.txt"
cmake -E copy \
    "$libtorrent_source/deps/try_signal/LICENSE" \
    "$licenses/TRY_SIGNAL-LICENSE.txt"
cmake -E copy \
    "$boost_source/LICENSE_1_0.txt" \
    "$licenses/BOOST-LICENSE_1_0.txt"
cmake -E copy \
    "$dependency_root/sources/openssl-3.5.7/LICENSE.txt" \
    "$licenses/OPENSSL-LICENSE.txt"

cmake -E remove_directory "$output"
xcodebuild -create-xcframework \
    -library "$build_root/combined/macos/libCNuvioEngine.a" \
    -headers "$headers" \
    -library "$build_root/combined/ios/libCNuvioEngine.a" \
    -headers "$headers" \
    -library "$build_root/combined/ios-simulator/libCNuvioEngine.a" \
    -headers "$headers" \
    -output "$output"

echo "created $output"
echo "license sidecars are in $licenses"
