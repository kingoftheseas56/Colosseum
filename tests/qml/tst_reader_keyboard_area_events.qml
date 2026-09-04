import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/reader2" as Reader2
import "../../qml/comicreader" as ComicReader

TestCase {
    name: "ReaderKeyboardAreaEvents"
    when: windowShown
    Window { id: testWindow; width: 480; height: 320; visible: true }

    Component {
        id: readerComp
        Item {
            width: 240; height: 120
            property int activateCount: 0
            property int contextCount: 0
            property int decreaseCount: 0
            property int increaseCount: 0
            property int homeCount: 0
            property int endCount: 0
            property alias area: area
            Reader2.ReaderKeyboardArea {
                id: area; anchors.fill: parent; keyboardTabStop: true
                keyboardActivate: function() { parent.activateCount++ }
                keyboardContext: function() { parent.contextCount++ }
                keyboardDecrease: function() { parent.decreaseCount++ }
                keyboardIncrease: function() { parent.increaseCount++ }
                keyboardHome: function() { parent.homeCount++ }
                keyboardEnd: function() { parent.endCount++ }
            }
        }
    }
    Component {
        id: comicComp
        Item {
            width: 240; height: 120
            property int activateCount: 0
            property int contextCount: 0
            property int decreaseCount: 0
            property int increaseCount: 0
            property int homeCount: 0
            property int endCount: 0
            property alias area: area
            ComicReader.ComicReaderKeyboardArea {
                id: area; anchors.fill: parent; keyboardTabStop: true
                keyboardActivate: function() { parent.activateCount++ }
                keyboardContext: function() { parent.contextCount++ }
                keyboardDecrease: function() { parent.decreaseCount++ }
                keyboardIncrease: function() { parent.increaseCount++ }
                keyboardHome: function() { parent.homeCount++ }
                keyboardEnd: function() { parent.endCount++ }
            }
        }
    }

    property var subject: null

    function cleanup() {
        if (subject) subject.destroy()
        subject = null
    }

    function exercise(component) {
        subject = component.createObject(testWindow.contentItem)
        verify(subject !== null)
        testWindow.requestActivate()
        subject.area.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(subject.area, "activeFocus", true)
        keyClick(Qt.Key_Return)
        keyClick(Qt.Key_Enter)
        keyClick(Qt.Key_Space)
        compare(subject.activateCount, 3)

        keyClick(Qt.Key_Menu)
        keyClick(Qt.Key_F10, Qt.ShiftModifier)
        compare(subject.contextCount, 2)

        keyClick(Qt.Key_Left)
        keyClick(Qt.Key_Down)
        compare(subject.decreaseCount, 2)

        keyClick(Qt.Key_Right)
        keyClick(Qt.Key_Up)
        compare(subject.increaseCount, 2)

        keyClick(Qt.Key_Home)
        keyClick(Qt.Key_End)
        compare(subject.homeCount, 1)
        compare(subject.endCount, 1)
    }

    function test_reader2_area_complete_key_matrix() { exercise(readerComp) }
    function test_comicreader_area_complete_key_matrix() { exercise(comicComp) }
}
