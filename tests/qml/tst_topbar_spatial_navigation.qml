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
    SignalSpy { id: stremioSpy; target: bar; signalName: "stremioClicked" }

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
        stremioSpy.clear()
        bar.activeMedium = ""
        bar.lifecycleActive = true
        wait(20)
    }

    function test_stremio_is_theatre_only_and_between_search_and_account() {
        bar.activeMedium = "Theatre"
        wait(0)
        var stremio = findChild(bar, "topBarStremioButton")
        var search = findChild(bar, "topBarSearch")
        var account = findChild(bar, "colosseumTopbarAccountButton")
        verify(stremio !== null)
        var asset = findChild(stremio, "topBarStremioOfficialAsset")
        verify(asset !== null)
        verify(String(asset.source).indexOf("assets/icons/stremio-official.svg") >= 0)
        bar.activeMedium = "Biblio"
        compare(stremio.visible, false)
        bar.activeMedium = "Tankoban"
        compare(stremio.visible, false)
        bar.activeMedium = "Theatre"
        wait(0)
        compare(stremio.visible, true)
        compare(search.parent, stremio.parent)
        compare(stremio.parent, account.parent)
        var siblings = search.parent.children
        verify(siblings.indexOf(search) < siblings.indexOf(stremio))
        verify(siblings.indexOf(stremio) < siblings.indexOf(account))
        var face = focusableFace(stremio)
        verify(face !== null)
        face.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Return)
        compare(stremioSpy.count, 1)
        bar.lifecycleActive = false
        compare(stremio.visible, false)
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
