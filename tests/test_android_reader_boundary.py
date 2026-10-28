from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
DESKTOP_PAPER = ROOT / "qml" / "reader2" / "Paper.qml"
ANDROID_PAPER = ROOT / "qml" / "reader2" / "AndroidPaper.qml"
READER_PAPER = ROOT / "qml" / "reader2" / "ReaderPaper.qml"
READER_SHELL = ROOT / "qml" / "reader2" / "ReaderShell.qml"
QMLDIR = ROOT / "qml" / "reader2" / "qmldir"
COMIC_READER = ROOT / "qml" / "comicreader"

REQUIRED_SURFACE = {
    "open", "next", "prev", "goTo", "setAppearance", "search", "clearSearch",
    "addHighlight", "removeHighlight", "clearSelection", "setReadAlongStyle",
    "paintReadAlong", "clearReadAlong", "ensureReadAlongVisible",
    "navigateReadAlong", "focusPaper",
}


def qml_functions(text: str) -> set[str]:
    return set(re.findall(r"\bfunction\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", text))


def test_all_paper_surfaces_match_reader_shell_commands():
    for path in (DESKTOP_PAPER, ANDROID_PAPER, READER_PAPER):
        assert REQUIRED_SURFACE <= qml_functions(path.read_text(encoding="utf-8"))


def test_android_path_has_no_qt_webengine_dependency():
    for path in (ANDROID_PAPER, READER_PAPER):
        text = path.read_text(encoding="utf-8")
        forbidden = ("import QtWebEngine", "import QtWebChannel", "WebEngineView {")
        assert not any(token in text for token in forbidden)


def test_reader_shell_routes_through_platform_selector():
    selector = READER_PAPER.read_text(encoding="utf-8")
    shell = READER_SHELL.read_text(encoding="utf-8")
    assert 'Qt.platform.os === "android"' in selector
    assert '"AndroidPaper.qml" : "Paper.qml"' in selector
    assert "\n    ReaderPaper {" in shell
    assert "\n    Paper {" not in shell


def test_reader_selector_is_registered_in_qmldir():
    text = QMLDIR.read_text(encoding="utf-8-sig")
    assert "ReaderPaper ReaderPaper.qml" in text
    assert "AndroidPaper AndroidPaper.qml" in text


def test_android_adapter_keeps_json_event_transport_and_generation():
    text = ANDROID_PAPER.read_text(encoding="utf-8")
    assert "signal paperEvent(string name, var payload)" in text
    assert "onEventRaised(name, json)" in text
    assert 'paperEvent("error", { gen: gen' in text


def test_comic_reader_remains_native_qml_not_webengine():
    offenders = []
    for path in COMIC_READER.rglob("*.qml"):
        text = path.read_text(encoding="utf-8")
        if "QtWebEngine" in text or "WebEngineView" in text:
            offenders.append(path.relative_to(ROOT).as_posix())
    assert offenders == []


if __name__ == "__main__":
    tests = [
        test_all_paper_surfaces_match_reader_shell_commands,
        test_android_path_has_no_qt_webengine_dependency,
        test_reader_shell_routes_through_platform_selector,
        test_reader_selector_is_registered_in_qmldir,
        test_android_adapter_keeps_json_event_transport_and_generation,
        test_comic_reader_remains_native_qml_not_webengine,
    ]
    for test in tests:
        test()
        print(f"ok   {test.__name__}")
    print("VERDICT: PASS")
