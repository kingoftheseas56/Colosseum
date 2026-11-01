"""Regression checks for routed desktop pages that replace/hide shared TopBar chrome."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class DesktopWindowControlsContract(unittest.TestCase):
    def test_house_icons_and_keyboard(self):
        chrome = source("qml/HouseWindowControls.qml")
        for expected in (
            "minimize.svg", "fullscreen.svg", "fullscreen-exit.svg",
            "power.svg", "KeyboardAction", 'objectPrefix + "Minimize"',
            'objectPrefix + "Fullscreen"', 'objectPrefix + "Quit"',
            "onTriggered: controls.minimizeRequested()",
            "onTriggered: controls.fullscreenRequested()",
            "onTriggered: controls.quitRequested()",
            'Qt.platform.os !== "android"',
        ):
            with self.subTest(expected=expected):
                self.assertIn(expected, chrome)

    def test_all_bypass_page_owners_have_controls(self):
        for file, prefix in (
            ("qml/ExtensionsStorePage.qml", "extensionsWindow"),
            ("qml/TheatreWorld.qml", "theatreSeeAllWindow"),
            ("qml/TrackerSyncCenterPage.qml", "trackerWindow"),
            ("qml/HistoryHighlightsStatsPage.qml", "historyStatsWindow"),
            ("qml/collections/CollectionsPanel.qml", "collectionsWindow"),
            ("qml/account/AccountCenter.qml", "accountCenterWindow"),
        ):
            with self.subTest(file=file):
                page = source(file)
                self.assertIn("HouseWindowControls {", page)
                self.assertIn('objectPrefix: "' + prefix + '"', page)
                for action in ("onMinimizeRequested:", "onFullscreenRequested:", "onQuitRequested:"):
                    self.assertIn(action, page)

    def test_extensions_subpages_layer_below_trio(self):
        page = source("qml/ExtensionsStorePage.qml")
        self.assertIn("topBarBackOnly: true", page)
        self.assertIn("id: seeAll", page)
        self.assertIn("id: management", page)
        self.assertIn("z: 10", page)
        self.assertIn("z: 15", page)
        self.assertIn("!tankoyomiConfiguration.visible", page)
        self.assertIn("onQuitRequested: root.closeRequested()", page)

    def test_theatre_overlay_restores_hidden_chrome(self):
        page = source("qml/TheatreWorld.qml")
        self.assertIn("parent: theatre", page)
        self.assertIn("z: 300", page)
        self.assertIn("HouseWindowControls {", page)
        self.assertIn("onMinimizeRequested: theatre.minimizeClicked()", page)
        self.assertIn("onQuitRequested: theatre.powerClicked()", page)

    def test_main_routes_to_shell_authorities(self):
        main = source("qml/Main.qml")
        # Isolate the owning Loader; dozens of other pages have the same signals.
        for start, end in (
            ("id: syncCenterLayer", "id: historyStatsLayer"),
            ("id: historyStatsLayer", "id: updateLayer"),
        ):
            with self.subTest(route=start):
                block = main.split(start, 1)[1].split(end, 1)[0]
                self.assertIn("item.minimizeRequested.connect(win.minimizeShell)", block)
                self.assertIn("item.fullscreenRequested.connect(win.toggleFullscreenShell)", block)
                self.assertIn("item.closeRequested.connect(function() { win.requestApplicationQuit() })", block)
        for start, end in (
            ("CollectionsUi.CollectionsPanel {", "Connections {"),
            ("AccountCenter {", "Connections {"),
        ):
            with self.subTest(route=start):
                block = main.split(start, 1)[1].split(end, 1)[0]
                self.assertIn("onMinimizeRequested: win.minimizeShell()", block)
                self.assertIn("onFullscreenRequested: win.toggleFullscreenShell()", block)
                self.assertIn("onQuitRequested: win.requestApplicationQuit()", block)

    def test_cosmere_missing_fullscreen_was_added(self):
        page = source("qml/CosmereUniversePage.qml")
        self.assertIn("onClicked: root.fullscreenRequested()", page)
        self.assertIn("onTriggered: root.fullscreenRequested()", page)
        self.assertIn("fullscreen-exit.svg", page)
        self.assertIn("onClicked: root.minimizeRequested()", page)
        self.assertIn("onClicked: root.closeRequested()", page)


if __name__ == "__main__":
    unittest.main()
