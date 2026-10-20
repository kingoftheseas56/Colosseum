#!/usr/bin/env bash
set -euo pipefail

script_directory=$(cd "$(dirname "$0")" && pwd -P)
engine_root=$(cd "$script_directory/.." && pwd -P)
apple_root="$engine_root/platform/apple"
macos_root="$engine_root/platform/macos"
dependency_root="$apple_root/.deps"
build_root="$macos_root/build/universal"
distribution_root="$macos_root/dist"
package_name=nuvio-engine-macos-universal
stage="$build_root/package/$package_name"
openssl_root="$dependency_root/openssl/install/macos"
boost_source="$dependency_root/sources/boost_1_86_0"
libtorrent_source="$dependency_root/sources/libtorrent-2.0.12"
jobs=${NUVIO_BUILD_JOBS:-8}

if [[ "$(uname -s)" != Darwin ]]; then
    echo "macOS package builds require macOS" >&2
    exit 2
fi

"$script_directory/prepare-native-dependencies.sh" "$dependency_root"
"$script_directory/build-apple-openssl.sh" "$dependency_root"
mkdir -p "$build_root" "$distribution_root"

compiler_engine_root=${engine_root// /\\ }
compiler_dependency_root=${dependency_root// /\\ }
reproducible_flags="-ffile-prefix-map=$compiler_engine_root=/nuvio-engine/source -ffile-prefix-map=$compiler_dependency_root=/nuvio-engine/dependencies"
configure_log="$build_root/configure.log"
build_log="$build_root/build.log"
if ! cmake \
    -S "$engine_root" \
    -B "$build_root" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="$reproducible_flags" \
    -DCMAKE_CXX_FLAGS="$reproducible_flags" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_OSX_SYSROOT=macosx \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
    -DNUVIO_ENGINE_BUILD_SHARED=ON \
    -DNUVIO_ENGINE_BUILD_TESTS=ON \
    -DNUVIO_ENGINE_ENABLE_LIBTORRENT=ON \
    -DOPENSSL_ROOT_DIR="$openssl_root" \
    -DOPENSSL_INCLUDE_DIR="$openssl_root/include" \
    -DOPENSSL_SSL_LIBRARY="$openssl_root/lib/libssl.a" \
    -DOPENSSL_CRYPTO_LIBRARY="$openssl_root/lib/libcrypto.a" \
    -DOPENSSL_USE_STATIC_LIBS=TRUE \
    -DFETCHCONTENT_SOURCE_DIR_NUVIO_BOOST="$boost_source" \
    -DFETCHCONTENT_SOURCE_DIR_NUVIO_LIBTORRENT="$libtorrent_source" \
    >"$configure_log" 2>&1; then
    cat "$configure_log" >&2
    exit 1
fi
if ! cmake --build "$build_root" --parallel "$jobs" >"$build_log" 2>&1; then
    cat "$build_log" >&2
    exit 1
fi
ctest --test-dir "$build_root" --output-on-failure

engine_version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build_root/CMakeCache.txt")
if [[ -z "$engine_version" ]]; then
    echo "could not resolve the configured engine version" >&2
    exit 1
fi
library="$build_root/libnuvio_engine.$engine_version.dylib"
if [[ ! -f "$library" ]]; then
    echo "built dylib is missing: $library" >&2
    exit 1
fi

cmake -E remove_directory "$build_root/package"
mkdir -p \
    "$stage/lib" \
    "$stage/include/nuvio_engine" \
    "$stage/jvm" \
    "$stage/licenses"
cp "$library" "$stage/lib/libnuvio_engine.dylib"
strip -x "$stage/lib/libnuvio_engine.dylib"
install_name_tool -id @rpath/libnuvio_engine.dylib "$stage/lib/libnuvio_engine.dylib"
codesign --force --sign - --timestamp=none "$stage/lib/libnuvio_engine.dylib"
"$engine_root/platform/jvm/gradlew" \
    clean test jar generatePomFileForJvmPublication \
    -Pnuvio.engine.testLibrary="$stage/lib/libnuvio_engine.dylib"
cp \
    "$engine_root/platform/jvm/build/libs/nuvio-engine-jvm-$engine_version.jar" \
    "$stage/jvm/"
cp \
    "$engine_root/platform/jvm/build/publications/jvm/pom-default.xml" \
    "$stage/jvm/nuvio-engine-jvm-$engine_version.pom"
cp "$engine_root/include/nuvio_engine/nuvio_engine.h" "$stage/include/nuvio_engine/"
cp "$engine_root/include/nuvio_engine/export.h" "$stage/include/nuvio_engine/"
cp "$engine_root/LICENSE" "$stage/licenses/NUVIO-ENGINE-LICENSE.txt"
cp "$engine_root/THIRD_PARTY_NOTICES.md" "$stage/licenses/THIRD_PARTY_NOTICES.md"
cp "$libtorrent_source/LICENSE" "$stage/licenses/LIBTORRENT-LICENSE.txt"
cp "$libtorrent_source/COPYING" "$stage/licenses/LIBTORRENT-COPYING.txt"
cp "$libtorrent_source/deps/try_signal/LICENSE" "$stage/licenses/TRY_SIGNAL-LICENSE.txt"
cp "$boost_source/LICENSE_1_0.txt" "$stage/licenses/BOOST-LICENSE_1_0.txt"
cp "$dependency_root/sources/openssl-3.5.7/LICENSE.txt" "$stage/licenses/OPENSSL-LICENSE.txt"
cp "$engine_root/README.md" "$stage/README.md"

compiler_version=$(xcrun clang++ --version | sed -n '1s/.*version \([^ ]*\).*/\1/p')
cmake_version=$(cmake --version | sed -n '1s/.*version //p')
cat > "$stage/BUILD-INFO.txt" <<EOF
Nuvio Engine: $engine_version
Target: macOS universal arm64 x86_64
Minimum OS: macOS 11.0
Libtorrent: 2.0.12 commit 740a0b9aeabe00e762cc0efe4a0f27593db2550b
Boost: 1.86.0 sha256 1bed88e40401b2cb7a1f76d4bab499e352fa4d0c5f31c0dbae64e24d34d7513b
OpenSSL: 3.5.7 sha256 a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8
JVM binding: $engine_version, Java 11 bytecode
Kotlin: 2.3.0
Kotlin Coroutines: 1.8.1
JNA: 5.19.1
AppleClang: $compiler_version
CMake: $cmake_version
EOF

smoke="$build_root/package-smoke"
xcrun clang \
    "$engine_root/tests/c_api_smoke.c" \
    -I "$stage/include" \
    -L "$stage/lib" \
    -lnuvio_engine \
    -Wl,-rpath,"$stage/lib" \
    -o "$smoke"
"$smoke"

archive="$distribution_root/$package_name.zip"
python3 "$engine_root/scripts/create-deterministic-zip.py" "$stage" "$archive"
(
    cd "$distribution_root"
    shasum -a 256 "$(basename "$archive")" > "$(basename "$archive").sha256"
)
"$engine_root/scripts/verify-macos-package.sh" "$archive"

echo "created $archive"
