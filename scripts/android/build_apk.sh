#!/usr/bin/env bash
# Build one Android ABI. Debug remains the default; release-unsigned is an
# explicit arm64-only preview lane with a separate build/output directory.
#
# Usage: scripts/android/build_apk.sh <arm64-v8a|x86_64> [output-dir]
# Requires scripts/android/install_toolchain.sh env plus build_deps.sh prefix.
set -euo pipefail

ABI="${1:?usage: build_apk.sh <arm64-v8a|x86_64> [output-dir]}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_ROOT="${COLOSSEUM_ANDROID_BUILD:-$REPO/native/build-android}"
FLAVOUR="${COLOSSEUM_ANDROID_APK_FLAVOUR:-debug}"
if [[ $# -ge 2 ]]; then
    OUT_DIR="$2"
elif [[ "$FLAVOUR" == "release-unsigned" ]]; then
    OUT_DIR="$BUILD_ROOT/out-release-unsigned"
else
    OUT_DIR="$BUILD_ROOT/out"
fi
JOBS="${JOBS:-$(nproc)}"
case "$ABI" in
    arm64-v8a) QT_ARCH=android_arm64_v8a ;;
    x86_64)    QT_ARCH=android_x86_64 ;;
    *) echo "unsupported ABI: $ABI" >&2; exit 2 ;;
esac
TOOLCHAIN="${COLOSSEUM_ANDROID_TOOLCHAIN:-$HOME/colosseum-android}"
QT_ANDROID_ROOT="${COLOSSEUM_QT_ANDROID_ROOT:-$TOOLCHAIN/Qt/${QT_VERSION:-6.11.1}/$QT_ARCH}"
DEPS_PREFIX="${COLOSSEUM_ANDROID_DEPS:-$HOME/colosseum-android/deps}/$ABI"
HELPER="$REPO/scripts/android/release_apk.py"

mapfile -t BUILD_CONFIG < <(python3 "$HELPER" config --abi "$ABI" --build-root "$BUILD_ROOT" --out-dir "$OUT_DIR")
[[ ${#BUILD_CONFIG[@]} -eq 3 ]] || { echo "invalid Android build configuration" >&2; exit 2; }
BUILD_DIR="${BUILD_CONFIG[0]}"
CONFIG_FLAVOUR="${BUILD_CONFIG[1]}"
OUTPUT_NAME="${BUILD_CONFIG[2]}"
[[ "$CONFIG_FLAVOUR" == "$FLAVOUR" ]] || { echo "build helper flavour mismatch" >&2; exit 2; }

: "${COLOSSEUM_QT_HOST_ROOT:?source the toolchain env.sh first}"
: "${ANDROID_SDK_ROOT:?}" "${ANDROID_NDK_ROOT:?}"
[[ -x "$QT_ANDROID_ROOT/bin/qt-cmake" ]] || { echo "missing Qt kit: $QT_ANDROID_ROOT" >&2; exit 1; }
[[ -f "$DEPS_PREFIX/.colosseum-deps-stamp" ]] || { echo "missing deps: $DEPS_PREFIX (run build_deps.sh)" >&2; exit 1; }

read -r -a EXTRA_ARGS <<< "${COLOSSEUM_ANDROID_CMAKE_ARGS:-}"
CMAKE_ARGS=(
    -S "$REPO/native" -B "$BUILD_DIR" -G Ninja
    -DQT_HOST_PATH="$COLOSSEUM_QT_HOST_ROOT"
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT"
    -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT"
    -DCMAKE_PREFIX_PATH="$DEPS_PREFIX"
    -DCMAKE_FIND_ROOT_PATH="$DEPS_PREFIX"
    -DOPENSSL_ROOT_DIR="$DEPS_PREFIX"
    -DOPENSSL_USE_STATIC_LIBS=ON
    -DOPENSSL_INCLUDE_DIR="$DEPS_PREFIX/include"
    -DOPENSSL_SSL_LIBRARY="$DEPS_PREFIX/lib/libssl.a"
    -DOPENSSL_CRYPTO_LIBRARY="$DEPS_PREFIX/lib/libcrypto.a"
    -DBoost_ROOT="$DEPS_PREFIX"
    -DCOLOSSEUM_NO_COMPILER_CACHE="${COLOSSEUM_NO_COMPILER_CACHE:-OFF}"
    -DBUILD_TESTING=OFF
)
if [[ "$FLAVOUR" == "release-unsigned" ]]; then
    [[ "$ABI" == "arm64-v8a" ]] || { echo "release-unsigned is arm64-only" >&2; exit 2; }
    : "${COLOSSEUM_NUVIO_ENGINE_PREFIX:?release-unsigned requires the hardened engine prefix}"
    [[ -f "$COLOSSEUM_NUVIO_ENGINE_PREFIX/.colosseum-nuvio-hardened-stamp" ]] || {
        echo "missing hardened Nuvio engine stamp: $COLOSSEUM_NUVIO_ENGINE_PREFIX" >&2; exit 1; }
    python3 "$REPO/scripts/android/verify_nuvio_hardened_artifact.py" \
        --prefix "$COLOSSEUM_NUVIO_ENGINE_PREFIX" --abi arm64-v8a
    mkdir -p "$OUT_DIR"
    SOURCE_BEFORE="$OUT_DIR/source-before.json"
    python3 "$HELPER" source-snapshot --repo "$REPO" --output "$SOURCE_BEFORE"
    mapfile -t RELEASE_ARGS < <(python3 "$HELPER" release-cmake-args \
        --abi "$ABI" --build-root "$BUILD_ROOT" --out-dir "$OUT_DIR")
    [[ ${#RELEASE_ARGS[@]} -gt 0 ]] || { echo "release CMake flags failed validation" >&2; exit 2; }
    CMAKE_ARGS+=("${EXTRA_ARGS[@]}" "${RELEASE_ARGS[@]}")
else
    [[ "$FLAVOUR" == "debug" ]] || { echo "unsupported APK flavour: $FLAVOUR" >&2; exit 2; }
    CMAKE_ARGS+=(-DCMAKE_BUILD_TYPE=Debug "${EXTRA_ARGS[@]}")
fi

"$QT_ANDROID_ROOT/bin/qt-cmake" "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" --target apk --parallel "$JOBS"

if [[ "$FLAVOUR" == "release-unsigned" ]]; then
    apk=$(python3 "$HELPER" select-qt-apk --build-dir "$BUILD_DIR")
else
    apk=$(find "$BUILD_DIR" -path '*/outputs/apk/*' -name '*.apk' -newer "$BUILD_DIR/CMakeCache.txt" | head -1)
    [[ -n "$apk" ]] || apk=$(find "$BUILD_DIR" -path '*/outputs/apk/*' -name '*.apk' | head -1)
fi
[[ -n "$apk" && -f "$apk" ]] || { echo "no APK produced under $BUILD_DIR" >&2; exit 1; }

# Correspondence, not just self-consistency: source -> generated overlay -> APK.
COLOSSEUM_REQUIRE_PACKAGING_GATE=1 NUVIO_ANDROID_BUILD_DIR="$BUILD_DIR" \
    NUVIO_ANDROID_PACKAGING_CANDIDATE="$apk" \
    python3 -I "$REPO/tests/test_android_runtime_packaging.py"
python3 "$REPO/tests/verify_android_apk_runtime_bundle.py" "$apk" \
    --qmlcachegen "$COLOSSEUM_QT_HOST_ROOT/libexec/qmlcachegen"
bash "$REPO/scripts/android/verify_apk_openssl_pair.sh" "$apk" "$ABI"

mkdir -p "$OUT_DIR"
if [[ "$FLAVOUR" == "release-unsigned" ]]; then
    python3 "$HELPER" verify-source-snapshots --repo "$REPO" \
        --before "$SOURCE_BEFORE" --after-output "$OUT_DIR/source-after.json"
    BUILD_TOOLS="$ANDROID_SDK_ROOT/build-tools/36.0.0"
    [[ -x "$BUILD_TOOLS/aapt" && -x "$BUILD_TOOLS/zipalign" ]] || {
        echo "missing aapt or zipalign in $BUILD_TOOLS" >&2; exit 1; }
    PACKAGE="${COLOSSEUM_ANDROID_PREVIEW_PACKAGE:-org.qtproject.example.colosseum.previewnuvio1}"
    VERSION_CODE="${COLOSSEUM_ANDROID_VERSION_CODE:-2}"
    python3 "$HELPER" verify-unsigned --repo "$REPO" --apk "$apk" --package "$PACKAGE" \
        --version-code "$VERSION_CODE" --aapt "$BUILD_TOOLS/aapt" \
        --zipalign "$BUILD_TOOLS/zipalign" \
        --engine-library "$COLOSSEUM_NUVIO_ENGINE_PREFIX/android/$ABI/lib/libnuvio_engine.so" \
        --output "$OUT_DIR/release-verification.json"
    cp "$apk" "$OUT_DIR/$OUTPUT_NAME"
else
    if [[ -n "${COLOSSEUM_ANDROID_KEYSTORE:-}" ]]; then
        : "${COLOSSEUM_ANDROID_KEYSTORE_PASSWORD:?keystore password required}"
        apksigner=$(ls "$ANDROID_SDK_ROOT"/build-tools/36.0.0/apksigner)
        "$apksigner" sign --ks "$COLOSSEUM_ANDROID_KEYSTORE" \
            --ks-key-alias "${COLOSSEUM_ANDROID_KEY_ALIAS:-colosseum-test}" \
            --ks-pass env:COLOSSEUM_ANDROID_KEYSTORE_PASSWORD \
            --key-pass env:COLOSSEUM_ANDROID_KEYSTORE_PASSWORD \
            --out "$OUT_DIR/$OUTPUT_NAME" "$apk"
        "$apksigner" verify --print-certs "$OUT_DIR/$OUTPUT_NAME" | grep -E 'SHA-256|DN' >&2
    else
        echo "[android-apk] no COLOSSEUM_ANDROID_KEYSTORE: keeping the per-machine Qt debug signature" >&2
        cp "$apk" "$OUT_DIR/$OUTPUT_NAME"
    fi
fi
(cd "$OUT_DIR" && sha256sum "$OUTPUT_NAME" > "$OUTPUT_NAME.sha256")
cat "$OUT_DIR/$OUTPUT_NAME.sha256"
