import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml" as App

// Android's desktop presentation keeps the desktop gutters at launch zoom.
TestCase {
    id: testCase
    name: "AndroidVisualBaseline"
    when: windowShown

    Window {
        id: testWindow
        width: 1280
        height: 720
        visible: true
    }

    Component {
        id: worldComponent
        App.WorldPage {
            width: 1280
            height: 720
            medium: "Theatre"
        }
    }

    Component {
        id: adaptiveComponent
        App.AdaptiveLayout {}
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

    function boardOf(world) {
        var scroll = findChild(world, "theatreWorldScroll")
        verify(scroll !== null)
        var kids = scroll.contentItem.children
        for (var i = 0; i < kids.length; ++i) {
            if (kids[i].spacing !== undefined && kids[i].topPadding === 12)
                return kids[i]
        }
        return null
    }

    function test_desktop_presentation_keeps_outer_margin() {
        var desktop = adaptiveComponent.createObject(testCase, { androidHost: false, viewportWidth: 1280 })
        compare(desktop.outerPageMargin, desktop.pageMargin)
        compare(desktop.outerPageMargin, 54)
        var android = adaptiveComponent.createObject(testCase, { androidHost: true, viewportWidth: 905 })
        compare(android.outerPageMargin, 54)
        var phone = adaptiveComponent.createObject(testCase,
            { androidHost: true, desktopPresentation: false, viewportWidth: 412 })
        compare(phone.outerPageMargin, phone.pageMargin)
        desktop.destroy(); android.destroy(); phone.destroy()
    }

    function test_desktop_world_keeps_page_margin() {
        var world = worldComponent.createObject(testWindow.contentItem, { androidHost: false })
        verify(world !== null)
        compare(world.outerPageMargin, 54)
        compare(world.headerOuterMargin, 54)
        var board = boardOf(world)
        verify(board !== null)
        compare(board.x, 54)
        compare(board.width, 1280 - 108)
        world.destroy()
    }

    function test_android_world_keeps_desktop_gutters() {
        var world = worldComponent.createObject(testWindow.contentItem, { androidHost: true })
        verify(world !== null)
        compare(world.outerPageMargin, 54)
        compare(world.headerOuterMargin, 54)
        var board = boardOf(world)
        verify(board !== null)
        compare(board.x, 54)
        compare(board.width, 1280 - 108)
        world.destroy()
    }
}
