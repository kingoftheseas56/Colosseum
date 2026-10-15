import QtQuick
import QtQuick.Window
import QtTest
import "../../../qml" as App

TestCase {
    name: "ComingSoonTabs"
    when: windowShown
    QtObject { id: noAccount; property bool accountFeaturesAvailable: false; property string mode: "localOnly"; property string username: "" }
    Window {
        id: testWindow
        visible: true; width: 1100; height: 180
        Item { id: wall; anchors.fill: parent }
        App.TopBar { id: bar; backdrop: wall; width: 1040; x: 30; y: 30; lifecycleActive: true; accountController: noAccount }
    }
    SignalSpy { id: selection; target: bar; signalName: "mediumSelected" }
    function init() { testWindow.requestActivate(); selection.clear(); bar.activeMedium = ""; bar.width = 1040; bar.trackersEnabled = false }
    function test_disabled_data() { return [{ tag: "Feria", label: "Feria" }, { tag: "Vinyl", label: "Vinyl" }] }
    function test_disabled(data) {
        var pill = findChild(bar, "modePill_" + data.label)
        var action = findChild(bar, "modePillFocus_" + data.label)
        verify(pill.visible && pill.comingSoon)
        verify(!action.enabled)
        verify(action.accessibleName.indexOf("Coming Soon") !== -1)
        verify(!action.activate(Qt.ShortcutFocusReason), "Existing input activation rejects disabled tab")
        mouseClick(action)
        compare(selection.count, 0)
        bar.activeMedium = data.label
        verify(!pill.active, "Unavailable tab never receives active styling")
    }
    function test_available_data() {
        return [ { tag: "Tankoban", label: "Tankoban" }, { tag: "Biblio", label: "Biblio" },
                 { tag: "Theatre", label: "Theatre" }, { tag: "Universes", label: "Universes" } ]
    }
    function test_available(data) {
        var action = findChild(bar, "modePillFocus_" + data.label)
        verify(action.enabled)
        verify(action.activate(Qt.ShortcutFocusReason))
        compare(selection.count, 1)
        compare(selection.signalArguments[0][0], data.label)
    }
    function desktopCases() {
        var rows = []
        var widths = [1000, 1040, 1173, 1200]
        for (var i = 0; i < widths.length; ++i) {
            var width = widths[i]
            for (var j = 0; j < 3; ++j) {
                var medium = ["", "Theatre", "Universes"][j]
                rows.push({tag: width + "_" + (medium || "Home"), width: width, medium: medium})
            }
        }
        return rows
    }
    function test_desktopGeometry_data() { return desktopCases() }
    function hasHomeLabel(item) {
        if (item.visible && item.text === "Home") return true
        var children = item.children || []
        for (var i = 0; i < children.length; ++i)
            if (hasHomeLabel(children[i])) return true
        return false
    }
    function test_desktopGeometry(data) {
        testWindow.width = data.width + 60
        bar.width = data.width; bar.activeMedium = data.medium; bar.trackersEnabled = true
        wait(20)
        var clock = findChild(bar, "topBarClock")
        var picker = findChild(bar, "topBarWorldPicker")
        var systems = findChild(bar, "topBarSystemChrome")
        var left = findChild(bar, "topBarLeftChrome")
        var delta = Math.abs(picker.mapToItem(bar, picker.width / 2, 0).x - bar.width / 2)
        var home = data.medium === "" || hasHomeLabel(left)
        var clear = picker.x >= left.x + left.width + 8 && picker.x + picker.width + 8 <= systems.x
        console.log("DESKTOP_CHROME " + data.tag + " clock=" + clock.visible + " centerDelta=" + delta
                    + " Home=" + home + " sidesClear=" + clear)
        verify(clock.visible, "Desktop logical width retains clock/date")
        verify(delta <= 0.5, "World picker is centered on the bar, delta=" + delta)
        verify(home, "World root retains visible Home destination label")
        verify(clear, "Left chrome, centered picker and system icons do not overlap")
    }
    function test_compactLayout_data() {
        return [{tag: "Home", medium: "", width: 904}, {tag: "Theatre", medium: "Theatre", width: 904}, {tag: "Universes", medium: "Universes", width: 904},
                {tag: "NarrowHome", medium: "", width: 800}, {tag: "NarrowUniverses", medium: "Universes", width: 800}]
    }
    function test_compactLayout(data) {
        bar.width = data.width; bar.activeMedium = data.medium; bar.trackersEnabled = true
        wait(20)
        var picker = findChild(bar, "topBarWorldPicker")
        var systems = findChild(bar, "topBarSystemChrome")
        verify(picker.x + picker.width + 8 <= systems.x, "Tabs leave space for system controls: " + picker.x + "," + picker.width + "," + systems.x)
        var left = findChild(bar, "topBarLeftChrome")
        verify(picker.x >= left.width + 8)
        bar.width = 1040; bar.trackersEnabled = false
    }
}
