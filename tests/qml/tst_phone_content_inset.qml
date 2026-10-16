import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneContentInset"
    when: windowShown

    Window {
        id: testWindow
        width: 412
        height: 915
        visible: true
    }

    Component {
        id: worldComponent
        App.WorldPage {
            width: 412
            height: 915
            medium: "Theatre"
            androidHost: true
            contentBottomInset: 120
        }
    }

    Component {
        id: downloadsComponent
        App.DownloadsPage {
            width: 412
            height: 915
            contentBottomInset: 120
            downloadsApi: null
            audiobooksApi: null
        }
    }

    function findChild(root, name) {
        if (!root)
            return null
        if (root.objectName === name)
            return root
        var children = root.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findChild(children[i], name)
            if (found)
                return found
        }
        return null
    }

    function test_world_viewport_ends_above_bar() {
        var worlds = ["Theatre", "Tankoban", "Biblio"]
        for (var i = 0; i < worlds.length; ++i) {
            var medium = worlds[i]
            var world = worldComponent.createObject(testWindow.contentItem, { medium: medium })
            verify(world !== null)
            var scroll = findChild(world, medium.toLowerCase() + "WorldScroll")
            verify(scroll !== null)
            compare(scroll.y + scroll.height, world.height - world.contentBottomInset)
            world.destroy()
        }
    }

    function test_downloads_viewport_ends_above_bar() {
        var downloads = downloadsComponent.createObject(testWindow.contentItem)
        verify(downloads !== null)
        var scroll = findChild(downloads, "downloadsPageScroll")
        verify(scroll !== null)
        compare(scroll.height, downloads.height - downloads.contentBottomInset)
        downloads.destroy()
    }
}
