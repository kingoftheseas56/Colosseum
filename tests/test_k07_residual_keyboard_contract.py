from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


EXPECT = {
    "qml/PortraitTile.qml": ["property bool keyboardEnabled", "KeyboardAction", "accessibleName:"],
    "qml/CarouselSlide.qml": ["id: primaryKeyboard", "id: secondaryKeyboard"],
    "qml/TrendingTop10.qml": ["id: top10Keys", "KeyboardCollectionController", "activeFocusOnTab"],
    "qml/NextToOpenTray.qml": ["id: rowKeyboard", "id: removeKeyboard"],
    "qml/PlayerLoadingScreen.qml": ["id: cancelKeyboard"],
    "qml/BackgroundActivitySection.qml": ["id: pauseKeyboard"],
    "qml/FeaturedCarousel.qml": ["id: featuredKeys", "KeyboardCollectionController"],
    "qml/CastRow.qml": ["id: expandKeyboard", "Show all cast"],
    "qml/MoreLikeThisRow.qml": ["id: moreLikeKeys", "KeyboardCollectionController"],
    "qml/comicreader/ComicReaderUnitError.qml": ["KeyboardAction", "retryAction", "skipAction"],
    "qml/ComicSeries.qml": ["id: comicSeriesKeys", "id: rowKeyboard", "id: trailingKeyboard"],
    "qml/GenrePage.qml": ["id: libraryKeyboard", "id: pageScrollKeys", "id: cardKeys"],
    "qml/BiblioGenrePage.qml": ["id: libraryKeyboard", "id: pageScrollKeys", "id: cardKeys"],
    "qml/TheatreGenrePage.qml": ["id: libraryKeyboard", "id: pageScrollKeys", "id: cardKeys"],
    "qml/ComicSeriesPage.qml": ["id: comicSeriesPageKeys", "id: issueKeyboard", "id: collectionKeyboard"],
    "qml/ComicArchiveIndex.qml": ["id: archiveIndexKeys", "id: archiveKeyboard"],
    "qml/ComicArchiveBoard.qml": ["id: archiveBoardKeys"],
    "qml/ComicTorrentSourcesPage.qml": ["id: torrentSourcesKeys", "id: sourceKeyboard"],
    "qml/ComicTorrentArchivePicker.qml": ["id: archivePickerKeys", "id: fileKeyboard"],
    "qml/ComicDbLedger.qml": ["id: editionKeyboard", "id: alternateSourcesKeyboard"],
    "qml/MangaTankobanLibraryThumbnailMock.qml": ["id: legacyMangaKeys", "id: volumeKeyboard", "id: chapterKeyboard"],
}


def test_residual_surfaces_have_explicit_keyboard_owners():
    for relative, markers in EXPECT.items():
        text = (ROOT / relative).read_text(encoding="utf-8-sig")
        for marker in markers:
            assert marker in text, f"{relative}: missing {marker!r}"


if __name__ == "__main__":
    test_residual_surfaces_have_explicit_keyboard_owners()
    print("K07_RESIDUAL_KEYBOARD_CONTRACT_OK")
