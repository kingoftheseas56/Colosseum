import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Finding 12: this is the production-boundary journey. WorldPage owns the real
// section coordinator and spatial Keys handler; ContinueRow, ContinueTile,
// FeaturedCarousel, and CarouselSlide are the shipped components under test.
TestCase {
    id: testCase
    name: "WorldKeyboardJourney"
    when: windowShown

    property var railAItems: [
        { id: "A0", kind: "book", title: "A0", progress: 0.1 },
        { id: "A1", kind: "book", title: "A1", progress: 0.2 },
        { id: "A2", kind: "book", title: "A2", progress: 0.3 },
        { id: "A3", kind: "book", title: "A3", progress: 0.4 },
        { id: "A4", kind: "book", title: "A4", progress: 0.5 }
    ]
    property var railBItems: [
        { id: "B0", kind: "book", title: "B0" },
        { id: "B1", kind: "book", title: "B1" },
        { id: "B2", kind: "book", title: "B2" }
    ]
    property var carouselSlides: [
        { id: "F0", title: "F0" },
        { id: "F1", title: "F1" },
        { id: "F2", title: "F2" }
    ]
    property string activatedId: ""
    property string removedId: ""

    Window {
        id: testWindow
        width: 760
        height: 720
        visible: true

        FocusScope {
            id: focusShell
            anchors.fill: parent
            focus: true

            Colosseum.WorldPage {
                id: world
                objectName: "productionWorldPage"
                anchors.fill: parent
                medium: "Journey"
                focus: true

                Colosseum.ContinueRow {
                    id: railA
                    objectName: "journeyRailA"
                    items: testCase.railAItems
                    showSeeAll: false
                    forgetHandler: function(item) {
                        testCase.removedId = item.id
                    }
                    onDetailRequested: function(item) { testCase.activatedId = item.id }
                }
                Colosseum.ContinueRow {
                    id: railB
                    objectName: "journeyRailB"
                    items: testCase.railBItems
                    showSeeAll: false
                    forgetHandler: function(item) {
                        testCase.removedId = item.id
                    }
                    onDetailRequested: function(item) { testCase.activatedId = item.id }
                }
                Colosseum.FeaturedCarousel {
                    id: featured
                    objectName: "journeyFeaturedCarousel"
                    slides: testCase.carouselSlides
                    onPrimaryClicked: function(index) {
                        testCase.activatedId = slides[index].id
                    }
                }
            }
        }
    }

    function findCollectionOwner(node, parentObjectName, ownerObjectName) {
        var queue = [node]
        while (queue.length) {
            var current = queue.shift()
            if (current.objectName === ownerObjectName
                    && current.keyboardReturnOwner === true
                    && current.keyboardItemAtIndex) {
                for (var parent = current.parent; parent; parent = parent.parent) {
                    if (parent.objectName === parentObjectName)
                        return current
                }
            }
            var children = current.children || []
            for (var i = 0; i < children.length; ++i)
                queue.push(children[i])
        }
        return null
    }

    function tileIsFullyVisible(owner, index) {
        var tile = owner.keyboardItemAtIndex(index)
        if (!tile)
            return false
        var left = tile.x
        var right = tile.x + tile.width
        return left >= owner.contentX - 0.5
                && right <= owner.contentX + owner.width + 0.5
    }

    function initTestCase() {
        verify(featured.keyboardSectionCoordinator === world.keyboardSectionCoordinator)
        verify(featured.index >= 0)
    }

    function test_real_world_sections_preserve_identity_offset_and_activation() {
        var ownerA = findCollectionOwner(world, "journeyRailA", "continueRail")
        var ownerB = findCollectionOwner(world, "journeyRailB", "continueRail")
        var carouselOwner = findCollectionOwner(world, "journeyFeaturedCarousel", "featuredCarouselView")
        verify(ownerA)
        verify(ownerB)
        verify(carouselOwner)
        compare(ownerA.keyboardItems.length, 5)
        compare(ownerB.keyboardItems.length, 3)
        compare(carouselOwner.keyboardItems.length, 3)
        verify(carouselOwner.keyboardItemAtIndex(0).slide !== undefined)

        ownerA.currentIndex = 3
        ownerA.contentX = 168
        testWindow.contentItem.focus = true
        testWindow.requestActivate()
        testWindow.raise()
        world.focus = true
        ownerA.focusPolicy = Qt.StrongFocus
        ownerB.focusPolicy = Qt.StrongFocus
        ownerA.focus = true
        focusShell.focus = true
        tryVerify(function() { return ownerA.activeFocus })
        compare(ownerA.keyboardIdentityForIndex(ownerA.currentIndex), "A3")
        verify(tileIsFullyVisible(ownerA, 3))
        var savedOffset = ownerA.contentX

        keyClick(Qt.Key_Down)
        tryCompare(ownerB, "currentIndex", 2)
        verify(ownerB.activeFocus)
        compare(ownerB.keyboardIdentityForIndex(ownerB.currentIndex), "B2")
        compare(world.keyboardSectionCoordinator.sourceIdentity, "A3")
        compare(world.keyboardSectionCoordinator.sourceOffset, savedOffset)
        verify(tileIsFullyVisible(ownerB, 2))

        keyClick(Qt.Key_Up)
        tryCompare(ownerA, "currentIndex", 3)
        verify(ownerA.activeFocus)
        compare(ownerA.keyboardIdentityForIndex(ownerA.currentIndex), "A3")
        compare(ownerA.contentX, savedOffset)
        verify(tileIsFullyVisible(ownerA, 3))

        // A reorder while away keeps the bookmarked stable identity, even though
        // its index changes from 3 to 2.
        keyClick(Qt.Key_Down)
        tryCompare(ownerB, "currentIndex", 2)
        testCase.railAItems = [
            { id: "A0", kind: "book", title: "A0" },
            { id: "A1", kind: "book", title: "A1" },
            { id: "A3", kind: "book", title: "A3" },
            { id: "A2", kind: "book", title: "A2" },
            { id: "A4", kind: "book", title: "A4" }
        ]
        tryCompare(ownerA, "keyboardItems", testCase.railAItems)
        keyClick(Qt.Key_Up)
        tryCompare(ownerA, "currentIndex", 2)
        compare(ownerA.keyboardIdentityForIndex(ownerA.currentIndex), "A3")
        verify(ownerA.activeFocus)

        // Remove the bookmarked item while the shorter rail is focused. Return
        // resolves to the nearest semantic peer at the surviving index.
        keyClick(Qt.Key_Down)
        tryCompare(ownerB, "currentIndex", 2)
        testCase.railAItems = [
            { id: "A0", kind: "book", title: "A0" },
            { id: "A1", kind: "book", title: "A1" },
            { id: "A2", kind: "book", title: "A2" },
            { id: "A4", kind: "book", title: "A4" }
        ]
        tryCompare(ownerA, "keyboardItems", testCase.railAItems)
        keyClick(Qt.Key_Up)
        tryCompare(ownerA, "currentIndex", 2)
        compare(ownerA.keyboardIdentityForIndex(ownerA.currentIndex), "A2")
        verify(ownerA.activeFocus)

        // A lateral move reanchors the active rail and clears the old section
        // return intent before Enter activates the exact current stable id.
        keyClick(Qt.Key_Down)
        tryCompare(ownerB, "currentIndex", 2)
        keyClick(Qt.Key_Left)
        tryCompare(ownerB, "currentIndex", 1)
        verify(!world.keyboardSectionCoordinator.sourceOwner)
        keyClick(Qt.Key_Return)
        compare(testCase.activatedId, "B1")
    }
}
