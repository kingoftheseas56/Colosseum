import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Vault execution Slice 8 — the taskbar "Open Media…" control's signal seam. Drives the
// PRODUCTION Taskbar (Sessions guarded to undefined here, so the switcher shows no tiles)
// and proves: the control renders only while the dock is open, and a real mouse click emits
// openMediaClicked. The native OS file dialog is NOT opened in-test — the signal is the unit
// (the dialog + routing are proven by the Lanista replay and the human-witnessed items).
TestCase {
    name: "OpenMediaControl"
    when: windowShown

    Window {
        id: testWindow
        width: 1280
        height: 720
        visible: true
    }

    Component { id: barComponent; Colosseum.Taskbar {} }
    property var bar: null

    SignalSpy { id: openSpy; signalName: "openMediaClicked" }

    function init() {
        bar = barComponent.createObject(testWindow, { width: 1280, height: 720, open: true })
        verify(bar !== null)
        openSpy.target = bar
        wait(320)   // let the dock's open-width animation (240ms Behavior) settle before hit-testing
    }
    function cleanup() {
        openSpy.clear()
        openSpy.target = null
        if (bar) bar.destroy()
        bar = null
    }

    // Renders while the dock is open; disappears when closed — it is an ACTION affordance
    // (visible: bar.open), not a persistent door (the always-present Vault door is Slice 10).
    function test_control_renders_only_when_open() {
        var ctl = findChild(bar, "taskbarOpenMedia")
        verify(ctl !== null)
        verify(ctl.visible)
        bar.open = false
        wait(30)
        verify(!ctl.visible)
        bar.open = true
        wait(320)
        verify(ctl.visible)
    }

    // A real mouse click on the control emits the open-request signal exactly once.
    function test_click_emits_open_request() {
        var ctl = findChild(bar, "taskbarOpenMedia")
        verify(ctl !== null)
        compare(openSpy.count, 0)
        mouseClick(ctl)
        compare(openSpy.count, 1)
    }

    function findChild(root, objectName) {
        if (!root) return null
        if (root.objectName === objectName) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findChild(kids[i], objectName)
            if (found) return found
        }
        return null
    }
}
