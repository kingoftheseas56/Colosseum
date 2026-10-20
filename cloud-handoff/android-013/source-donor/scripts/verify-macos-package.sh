#!/usr/bin/env bash
set -euo pipefail

script_directory=$(cd "$(dirname "$0")" && pwd -P)
engine_root=$(cd "$script_directory/.." && pwd -P)
archive=${1:-"$engine_root/platform/macos/dist/nuvio-engine-macos-universal.zip"}
checksum="$archive.sha256"

if [[ "$(uname -s)" != Darwin ]]; then
    echo "macOS package verification requires macOS" >&2
    exit 2
fi

fail() {
    echo "macOS package verification failed: $*" >&2
    exit 1
}

[[ -f "$archive" ]] || fail "missing $archive"
[[ -f "$checksum" ]] || fail "missing $checksum"
(
    cd "$(dirname "$archive")"
    shasum -a 256 -c "$(basename "$checksum")"
)

temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT
unzip -q "$archive" -d "$temporary_directory"
package="$temporary_directory/nuvio-engine-macos-universal"
library="$package/lib/libnuvio_engine.dylib"
[[ -f "$library" ]] || fail "dylib is missing"
[[ -f "$package/BUILD-INFO.txt" ]] || fail "build metadata is missing"
engine_version=$(sed -n 's/^Nuvio Engine: //p' "$package/BUILD-INFO.txt")
[[ "$engine_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "invalid engine version metadata"
jvm_jar="$package/jvm/nuvio-engine-jvm-$engine_version.jar"
jvm_pom="$package/jvm/nuvio-engine-jvm-$engine_version.pom"
[[ -f "$jvm_jar" ]] || fail "JVM binding is missing"
[[ -f "$jvm_pom" ]] || fail "JVM dependency metadata is missing"

architectures=$(xcrun lipo -archs "$library" | tr ' ' '\n' | sort | tr '\n' ' ')
[[ "$architectures" == "arm64 x86_64 " ]] || fail "unexpected architectures: $architectures"
for architecture in arm64 x86_64; do
    build_version=$(xcrun vtool -arch "$architecture" -show-build "$library")
    [[ "$build_version" == *"platform MACOS"* ]] || fail "$architecture is not a macOS slice"
    [[ "$build_version" == *"minos 11.0"* ]] || fail "$architecture does not target macOS 11.0"

    install_name=$(xcrun otool -arch "$architecture" -D "$library" | sed -n '$p')
    [[ "$install_name" == "@rpath/libnuvio_engine.dylib" ]] || fail "$architecture has an unexpected install name: $install_name"

    expected_symbols=$(LC_ALL=C sort "$engine_root/cmake/nuvio-engine-apple.exports")
    actual_symbols=$(xcrun nm -arch "$architecture" -gjU "$library" | LC_ALL=C sort -u)
    if [[ "$actual_symbols" != "$expected_symbols" ]]; then
        diff -u <(printf '%s\n' "$expected_symbols") <(printf '%s\n' "$actual_symbols") || true
        fail "$architecture export surface differs from the stable C ABI"
    fi

    dependencies=$(xcrun otool -arch "$architecture" -L "$library" | sed -n '3,$s/^[[:space:]]*\([^[:space:]]*\).*/\1/p')
    unexpected_dependencies=$(grep -Ev '^(/System/Library/Frameworks/(Security|CoreFoundation|SystemConfiguration)\.framework/|/usr/lib/lib(c\+\+\.1|System\.B)\.dylib$)' <<< "$dependencies" || true)
    [[ -z "$unexpected_dependencies" ]] || fail "$architecture has private dependencies: $unexpected_dependencies"
done

codesign --verify --strict --verbose=2 "$library"
xcrun strings "$library" > "$temporary_directory/library.strings"
grep -Eq 'OpenSSL 3\.5\.7' "$temporary_directory/library.strings" || fail "pinned OpenSSL is missing"
if grep -Fq "$engine_root" "$temporary_directory/library.strings"; then
    fail "dylib exposes the local repository path"
fi
if grep -Eq '/var/folders/|nuvio-apple-openssl-' "$temporary_directory/library.strings"; then
    fail "dylib exposes a temporary build path"
fi

cmp -s "$package/include/nuvio_engine/nuvio_engine.h" "$engine_root/include/nuvio_engine/nuvio_engine.h" || fail "C header mismatch"
cmp -s "$package/include/nuvio_engine/export.h" "$engine_root/include/nuvio_engine/export.h" || fail "export header mismatch"
cmp -s "$package/README.md" "$engine_root/README.md" || fail "README mismatch"
jar_entries=$(jar tf "$jvm_jar")
for entry in \
    com/nuvio/engine/NuvioEngine.class \
    com/nuvio/engine/NuvioEngineRuntime.class \
    com/nuvio/engine/internal/JnaNuvioNativeApi.class; do
    grep -Fxq "$entry" <<< "$jar_entries" || fail "JVM binding is missing $entry"
done
if grep -q '^com/sun/jna/' <<< "$jar_entries"; then
    fail "JVM binding unexpectedly bundles JNA"
fi
manifest=$(unzip -p "$jvm_jar" META-INF/MANIFEST.MF | tr -d '\r')
grep -Fxq 'Implementation-Title: Nuvio Engine JVM' <<< "$manifest" || fail "JVM manifest title mismatch"
grep -Fxq "Implementation-Version: $engine_version" <<< "$manifest" || fail "JVM manifest version mismatch"
javap -verbose -classpath "$jvm_jar" com.nuvio.engine.NuvioEngineRuntime | grep -Fq 'major version: 55' || fail "JVM binding does not target Java 11"
grep -Fq '<artifactId>kotlinx-coroutines-core-jvm</artifactId>' "$jvm_pom" || fail "coroutines dependency metadata is missing"
grep -Fq '<version>1.8.1</version>' "$jvm_pom" || fail "coroutines dependency version mismatch"
grep -Fq '<artifactId>jna</artifactId>' "$jvm_pom" || fail "JNA dependency metadata is missing"
grep -Fq '<version>5.19.1</version>' "$jvm_pom" || fail "JNA dependency version mismatch"
license_pairs=(
    "$package/licenses/NUVIO-ENGINE-LICENSE.txt:$engine_root/LICENSE"
    "$package/licenses/THIRD_PARTY_NOTICES.md:$engine_root/THIRD_PARTY_NOTICES.md"
    "$package/licenses/LIBTORRENT-LICENSE.txt:$engine_root/platform/apple/.deps/sources/libtorrent-2.0.12/LICENSE"
    "$package/licenses/LIBTORRENT-COPYING.txt:$engine_root/platform/apple/.deps/sources/libtorrent-2.0.12/COPYING"
    "$package/licenses/TRY_SIGNAL-LICENSE.txt:$engine_root/platform/apple/.deps/sources/libtorrent-2.0.12/deps/try_signal/LICENSE"
    "$package/licenses/BOOST-LICENSE_1_0.txt:$engine_root/platform/apple/.deps/sources/boost_1_86_0/LICENSE_1_0.txt"
    "$package/licenses/OPENSSL-LICENSE.txt:$engine_root/platform/apple/.deps/sources/openssl-3.5.7/LICENSE.txt"
)
for pair in "${license_pairs[@]}"; do
    packaged=${pair%%:*}
    source=${pair#*:}
    [[ -f "$packaged" ]] || fail "missing license $(basename "$packaged")"
    cmp -s "$packaged" "$source" || fail "license mismatch: $(basename "$packaged")"
done

smoke="$temporary_directory/nuvio-engine-smoke"
xcrun clang \
    "$engine_root/tests/c_api_smoke.c" \
    -I "$package/include" \
    -L "$package/lib" \
    -lnuvio_engine \
    -Wl,-rpath,"$package/lib" \
    -o "$smoke"
"$smoke"

echo "verified macOS universal package, ABI, JVM binding, TLS, metadata, paths, signatures, and licenses"
