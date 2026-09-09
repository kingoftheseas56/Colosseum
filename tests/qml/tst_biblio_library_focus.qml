import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "BiblioLibraryFocus"
    when: windowShown

    Window {
        id: testWindow
        width: 960
        height: 640
        visible: true

        Item {
            id: clippedHost
            width: 960
            height: 120
            clip: true
        }

    }

    Component {
        id: pageComponent
        Colosseum.BiblioLibraryPage {
            width: 960
            height: 640
            visible: true
        }
    }

    property var page
    property var search
    property var offscreenPage

    function rows(prefix) {
        var out = []
        for (var i = 0; i < 12; ++i)
            out.push({ entry: { id: prefix + i, title: "Book " + i, author: "Author" } })
        return out
    }

    function init() {
        page = pageComponent.createObject(testWindow)
        verify(page !== null)
        page.visibleRows = rows("initial-")
        wait(40)
        search = findChild(page, "biblioLibrarySearch")
        verify(search !== null)
    }

    function cleanup() {
        if (page)
            page.destroy()
        if (offscreenPage)
            offscreenPage.destroy()
        page = null
        search = null
        offscreenPage = null
    }

    function test_a_query_model_change_keeps_search_focus() {
        page.visibleRows = rows("filtered-")
        wait(40)
        verify(page.visibleCount === 12,
               "changing visibleRows must keep the populated Library model stable")
    }

    function test_offscreen_wall_does_not_auto_focus() {
        var externalAnchor = Qt.createQmlObject(
            'import QtQuick 2.15; Item { width: 8; height: 8; focus: true }', testWindow)
        offscreenPage = pageComponent.createObject(clippedHost)
        verify(offscreenPage !== null)
        offscreenPage.y = clippedHost.height + 40
        offscreenPage.visibleRows = rows("offscreen-")
        externalAnchor.forceActiveFocus()
        offscreenPage.focusKeyboardWall()
        wait(80)
        verify(!offscreenPage._keyboardWallForTest.focus,
               "a clipped retained Library wall must not remain eager-focus eligible")
        verify(!offscreenPage._keyboardWallForTest.activeFocus,
               "a retained Library wall clipped outside the window must not claim focus")
        externalAnchor.destroy()
    }

    function findChild(root, objectName) {
        if (!root)
            return null
        if (root.objectName === objectName)
            return root
        var children = root.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findChild(children[i], objectName)
            if (found)
                return found
        }
        return null
    }
}
