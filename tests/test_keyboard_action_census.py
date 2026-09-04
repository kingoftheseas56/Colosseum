"""Arc 41's explicit residual action census.

The lead scanner reports candidate pointer rows, not verdicts.  These are the
remaining rows that intentionally do not carry a keyboard marker in their own
file after the keyboard owners were wired.  Keeping the classifications here
prevents a future scan from silently turning a shared primitive, an inert
capture layer, or an inactive legacy component into an unexplained gap.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


RESIDUAL_CENSUS = {
    # Reader2's local MouseArea replacement owns focus, activation, context,
    # and slider keys for these controls; the shared type is intentionally the
    # owner rather than a duplicate KeyboardAction in every reader surface.
    "qml/reader2/ReaderChrome.qml": (16, "DELEGATED_READER_PRIMITIVE", ["ReaderKeyboardArea"]),
    "qml/reader2/BottomRail.qml": (4, "DELEGATED_READER_PRIMITIVE", ["ReaderKeyboardArea"]),
    "qml/reader2/TopBar.qml": (1, "DELEGATED_READER_PRIMITIVE", ["ReaderKeyboardArea"]),

    # ComicReader uses the analogous reader-local pointer/keyboard primitive.
    "qml/comicreader/ComicReaderHud.qml": (14, "DELEGATED_COMIC_READER_PRIMITIVE", ["ComicReaderKeyboardArea"]),
    "qml/comicreader/ComicReaderLoupe.qml": (4, "DELEGATED_COMIC_READER_PRIMITIVE", ["ComicReaderKeyboardArea"]),
    "qml/comicreader/ComicReaderCommandBar.qml": (1, "DELEGATED_COMIC_READER_PRIMITIVE", ["ComicReaderKeyboardArea"]),

    # These are parent-owned composite collections: the parent handles current
    # index, visibility, activation, and focus indication for the card.
    "qml/CataloguePosterCard.qml": (2, "PARENT_OWNED_COLLECTION", ["keyboardFocused"]),
    "qml/VaultPosterCard.qml": (2, "PARENT_OWNED_COLLECTION", ["openRequested"]),
    "qml/VaultWideCard.qml": (2, "PARENT_OWNED_COLLECTION", ["openRequested"]),

    # Scroll/scrub mechanics are not independent semantic actions. Their
    # keyboard equivalent is the owning reader/page scroll or collection
    # controller; the wheel/drag handler itself remains pointer mechanics.
    "qml/ScrollGlide.qml": (1, "SCROLL_MECHANIC", ["WheelHandler"]),
    "qml/comicreader/ComicReaderStripSurface.qml": (1, "SCROLL_MECHANIC", ["WheelHandler"]),

    # Full-screen click-catchers and the player stats card deliberately absorb
    # background input; they have no user action to expose.
    "qml/TheatreWorld.qml": (1, "INERT_CAPTURE_LAYER", ["swallow clicks"]),
    "qml/WorldPage.qml": (1, "INERT_CAPTURE_LAYER", ["absorb stray clicks"]),
    "qml/FullscreenTransitionShield.qml": (1, "INERT_CAPTURE_LAYER", ["MouseArea"]),
    "qml/player2/controls/PlaybackStatsCard.qml": (1, "INERT_CAPTURE_LAYER", ["Absorb background clicks"]),

    # These are verification-only harness surfaces, not shipped app routes.
    "qml/reader2/HarnessShelf.qml": (2, "TEST_HARNESS", ["bookChosen"]),
    "qml/player2/Harness.qml": (1, "TEST_HARNESS", ["shellToggle"]),

    # Main.qml selects this component only for the retired legacy WeebCentral
    # corridor. The live MangaSeries route is the keyboard-owned surface.
    "qml/MangaSeriesThumbnailMock.qml": (10, "INACTIVE_LEGACY_ROUTE", ["MouseArea"]),
}


def test_residual_census_is_complete_and_evidenced():
    assert sum(row_count for row_count, _classification, _markers in RESIDUAL_CENSUS.values()) == 65
    for relative, (_row_count, _classification, markers) in RESIDUAL_CENSUS.items():
        path = ROOT / relative
        assert path.exists(), f"census file disappeared: {relative}"
        text = path.read_text(encoding="utf-8-sig")
        for marker in markers:
            assert marker in text, f"{relative}: missing classification evidence {marker!r}"

    manga_route = (ROOT / "qml/Main.qml").read_text(encoding="utf-8-sig")
    assert 'source: legacyWeebCentral ? "MangaSeriesThumbnailMock.qml" : "MangaSeries.qml"' in manga_route

    reader_area = (ROOT / "qml/reader2/ReaderKeyboardArea.qml").read_text(encoding="utf-8-sig")
    assert "Keys.onPressed" in reader_area and "activeFocusOnTab" in reader_area
    comic_area = (ROOT / "qml/comicreader/ComicReaderKeyboardArea.qml").read_text(encoding="utf-8-sig")
    assert "Keys.onPressed" in comic_area and "activeFocusOnTab" in comic_area

    vault_page = (ROOT / "qml/VaultPage.qml").read_text(encoding="utf-8-sig")
    assert "KeyboardCollectionController" in vault_page and "vaultBrowseGrid" in vault_page
    catalogue_grid = (ROOT / "qml/CataloguePosterGrid.qml").read_text(encoding="utf-8-sig")
    assert "Keys.onPressed" in catalogue_grid and "itemRequested" in catalogue_grid


if __name__ == "__main__":
    test_residual_census_is_complete_and_evidenced()
    print("KEYBOARD_ACTION_CENSUS_OK residual_rows=65")
