from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "native" / "player" / "androidmedia3item.h"
SOURCE = ROOT / "native" / "player" / "androidmedia3item.cpp"
MAIN = (ROOT / "native" / "main.cpp").read_text(encoding="utf-8")
CMAKE = (ROOT / "native" / "CMakeLists.txt").read_text(encoding="utf-8")


def require(text: str, needle: str, reason: str) -> None:
    if needle not in text:
        raise AssertionError(f"{reason}: missing {needle!r}")


def test_facade_files_and_contract() -> None:
    if not HEADER.exists() or not SOURCE.exists():
        raise AssertionError("Android Media3 PlayerItem facade files must exist")
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    require(header, "class AndroidMedia3Item", "facade class")
    require(header, "public QQuickItem", "facade must render in Qt Quick")
    require(header, "public PlayerBackendContract", "facade must implement neutral backend")
    require(header, "AndroidMedia3State", "facade must own the accepted state core")
    require(header, "updatePaintNode", "facade must drive the OES render node")
    require(source, "Media3PlayerHost", "facade must own the accepted Java host")


def test_lifetime_safe_jni_bridge() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    require(source, "QPointer<AndroidMedia3Item>", "JNI registry must not retain dead facade pointers")
    require(source, "QMutex", "JNI registry must be synchronized")
    for callback in (
        "nativeOnReady", "nativeOnEnded", "nativeOnError", "nativeOnTimeline",
        "nativeOnVideoSize", "nativeOnFirstFrame", "nativeOnSeekDiscontinuity",
        "nativeOnTracks", "nativeOnMetadata", "nativeOnCues",
    ):
        require(source, f"Media3PlayerHost_{callback}", f"JNI callback {callback} must be implemented")


def test_renderer_handshake() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    require(source, "AndroidMedia3VideoNode", "facade must use accepted OES renderer")
    require(source, "setVideoSurface", "renderer surface must attach to Media3")
    require(source, "clearVideoSurface", "renderer surface teardown must be synchronous")
    require(source, "QMetaObject::invokeMethod", "cross-thread frame scheduling must queue to the item")


def test_android_only_registration_and_build_graph() -> None:
    require(MAIN, "qmlRegisterType<AndroidMedia3Item>", "Android must register Media3 as PlayerItem")
    require(MAIN, "#if defined(Q_OS_ANDROID)", "PlayerItem registration must be platform selected")
    require(CMAKE, "player/androidmedia3item.cpp", "Android facade source must enter the Android target")
    require(CMAKE, "player/androidmedia3videonode.cpp", "accepted renderer must enter the Android target")


def test_shared_qml_surface_is_preserved() -> None:
    header = HEADER.read_text(encoding="utf-8")
    for name in (
        "position", "duration", "pause", "volume", "mute", "speed",
        "audioTracks", "subtitleTracks", "chapters", "currentUrl",
        "decodedWidth", "decodedHeight", "cacheTime", "cacheBufferingState",
        "coreSeeking", "mediaTitle", "capabilities",
    ):
        require(header, name, f"shared QML property {name}")
    for method in (
        "loadSource", "stopPlayback", "seekExact", "seekStep",
        "applyPlaybackProfile", "refreshAudioOutput", "playbackStat",
        "setHostLifecycleState", "setAudioFocusState",
        "releaseVideoSurface", "restoreVideoSurface",
    ):
        require(header, method, f"shared QML operation {method}")
    for signal in ("fileStarted", "fileLoaded", "endFile", "playbackError"):
        require(header, signal, f"shared QML signal {signal}")


if __name__ == "__main__":
    test_facade_files_and_contract()
    test_lifetime_safe_jni_bridge()
    test_renderer_handshake()
    test_android_only_registration_and_build_graph()
    test_shared_qml_surface_is_preserved()
    print("android media3 item contract: PASS")
