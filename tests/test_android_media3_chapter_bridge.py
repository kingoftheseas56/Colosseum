from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JAVA = (ROOT / "native/platform/android/src/org/colosseum/player/Media3PlayerHost.java").read_text(encoding="utf-8")
CPP = (ROOT / "native/player/androidmedia3item.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "native/player/androidmedia3item.h").read_text(encoding="utf-8")


def require(text: str, token: str, why: str) -> None:
    if token not in text:
        raise AssertionError(f"{why}: missing {token!r}")


def test_media3_chapters_reach_shared_state() -> None:
    for token in ("onMetadata(Metadata metadata)", "Chapter.class", "chaptersByStartMs", "nativeOnChapters("):
        require(JAVA, token, "Java host must accumulate timed chapter metadata")
    require(JAVA, "chaptersByStartMs.clear()", "source replacement must clear prior chapters")
    require(JAVA, "chapter.isHidden()", "hidden chapter metadata must stay out of UI")
    require(HEADER, "handleChapters", "native facade needs a chapter callback sink")
    require(CPP, "parseChapters", "native facade must normalize chapter JSON")
    require(CPP, "Media3PlayerHost_nativeOnChapters", "JNI chapter callback must exist")
    require(CPP, "m_state.replaceChapters", "chapter callback must publish through state core")


if __name__ == "__main__":
    test_media3_chapters_reach_shared_state()
    print("android media3 chapter bridge: PASS")
