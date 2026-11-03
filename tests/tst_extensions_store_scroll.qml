import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtTest
import "../qml"

Item {
    id: harness
    width: 900
    height: 600
    property var rail: null
    property var world: null

    Window {
        id: hostWindow
        width: 900
        height: 600
        visible: true
        Item { id: surface; anchors.fill: parent }
    }

    Component {
        id: railComponent
        ExtensionsStoreRail {
            width: 600
            title: "Essentials"
            automationId: "testEssentials"
            canSeeAll: false
            items: [
                { slug: "one", name: "One", description: "Source one" },
                { slug: "two", name: "Two", description: "Source two" },
                { slug: "three", name: "Three", description: "Source three" },
                { slug: "four", name: "Four", description: "Source four" }
            ]
        }
    }

    Component {
        id: worldComponent
        WorldPage {
            width: 900
            height: 600
            medium: "Extensions"
            prominentScrollBar: true
            Item { width: parent.width; height: 1000 }
        }
    }

    TestCase {
        name: "ExtensionsStoreScroll"
        when: windowShown

        function init() {
            harness.rail = railComponent.createObject(surface)
            verify(harness.rail !== null)
        }
        function cleanup() {
            if (harness.rail) harness.rail.destroy()
            if (harness.world) harness.world.destroy()
            harness.rail = null
            harness.world = null
        }
        function test_compactCardsAndGrabbableHorizontalBar() {
            var card = findChild(harness.rail, "extensionsCard_one")
            var flick = findChild(harness.rail, "testEssentials")
            var bar = findChild(harness.rail, "testEssentialsScrollBar")
            verify(card !== null && flick !== null && bar !== null)
            compare(card.width, 260)
            compare(bar.policy, ScrollBar.AlwaysOn)
            verify(bar.visible && bar.height > 0)
            verify(flick.contentWidth > flick.width)
            mouseClick(bar, bar.width * 0.85, bar.height / 2, Qt.LeftButton)
            tryVerify(function() { return flick.contentX > 0 }, 1000)
        }
        function test_verticalBarIsVisibleWhenStoreOverflows() {
            harness.world = worldComponent.createObject(surface)
            verify(harness.world !== null)
            var bar = findChild(harness.world, "extensionsWorldScrollBar")
            verify(bar !== null)
            compare(bar.policy, ScrollBar.AlwaysOn)
            verify(bar.visible && bar.prominent)
            compare(bar.width, 14)
        }
    }
}
