import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneGalaxyEscape"
    when: windowShown

    Window {
        id: host
        width: 354
        height: 792
        visible: true
    }

    Component {
        id: pageComponent
        App.GalaxyUniversePage {
            anchors.fill: parent
            extensionId: ""
            reducedMotion: true
        }
    }

    property var page: null

    SignalSpy {
        id: backRequestedSpy
        target: testCase.page
        signalName: "backRequested"
    }

    function init() {
        host.width = 354
        host.height = 792
        page = pageComponent.createObject(host.contentItem)
        verify(page !== null, "the actual GalaxyUniversePage instantiates without an extension")
        wait(50)
        compare(page.extensionId, "")
        verify(page.destinations.some(function(destination) { return destination.id === "high" }),
               "the local High Republic starter destination is available")
        const map = findChild(page, "starWarsGalaxySystem")
        verify(map !== null, "the actual Galaxy map is available for a local presentation fixture")
        map.destinations = [page.starterDestinations[0]]
        wait(20)
    }

    function cleanup() {
        if (page)
            page.destroy()
        page = null
        wait(0)
    }

    function findChild(parent, objectName) {
        if (!parent)
            return null
        if (parent.objectName === objectName)
            return parent
        const children = parent.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findChild(children[i], objectName)
            if (found)
                return found
        }
        return null
    }

    function findDescendant(parent, predicate) {
        if (!parent)
            return null
        if (predicate(parent))
            return parent
        const children = parent.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findDescendant(children[i], predicate)
            if (found)
                return found
        }
        return null
    }

    function touchItem(item) {
        const root = host.contentItem
        const point = item.mapToItem(root, item.width / 2, item.height / 2)
        verify(point.x >= 0 && point.y >= 0
               && point.x <= root.width && point.y <= root.height,
               "the map hit target is inside the visible viewport")
        touchEvent(root).press(0, root, point.x, point.y).commit()
        wait(20)
        touchEvent(root).release(0, root, point.x, point.y).commit()
        wait(50)
    }

    function assertDestinationBackAt(width, height) {
        host.width = width
        host.height = height
        wait(40)
        compare(page.width, width)
        compare(page.height, height)
        verify(page.visible, "the Galaxy page remains on screen before opening a destination")

        const map = findChild(page, "starWarsGalaxySystem")
        const gate = findChild(map, "starWarsGalaxyGate_high")
        const hitTarget = findDescendant(gate, function(item) {
            return item.containsMouse !== undefined && item.mouseX !== undefined
                   && item.mouseY !== undefined
        })
        verify(map && gate && gate.visible && hitTarget && hitTarget.enabled,
               "the actual High Republic map hit target is visible and enabled")

        touchItem(hitTarget)
        compare(page.selectedDestinationId, "high",
                "a normal touch on the visible map opens the local High Republic destination")
        verify(page.visible)

        backRequestedSpy.clear()
        const consumed = page.requestEscape()
        compare(consumed, true,
                "Back from a destination is consumed by the Galaxy page")
        compare(page.selectedDestinationId, "",
                "the consumed Back returns to the Galaxy landing map")
        verify(page.visible,
               "consuming destination Back keeps the Galaxy Universe page visible")
        compare(backRequestedSpy.count, 0,
                "destination Back does not request closing the Galaxy Universe")

        const landingEscape = page.requestEscape()
        compare(landingEscape, false,
                "Back on the landing map is left for the Main shell to handle")
        compare(backRequestedSpy.count, 0,
                "the shell-owned landing Back does not emit a duplicate page close request")
    }

    function test_phone_portrait_and_landscape_back_unwinds_destination_first() {
        backRequestedSpy.clear()
        assertDestinationBackAt(354, 792)
        assertDestinationBackAt(792, 354)
    }
}
