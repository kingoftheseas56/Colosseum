#!/usr/bin/env bash
# Install the pinned Colosseum Android toolchain on a Linux x86_64 host.
#
#   Qt 6.11.1 (aqtinstall): host linux_gcc_64 + android_arm64_v8a + android_x86_64
#   Android SDK: cmdline-tools, platform-tools, platforms;android-36, build-tools;36.0.0
#   Android NDK: 27.2.12479018 (r27c)
#
# JDK 21 must already be on PATH (CI uses actions/setup-java).
# Nothing is written inside the repository. Idempotent: finished components are skipped.
#
# Environment (all optional):
#   COLOSSEUM_ANDROID_TOOLCHAIN  root for everything below   (default: $HOME/colosseum-android)
#   QT_VERSION                   Qt version                  (default: 6.11.1)
#   QT_ROOT                      aqt output dir              (default: $TOOLCHAIN/Qt)
#   ANDROID_SDK_ROOT             SDK dir                     (default: $TOOLCHAIN/android-sdk)
#   SKIP_QT=1 / SKIP_SDK=1       skip a component
#
# Prints shell exports on success (also written to $TOOLCHAIN/env.sh).
set -euo pipefail

TOOLCHAIN="${COLOSSEUM_ANDROID_TOOLCHAIN:-$HOME/colosseum-android}"
QT_VERSION="${QT_VERSION:-6.11.1}"
QT_ROOT="${QT_ROOT:-$TOOLCHAIN/Qt}"
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$TOOLCHAIN/android-sdk}"
NDK_VERSION="27.2.12479018"
PLATFORM="android-36"
BUILD_TOOLS="36.0.0"
CMDLINE_TOOLS_ZIP="commandlinetools-linux-13114758_latest.zip"
QT_ANDROID_MODULES=(qtwebsockets qtimageformats)
QT_HOST_MODULES=(qtwebsockets qtimageformats)

mkdir -p "$TOOLCHAIN"
log() { printf '[android-toolchain] %s\n' "$*" >&2; }

java_major=$(java -version 2>&1 | sed -nE 's/.*version "([0-9]+).*/\1/p' | head -1)
if [[ -z "$java_major" || "$java_major" -lt 21 ]]; then
    log "JDK 21+ required on PATH (found: ${java_major:-none})"
    exit 1
fi

if [[ "${SKIP_QT:-0}" != 1 ]]; then
    if ! command -v aqt >/dev/null 2>&1; then
        python3 -m pip install --quiet "aqtinstall==3.3.0"
    fi
    if [[ ! -x "$QT_ROOT/$QT_VERSION/gcc_64/bin/qmake" ]]; then
        log "Qt $QT_VERSION host linux_gcc_64"
        aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 \
            -O "$QT_ROOT" -m "${QT_HOST_MODULES[@]}" >&2
    fi
    for arch in android_arm64_v8a android_x86_64; do
        if [[ ! -x "$QT_ROOT/$QT_VERSION/$arch/bin/qt-cmake" ]]; then
            log "Qt $QT_VERSION $arch"
            aqt install-qt all_os android "$QT_VERSION" "$arch" \
                -O "$QT_ROOT" -m "${QT_ANDROID_MODULES[@]}" >&2
        fi
    done
fi

if [[ "${SKIP_SDK:-0}" != 1 ]]; then
    sdkmanager="$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager"
    if [[ ! -x "$sdkmanager" ]]; then
        log "Android cmdline-tools"
        tmp=$(mktemp -d)
        curl -fsSL -o "$tmp/cmdline-tools.zip" \
            "https://dl.google.com/android/repository/$CMDLINE_TOOLS_ZIP"
        unzip -q "$tmp/cmdline-tools.zip" -d "$tmp"
        mkdir -p "$ANDROID_SDK_ROOT/cmdline-tools"
        rm -rf "$ANDROID_SDK_ROOT/cmdline-tools/latest"
        mv "$tmp/cmdline-tools" "$ANDROID_SDK_ROOT/cmdline-tools/latest"
        rm -rf "$tmp"
    fi
    need=()
    [[ -d "$ANDROID_SDK_ROOT/platform-tools" ]] || need+=("platform-tools")
    [[ -d "$ANDROID_SDK_ROOT/platforms/$PLATFORM" ]] || need+=("platforms;$PLATFORM")
    [[ -d "$ANDROID_SDK_ROOT/build-tools/$BUILD_TOOLS" ]] || need+=("build-tools;$BUILD_TOOLS")
    [[ -f "$ANDROID_SDK_ROOT/ndk/$NDK_VERSION/source.properties" ]] || need+=("ndk;$NDK_VERSION")
    if ((${#need[@]})); then
        log "sdkmanager: ${need[*]}"
        yes | "$sdkmanager" --sdk_root="$ANDROID_SDK_ROOT" --licenses >/dev/null 2>&1 || true
        "$sdkmanager" --sdk_root="$ANDROID_SDK_ROOT" "${need[@]}" >/dev/null
    fi
fi

cat > "$TOOLCHAIN/env.sh" <<EOF
export COLOSSEUM_ANDROID_TOOLCHAIN="$TOOLCHAIN"
export QT_VERSION="$QT_VERSION"
export COLOSSEUM_QT_HOST_ROOT="$QT_ROOT/$QT_VERSION/gcc_64"
export ANDROID_SDK_ROOT="$ANDROID_SDK_ROOT"
export ANDROID_HOME="$ANDROID_SDK_ROOT"
export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/$NDK_VERSION"
EOF
log "toolchain ready: $TOOLCHAIN (source $TOOLCHAIN/env.sh)"
cat "$TOOLCHAIN/env.sh"
