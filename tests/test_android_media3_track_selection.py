from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JAVA = (ROOT / "native/platform/android/src/org/colosseum/player/Media3PlayerHost.java").read_text(encoding="utf-8")
CPP = (ROOT / "native/player/androidmedia3item.cpp").read_text(encoding="utf-8")


def require(text: str, token: str, why: str) -> None:
    if token not in text:
        raise AssertionError(f"{why}: missing {token!r}")


def test_embedded_track_selection_reaches_media3() -> None:
    require(JAVA, "public void selectAudioTrack(String encodedId)", "Java host needs audio selection")
    require(JAVA, "public void selectSubtitleTrack(String encodedId)", "Java host needs subtitle selection")
    require(JAVA, "TrackSelectionOverride", "Media3 override must select the encoded track")
    require(JAVA, "setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true)", "empty subtitle selection must disable text")
    require(CPP, 'callHostTrack("selectAudioTrack", value)', "C++ audio property must reach Java")
    require(CPP, 'callHostTrack("selectSubtitleTrack", value)', "C++ subtitle property must reach Java")


if __name__ == "__main__":
    test_embedded_track_selection_reaches_media3()
    print("android media3 track selection: PASS")
