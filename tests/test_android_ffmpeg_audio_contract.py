#!/usr/bin/env python3
"""Source-level contract for LGPL-only FFmpeg decoding in Colosseum Android."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
ANDROID = ROOT / "native/platform/android"


def read(path: Path) -> str:
    assert path.is_file(), f"Missing required file: {path}"
    return path.read_text(encoding="utf-8")


def main() -> None:
    gradle = read(ANDROID / "build.gradle")
    host = read(ANDROID / "src/org/colosseum/player/Media3PlayerHost.java")
    script = read(ROOT / "scripts/android/build_media3_ffmpeg.sh")
    apk_script = read(ROOT / "scripts/android/build_apk.sh")
    workflow = read(ROOT / ".github/workflows/android.yml")
    notice = read(ROOT / "THIRD_PARTY_NOTICES.md")
    lgpl = read(ANDROID / "assets/licenses/FFmpeg-LGPL-2.1.txt")
    ignore = read(ROOT / ".gitignore")

    # Platform codecs/passthrough must win over the software extension.
    assert "EXTENSION_RENDERER_MODE_ON" in host
    assert "EXTENSION_RENDERER_MODE_PREFER" not in host
    assert "setEnableDecoderFallback(true)" in host
    assert "onAudioDecoderInitialized" in host

    dependencies = re.findall(r"implementation 'androidx\.media3:([^:]+):([^']+)'", gradle)
    assert {version for _, version in dependencies} == {"1.11.0"}, dependencies
    assert {"media3-exoplayer", "media3-exoplayer-hls",
            "media3-exoplayer-dash", "media3-decoder"} <= {
                name for name, _ in dependencies
            }
    assert "implementation 'com.google.guava:guava:33.3.1-android'" in gradle
    assert "compileOnly 'org.checkerframework:checker-qual:3.13.0'" in gradle
    assert "implementation fileTree(dir: 'libs', include: ['*.jar', '*.aar'])" in gradle
    assert "org.jellyfin.media3" not in gradle
    assert "nextlib-media3ext" not in gradle

    assert "MEDIA_COMMIT=2bc207851df311340767e913931ca7b28cab1794" in script
    assert "FFMPEG_COMMIT=ea3d24bbe3c58b171e55fe2151fc7ffaca3ab3d2" in script
    for required in ("--disable-gpl", "--disable-nonfree",
                     "--disable-version3", "--disable-everything", "--disable-iconv",
                     "CONFIG_(GPL|NONFREE|VERSION3|ICONV)", "ac3 eac3 dca truehd mlp"):
        assert required in script, required
    assert "FfmpegAudioDecoder FfmpegAudioRenderer FfmpegDecoderException FfmpegLibrary" in script
    assert "JAVA_TARGET=" in script
    assert 'cmake -S "$FFMPEG_MAIN/jni"' in script
    assert '-DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake"' in script
    assert '-DANDROID_ABI="$abi"' in script
    assert "libffmpegJNI.so" in script
    assert 'for abi in arm64-v8a x86_64' in script
    assert "./gradlew" not in script and "GRADLE_ZIP=" not in script
    assert "libffmpegJNI.so" in apk_script
    assert "FfmpegAudioRenderer.java" in apk_script
    assert "build_media3_ffmpeg.sh" in apk_script
    assert "build_media3_ffmpeg.sh" in workflow
    assert "test_android_ffmpeg_audio_contract.py" in workflow
    assert "/native/platform/android/libs/*/libffmpegJNI.so" in ignore
    assert "/native/platform/android/src/androidx/media3/decoder/ffmpeg/*.java" in ignore

    assert "FFmpeg" in notice and "LGPL-2.1" in notice
    assert "GNU LESSER GENERAL PUBLIC LICENSE" in lgpl
    assert "Version 2.1" in lgpl
    print("Android LGPL-only FFmpeg audio source contract: PASS")


if __name__ == "__main__":
    main()
