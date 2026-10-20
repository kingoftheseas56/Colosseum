#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 x86_64|aarch64" >&2
    exit 2
fi
target_architecture=$1
if [[ "$(uname -s)" != Linux || "$(uname -m)" != "$target_architecture" ]]; then
    echo "this build must run in the matching Linux container" >&2
    exit 2
fi

engine_root=/source
dependency_root="$engine_root/platform/linux/.deps"
build_root="$engine_root/platform/linux/build/$target_architecture"
distribution_root="$engine_root/platform/linux/dist"
package_name="nuvio-engine-linux-$target_architecture"
stage="$build_root/package/$package_name"
openssl_root="$dependency_root/openssl/install/$target_architecture"
boost_source="$dependency_root/sources/boost_1_86_0"
libtorrent_source="$dependency_root/sources/libtorrent-2.0.12"
jobs=${NUVIO_BUILD_JOBS:-$(nproc)}
if (( jobs > 8 )); then
    jobs=8
fi

"$engine_root/scripts/prepare-native-dependencies.sh" "$dependency_root"
"$engine_root/scripts/build-linux-openssl.sh" "$dependency_root" "$target_architecture"
mkdir -p "$build_root" "$distribution_root"

configure_log="$build_root/configure.log"
build_log="$build_root/build.log"
if ! cmake \
    -S "$engine_root" \
    -B "$build_root" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS='-ffile-prefix-map=/source/platform/linux/.deps=/nuvio-engine/dependencies -ffile-prefix-map=/source=/nuvio-engine/source' \
    -DCMAKE_CXX_FLAGS='-ffile-prefix-map=/source/platform/linux/.deps=/nuvio-engine/dependencies -ffile-prefix-map=/source=/nuvio-engine/source' \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
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
if [[ ! "$engine_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "could not resolve the configured engine version" >&2
    exit 1
fi
engine_soversion=${engine_version%%.*}
library_name="libnuvio_engine.so.$engine_version"
if [[ ! -f "$build_root/$library_name" ]]; then
    echo "built shared library is missing: $build_root/$library_name" >&2
    exit 1
fi

cmake -E remove_directory "$build_root/package"
mkdir -p "$stage/lib" "$stage/include/nuvio_engine" "$stage/licenses"
cp "$build_root/$library_name" "$stage/lib/$library_name"
strip --strip-unneeded "$stage/lib/$library_name"
ln -s "$library_name" "$stage/lib/libnuvio_engine.so.$engine_soversion"
ln -s "libnuvio_engine.so.$engine_soversion" "$stage/lib/libnuvio_engine.so"
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

cat > "$stage/BUILD-INFO.txt" <<EOF
Nuvio Engine: $engine_version
Target: Linux $target_architecture
Minimum glibc: 2.35
Libtorrent: 2.0.12 commit 740a0b9aeabe00e762cc0efe4a0f27593db2550b
Boost: 1.86.0 sha256 1bed88e40401b2cb7a1f76d4bab499e352fa4d0c5f31c0dbae64e24d34d7513b
OpenSSL: 3.5.7 sha256 a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8
Builder: Ubuntu 22.04
Builder base: ubuntu:22.04@sha256:0e0a0fc6d18feda9db1590da249ac93e8d5abfea8f4c3c0c849ce512b5ef8982
GCC: $(g++ -dumpfullversion -dumpversion)
CMake: $(cmake --version | awk 'NR == 1 { print $3 }')
glibc: $(ldd --version | awk 'NR == 1 { print $NF }')
EOF

smoke="$build_root/package-smoke"
cc \
    "$engine_root/tests/c_api_smoke.c" \
    -I "$stage/include" \
    -L "$stage/lib" \
    -lnuvio_engine \
    -Wl,-rpath,"$stage/lib" \
    -o "$smoke"
"$smoke"

archive="$distribution_root/$package_name.tar.gz"
tar \
    --sort=name \
    --mtime='@0' \
    --owner=0 \
    --group=0 \
    --numeric-owner \
    -C "$build_root/package" \
    -cf - \
    "$package_name" \
    | gzip -n > "$archive"
(
    cd "$distribution_root"
    sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256"
)

"$engine_root/scripts/verify-linux-package-in-container.sh" \
    "$archive" \
    "$target_architecture"

echo "created $archive"
