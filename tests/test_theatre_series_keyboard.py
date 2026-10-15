from pathlib import Path


ROOT = Path(__file__).resolve().parents[1] / "qml"
SOURCE = ROOT / "TheatreSeries.qml"


def test_theatre_series_keyboard_surface_is_declared():
    text = SOURCE.read_text(encoding="utf-8-sig")
    for marker in [
        "function openHeroForPlay()",
        "id: heroWatchKeyboard",
        "id: notificationKeyboard",
        "id: episodeKeys",
        "id: episodeContextFocus",
        "id: orderKeys",
        "id: seasonKeys",
        "id: seasonMenuFocus",
        "id: seasonDownloadKeyboard",
        "id: jumpToggleKeyboard",
        "id: jumpSubmitKeyboard",
        "id: jumpRangeKeyboard",
        "id: theatreSeriesScrollKeys",
    ]:
        assert marker in text, f"TheatreSeries.qml: missing {marker!r}"

    # Per-episode actions are intentionally a single collection focus stop;
    # individual delegates are keyboard-addressable through its controller.
    assert "activeFocusOnTab: true" not in text


if __name__ == "__main__":
    test_theatre_series_keyboard_surface_is_declared()
    print("K02_THEATRE_SERIES_KEYBOARD_OK")
