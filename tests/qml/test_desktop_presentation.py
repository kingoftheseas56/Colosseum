"""Execute production presentation components and Main handlers with bounded service fakes."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = (ROOT / "qml/Main.qml").read_text(encoding="utf-8")

    def function(name):
        start = source.index("    function " + name + "(")
        end = source.index("\n    }", start) + len("\n    }")
        return source[start:end]

    start = source.index("        visible:", source.index("    Taskbar {"))
    dock_visibility = source[start:source.index("        enabled:", start)]
    qml = '''import QtQuick
import QtQuick.Window
import QtTest
import "QML_URI" as UI
import "POLICY_URI" as ShellBackPolicy
Item {
    id: win
    width: 1280; height: 720
    property bool androidHost: true
    readonly property bool desktopPresentation: policy.desktopPresentation
    property bool immersiveSurfaceOpen: false
    property bool vinylVideoFullscreen: false
    property bool vinylEnabled: false
    property bool feriaComingSoon: true
    property var vinylWorld: null
    property string currentSurface: "Home"
    property bool homeIntroWidgetsLoaded: false
    property int homeIntroWidgetCursor: 0
    property var homeIntroPendingLoader: null
    property int cancelled: 0
    property int keyboardHides: 0
    readonly property var shellInputMethod: keyboard
    function guardNamedCollectionsNavigation(fn) { return false }
    function cancelCatalogueRoute() { cancelled++ }
    function parkFeria(value) {}
    function setGuiStallContext(a, b) {}
    function refreshWallpaper() {}
    function armHomeIntroWidgets() {}
    function armHomeContinueRail() {}
    function shellEscapeState() { return {worldOpen: worldStack.current !== ""} }
    function goHomeTop() {}
    UI.AdaptiveLayout { id: policy; viewportWidth: win.width }
    Item { id: worldStack; property string current: "" }
    ListModel { id: openModes }
    QtObject { id: phoneShell; function recordWorldOpened(medium) {} }
    QtObject { id: feriaLayer; property bool open: false; property var item: null }
    QtObject { id: boot; property bool visible: false }
    QtObject { id: keyboard; property bool visible: true; function hide() { win.keyboardHides++; visible = false } }
    QtObject { id: homeBookshelfLoader; property bool active: false; property string source: ""; function setSource(url, properties) { source = url } }
    QtObject { id: homeTheatreStripLoader; property bool active: false; property string source: ""; function setSource(url, properties) { source = url } }
    QtObject { id: homeReadingDeskLoader; property bool active: false; property string source: ""; function setSource(url, properties) { source = url } }
    QtObject { id: homeVaultWidgetLoader; property bool active: false; property string source: ""; function setSource(url, properties) { source = url } }
    Item { id: wall }
    Timer { id: homeIntroWidgetsTimer }
    UI.Taskbar { id: taskbar; DOCK_VISIBILITY }
    Window {
        id: platformWindow
        width: 1280; height: 720
        property bool desktopPresentation: policy.desktopPresentation
        property bool immersiveSurfaceOpen: false
        property var worldDefinitions: [{name:"Theatre",icon:""}]
        property int routes: 0
        function openWorld(name) { routes++ }
        Item { id: chrome; x:54; y:18; width:1172; height:56 }
        Flickable { id: page; width:1280; height:620 }
        Column { id: board; x:54; width:1172; spacing:32 }
        Rectangle { id: hero; height:360; radius:20 }
        Item { id: hall }
        Loader { id: fixtureLoader; active:false }
        QtObject { id: escapeCommand; function invoke(reason) { win.handleEscape() } }
        UI.PhoneShellHost {
            id: androidShell
            androidHost: true
            window: platformWindow
            worldStack: worldStack
            topbar: chrome; page: page; contentCol: board; hero: hero; hallDoor: hall
            homeBookshelfLoader: fixtureLoader; homeTheatreStripLoader: fixtureLoader
            homeReadingDeskLoader: fixtureLoader; homeVaultWidgetLoader: fixtureLoader
            downloadsLayer: fixtureLoader; extensionsLayer: fixtureLoader
            historyStatsLayer: fixtureLoader; syncCenterLayer: fixtureLoader
            stremioPanel: hall; accountFlyout: hall; wall: hall; escapeCommand: escapeCommand
        }
        Component { id: nullWindowShellFactory; UI.PhoneShellHost { androidHost: true } }
        UI.SourcesSheet { id: sources; visible:false; backdrop: hall }
    }
    UI.WorldPage { id: world; width:1280; height:720; backdrop:wall; medium:"Theatre" }
    HANDLERS
    TestCase {
        name: "DesktopPresentation"
        when: windowShown
        function initTestCase() {
            Qt.application.organization = "ColosseumTests"
            Qt.application.domain = "tests.colosseum"
        }
        function init() {
            worldStack.current = ""; currentSurface = "Home"; openModes.clear()
            cancelled = 0; keyboardHides = 0; keyboard.visible = true
            homeIntroWidgetsLoaded = false; homeIntroWidgetCursor = 0; homeIntroPendingLoader = null
        }
        function test_actual_android_shell_reserves_no_phone_chrome() {
            compare(androidShell.androidHost, true)
            compare(androidShell.desktopPresentation, true)
            compare(androidShell.phoneWorldNavigationVisible, false)
            compare(androidShell.wideWorldNavigationVisible, false)
            compare(androidShell.contentBottomInset, 0)
            compare(androidShell.wideWorldNavigationLeftInset, 0)
            compare(chrome.x, 54); compare(chrome.y, 18); compare(chrome.width, 1172)
            compare(hero.height, 360); compare(board.spacing, 32)
            androidShell.enterAndroidStartWorld(); compare(platformWindow.routes, 0)
            platformWindow.width = 700
            compare(sources.phoneLayout, false)
            compare(androidShell.phoneWorldNavigationVisible, false)
            platformWindow.width = 1280
            compare(world.pageMargin, 54); compare(world.contentBottomInset, 0)
        }
        function test_null_window_host_keeps_policy_default_without_binding_warnings() {
            failOnWarning(/.*(binding loop|TypeError|Cannot read property).*/i)
            const shell = nullWindowShellFactory.createObject(win, {
                window:null, worldStack:worldStack, topbar:chrome, page:page, contentCol:board,
                hero:hero, hallDoor:hall, homeBookshelfLoader:fixtureLoader,
                homeTheatreStripLoader:fixtureLoader, homeReadingDeskLoader:fixtureLoader,
                homeVaultWidgetLoader:fixtureLoader, downloadsLayer:fixtureLoader,
                extensionsLayer:fixtureLoader, historyStatsLayer:fixtureLoader, syncCenterLayer:fixtureLoader,
                stremioPanel:hall, accountFlyout:hall, wall:hall, escapeCommand:escapeCommand
            })
            verify(shell !== null)
            compare(shell.desktopPresentation, true)
            compare(shell.phoneWorldNavigationVisible, false)
            compare(shell.wideWorldNavigationLeftInset, 0)
            compare(shell.contentBottomInset, 0)
            shell.destroy()
        }
        function test_android_back_shortcut_closes_world() {
            win.openWorld("Theatre"); keyboard.visible = true
            platformWindow.show()
            platformWindow.requestActivate()
            androidShell.forceActiveFocus()
            tryCompare(androidShell, "activeFocus", true)
            keyClick(Qt.Key_Back)
            compare(keyboardHides, 1)
            compare(worldStack.current, "Theatre")
            keyClick(Qt.Key_Back)
            tryCompare(worldStack, "current", "")
            platformWindow.hide()
        }
        function test_desktop_topbar_shows_clock_when_chrome_has_room() {
            const clock = findChild(world, "topBarClock")
            verify(clock !== null)
            world.width = 1920
            compare(clock.visible, true)
            world.width = 900
            compare(clock.visible, false)
            world.width = 1280
        }
        function test_policy_preserves_desktop_metrics() {
            compare(policy.desktopPresentation, true)
            compare(policy.pageMargin, 54)
            compare(policy.compactChrome, false)
            compare(win.androidHost, true)
        }
        function test_world_round_trip() {
            for (const name of ["Tankoban", "Biblio", "Theatre", "Universes"]) {
                win.openWorld(name); compare(worldStack.current, name); compare(currentSurface, name)
                win.closeWorld(); compare(worldStack.current, ""); compare(currentSurface, "Home")
            }
            compare(openModes.count, 4)
        }
        function test_all_home_widgets_load() {
            const loaders = [homeBookshelfLoader, homeTheatreStripLoader, homeReadingDeskLoader, homeVaultWidgetLoader]
            for (const loader of loaders) {
                win.loadHomeIntroWidgets(); compare(homeIntroPendingLoader, loader)
                verify(loader.source.length > 0); win.homeIntroWidgetSettled(loader)
            }
            compare(homeIntroWidgetCursor, 4); compare(homeIntroWidgetsLoaded, true)
        }
        function test_dock_on_android_desktop() { compare(taskbar.visible, true) }
        function test_keyboard_dismisses_before_navigation() {
            win.openWorld("Theatre"); cancelled = 0
            win.handleEscape()
            compare(keyboardHides, 1); compare(worldStack.current, "Theatre"); compare(cancelled, 0)
            win.handleEscape(); compare(worldStack.current, "")
        }
    }
}
'''
    qml = qml.replace("QML_URI", (ROOT / "qml").as_uri()).replace(
        "POLICY_URI", (ROOT / "qml/ShellBackPolicy.js").as_uri())
    qml = qml.replace("DOCK_VISIBILITY", dock_visibility).replace("HANDLERS", "\n".join(
        function(n) for n in ["openWorld", "closeWorld", "loadHomeIntroWidgets", "homeIntroWidgetSettled", "handleEscape"]))
    with tempfile.TemporaryDirectory(prefix="colosseum-desktop-policy-") as directory:
        path = Path(directory)
        (path / "tst_desktop.qml").write_text(qml, encoding="utf-8")
        report = path / "results.txt"
        env = dict(os.environ, QT_QUICK_BACKEND="software")
        result = subprocess.run([sys.argv[1], "-platform", "offscreen", "-input", directory,
                                 "-o", str(report) + ",txt"], env=env, timeout=60)
        print(report.read_text(encoding="utf-8") if report.exists() else "No Qt test report")
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
