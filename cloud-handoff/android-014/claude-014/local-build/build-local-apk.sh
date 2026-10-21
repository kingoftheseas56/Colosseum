#!/usr/bin/env bash
# Local Windows (Git Bash) arm64 Nuvio diagnostic APK build. Mirrors
# scripts/android/build_apk.sh + the task014 Actions diagnostic path:
# same configure arguments, the source -> overlay -> APK correspondence gate
# with skips as failures, the runtime bundle and OpenSSL pair checks, and the
# final identity/ELF/16 KiB/engine-member verification.
#
# Usage: build-local-apk.sh <repo> <diagnostic-package> [out-dir]
# Everything lives on D:\ColosseumAndroid. The engine prefix is prepared and
# stamp-verified on this host (prepare_nuvio_engine.py); never transplanted.
set -euo pipefail
REPO="$(cd "${1:?repo}" && pwd)"; PKG="${2:?package}"
ROOT=/d/ColosseumAndroid
OUT="${3:-$ROOT/out/$(git -C "$REPO" rev-parse --short=8 HEAD)-${PKG##*.}}"
export ANDROID_SDK_ROOT=D:/ColosseumAndroid/sdk
export ANDROID_NDK_ROOT=D:/ColosseumAndroid/sdk/ndk/27.2.12479018
export JAVA_HOME="C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot"
export READELF=/d/ColosseumAndroid/sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe
export PATH="/d/ColosseumAndroid/bin:/c/Qt/Tools/CMake_64/bin:/c/Qt/Tools/Ninja:$JAVA_HOME/bin:$PATH"
QT_ANDROID=D:/ColosseumAndroid/Qt/6.11.1/android_arm64_v8a
QT_HOST=C:/Qt/6.11.1/msvc2022_64
DEPS=D:/ColosseumAndroid/deps/arm64-v8a
ENGINE=D:/ColosseumAndroid/nuvio-prefix
BUILD=$ROOT/build/arm64-v8a-dqt
mkdir -p "$OUT"
LOG="$OUT/build.log"; : > "$LOG"
say() { echo "$*" | tee -a "$LOG"; }

[[ -f "$DEPS/.colosseum-deps-stamp" ]] || { say "missing deps prefix $DEPS"; exit 1; }
say "source_commit=$(git -C "$REPO" rev-parse HEAD) dirty=$(git -C "$REPO" status --porcelain -- native qml | wc -l)"
python -I "$REPO/scripts/android/verify_nuvio_hardened_artifact.py" --prefix "$ENGINE" --abi arm64-v8a 2>&1 | tee -a "$LOG"

ARGS=(-DCOLOSSEUM_ANDROID_NUVIO_BACKEND=ON "-DCOLOSSEUM_NUVIO_ENGINE_PREFIX=$ENGINE"
      "-DCOLOSSEUM_NUVIO_DIAGNOSTIC_PACKAGE=$PKG" -DCOLOSSEUM_ANDROID_NUVIO_X86_64_DIAGNOSTIC=OFF)
printf '%s\n' "${ARGS[@]}" > "$OUT/configure-args.txt"
"$QT_ANDROID/bin/qt-cmake.bat" -S "$REPO/native" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DQT_HOST_PATH="$QT_HOST" \
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
    -DCMAKE_PREFIX_PATH="$DEPS" -DCMAKE_FIND_ROOT_PATH="$DEPS" \
    -DOPENSSL_ROOT_DIR="$DEPS" -DOPENSSL_USE_STATIC_LIBS=ON \
    -DOPENSSL_INCLUDE_DIR="$DEPS/include" \
    -DOPENSSL_SSL_LIBRARY="$DEPS/lib/libssl.a" -DOPENSSL_CRYPTO_LIBRARY="$DEPS/lib/libcrypto.a" \
    -DBoost_ROOT="$DEPS" -DCOLOSSEUM_NO_COMPILER_CACHE=ON -DBUILD_TESTING=OFF \
    "${ARGS[@]}" >> "$LOG" 2>&1 || { tail -30 "$LOG"; exit 1; }
cmake --build "$BUILD" --target apk --parallel "${JOBS:-8}" >> "$LOG" 2>&1 || { grep -iE "error" "$LOG" | tail -30; exit 1; }
grep -E '^(ANDROID_ABI|CMAKE_BUILD_TYPE|COLOSSEUM_ANDROID_NUVIO_BACKEND|COLOSSEUM_ANDROID_NUVIO_X86_64_DIAGNOSTIC|COLOSSEUM_NUVIO_ENGINE_PREFIX|COLOSSEUM_NUVIO_DIAGNOSTIC_PACKAGE):' \
    "$BUILD/CMakeCache.txt" > "$OUT/cmake-cache-identity.txt"

apk=$(find "$BUILD" -path '*/outputs/apk/*' -name '*.apk' -newer "$BUILD/CMakeCache.txt" | head -1)
[[ -n "$apk" ]] || apk=$(find "$BUILD" -path '*/outputs/apk/*' -name '*.apk' | head -1)
[[ -n "$apk" ]] || { say "no APK produced"; exit 1; }
COLOSSEUM_REQUIRE_PACKAGING_GATE=1 NUVIO_ANDROID_BUILD_DIR="$BUILD" NUVIO_ANDROID_PACKAGING_CANDIDATE="$apk" \
    python -I "$REPO/tests/test_android_runtime_packaging.py" 2>&1 | tee -a "$LOG"
grep -q '^PACKAGING_GATE_OK tests=6 skipped=0' "$LOG" || { say "packaging gate failed"; exit 1; }
python "$REPO/tests/verify_android_apk_runtime_bundle.py" "$apk" --qmlcachegen "$QT_HOST/bin/qmlcachegen.exe" 2>&1 | tee -a "$LOG"
grep -q '^ANDROID_RUNTIME_BUNDLE_OK' "$LOG" || { say "runtime bundle check failed"; exit 1; }
bash "$REPO/scripts/android/verify_apk_openssl_pair.sh" "$apk" arm64-v8a 2>&1 | tee -a "$LOG"
grep -q '^APK_OPENSSL_RUNTIME_PAIR_OK' "$LOG" || { say "OpenSSL pair check failed"; exit 1; }
cp "$apk" "$OUT/colosseum-arm64-v8a-debug.apk"
(cd "$OUT" && sha256sum colosseum-arm64-v8a-debug.apk > colosseum-arm64-v8a-debug.apk.sha256)
say "LOCAL_APK_BUILD_OK $(cat "$OUT/colosseum-arm64-v8a-debug.apk.sha256")"
