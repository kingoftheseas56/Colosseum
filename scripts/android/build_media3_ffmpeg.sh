#!/usr/bin/env bash
# Build AndroidX Media3 1.11.0's FFmpeg audio decoder from pinned sources.
# Java is compiled by Colosseum's existing Gradle build; FFmpeg ships as
# an LGPL-only libffmpegJNI.so, never as a GPL prebuilt AAR.
# Runs on Linux CI / Linux developers with the Android SDK and NDK installed.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ANDROID_DIR="$REPO/native/platform/android"
MEDIA_COMMIT=2bc207851df311340767e913931ca7b28cab1794
FFMPEG_COMMIT=ea3d24bbe3c58b171e55fe2151fc7ffaca3ab3d2

: "${ANDROID_SDK_ROOT:?ANDROID_SDK_ROOT is required}"
: "${ANDROID_NDK_ROOT:?ANDROID_NDK_ROOT is required}"
[[ -d "$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64" ]] || {
    echo "Expected Linux Android NDK toolchain at $ANDROID_NDK_ROOT" >&2
    exit 1
}

WORK="${COLOSSEUM_ANDROID_FFMPEG_WORK:-${RUNNER_TEMP:-$REPO/native/build-android}/media3-ffmpeg-build}"
MEDIA="$WORK/media"
FFMPEG_MAIN="$MEDIA/libraries/decoder_ffmpeg/src/main"
FFMPEG="$FFMPEG_MAIN/jni/ffmpeg"
mkdir -p "$WORK"

# Pin both source trees to their upstream release commits. A moved tag is a hard failure.
if [[ ! -d "$MEDIA/.git" ]]; then
    git clone --quiet --depth 1 --branch 1.11.0 \
        https://github.com/androidx/media.git "$MEDIA"
fi
[[ "$(git -C "$MEDIA" rev-parse HEAD)" == "$MEDIA_COMMIT" ]] || {
    echo "Media3 source SHA mismatch (expected $MEDIA_COMMIT)" >&2
    exit 1
}
if [[ ! -d "$FFMPEG/.git" ]]; then
    git clone --quiet --depth 1 --branch n6.0 \
        https://github.com/FFmpeg/FFmpeg.git "$FFMPEG"
fi
[[ "$(git -C "$FFMPEG" rev-parse HEAD)" == "$FFMPEG_COMMIT" ]] || {
    echo "FFmpeg source SHA mismatch (expected $FFMPEG_COMMIT)" >&2
    exit 1
}

# Google's build script already disables all features except explicitly enabled decoders.
# Make the non-GPL/nonfree boundary explicit rather than relying on FFmpeg defaults.
BUILDER="$FFMPEG_MAIN/jni/build_ffmpeg.sh"
python3 - "$BUILDER" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
source = path.read_text(encoding="utf-8")
anchor = "    --disable-everything"
replacement = """    --disable-everything
    --disable-gpl
    --disable-nonfree
    --disable-version3
    --disable-iconv"""
if source.count(anchor) != 1:
    raise SystemExit("The pinned upstream FFmpeg build script has changed")
path.write_text(source.replace(anchor, replacement), encoding="utf-8")
PY

export ANDROID_HOME="$ANDROID_SDK_ROOT"
export ANDROID_NDK_HOME="$ANDROID_NDK_ROOT"

# Build for the same two ABIs that Colosseum ships, plus upstream's other two
# supported Android ABIs. Gradle will package only the ABI selected by Qt.
# ffmpeg's dca decoder covers both DTS core and DTS-HD input.
bash "$BUILDER" "$FFMPEG_MAIN" "$ANDROID_NDK_ROOT" linux-x86_64 28 \
    ac3 eac3 dca truehd mlp

# Fail closed if FFmpeg actually enabled GPL, nonfree or version3 features.
for flag in GPL NONFREE VERSION3 ICONV; do
    grep -Eq "^#define CONFIG_${flag} 0$" "$FFMPEG/config.h" || {
        echo "FFmpeg build was not verified LGPL-2.1-only: CONFIG_$flag" >&2
        exit 1
    }
done
for abi in armeabi-v7a arm64-v8a x86 x86_64; do
    for lib in avcodec avutil swresample; do
        test -s "$FFMPEG/android-libs/$abi/lib$lib.a"
    done
done

# Copy only the Apache-2.0 FFmpeg *audio* Java classes. They compile against
# Colosseum's normal Media3 1.11.0 dependencies, so no separate Gradle download
# or Java artifact built using a potentially incompatible Media3 version exists.
JAVA_SOURCE="$FFMPEG_MAIN/java/androidx/media3/decoder/ffmpeg"
JAVA_TARGET="$ANDROID_DIR/src/androidx/media3/decoder/ffmpeg"
mkdir -p "$JAVA_TARGET"
for cls in FfmpegAudioDecoder FfmpegAudioRenderer FfmpegDecoderException FfmpegLibrary; do
    test -s "$JAVA_SOURCE/$cls.java"
    cp "$JAVA_SOURCE/$cls.java" "$JAVA_TARGET/$cls.java"
done

# Build Media3's Apache-2.0 JNI bridge with the normal Android NDK/CMake.
# Its statically linked FFmpeg libraries have already been checked LGPL-only;
# one independently replaceable libffmpegJNI.so is packaged per ABI.
command -v cmake >/dev/null
command -v ninja >/dev/null
for abi in arm64-v8a x86_64; do
    native_dir="$WORK/native-$abi"
    cmake -S "$FFMPEG_MAIN/jni" -B "$native_dir" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-28 \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "$native_dir" --target ffmpegJNI --parallel "${JOBS:-4}"
    lib="$native_dir/libffmpegJNI.so"
    test -s "$lib"
    mkdir -p "$ANDROID_DIR/libs/$abi"
    cp "$lib" "$ANDROID_DIR/libs/$abi/libffmpegJNI.so"
done
test -s "$JAVA_TARGET/FfmpegAudioRenderer.java"
test -s "$ANDROID_DIR/libs/arm64-v8a/libffmpegJNI.so"
test -s "$ANDROID_DIR/libs/x86_64/libffmpegJNI.so"

EVIDENCE="$REPO/native/build-android/ffmpeg-audio-evidence.txt"
mkdir -p "$(dirname "$EVIDENCE")"
{
    echo "Upstream Media3 release 1.11.0 commit $MEDIA_COMMIT (Apache-2.0)"
    echo "Upstream FFmpeg n6.0 commit $FFMPEG_COMMIT (LGPL-2.1-or-later)"
    echo "FFmpeg configure: --disable-everything --disable-gpl --disable-nonfree --disable-version3 --disable-iconv --enable-decoder={ac3,eac3,dca,truehd,mlp}"
    grep -E '^#define CONFIG_(GPL|NONFREE|VERSION3|ICONV) ' "$FFMPEG/config.h"
    sha256sum "$ANDROID_DIR/libs/arm64-v8a/libffmpegJNI.so" "$ANDROID_DIR/libs/x86_64/libffmpegJNI.so"
} > "$EVIDENCE"
cat "$EVIDENCE"
