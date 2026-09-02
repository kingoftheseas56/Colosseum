from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def text(rel):
    return (ROOT / rel).read_text(encoding="utf-8")

def require(rel, *needles):
    body = text(rel)
    missing = [needle for needle in needles if needle not in body]
    assert not missing, f"{rel}: missing {missing}"

for helper in ("qml/comicreader/ComicReaderKeyboardArea.qml", "qml/reader2/ReaderKeyboardArea.qml"):
    require(helper, "activeFocusOnTab", "Qt.Key_Return", "Qt.Key_Space",
            "Qt.Key_Menu", "Qt.Key_F10", "activeFocus", "border.width")

require("qml/comicreader/ComicReaderInput.qml",
        "Qt.Key_Menu", "Qt.Key_F10", "openContextMenu")
require("qml/comicreader/ComicReaderPagesOverlay.qml",
        "activeFocusOnTab", "KeyboardCollectionController", "keyboardTabStop: false")
require("qml/reader2/SearchSheet.qml",
        "activeFocusOnTab", "ReaderKeyboardCollectionController", "keyboardTabStop: false")
require("qml/reader2/LeftPanel.qml",
        "ReaderKeyboardCollectionController", "keyboardTabStop: false", "Qt.Key_Delete")
require("qml/reader2/AppearancePanel.qml", "keyboardDecrease", "keyboardIncrease")
require("qml/reader2/BottomRail.qml", "keyboardDecrease", "keyboardIncrease", "keyboardHome", "keyboardEnd")
require("qml/reader2/ReaderChrome.qml", "keyboardDecrease", "keyboardIncrease", "keyboardHome", "keyboardEnd")
require("qml/reader2/ReaderKeyboardArea.qml", "function focusKeyboard()")
require("qml/reader2/ReaderChrome.qml", "component HudGlyphButton", "function focusKeyboard()")
require("qml/reader2/qmldir", "ReaderKeyboardArea ReaderKeyboardArea.qml", "ReaderKeyboardCollectionController ReaderKeyboardCollectionController.qml", "ReaderKeyboardScrollController ReaderKeyboardScrollController.qml")
assert 'import "../"' not in text("qml/reader2/LeftPanel.qml")
print("keyboard reader contract: PASS")

# Late Lead-Arc41 parity/lifecycle requirements.
require("qml/comicreader/ComicReaderInput.qml", "openContextMenu(-1, -1)")
require("qml/comicreader/ComicReaderShell.qml",
        "contextChooserOpen", "keyboardContextLeftPage", "keyboardContextRightPage",
        "function openKeyboardContextChooser()", "Qt.Key_Left", "Qt.Key_Right")
require("qml/reader2/SearchSheet.qml",
        "KeyNavigation.tab: input", "KeyNavigation.backtab: sheet.hasResults ? resList : input")
require("qml/reader2/LeftPanel.qml", "function trapTab(event)", "nextItemInFocusChain")
require("qml/reader2/AppearancePanel.qml", "function trapTab(event)", "nextItemInFocusChain")
require("qml/reader2/SelectionMenu.qml", "function trapTab(event)", "nextItemInFocusChain")
require("qml/reader2/DictCard.qml", "KeyNavigation.tab", "KeyNavigation.backtab", "ReaderKeyboardScrollController")
require("qml/reader2/FootnoteCard.qml", "Qt.Key_Tab", "Qt.Key_Backtab", "ReaderKeyboardScrollController")
require("qml/reader2/ReaderChrome.qml",
        "function closePanel(restoreFocus)", "function closeAppearance(restoreFocus)",
        "function closeSearch(restoreFocus)", "function closeAnyPanel()")
require("qml/reader2/ReaderShell.qml", "function dismissSelectionMenu(restoreFocus)", "dismissSelectionMenu(false)")
assert "onSelMenuShownChanged: if (shell.selMenuShown) Qt.callLater(function () { paper.focusPaper() })" not in text("qml/reader2/ReaderShell.qml")
print("late reader lifecycle contract: PASS")