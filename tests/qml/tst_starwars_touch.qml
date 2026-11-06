import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "StarWarsTouch"
    when: windowShown
    Window { id: testWindow; width: 1000; height: 650; visible: true }
    Component {
        id: galaxyComponent
        App.StarWarsGalaxySystem {
            width: 1000; height: 650
            destinations: [{ id: "test", name: "TEST DESTINATION", node: true,
                             radius: 450, angle: 0, y: 0, light: "#ffffff",
                             mid: "#aaaaaa", dark: "#444444" }]
        }
    }
    SignalSpy { id: skywalker; signalName: "skywalkerActivated" }
    SignalSpy { id: destination; signalName: "destinationActivated" }
    property var galaxy: null
    property var pendingTouch: null

    function init() {
        testWindow.requestActivate()
        galaxy = galaxyComponent.createObject(testWindow.contentItem)
        verify(galaxy !== null)
        if (galaxy.touchInput !== undefined) galaxy.touchInput = true
        skywalker.target = galaxy; skywalker.clear()
        destination.target = galaxy; destination.clear()
        waitForRendering(galaxy)
    }
    function releaseTouch(x, y) {
        if (!pendingTouch) return
        pendingTouch.release(0, testWindow.contentItem, x, y).commit()
        pendingTouch = null
    }
    function cleanup() {
        releaseTouch(-50, -50)
        skywalker.target = null; destination.target = null
        if (galaxy) galaxy.destroy()
        galaxy = null
    }
    function cases() { return [{ tag: "skywalker", name: "starWarsGalaxySkywalker" },
                               { tag: "destination", name: "starWarsGalaxyGate_test" }] }
    function pointerArea(item) {
        if (!item) return null
        const children = item.children || []
        for (let i = 0; i < children.length; ++i)
            if (children[i].pressed !== undefined && children[i].acceptedButtons !== undefined)
                return children[i]
        return null
    }
    function beginTouch(item, x, y) {
        const p = item.mapToItem(testWindow.contentItem, x, y)
        pendingTouch = touchEvent(testWindow.contentItem)
        pendingTouch.press(0, testWindow.contentItem, p.x, p.y).commit()
        return p
    }

    function test_node_tap_activates_data() { return cases() }
    function test_node_tap_activates(data) {
        const area = pointerArea(findChild(galaxy, data.name))
        verify(area !== null)
        const p = beginTouch(area, area.width / 2, area.height / 2)
        wait(25)
        releaseTouch(p.x, p.y)
        compare(skywalker.count + destination.count, 1)
    }

    function test_swipe_inside_node_does_not_activate_data() { return cases() }
    function test_swipe_inside_node_does_not_activate(data) {
        const area = pointerArea(findChild(galaxy, data.name))
        verify(area !== null && area.width > 40)
        const p = beginTouch(area, area.width / 2 - 18, area.height / 2)
        wait(25)
        pendingTouch.move(0, testWindow.contentItem, p.x + 18, p.y).commit()
        wait(25)
        pendingTouch.move(0, testWindow.contentItem, p.x + 36, p.y).commit()
        wait(25)
        releaseTouch(p.x + 36, p.y)
        compare(skywalker.count + destination.count, 0,
                "A finger swipe staying inside a node hit box must not activate its destination")
    }

    function test_background_drag_rotates_existing_map() {
        const before = galaxy.cameraYaw
        const p = beginTouch(galaxy, 80, 600)
        wait(25)
        pendingTouch.move(0, testWindow.contentItem, p.x + 60, p.y).commit()
        wait(25)
        releaseTouch(p.x + 60, p.y)
        verify(galaxy.cameraYaw > before)
        compare(skywalker.count + destination.count, 0)
    }

    function test_swipe_returning_to_start_stays_canceled_data() { return cases() }
    function test_swipe_returning_to_start_stays_canceled(data) {
        const area = pointerArea(findChild(galaxy, data.name))
        verify(area !== null)
        const p = beginTouch(area, area.width / 2 - 18, area.height / 2)
        wait(25)
        pendingTouch.move(0, testWindow.contentItem, p.x + 36, p.y).commit()
        wait(25)
        pendingTouch.move(0, testWindow.contentItem, p.x, p.y).commit()
        wait(25)
        releaseTouch(p.x, p.y)
        compare(skywalker.count + destination.count, 0)
    }

    function test_disabling_and_reenabling_node_cancels_current_press_data() { return cases() }
    function test_disabling_and_reenabling_node_cancels_current_press(data) {
        const area = pointerArea(findChild(galaxy, data.name))
        verify(area !== null)
        const p = beginTouch(area, area.width / 2, area.height / 2)
        wait(25)
        area.enabled = false
        wait(25)
        area.enabled = true
        releaseTouch(p.x, p.y)
        compare(skywalker.count + destination.count, 0)
        const next = beginTouch(area, area.width / 2, area.height / 2)
        wait(25)
        releaseTouch(next.x, next.y)
        compare(skywalker.count + destination.count, 1, "The next intentional tap must recover")
    }

    function test_real_mouse_keeps_existing_node_activation_data() { return cases() }
    function test_real_mouse_keeps_existing_node_activation(data) {
        const area = pointerArea(findChild(galaxy, data.name))
        verify(area !== null)
        mouseClick(area, area.width / 2, area.height / 2, Qt.LeftButton)
        compare(skywalker.count + destination.count, 1)
    }
}
