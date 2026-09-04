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
        collection.currentIndex = 0
        scroller.contentY = 0
        wait(10)
    }

    function test_atomic_action_activation_and_context() {
        action.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Return)
        keyClick(Qt.Key_Enter)
        keyClick(Qt.Key_Space)
        compare(actionSpy.count, 3)
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

    function test_collection_activation_and_context() {
        collection.forceActiveFocus(Qt.OtherFocusReason)
        collection.currentIndex = 5
        keyClick(Qt.Key_Return)
        keyClick(Qt.Key_Enter)
        keyClick(Qt.Key_Space)
        compare(activatedSpy.count, 3)
        compare(activatedSpy.signalArguments[0][0], 5)
        keyClick(Qt.Key_Menu)
        keyClick(Qt.Key_F10, Qt.ShiftModifier)
        compare(collectionContextSpy.count, 2)
        compare(collectionContextSpy.signalArguments[0][0], 5)
    }
    function test_scroll_navigation_keys() {
        scroller.forceActiveFocus(Qt.OtherFocusReason)
        keyClick(Qt.Key_Down)
        compare(scroller.contentY, 40)
        keyClick(Qt.Key_PageDown)
        compare(scroller.contentY, 120)
        keyClick(Qt.Key_End)
        compare(scroller.contentY, 400)
        keyClick(Qt.Key_Up)
        compare(scroller.contentY, 360)
        keyClick(Qt.Key_PageUp)
        compare(scroller.contentY, 280)
        keyClick(Qt.Key_Home)
        compare(scroller.contentY, 0)
    }
}
