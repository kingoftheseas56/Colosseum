import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/guide" as Guide

// Living Guide Task 5 — the reusable immersive overlay contract. Drives GuideOverlay in a test window
// with a focusable "host stand-in" and proves: a deep link opens the requested lesson; Escape closes
// the overlay before any host back is forwarded; focus returns to whatever held it before Guide opened;
// repeated open/close cycles stay balanced; and there is exactly one close signal per open. Host
// INTEGRATION (Theatre pause/resume, reader state) is deliberately NOT here — the overlay stays
// host-agnostic and exposes `active` + the pure capturePlayback/shouldResume for hosts to wire later.
TestCase {
    name: "GuideOverlay"
    when: windowShown

    Window {
        id: win
        width: 900; height: 640; visible: true
        property int hostEscapes: 0
        Item {
            id: hostFocus
            objectName: "hostFocus"
            anchors.fill: parent
            focus: true
            Keys.onEscapePressed: win.hostEscapes++   // the host's own back — must NOT fire while Guide owns Escape
        }
    }

    Component { id: overlayComp; Guide.GuideOverlay {} }
    property var overlay: null

    SignalSpy { id: openedSpy; signalName: "opened" }
    SignalSpy { id: closedSpy; signalName: "closed" }

    function fixtureCatalog() {
        return Qt.createQmlObject('import QtQuick 2.15; QtObject {\n'
            + 'property var allLessons: [{ id: "fixture.overlay", section: "start", title: "Overlay fixture", '
            + 'outcome: "A fixture lesson.", status: "published", order: 1, worlds: [], evidence: [], '
            + 'verifiedCommit: "fixture", verifiedDate: "2026-08-09", contexts: ["home"], searchTerms: ["overlay"], '
            + 'blocks: [{kind:"paragraph", text:"Overlay body"}], related: [] }];\n'
            + 'property var publishedLessons: allLessons;\n'
            + 'function find(id) { return id === "fixture.overlay" ? allLessons[0] : null; }\n'
            + 'function search(q, c) { return []; }\n'
            + 'function section(s) { return s === "start" ? allLessons : []; }\n'
            + '}', win, "overlayFixtureCatalog");
    }

    function init() {
        win.hostEscapes = 0
        hostFocus.forceActiveFocus()
        overlay = overlayComp.createObject(win, { catalog: fixtureCatalog() })
        verify(overlay !== null)
        openedSpy.target = overlay
        closedSpy.target = overlay
        wait(30)
    }
    function cleanup() {
        openedSpy.clear(); closedSpy.clear()
        openedSpy.target = null; closedSpy.target = null
        if (overlay) overlay.destroy()
        overlay = null
    }

    function guidePage() { return findChild(overlay, "guidePage") }

    // deep link — open(lessonId, origin) drives the embedded GuidePage straight to that lesson's article
    function test_deep_link_opens_requested_lesson() {
        overlay.open("fixture.overlay", "Manga reader")
        verify(overlay.active)
        var gp = guidePage()
        verify(gp !== null)
        tryCompare(gp, "currentView", "article")
        compare(gp.currentLesson.id, "fixture.overlay")
        compare(openedSpy.count, 1)
    }

    // Escape closes the overlay (via GuidePage's own Escape) and does NOT forward the host back action
    function test_escape_closes_overlay_before_host_back() {
        overlay.open("fixture.overlay", "Manga reader")
        verify(overlay.active)
        keyClick(Qt.Key_Escape)
        tryCompare(overlay, "active", false)
        compare(closedSpy.count, 1)
        compare(win.hostEscapes, 0)
    }

    // focus returns to whatever held it before Guide opened
    function test_focus_returns_to_prior_item_on_close() {
        win.requestActivate()
        hostFocus.forceActiveFocus()
        tryVerify(function() { return hostFocus.activeFocus })   // host holds focus before Guide opens
        overlay.open("fixture.overlay", "")
        tryVerify(function() { return !hostFocus.activeFocus })  // Guide trapped focus
        overlay.close()
        tryVerify(function() { return hostFocus.activeFocus })   // focus returned to the host
    }

    // repeated cycles stay balanced and never destroy the host beneath
    function test_repeated_open_close_cycles() {
        overlay.open("fixture.overlay", ""); verify(overlay.active)
        overlay.close(); verify(!overlay.active)
        overlay.open("fixture.overlay", ""); verify(overlay.active)
        overlay.close(); verify(!overlay.active)
        verify(hostFocus !== null)              // host stand-in survived every cycle
        compare(openedSpy.count, 2)
        compare(closedSpy.count, 2)
    }

    // exactly one close per open — a double close never double-emits
    function test_no_duplicate_close_signal() {
        overlay.open("fixture.overlay", "")
        overlay.close()
        overlay.close()                          // second close is a no-op
        compare(closedSpy.count, 1)
    }

    // the pure host-state helpers are exposed on the overlay for hosts to wire pause/resume
    function test_playback_helpers_are_exposed() {
        compare(overlay.capturePlayback(true).resumeOnClose, true)
        compare(overlay.shouldResume({ resumeOnClose: true }, true), true)
        compare(overlay.shouldResume({ resumeOnClose: true }, false), false)
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
