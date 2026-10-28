import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum

TestCase {
    name: "VaultHeader"
    when: windowShown

    Window {
        id: testWindow
        width: 1280; height: 800
        visible: true
        Colosseum.VaultPage {
            id: vault
            anchors.fill: parent
        }
    }
    SignalSpy { id: backSpy; target: vault; signalName: "backRequested" }
    SignalSpy { id: minimizeSpy; target: vault; signalName: "minimizeRequested" }
    SignalSpy { id: fullscreenSpy; target: vault; signalName: "fullscreenRequested" }
    SignalSpy { id: closeSpy; target: vault; signalName: "closeRequested" }

    function control(name) {
        var item = findChild(vault, name)
        verify(item !== null, name + " is present")
        return item
    }
    function box(item) {
        var point = item.mapToItem(vault, 0, 0)
        return {left: point.x, top: point.y, right: point.x + item.width,
                bottom: point.y + item.height, cx: point.x + item.width / 2,
                cy: point.y + item.height / 2}
    }
    function init() {
        testWindow.width = 1280
        testWindow.requestActivate()
        tryVerify(function() { return testWindow.active })
        control("vaultBrowseSearchInput").text = ""
        backSpy.clear(); minimizeSpy.clear(); fullscreenSpy.clear(); closeSpy.clear()
    }
    function test_alignment_data() {
        return [{tag: "wide", width: 1280}, {tag: "desktop", width: 1000},
                {tag: "compact", width: 800}, {tag: "small", width: 640},
                {tag: "stacked", width: 480}]
    }
    function test_alignment(data) {
        testWindow.width = data.width
        waitForRendering(vault)
        var header = control("vaultHeader")
        var glass = box(control("vaultHeaderGlass"))
        var title = box(control("vaultHeaderTitle"))
        var back = box(control("vaultHeaderBack"))
        var search = box(control("vaultBrowseSearchField"))
        var buttons = box(control("vaultHeaderWindowControls"))
        compare(glass.left, vault.width - glass.right, "equal outer insets")
        compare(search.cx, glass.cx, "search is centered on the whole header")
        compare(title.cy, back.cy, "Back and title share one centerline")
        compare(title.cy, buttons.cy, "window controls share the title centerline")
        if (!header.stackedSearch) {
            compare(title.cy, glass.cy, "title is centered on the actual glass")
            compare(search.cy, glass.cy, "search is centered on the actual glass")
            verify(search.left > title.right, "title never overlaps search")
            verify(search.right < buttons.left, "search never overlaps window controls")
        } else {
            verify(search.top >= title.bottom, "search moves below the controls on narrow windows")
            verify(search.left >= glass.left + 12 && search.right <= glass.right - 12)
            verify(search.bottom <= glass.bottom - 12)
        }
        verify(title.left > back.right, "Back and title have separate hit/label zones")
    }
    function test_window_actions_keep_their_intents() {
        mouseClick(control("vaultHeaderBack"))
        compare(backSpy.count, 1)
        mouseClick(control("vaultHeaderMinimize"))
        compare(minimizeSpy.count, 1)
        mouseClick(control("vaultHeaderFullscreen"))
        compare(fullscreenSpy.count, 1)
        mouseClick(control("vaultHeaderClose"))
        compare(closeSpy.count, 1)
    }
    function test_search_clear_preserves_geometry() {
        var field = control("vaultBrowseSearchField")
        var input = control("vaultBrowseSearchInput")
        var before = box(field)
        input.text = "One Piece"
        compare(vault.searchQuery, "One Piece")
        verify(vault.searchViewActive)
        mouseClick(control("vaultHeaderClearSearch"))
        compare(input.text, "")
        compare(vault.searchQuery, "")
        verify(!vault.searchViewActive)
        compare(box(field).cx, before.cx)
        compare(box(field).cy, before.cy)
        tryVerify(function() { return input.activeFocus })
    }
}