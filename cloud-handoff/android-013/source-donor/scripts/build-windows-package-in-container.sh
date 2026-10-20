#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 x86_64|aarch64" >&2
    exit 2
fi
target_architecture=$1
case "$target_architecture" in
    x86_64) target_triple=x86_64-w64-mingw32 ;;
    aarch64) target_triple=aarch64-w64-mingw32 ;;
    *) echo "unsupported Windows architecture: $target_architecture" >&2; exit 2 ;;
esac
if [[ "$(uname -s)" != Linux ]]; then
    echo "this build must run in the Windows package container" >&2
    exit 2
fi

engine_root=/source
dependency_root="$engine_root/platform/windows/.deps"
build_root="$engine_root/platform/windows/build/$target_architecture"
distribution_root="$engine_root/platform/windows/dist"
package_name="nuvio-engine-windows-$target_architecture"
stage="$build_root/package/$package_name"
openssl_root="$dependency_root/openssl/install/$target_architecture"
boost_source="$dependency_root/sources/boost_1_86_0"
libtorrent_source="$dependency_root/sources/libtorrent-2.0.12"
toolchain="$engine_root/cmake/windows-llvm-mingw-toolchain.cmake"
jobs=${NUVIO_BUILD_JOBS:-$(nproc)}
if (( jobs > 8 )); then
    jobs=8
fi

"$engine_root/scripts/prepare-native-dependencies.sh" "$dependency_root"
"$engine_root/scripts/build-windows-openssl.sh" "$dependency_root" "$target_architecture"
mkdir -p "$build_root" "$distribution_root"

common_flags="-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -ffile-prefix-map=/source/platform/windows/.deps=/nuvio-engine/dependencies -ffile-prefix-map=/source=/nuvio-engine/source -ffunction-sections -fdata-sections -mguard=cf"
linker_flags="-static -mguard=cf -Wl,--dynamicbase,--nxcompat,--high-entropy-va,--gc-sections,--exclude-all-symbols"
configure_log="$build_root/configure.log"
build_log="$build_root/build.log"
if ! cmake \
    -S "$engine_root" \
    -B "$build_root" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DNUVIO_WINDOWS_TRIPLE="$target_triple" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="$common_flags" \
    -DCMAKE_CXX_FLAGS="$common_flags" \
    -DCMAKE_SHARED_LINKER_FLAGS="$linker_flags" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DNUVIO_ENGINE_BUILD_SHARED=ON \
    -DNUVIO_ENGINE_BUILD_TESTS=OFF \
    -DNUVIO_ENGINE_ENABLE_LIBTORRENT=ON \
    -DOPENSSL_ROOT_DIR="$openssl_root" \
    -DOPENSSL_INCLUDE_DIR="$openssl_root/include" \
    -DOPENSSL_SSL_LIBRARY="$openssl_root/lib/libssl.a" \
    -DOPENSSL_CRYPTO_LIBRARY="$openssl_root/lib/libcrypto.a" \
    -DSSL_EAY="$openssl_root/lib/libssl.a" \
    -DLIB_EAY="$openssl_root/lib/libcrypto.a" \
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

engine_version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build_root/CMakeCache.txt")
if [[ ! "$engine_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "could not resolve the configured engine version" >&2
    exit 1
fi

cmake -E remove_directory "$build_root/package"
mkdir -p \
    "$stage/bin" \
    "$stage/lib" \
    "$stage/include/nuvio_engine" \
    "$stage/licenses" \
    "$stage/tools"
cp "$build_root/nuvio_engine.dll" "$stage/bin/nuvio_engine.dll"
cp "$build_root/libnuvio_engine.dll.a" "$stage/lib/libnuvio_engine.dll.a"
cp "$engine_root/cmake/nuvio-engine-windows.def" "$stage/lib/nuvio_engine.def"
/opt/llvm-mingw/bin/llvm-strip --strip-unneeded "$stage/bin/nuvio_engine.dll"
cp "$engine_root/include/nuvio_engine/nuvio_engine.h" "$stage/include/nuvio_engine/"
cp "$engine_root/include/nuvio_engine/export.h" "$stage/include/nuvio_engine/"
cp "$engine_root/LICENSE" "$stage/licenses/NUVIO-ENGINE-LICENSE.txt"
cp "$engine_root/THIRD_PARTY_NOTICES.md" "$stage/licenses/THIRD_PARTY_NOTICES.md"
cp "$libtorrent_source/LICENSE" "$stage/licenses/LIBTORRENT-LICENSE.txt"
cp "$libtorrent_source/COPYING" "$stage/licenses/LIBTORRENT-COPYING.txt"
cp "$libtorrent_source/deps/try_signal/LICENSE" "$stage/licenses/TRY_SIGNAL-LICENSE.txt"
cp "$boost_source/LICENSE_1_0.txt" "$stage/licenses/BOOST-LICENSE_1_0.txt"
cp "$dependency_root/sources/openssl-3.5.7/LICENSE.txt" "$stage/licenses/OPENSSL-LICENSE.txt"
cp /opt/llvm-mingw/LICENSE.TXT "$stage/licenses/LLVM-MINGW-LICENSE.txt"
cp "$engine_root/README.md" "$stage/README.md"

compiler_version=$("/opt/llvm-mingw/bin/${target_triple}-clang" --version | awk 'NR == 1 { print $4 }')
cat > "$stage/BUILD-INFO.txt" <<EOF
Nuvio Engine: $engine_version
Target: Windows $target_architecture
Minimum OS: Windows 10
Runtime: UCRT
Libtorrent: 2.0.12 commit 740a0b9aeabe00e762cc0efe4a0f27593db2550b
Boost: 1.86.0 sha256 1bed88e40401b2cb7a1f76d4bab499e352fa4d0c5f31c0dbae64e24d34d7513b
OpenSSL: 3.5.7 sha256 a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8
llvm-mingw: 20260407 LLVM $compiler_version UCRT
CMake: $(cmake --version | awk 'NR == 1 { print $3 }')
Builder base: ubuntu:22.04@sha256:0e0a0fc6d18feda9db1590da249ac93e8d5abfea8f4c3c0c849ce512b5ef8982
EOF

"/opt/llvm-mingw/bin/${target_triple}-clang" \
    "$engine_root/tests/c_api_smoke.c" \
    -DNUVIO_ENGINE_USING_SHARED \
    -I "$stage/include" \
    -L "$stage/lib" \
    -lnuvio_engine \
    -mguard=cf \
    -Wl,--dynamicbase,--nxcompat,--high-entropy-va \
    -o "$stage/tools/nuvio_engine_smoke.exe"

archive="$distribution_root/$package_name.zip"
python3 "$engine_root/scripts/create-deterministic-zip.py" "$stage" "$archive"
(
    cd "$distribution_root"
    sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256"
)
"$engine_root/scripts/verify-windows-package-in-container.sh" \
    "$archive" \
    "$target_architecture"

echo "created $archive"
