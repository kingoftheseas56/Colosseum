#!/usr/bin/env bash
# Build the upstream Media3 1.11.0 FFmpeg decoder as a locally packaged AAR.
# The FFmpeg native code is configured LGPL-only. Never substitute a GPL AAR.
# Runs on Linux CI / Linux developers with the Android SDK and NDK installed.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ANDROID_DIR="$REPO/native/platform/android"
AAR="$ANDROID_DIR/libs/media3-ffmpeg-lgpl-1.11.0.aar"
MEDIA_COMMIT=2bc207851df311340767e913931ca7b28cab1794
FFMPEG_COMMIT=ea3d24bbe3c58b171e55fe2151fc7ffaca3ab3d2

: "${ANDROID_SDK_ROOT:?ANDROID_SDK_ROOT is required}"
: "${ANDROID_NDK_ROOT:?ANDROID_NDK_ROOT is required}"
[[ -d "$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64" ]] || {
    echo "Expected Linux Android NDK toolchain at $ANDROID_NDK_ROOT" >&2
    exit 1
}

if [[ -f "$AAR" ]]; then
    echo "Using existing locally built FFmpeg extension: $AAR"
    exit 0
fi

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

# The upstream wrapper defaults to a substantially larger "-all" Gradle archive.
# CI does not need Gradle documentation/sources; keep the exact upstream
# Gradle 9.1.0 version and allow a realistic timeout for GitHub runners.
python3 - "$MEDIA/gradle/wrapper/gradle-wrapper.properties" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
source = path.read_text(encoding="utf-8")
original = "gradle-9.1.0-all.zip"
if source.count(original) != 1:
    raise SystemExit("Unexpected pinned Media3 Gradle wrapper distribution")
source = source.replace(original, "gradle-9.1.0-bin.zip")
source += "\nnetworkTimeout=120000\n"
path.write_text(source, encoding="utf-8")
PY

# The upstream module is Apache-2.0. Its generated native .so contains the
# separately licensed LGPL-only FFmpeg implementation. Build against exactly
# Media3 1.11.0: avoid 1.9.x AAR / 1.11.x API mismatches.
(
    cd "$MEDIA"
    ./gradlew --no-daemon --console=plain \
        -Dorg.gradle.jvmargs="-Xmx3g -XX:MaxMetaspaceSize=512m" \
        :lib-decoder-ffmpeg:assembleRelease
)

mapfile -t built < <(find "$MEDIA/libraries/decoder_ffmpeg" -type f \
    -path '*/outputs/aar/*' -name '*release.aar' | sort)
[[ ${#built[@]} -eq 1 ]] || {
    echo "Expected exactly one FFmpeg release AAR; found ${#built[@]}" >&2
    exit 1
}
mkdir -p "$(dirname "$AAR")"
cp "${built[0]}" "$AAR"

# Verify that all native ABIs, and the Java renderer loaded by reflection,
# actually made it into the local AAR before allowing APK compilation.
python3 - "$AAR" <<'PY'
from io import BytesIO
from pathlib import Path
from zipfile import ZipFile
import sys

aar = Path(sys.argv[1])
with ZipFile(aar) as archive:
    entries = set(archive.namelist())
    for abi in ("arm64-v8a", "x86_64"):
        native = f"jni/{abi}/libffmpegJNI.so"
        if native not in entries:
            raise SystemExit(f"FFmpeg AAR missing {native}")
    with ZipFile(BytesIO(archive.read("classes.jar"))) as classes:
        renderer = "androidx/media3/decoder/ffmpeg/FfmpegAudioRenderer.class"
        if renderer not in classes.namelist():
            raise SystemExit("FFmpeg AAR missing reflective renderer class")
PY

EVIDENCE="$REPO/native/build-android/ffmpeg-audio-evidence.txt"
mkdir -p "$(dirname "$EVIDENCE")"
{
    echo "Upstream Media3 release 1.11.0 commit $MEDIA_COMMIT (Apache-2.0)"
    echo "Upstream FFmpeg n6.0 commit $FFMPEG_COMMIT (LGPL-2.1-or-later)"
    echo "FFmpeg configure: --disable-everything --disable-gpl --disable-nonfree --disable-version3 --disable-iconv --enable-decoder={ac3,eac3,dca,truehd,mlp}"
    grep -E '^#define CONFIG_(GPL|NONFREE|VERSION3|ICONV) ' "$FFMPEG/config.h"
    sha256sum "$AAR"
} > "$EVIDENCE"
cat "$EVIDENCE"
