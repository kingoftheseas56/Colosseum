import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

// Actual Explore page and its bounded desktop-layout scroller. Injected preferences
// record commit attempts; the test never writes a user's shelf order.
TestCase {
    name: "AndroidBetaBiblioExploreTouch"
    when: windowShown
    Window { id: testWindow; width: 960; height: 640; visible: true }
    QtObject {
        id: prefs
        property var order: []
        property var hidden: []
        property int moveCount: 0
        signal changed()
        function move(key, index) { ++moveCount }
        function setVisible(key, visible) {}
    }
    Component {
        id: exploreComponent
        App.BiblioExplorePage {
            width: 960; height: 640; editMode: true; active: false
            catalogSource: null; extensionsSource: null; preferences: prefs
        }
    }
    SignalSpy { id: opened; signalName: "itemRequested" }

    function page() {
        prefs.moveCount = 0
        testWindow.requestActivate()
        const item = createTemporaryObject(exploreComponent, testWindow.contentItem)
        verify(item !== null)
        opened.target = item; opened.clear()
        wait(60)
        return item
    }
    function gesture(item) {
        const flick = findChild(item, "biblioExploreFlick")
        verify(flick !== null)
        verify(item.customizableRowKeys.length > 0)
        const handle = findChild(item, "biblioExploreDrag_" + item.customizableRowKeys[Math.min(1, item.customizableRowKeys.length - 1)])
        verify(handle !== null)
        const start = handle.mapToItem(flick, handle.width / 2, handle.height / 2)
        return { flick: flick, start: start, points: touchEvent(flick) }
    }
    function test_swipe_from_reorder_handle_cancels_preview_when_scroller_takes_grab() {
        const item = page()
        const g = gesture(item)
        const initialY = g.flick.contentY
        g.points.press(0, g.flick, g.start.x, g.start.y).commit()
        try {
            wait(25)
            for (var offset = 20; offset <= 100; offset += 20) {
                g.points.move(0, g.flick, g.start.x, g.start.y - offset).commit()
                wait(20)
            }
        } finally {
            g.points.release(0, g.flick, g.start.x, g.start.y - 100).commit()
        }
        verify(g.flick.contentY > initialY, "bounded Explore viewport receives vertical finger drag")
        compare(item.dragKeys, null, "a canceled drag must discard its temporary order")
        compare(item.draggingKey, "")
        compare(prefs.moveCount, 0, "cancellation must not persist a reorder")
        compare(opened.count, 0)
    }
    function test_hiding_page_cancels_preview_before_late_release() {
        const item = page()
        const g = gesture(item)
        g.points.press(0, g.flick, g.start.x, g.start.y).commit()
        try {
            wait(25)
            item.visible = false
            wait(25)
            item.visible = true
        } finally {
            item.visible = true
            g.points.release(0, g.flick, g.start.x, g.start.y).commit()
        }
        compare(item.dragKeys, null, "hiding the page must discard the temporary order")
        compare(item.draggingKey, "")
        compare(prefs.moveCount, 0)
        compare(opened.count, 0)
    }
}
