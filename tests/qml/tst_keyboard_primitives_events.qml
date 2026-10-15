import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    id: testCase
    name: "KeyboardPrimitiveEvents"
    when: windowShown

    Window {
        id: testWindow
        width: 640
        height: 480
        visible: true

        Colosseum.KeyboardAction {
            id: action
            objectName: "testAction"
            width: 100
            height: 40
            contextEnabled: true
            pointerEnabled: false
        }

        ListView {
            id: collection
            y: 60
            width: 300
            height: 180
            model: 12
            delegate: Rectangle { width: 300; height: 30 }
            focus: true
            Keys.onPressed: function(event) { collectionNav.handle(event) }
        }

        Colosseum.KeyboardCollectionController {
            id: collectionNav
            view: collection
            orientation: "grid"
            columns: 3
            count: collection.count
            currentIndex: collection.currentIndex
            pageStep: 3
            contextEnabled: true
        }

        Flickable {
            id: scroller
            y: 260
            width: 300
            height: 100
            contentWidth: width
            contentHeight: 500
            Keys.onPressed: function(event) { scrollNav.handle(event) }
        }

        Colosseum.KeyboardScrollController {
            id: scrollNav
            flick: scroller
            lineStep: 40
            pageFraction: 0.8
        }

        Item {
            id: boundaryParent
            x: 340; y: 260; width: 280; height: 100
            property int boundaryCount: 0
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Down) {
                    boundaryParent.boundaryCount += 1
                    event.accepted = true
                }
            }

            Flickable {
                id: boundaryChild
                objectName: "boundaryChild"
                width: parent.width; height: parent.height
                contentWidth: width; contentHeight: height
                focusPolicy: Qt.TabFocus
                Keys.onPressed: (event) => boundaryCollection.handle(event)
            }

            Colosseum.KeyboardCollectionController {
                id: boundaryCollection
                view: boundaryChild
                orientation: "vertical"
                count: 1
                currentIndex: 0
            }
        }
    }

    SignalSpy { id: actionSpy; target: action; signalName: "triggered" }
    SignalSpy { id: actionContextSpy; target: action; signalName: "contextRequested" }
    SignalSpy { id: activatedSpy; target: collectionNav; signalName: "activated" }
    SignalSpy { id: collectionContextSpy; target: collectionNav; signalName: "contextRequested" }

    function init() {
        testWindow.requestActivate()
        actionSpy.clear()
        actionContextSpy.clear()
        activatedSpy.clear()
        collectionContextSpy.clear()
        collection.model = 12
        collectionNav.columns = 3
        collection.currentIndex = 0
        scroller.contentY = 0
        scroller.topMargin = 0
        scroller.bottomMargin = 0
        wait(10)
    }

    function test_atomic_action_activation_and_context() {
        action.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Return)
        keyClick(Qt.Key_Enter)
        keyClick(Qt.Key_Space)
        compare(actionSpy.count, 2)
        keyClick(Qt.Key_Menu)
        keyClick(Qt.Key_F10, Qt.ShiftModifier)
        compare(actionContextSpy.count, 2)
    }
    function test_collection_spatial_and_boundary_keys() {
        collection.forceActiveFocus(Qt.OtherFocusReason)
        collection.currentIndex = 4
        keyClick(Qt.Key_Left); compare(collection.currentIndex, 3)
        keyClick(Qt.Key_Right); compare(collection.currentIndex, 4)
        keyClick(Qt.Key_Up); compare(collection.currentIndex, 1)
        keyClick(Qt.Key_Down); compare(collection.currentIndex, 4)
        keyClick(Qt.Key_Home); compare(collection.currentIndex, 0)
        keyClick(Qt.Key_End); compare(collection.currentIndex, 11)
        keyClick(Qt.Key_PageUp); compare(collection.currentIndex, 8)
        keyClick(Qt.Key_PageDown); compare(collection.currentIndex, 11)
    }

    function test_collection_ragged_last_row_clamps_to_nearest_item() {
        collection.model = 7
        collectionNav.columns = 4
        wait(10)
        collection.forceActiveFocus(Qt.OtherFocusReason)
        collection.currentIndex = 3
        keyClick(Qt.Key_Down)
        compare(collection.currentIndex, 6)
        keyClick(Qt.Key_Up)
        compare(collection.currentIndex, 3)
    }

    function test_collection_activation_and_context() {
        collection.forceActiveFocus(Qt.OtherFocusReason)
        collection.currentIndex = 5
        keyClick(Qt.Key_Return)
        keyClick(Qt.Key_Enter)
        keyClick(Qt.Key_Space)
        compare(activatedSpy.count, 2)
        compare(activatedSpy.signalArguments[0][0], 5)
        keyClick(Qt.Key_Menu)
        keyClick(Qt.Key_F10, Qt.ShiftModifier)
        compare(collectionContextSpy.count, 2)
        compare(collectionContextSpy.signalArguments[0][0], 5)
    }
    function test_scroll_navigation_keys() {
        scroller.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Down)
        compare(scroller.contentY, 25)
        keyClick(Qt.Key_PageDown)
        compare(scroller.contentY, 105)
        keyClick(Qt.Key_End)
        compare(scroller.contentY, 400)
        keyClick(Qt.Key_Up)
        compare(scroller.contentY, 375)
        keyClick(Qt.Key_PageUp)
        compare(scroller.contentY, 295)
        keyClick(Qt.Key_Home)
        compare(scroller.contentY, 0)
    }

    function test_scroll_navigation_respects_nonzero_margins() {
        scroller.topMargin = 20
        scroller.bottomMargin = 12
        scroller.contentY = 0
        scroller.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Home)
        compare(scroller.contentY, -20)
        keyClick(Qt.Key_End)
        compare(scroller.contentY, 412)
        keyClick(Qt.Key_PageUp)
        verify(scroller.contentY < 412)
        keyClick(Qt.Key_PageDown)
        verify(scroller.contentY <= 412)
    }

    function test_collection_boundary_key_reaches_parent_keys_handler() {
        boundaryParent.boundaryCount = 0
        boundaryChild.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Down)
        compare(boundaryParent.boundaryCount, 1)
    }

    function test_real_featured_swipe_boundary_reaches_parent_keys_handler() {
        var fixture = Qt.createQmlObject(
            'import QtQuick 2.15; import "../../qml" as C; Item { width: 640; height: 360;'
            + 'property int parentKeyCount: 0; property alias carousel: carousel;'
            + 'Keys.onPressed: function(event) { if (event.key === Qt.Key_Down) { parentKeyCount += 1; event.accepted = true } }'
            + 'C.FeaturedCarousel { id: carousel; automationId: "fixtureSwipe"; width: 600; height: 300; slides: [{title:"A"},{title:"B"}] } }', testWindow.contentItem)
        wait(20)
        function findByName(node, name) {
            if (!node)
                return null
            if (node.objectName === name)
                return node
            var children = node.children || []
            for (var i = 0; i < children.length; ++i) {
                var found = findByName(children[i], name)
                if (found)
                    return found
            }
            return null
        }
        var owner = findByName(fixture, "fixtureSwipe")
        verify(owner)
        owner.currentIndex = 1
        owner.forceActiveFocus(Qt.OtherFocusReason)
        verify(owner.activeFocus)
        keyClick(Qt.Key_Down)
        compare(fixture.parentKeyCount, 1)
        compare(owner.currentIndex, 1)
        fixture.destroy()
    }
}
