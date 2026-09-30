#!/usr/bin/env bash
# Configure and build a single-ABI debug APK of Colosseum.
#
# Usage: scripts/android/build_apk.sh <arm64-v8a|x86_64> [output-dir]
#
# Requires the toolchain from scripts/android/install_toolchain.sh (source its env.sh) and the
# dependency prefix from scripts/android/build_deps.sh. The APK is copied to
# <output-dir>/colosseum-<abi>-debug.apk (default output-dir: native/build-android/out).
#
# Environment:
#   COLOSSEUM_QT_HOST_ROOT     Qt 6.11 linux gcc_64 kit (required)
#   ANDROID_SDK_ROOT, ANDROID_NDK_ROOT (required)
#   QT_VERSION / COLOSSEUM_ANDROID_TOOLCHAIN  locate the Android Qt kit
#   COLOSSEUM_QT_ANDROID_ROOT  override the Android Qt kit directly
#   COLOSSEUM_ANDROID_DEPS     deps root (default: $HOME/colosseum-android/deps)
#   COLOSSEUM_ANDROID_BUILD    build root (default: native/build-android, git-ignored)
#   JOBS                       parallelism (default: nproc)
#   COLOSSEUM_ANDROID_CMAKE_ARGS  extra configure arguments (word-split), e.g. ccache launchers
#   COLOSSEUM_ANDROID_KEYSTORE           optional test-only keystore; when set, the APK is re-signed
#   COLOSSEUM_ANDROID_KEYSTORE_PASSWORD  with it (apksigner) so successive builds install over
#   COLOSSEUM_ANDROID_KEY_ALIAS          each other. Never a release key. (alias default: colosseum-test)
set -euo pipefail

ABI="${1:?usage: build_apk.sh <arm64-v8a|x86_64> [output-dir]}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_ROOT="${COLOSSEUM_ANDROID_BUILD:-$REPO/native/build-android}"
OUT_DIR="${2:-$BUILD_ROOT/out}"
JOBS="${JOBS:-$(nproc)}"
case "$ABI" in
    arm64-v8a) QT_ARCH=android_arm64_v8a ;;
    x86_64)    QT_ARCH=android_x86_64 ;;
    *) echo "unsupported ABI: $ABI" >&2; exit 2 ;;
esac
TOOLCHAIN="${COLOSSEUM_ANDROID_TOOLCHAIN:-$HOME/colosseum-android}"
QT_ANDROID_ROOT="${COLOSSEUM_QT_ANDROID_ROOT:-$TOOLCHAIN/Qt/${QT_VERSION:-6.11.1}/$QT_ARCH}"
DEPS_PREFIX="${COLOSSEUM_ANDROID_DEPS:-$HOME/colosseum-android/deps}/$ABI"
BUILD_DIR="$BUILD_ROOT/$ABI"

: "${COLOSSEUM_QT_HOST_ROOT:?source the toolchain env.sh first}"
: "${ANDROID_SDK_ROOT:?}" "${ANDROID_NDK_ROOT:?}"
[[ -x "$QT_ANDROID_ROOT/bin/qt-cmake" ]] || { echo "missing Qt kit: $QT_ANDROID_ROOT" >&2; exit 1; }
[[ -f "$DEPS_PREFIX/.colosseum-deps-stamp" ]] || { echo "missing deps: $DEPS_PREFIX (run build_deps.sh)" >&2; exit 1; }

"$QT_ANDROID_ROOT/bin/qt-cmake" -S "$REPO/native" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DQT_HOST_PATH="$COLOSSEUM_QT_HOST_ROOT" \
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
    -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
    -DCMAKE_PREFIX_PATH="$DEPS_PREFIX" \
    -DCMAKE_FIND_ROOT_PATH="$DEPS_PREFIX" \
    -DOPENSSL_ROOT_DIR="$DEPS_PREFIX" \
    -DOPENSSL_USE_STATIC_LIBS=ON \
    -DBoost_ROOT="$DEPS_PREFIX" \
    -DCOLOSSEUM_NO_COMPILER_CACHE="${COLOSSEUM_NO_COMPILER_CACHE:-OFF}" \
    -DBUILD_TESTING=OFF \
    ${COLOSSEUM_ANDROID_CMAKE_ARGS:-}

cmake --build "$BUILD_DIR" --target apk --parallel "$JOBS"

apk=$(find "$BUILD_DIR" -path '*/outputs/apk/*' -name '*.apk' -newer "$BUILD_DIR/CMakeCache.txt" | head -1)
[[ -n "$apk" ]] || apk=$(find "$BUILD_DIR" -path '*/outputs/apk/*' -name '*.apk' | head -1)
[[ -n "$apk" ]] || { echo "no APK produced under $BUILD_DIR" >&2; exit 1; }
mkdir -p "$OUT_DIR"
if [[ -n "${COLOSSEUM_ANDROID_KEYSTORE:-}" ]]; then
    : "${COLOSSEUM_ANDROID_KEYSTORE_PASSWORD:?keystore password required}"
    apksigner=$(ls "$ANDROID_SDK_ROOT"/build-tools/36.0.0/apksigner)
    "$apksigner" sign --ks "$COLOSSEUM_ANDROID_KEYSTORE" \
        --ks-key-alias "${COLOSSEUM_ANDROID_KEY_ALIAS:-colosseum-test}" \
        --ks-pass env:COLOSSEUM_ANDROID_KEYSTORE_PASSWORD \
        --key-pass env:COLOSSEUM_ANDROID_KEYSTORE_PASSWORD \
        --out "$OUT_DIR/colosseum-$ABI-debug.apk" "$apk"
    "$apksigner" verify --print-certs "$OUT_DIR/colosseum-$ABI-debug.apk" | grep -E 'SHA-256|DN' >&2
else
    echo "[android-apk] no COLOSSEUM_ANDROID_KEYSTORE: keeping the per-machine Qt debug signature" >&2
    cp "$apk" "$OUT_DIR/colosseum-$ABI-debug.apk"
fi
(cd "$OUT_DIR" && sha256sum "colosseum-$ABI-debug.apk" > "colosseum-$ABI-debug.apk.sha256")
cat "$OUT_DIR/colosseum-$ABI-debug.apk.sha256"
