#!/usr/bin/env python3
"""Source-level guardrail for Colosseum's LGPL-only Android audio fallback."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
ANDROID = ROOT / "native" / "platform" / "android"


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"Required Android audio source missing: {path}")
    return path.read_text(encoding="utf-8")


def main() -> None:
    gradle = read(ANDROID / "build.gradle")
    host = read(ANDROID / "src/org/colosseum/player/Media3PlayerHost.java")
    build_script = read(ROOT / "scripts/android/build_media3_ffmpeg.sh")
    apk_script = read(ROOT / "scripts/android/build_apk.sh")
    workflow = read(ROOT / ".github/workflows/android.yml")
    notice = read(ROOT / "THIRD_PARTY_NOTICES.md")
    lgpl = read(ANDROID / "assets/licenses/FFmpeg-LGPL-2.1.txt")

    assert "EXTENSION_RENDERER_MODE_ON" in host
    assert "EXTENSION_RENDERER_MODE_PREFER" not in host
    assert "setEnableDecoderFallback(true)" in host
    assert "onAudioDecoderInitialized" in host

    dependencies = re.findall(r"implementation 'androidx\.media3:([^:]+):([^']+)'", gradle)
    versions = {version for name, version in dependencies}
    assert versions == {"1.11.0"}, f"Media3 mismatch: {dependencies!r}"
    assert {"media3-exoplayer", "media3-exoplayer-hls",
            "media3-exoplayer-dash", "media3-decoder"} <= {
                name for name, _ in dependencies
            }
    assert "implementation fileTree(dir: 'libs', include: ['*.jar', '*.aar'])" in gradle
    assert "org.jellyfin.media3" not in gradle
    assert "nextlib-media3ext" not in gradle

    assert "MEDIA_COMMIT=2bc207851df311340767e913931ca7b28cab1794" in build_script
    assert "FFMPEG_COMMIT=ea3d24bbe3c58b171e55fe2151fc7ffaca3ab3d2" in build_script
    for flag in ("--disable-gpl", "--disable-nonfree",
                 "--disable-version3", "--disable-iconv", "--disable-everything"):
        assert flag in build_script, f"Missing required LGPL boundary: {flag}"
    assert "ac3 eac3 dca truehd mlp" in build_script
    assert "CONFIG_" in build_script
    assert "CONFIG_(GPL|NONFREE|VERSION3|ICONV)" in build_script
    assert "gradle-9.1.0-bin.zip" in build_script
    assert "a17ddd85a26b6a7f5ddb71ff8b05fc5104c0202c6e64782429790c933686c806" in build_script
    assert "sha256sum --check" in build_script
    assert "downloads.gradle.org/distributions" in build_script
    assert "gradle-9.1.0/bin/gradle" in build_script
    assert ":lib-decoder-ffmpeg:assembleRelease" in build_script
    assert "libffmpegJNI.so" in build_script
    assert "media3-ffmpeg-lgpl-1.11.0.aar" in apk_script
    assert "build_media3_ffmpeg.sh" in apk_script
    assert "build_media3_ffmpeg.sh" in workflow
    assert "test_android_ffmpeg_audio_contract.py" in workflow

    assert "FFmpeg" in notice and "LGPL-2.1" in notice
    assert "GNU LESSER GENERAL PUBLIC LICENSE" in lgpl
    assert "Version 2.1" in lgpl
    print("Android LGPL-only FFmpeg audio source contract: PASS")


if __name__ == "__main__":
    main()
