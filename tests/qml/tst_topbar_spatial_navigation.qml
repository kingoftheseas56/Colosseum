import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    id: testCase
    name: "TopBarSpatialNavigation"
    when: windowShown

    Window {
        id: testWindow
        width: 1100
        height: 180
        visible: true

        Item { id: backdrop; anchors.fill: parent }

        Colosseum.TopBar {
            id: bar
            backdrop: backdrop
            x: 30; y: 30
            width: 1040
        }
    }

    SignalSpy { id: boundarySpy; target: bar; signalName: "boundaryArrowRequested" }

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

    function focusableFace(root) {
        if (!root) return null
        if (root.focusPolicy !== undefined && root.focusPolicy !== Qt.NoFocus)
            return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = focusableFace(kids[i])
            if (found) return found
        }
        return null
    }

    function init() {
        testWindow.requestActivate()
        boundarySpy.clear()
        wait(20)
    }

    function test_right_moves_between_visible_mode_pills() {
        var tankoban = focusableFace(findChild(bar, "modePill_Tankoban"))
        var biblio = focusableFace(findChild(bar, "modePill_Biblio"))
        verify(tankoban !== null)
        verify(biblio !== null)
        tankoban.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Right)
        verify(biblio.activeFocus)
    }

    function test_down_at_topbar_boundary_is_exported_to_host() {
        var tankoban = focusableFace(findChild(bar, "modePill_Tankoban"))
        verify(tankoban !== null)
        tankoban.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Down)
        compare(boundarySpy.count, 1)
        compare(boundarySpy.signalArguments[0][0], Qt.Key_Down)
    }
}
