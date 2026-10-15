from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
text = (ROOT / "qml" / "MangaSeries.qml").read_text(encoding="utf-8")

required_feature_seams = [
    "property bool chapterMode",
    "property string requestedVolumeNumber",
    "function _enterChapterMode()",
    "function _enterTankobanMode()",
    "function _loadChapterCatalogue(force)",
    "function openRequestedVolume()",
    "MangaChapterSeriesView {",
    "onChapterModeRequested: page._enterChapterMode()",
    "function requestReaderEscape()",
]
required_keyboard_seams = [
    "id: mangaSeriesMinKeyboard",
    "id: mangaSeriesFsKeyboard",
    "id: mangaSeriesCloseKeyboard",
]
missing = [needle for needle in required_feature_seams + required_keyboard_seams if needle not in text]
assert not missing, "MangaSeries keyboard work must preserve existing chapter/volume behavior; missing: " + ", ".join(missing)
print("MANGA_SERIES_ARC41_FEATURE_PRESERVATION_OK")
