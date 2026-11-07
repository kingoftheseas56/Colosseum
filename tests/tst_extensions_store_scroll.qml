import QtQuick
import QtQuick.Controls.Basic
import QtTest
import "../qml"

Item {
    id: harness
    width: 900
    height: 600
    property var rail: null
    property var world: null

    Item { id: surface; anchors.fill: parent }

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
            glassScrollBar: true
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
            verify(card.width < 240, "The desktop shelf should fit more add-ons across")
            compare(bar.policy, ScrollBar.AlwaysOn)
            verify(bar.visible && bar.glassStyle && bar.height > bar.contentItem.height)
            verify(flick.contentWidth > flick.width)
            mouseClick(bar, bar.width * 0.85, bar.height / 2, Qt.LeftButton)
            tryVerify(function() { return flick.contentX > 0 }, 1000)
        }
        function test_shelf_buttons_page_in_both_directions() {
            var flick = findChild(harness.rail, "testEssentials")
            var previous = findChild(harness.rail, "testEssentialsPrevious")
            var next = findChild(harness.rail, "testEssentialsNext")
            verify(flick && previous && next)
            verify(!previous.enabled && next.enabled)
            mouseClick(next)
            tryVerify(function() { return flick.contentX > 0 })
            verify(previous.enabled && !next.enabled)
            previous.forceActiveFocus()
            keyClick(Qt.Key_Return)
            tryCompare(flick, "contentX", 0)
            verify(!previous.enabled && next.enabled)
        }
        function test_verticalBarIsVisibleWhenStoreOverflows() {
            harness.world = worldComponent.createObject(surface)
            verify(harness.world !== null)
            var bar = findChild(harness.world, "extensionsWorldScrollBar")
            verify(bar !== null)
            compare(bar.policy, ScrollBar.AlwaysOn)
            verify(bar.visible && bar.glassStyle && bar.width > bar.contentItem.width)
            mouseClick(bar, bar.width / 2, bar.height * 0.85, Qt.LeftButton)
            tryVerify(function() { return bar.flick.contentY > 0 }, 1000)
        }
    }
}
