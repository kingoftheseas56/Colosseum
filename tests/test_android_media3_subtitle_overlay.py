from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLAYER = (ROOT / "qml" / "PlayerPage.qml").read_text(encoding="utf-8")
STATE = (ROOT / "native" / "player" / "androidmedia3state.cpp").read_text(encoding="utf-8")
ITEM = (ROOT / "native" / "player" / "androidmedia3item.h").read_text(encoding="utf-8")


def require(text: str, needle: str, reason: str) -> None:
    if needle not in text:
        raise AssertionError(f"{reason}: missing {needle!r}")


def test_media3_cues_reach_shared_player_surface() -> None:
    require(STATE, '"subtitleCueOverlay"), true', "Media3 must advertise cue-overlay capability")
    require(ITEM, "Q_PROPERTY(QVariantList subtitleCues", "facade must expose normalized cues")
    require(PLAYER, "id: media3SubtitleOverlay", "shared player must own the Media3 cue overlay")
    require(PLAYER, 'supportsPlayerCapability("subtitleCueOverlay")', "overlay must be capability-gated")
    require(PLAYER, "mpv.subtitleCues", "overlay must consume backend-normalized cues")


if __name__ == "__main__":
    test_media3_cues_reach_shared_player_surface()
    print("android media3 subtitle overlay: PASS")
