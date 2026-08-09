import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Living Guide foundation Task 4 — the taskbar Guide door's signal/presentation seam. Drives
// the PRODUCTION Taskbar (Sessions guarded to undefined here, so the switcher shows no tiles)
// and proves: the door renders only while the dock is open, a real click emits guideClicked
// exactly once, guideActive drives ONLY the Guide underline, and the door exposes the
// accessible name "Guide". The shell route itself (z:59 loader over the utility pages, the
// underlay-underline masking, Escape-before-underlay ordering) lives in Main.qml, which is not
// instantiable in a Quick Test — those are held by tests/test_guide_taskbar_p0.ps1 and the
// post-Task-4 runtime eyes-on, mirroring how tst_open_media_control proves its own control.
TestCase {
    name: "GuideShell"
    when: windowShown

    Window {
        id: testWindow
        width: 1280
        height: 720
        visible: true
    }

    Component { id: barComponent; Colosseum.Taskbar {} }
    property var bar: null

    SignalSpy { id: guideSpy; signalName: "guideClicked" }

    function init() {
        bar = barComponent.createObject(testWindow, { width: 1280, height: 720, open: true })
        verify(bar !== null)
        guideSpy.target = bar
        wait(320)   // let the dock's open-width animation (240ms Behavior) settle before hit-testing
    }
    function cleanup() {
        guideSpy.clear()
        guideSpy.target = null
        if (bar) bar.destroy()
        bar = null
    }

    // The Guide door is an expanded-taskbar utility (visible: bar.open), never a closed-dock
    // permanent button.
    function test_guide_door_renders_only_when_dock_open() {
        var btn = findChild(bar, "colosseumGuideTaskbarButton")
        verify(btn !== null)
        verify(btn.visible)
        bar.open = false
        wait(30)
        verify(!btn.visible)
        bar.open = true
        wait(320)
        verify(btn.visible)
    }

    // A real mouse click emits the Guide open-request signal exactly once.
    function test_guide_click_emits_guideClicked_once() {
        var btn = findChild(bar, "colosseumGuideTaskbarButton")
        verify(btn !== null)
        compare(guideSpy.count, 0)
        mouseClick(btn)
        compare(guideSpy.count, 1)
    }

    // guideActive drives ONLY the Guide active underline; it is off by default and follows the
    // property. (The masking of the OTHER utilities' underlines while Guide is open is a Main.qml
    // binding, held by the static gate.)
    function test_guide_active_drives_only_the_guide_underline() {
        var underline = findChild(bar, "colosseumGuideUnderline")
        verify(underline !== null)
        verify(!underline.visible)
        bar.guideActive = true
        wait(30)
        verify(underline.visible)
        bar.guideActive = false
        wait(30)
        verify(!underline.visible)
    }

    // The door reports a stable accessible name so screen readers announce it as "Guide".
    function test_guide_door_exposes_accessible_name() {
        var btn = findChild(bar, "colosseumGuideTaskbarButton")
        verify(btn !== null)
        compare(btn.Accessible.name, "Guide")
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
