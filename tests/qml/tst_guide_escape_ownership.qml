import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Task 4 F1 regression (Preflight blocker) — the assembled Escape-ownership contract when the Living
// Guide floats over the Update page. Uses the REAL UpdatePage. Two enabled Escape shortcuts on one
// window are an ambiguous overload that fires NEITHER (proven by census); so while Guide floats, the
// preserved UpdatePage must yield its own Escape (enabled: !guideActive) so a single Guide-style Escape
// is the sole enabled shortcut and actually closes Guide.
TestCase {
    name: "GuideEscapeOwnership"
    when: windowShown

    Window {
        id: win
        width: 1280; height: 720; visible: true
        property int guideEsc: 0
        // stand-in for GuidePage's own Escape (Qt.ApplicationShortcut), always enabled while Guide floats
        Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: win.guideEsc++ }
    }

    Component { id: updateComp; Colosseum.UpdatePage {} }
    property var page: null

    function init() {
        page = updateComp.createObject(win, { guideActive: false })
        verify(page !== null)
        win.requestActivate(); wait(60)
    }
    function cleanup() { if (page) page.destroy(); page = null }

    function fire() {
        win.guideEsc = 0
        win.requestActivate(); wait(40)
        keyClick(Qt.Key_Escape); wait(40)
    }

    // baseline: guideActive false → UpdatePage's Escape + the guide Escape are both enabled → ambiguous → neither fires
    function test_ungated_update_escape_makes_guide_ambiguous() {
        page.guideActive = false; wait(20)
        fire()
        compare(win.guideEsc, 0)
    }

    // the fix: guideActive true → UpdatePage yields its Escape → the guide Escape is the SOLE enabled shortcut → fires
    function test_gated_update_escape_lets_guide_own_escape() {
        page.guideActive = true; wait(20)
        fire()
        compare(win.guideEsc, 1)
    }
}
