import QtQuick
import QtQuick.Window
import QtTest
import "../../../qml" as App

TestCase {
    name: "ComingSoonTabs"
    when: windowShown
    Window {
        id: testWindow
        visible: true; width: 1100; height: 180
        Item { id: wall; anchors.fill: parent }
        App.TopBar { id: bar; backdrop: wall; width: 1040; x: 30; y: 30; lifecycleActive: false }
    }
    SignalSpy { id: selection; target: bar; signalName: "mediumSelected" }
    function init() { testWindow.requestActivate(); selection.clear(); bar.activeMedium = "" }
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
    function test_compactLayout_data() {
        return [{tag: "Home", medium: ""}, {tag: "Theatre", medium: "Theatre"}, {tag: "Universes", medium: "Universes"}]
    }
    function test_compactLayout(data) {
        bar.width = 904; bar.activeMedium = data.medium; bar.trackersEnabled = true
        wait(20)
        var picker = findChild(bar, "topBarWorldPicker")
        var systems = findChild(bar, "topBarSystemChrome")
        verify(picker.x + picker.width + 8 <= systems.x, "Tabs leave space for system controls: " + picker.x + "," + picker.width + "," + systems.x)
        var left = findChild(bar, "topBarLeftChrome")
        verify(picker.x >= left.width + 8)
        bar.width = 1040; bar.trackersEnabled = false
    }
}
