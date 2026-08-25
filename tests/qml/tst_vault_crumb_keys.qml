import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Vault ux uplift S15 — the breadcrumb's keyboard reach: the crumb root is the focus unit
// (the grid's navigation law), Left/Right move the focused segment, Return/Space ascends to
// that level (the mouse path's twin), and the ring pins to the focused segment.
TestCase {
    name: "VaultCrumbKeys"
    when: windowShown

    Window { id: testWindow; width: 900; height: 140; visible: true }

    Colosseum.VaultBrowseCrumb {
        id: crumb
        parent: testWindow.contentItem
        x: 20; y: 20
        width: 500
        stack: [
            { key: "/a", displayTitle: "Archive", nodeType: "" },
            { key: "/a/Shows", displayTitle: "Shows", nodeType: "folder" },
            { key: "/a/Shows/Sopranos", displayTitle: "Sopranos", nodeType: "show" }
        ]
    }
    SignalSpy { id: segSpy; signalName: "segmentClicked" }

    function init() {
        segSpy.target = crumb
        segSpy.clear()
        crumb.keyboardSegment = -1
        mouseMove(testWindow, testWindow.width - 1, testWindow.height - 1)
        wait(20)
    }
    function cleanup() {
        segSpy.target = null
    }

    function test_arrow_cycling_activates_on_return_and_space() {
        testWindow.requestActivate()
        crumb.forceActiveFocus()
        wait(20)
        verify(crumb.activeFocus === true)

        // Right moves to the first clickable segment (Archive, the root-level ascender).
        keyClick(Qt.Key_Right, Qt.NoModifier)
        wait(20)
        compare(crumb.keyboardSegment, 0)
        keyClick(Qt.Key_Return, Qt.NoModifier)
        wait(20)
        compare(segSpy.count, 1)
        compare(segSpy.signalArguments[0][0], 0)

        // Right again → Shows (segment index 1); Space activates (the twin).
        keyClick(Qt.Key_Right, Qt.NoModifier)
        wait(20)
        compare(crumb.keyboardSegment, 1)
        keyClick(Qt.Key_Space, Qt.NoModifier)
        wait(20)
        compare(segSpy.count, 2)
        compare(segSpy.signalArguments[1][0], 1)

        // The last clickable segment (Shows) is the end of the walk; Right clamps there.
        keyClick(Qt.Key_Right, Qt.NoModifier)
        wait(20)
        compare(crumb.keyboardSegment, 1)
        keyClick(Qt.Key_Left, Qt.NoModifier)
        wait(20)
        compare(crumb.keyboardSegment, 0)
    }
}
