"""The Android player keeps platform admission without shipping the Vault world."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def require(text: str, token: str, why: str) -> None:
    if token not in text:
        raise AssertionError(f"{why}: missing {token!r}")


def forbid(text: str, token: str, why: str) -> None:
    if token in text:
        raise AssertionError(f"{why}: found forbidden {token!r}")


def test_android_media_admission_is_world_independent() -> None:
    android_cmake = (ROOT / "native/cmake/android.cmake").read_text(encoding="utf-8")
    require(android_cmake, "player/MediaAdmissionProbe_android.cpp",
            "Android must provide admission without desktop libmpv")
    forbid(android_cmake, "engine/VaultThumbnailer_android.cpp",
           "Vault thumbnails must not be compiled into Android")
    java = ROOT / "native/platform/android/src/org/colosseum/media/MediaAdmissionProbe.java"
    assert java.is_file(), "Android's neutral media admission bridge is missing"
    java_text = java.read_text(encoding="utf-8")
    probe_text = (ROOT / "native/player/MediaAdmissionProbe_android.cpp").read_text(encoding="utf-8")
    require(java_text, "package org.colosseum.media;", "media admission has no Vault owner")
    require(java_text, "public final class MediaAdmissionProbe", "JNI class must match")
    require(java_text, "MediaMetadataRetriever", "Android must use platform media decoding")
    require(java_text, "getFrameAtTime", "admission must require decoded-frame evidence")
    require(java_text, 'result("Admitted", width, height, "")', "decoded frame admits video")
    require(java_text, '"RejectedNoVideo"', "audio-only media must be rejected")
    require(java_text, '"RejectedTimeout"', "admission must retain its timeout verdict")
    require(java_text, "future.get(Math.max(MIN_TIMEOUT_MS, timeoutMs), TimeUnit.MILLISECONDS)",
            "admission must remain bounded")
    for cleanup in ("future.cancel(true)", "executor.shutdownNow()", "frame.recycle()", "retriever.release()"):
        require(java_text, cleanup, "admission must release native and worker resources")
    require(probe_text, '"org/colosseum/media/MediaAdmissionProbe", "probe"',
            "native admission must call the neutral Android bridge")
    require(probe_text, '"(Landroid/content/Context;Ljava/lang/String;I)Ljava/lang/String;"',
            "JNI signature must match the Java admission method")
    for text in (java_text, probe_text):
        forbid(text, "Vault", "active media admission must have no Vault dependency")
        forbid(text, "mpv", "Android admission must not link libmpv")
    forbid(java_text, "writeThumbnail", "Vault's thumbnail writer must not ship with admission")
    forbid(java_text, "FileOutputStream", "admission must not write Vault artwork")
    require(java_text, 'value.startsWith("content://")', "Android document URIs remain supported")
    require(java_text, 'value.startsWith("file://")', "file URIs remain supported")


def test_desktop_media_backend_is_retained() -> None:
    # Desktop implementations remain intact; source graph platform checks live in
    # test_android_vault_contract.py, rather than relying on source-list placement.
    assert (ROOT / "native/player/MediaAdmissionProbe.cpp").is_file()
    thumb = (ROOT / "native/engine/VaultThumbnailer.cpp").read_text(encoding="utf-8")
    forbid(thumb, '#include "player/mpvitem.h"',
           "desktop Vault thumbnail code must retain its established media boundary")
    enrich = (ROOT / "native/engine/VaultEnricher.cpp").read_text(encoding="utf-8")
    require(enrich, "if (row.durationSec < 0.0)", "enrichment must preserve known duration")


def main() -> None:
    test_android_media_admission_is_world_independent()
    test_desktop_media_backend_is_retained()
    print("android media boundary: PASS")


if __name__ == "__main__":
    main()
