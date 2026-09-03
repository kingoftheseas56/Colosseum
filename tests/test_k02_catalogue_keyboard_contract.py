from pathlib import Path


ROOT = Path(__file__).resolve().parents[1] / "qml"


EXPECT = {
    "ContinueSeeAllPage.qml": ["id: continueKeys", "collectionManaged: true", "id: chipKeyboard"],
    "MangaCatalogPage.qml": ["id: catalogKeys", "id: filterKeyboard"],
    "ReadingDesk.qml": ["id: deskKeyboard", "id: genreKeys"],
    "TheatreCatalogPage.qml": ["id: customizeKeyboard"],
    "TheatreCinemaHero.qml": ["id: primaryKeyboard", "id: secondaryKeyboard", "id: dotKeys"],
    "TheatreMarquee.qml": ["id: marqueeKeyboard", "id: continueKeys"],
    "TheatrePeekHero.qml": ["id: peekKeys"],
    "TheatreSeeAllPage.qml": ["id: backKeyboard", "id: retryKeyboard"],
    "TheatreStrip.qml": ["id: stripKeyboard"],
    "CataloguePosterGrid.qml": ["Qt.Key_Space"],
}


def test_catalogue_keyboard_contract_markers():
    for name, markers in EXPECT.items():
        text = (ROOT / name).read_text(encoding="utf-8-sig")
        for marker in markers:
            assert marker in text, f"{name}: missing {marker!r}"


def test_collection_surfaces_have_a_single_keyboard_owner():
    for name in ("ContinueRow.qml", "GenreMosaic.qml", "TheatreDiscoveryTiles.qml"):
        text = (ROOT / name).read_text(encoding="utf-8-sig")
        assert "KeyboardCollectionController" in text, f"{name}: missing collection controller"
        assert "Keys.onPressed" in text, f"{name}: missing key event seam"


if __name__ == "__main__":
    test_catalogue_keyboard_contract_markers()
    test_collection_surfaces_have_a_single_keyboard_owner()
    print("K02_CATALOGUE_KEYBOARD_CONTRACT_OK")
