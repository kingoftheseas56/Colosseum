import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "TankobanVolumeTouch"
    when: windowShown
    Window { id: testWindow; width: 1000; height: 650; visible: true }
    Component { id: libraryComponent; App.MangaTankobanLibrary { width: 1000; height: 480 } }
    SignalSpy { id: reads; signalName: "readVolumeRequested" }
    property var library: null
    property var pendingTouch: null

    function init() {
        testWindow.requestActivate()
        library = libraryComponent.createObject(testWindow.contentItem)
        verify(library !== null)
        if (library.touchInput !== undefined) library.touchInput = true
        let rows = []
        for (let i = 1; i <= 12; ++i)
            rows.push({ id: "volume-" + i, number: i, title: "Volume " + i, cover: "", state: "ready" })
        library.volumeRows = rows
        reads.target = library; reads.clear()
        waitForRendering(library)
        wait(280)
    }
    function releaseTouch(x, y) {
        if (!pendingTouch) return
        pendingTouch.release(0, testWindow.contentItem, x, y).commit()
        pendingTouch = null
    }
    function cleanup() {
        releaseTouch(-50, -50)
        reads.target = null
        if (library) library.destroy()
        library = null
    }
    function flow() { return findChild(library, "volumeFlow") }
    function card(number) { return findChild(library, "tankobanVolumeCard_" + number) }
    function beginTouch(item) {
        const p = item.mapToItem(testWindow.contentItem, item.width / 2, item.height / 2)
        verify(p.x > 0 && p.x < testWindow.width)
        pendingTouch = touchEvent(testWindow.contentItem)
        pendingTouch.press(0, testWindow.contentItem, p.x, p.y).commit()
        return p
    }

    function test_press_on_visible_neighbor_keeps_content_under_finger() {
        const owner = flow()
        const neighbor = card(2)
        verify(owner !== null && neighbor !== null)
        const before = owner.contentX
        const left = neighbor.mapToItem(owner, 0, 0).x
        const right = neighbor.mapToItem(owner, neighbor.width, 0).x
        verify(left >= 0 && right <= owner.width, "The neighboring card must already be fully visible")
        const p = beginTouch(neighbor)
        wait(100)
        verify(Math.abs(owner.contentX - before) < 1,
               "Pressing an already-visible volume must not center it out from under the finger")
        releaseTouch(p.x, p.y)
    }

    function test_visible_neighbor_tap_keeps_existing_read_route() {
        const p = beginTouch(card(2))
        wait(100)
        releaseTouch(p.x, p.y)
        tryCompare(reads, "count", 1)
        compare(reads.signalArguments[0][0], "volume-2")
    }

    function test_drag_from_neighbor_scrolls_without_reading_or_selecting() {
        library.selecting = true
        const owner = flow()
        const before = owner.contentX
        const p = beginTouch(card(2))
        wait(25)
        for (let i = 1; i <= 4; ++i) {
            pendingTouch.move(0, testWindow.contentItem, p.x - i * 35, p.y).commit()
            wait(25)
        }
        releaseTouch(p.x - 140, p.y)
        tryVerify(function() { return owner.contentX > before + 20 })
        compare(reads.count, 0)
        compare(library.selectedNumbers.length, 0)
    }

    function test_programmatic_navigation_keeps_beta_centered_cover_flow() {
        library.focusAtIndex(2)
        wait(280)
        const selected = card(3)
        verify(selected !== null)
        const center = selected.mapToItem(flow(), selected.width / 2, 0)
        verify(Math.abs(center.x - flow().width / 2) < 1)
        compare(library.currentItemScale, 1.10)
    }
}
