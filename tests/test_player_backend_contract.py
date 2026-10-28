from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLAYER_PAGE = (ROOT / "qml" / "PlayerPage.qml").read_text(encoding="utf-8")
AUDIOBOOK = (ROOT / "qml" / "AudiobookSession.qml").read_text(encoding="utf-8")
MPV_HEADER = (ROOT / "native" / "player" / "mpvitem.h").read_text(encoding="utf-8")
BACKEND_CONTRACT = (ROOT / "native" / "player" / "playerbackendcontract.h").read_text(encoding="utf-8")
MAIN = (ROOT / "native" / "main.cpp").read_text(encoding="utf-8")
READER_HARNESS = (ROOT / "native" / "reader2" / "reader2_harness_main.cpp").read_text(encoding="utf-8")


def require(text: str, needle: str, reason: str) -> None:
    if needle not in text:
        raise AssertionError(f"{reason}: missing {needle!r}")


def forbid(text: str, needle: str, reason: str) -> None:
    if needle in text:
        raise AssertionError(f"{reason}: found {needle!r}")


def test_neutral_qml_type() -> None:
    require(MAIN, '"PlayerItem"', "desktop must register the neutral player type")
    require(PLAYER_PAGE, "PlayerItem {", "video player must instantiate the neutral type")
    require(AUDIOBOOK, "PlayerItem {", "audiobook playback must instantiate the neutral type")
    require(READER_HARNESS, '"PlayerItem"', "reader harness must register the neutral player type")


def test_neutral_operations() -> None:
    for surface_name, surface in (("PlayerPage", PLAYER_PAGE), ("AudiobookSession", AUDIOBOOK)):
        forbid(surface, "mpv.command(", f"{surface_name} must not issue raw libmpv commands")
        forbid(surface, "mpv.setProperty(", f"{surface_name} must not set raw libmpv properties")
        forbid(surface, "mpv.mpvProperty(", f"{surface_name} must not read raw libmpv properties")

    for method in (
        "loadSource", "stopPlayback", "applyPlaybackProfile",
        "refreshAudioOutput", "playbackStat", "capabilities",
        "setHostLifecycleState", "setAudioFocusState",
        "releaseVideoSurface", "restoreVideoSurface",
    ):
        require(MPV_HEADER, method, f"MpvItem must implement neutral operation {method}")

    require(MPV_HEADER, "public PlayerBackendContract",
            "desktop backend must implement the portable host contract")
    for method in ("loadSource", "stopPlayback", "setHostLifecycleState",
                   "setAudioFocusState", "releaseVideoSurface", "restoreVideoSurface"):
        require(BACKEND_CONTRACT, method, f"portable backend contract must define {method}")


def test_capability_gates() -> None:
    require(PLAYER_PAGE, "supportsPlayerCapability", "shared QML must query backend capabilities")
    for capability in ("frameCapture", "gifCapture", "frameStepping", "playbackStats",
                       "loudnessNormalization", "videoTransform", "audioDelay", "subtitleDelay"):
        require(PLAYER_PAGE, f'supportsPlayerCapability("{capability}")',
                f"shared QML must gate optional capability {capability}")


def test_sources_converge_on_one_entrypoint() -> None:
    require(PLAYER_PAGE, "mpv.loadSource(directUrl, requestHeaders)",
            "header-carrying direct HTTP playback must use the neutral source entrypoint")
    require(PLAYER_PAGE, "mpv.loadSource(url)",
            "torrent localhost playback must use the same neutral source entrypoint")
    forbid(PLAYER_PAGE, "mpv.loadFile(", "PlayerPage must not bypass the neutral source entrypoint")
    forbid(PLAYER_PAGE, "mpv.loadFileWithHeaders(",
           "PlayerPage must not bypass the neutral source entrypoint")


if __name__ == "__main__":
    test_neutral_qml_type()
    test_neutral_operations()
    test_capability_gates()
    test_sources_converge_on_one_entrypoint()
    print("player backend contract: PASS")
