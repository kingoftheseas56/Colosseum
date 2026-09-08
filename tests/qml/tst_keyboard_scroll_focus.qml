import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum

// Packet 1 owns the behavioural oracle for bounded directional reveal. These
// are real keyClick() deliveries through the Flickable's parent controller;
// forceActiveFocus() is used only to establish the starting point.
TestCase {
    name: "KeyboardScrollFocus"
    when: windowShown

    Window {
        id: window
        visible: true
        width: 400
        height: 300

        Flickable {
            id: page
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: 2200

            Keys.onPressed: event => scroll.handle(event)

            Colosseum.KeyboardScrollController {
                id: scroll
                flick: page
                lineStep: 72
            }

            // The near and far targets are toggled independently by each
            // fixture so candidate selection cannot hide a long-text jump.
            Colosseum.KeyboardAction {
                id: source
                objectName: "source"
                x: 30
                y: 220
                width: 120
                height: 40
                pointerEnabled: false
            }
            Colosseum.KeyboardAction {
                id: nearTarget
                objectName: "nearTarget"
                x: 30
                y: 310
                width: 120
                height: 40
                pointerEnabled: false
            }
            Colosseum.KeyboardAction {
                id: farTarget
                objectName: "farTarget"
                x: 30
                y: 1800
                width: 120
                height: 40
                pointerEnabled: false
            }

            TextInput {
                id: editor
                objectName: "editor"
                x: 180
                y: 20
                width: 150
                height: 40
                text: "edit me"
            }
        }
    }

    function init() {
        window.requestActivate()
        page.contentY = 0
        nearTarget.visible = true
        farTarget.visible = false
        source.forceActiveFocus(Qt.OtherFocusReason)
        tryCompare(source, "activeFocus", true)
    }

    function test_near_target_reveals_at_edge() {
        source.forceActiveFocus() // setup only
        keyClick(Qt.Key_Down)
        tryCompare(nearTarget, "activeFocus", true)
        var p = nearTarget.mapToItem(page, 0, 0)
        verify(p.y >= 0 && p.y + nearTarget.height <= page.height)
        verify(page.contentY > 0 && page.contentY <= 72)
    }

    function test_long_text_is_not_skipped() {
        nearTarget.visible = false
        farTarget.visible = true
        source.forceActiveFocus() // setup only
        var before = page.contentY
        keyClick(Qt.Key_Down)
        verify(page.contentY > before && page.contentY - before <= 72)
        verify(!farTarget.activeFocus)
    }

    function test_long_text_advances_in_bounded_steps() {
        nearTarget.visible = false
        farTarget.visible = true
        source.forceActiveFocus() // setup only
        var previous = page.contentY
        for (var i = 0; i < 5; ++i) {
            keyClick(Qt.Key_Down)
            verify(page.contentY >= previous)
            verify(page.contentY - previous <= 72)
            verify(!farTarget.activeFocus)
            previous = page.contentY
        }
    }

    function test_editor_bubbled_vertical_arrow_does_not_scroll() {
        editor.forceActiveFocus()
        editor.cursorPosition = 3
        keyClick(Qt.Key_Down)
        compare(page.contentY, 0)
        verify(editor.activeFocus)
        keyClick(Qt.Key_Left)
        compare(editor.cursorPosition, 2)
    }

    function test_modifier_does_not_enter_directional_scroll() {
        source.forceActiveFocus()
        keyClick(Qt.Key_Down, Qt.ControlModifier)
        compare(page.contentY, 0)
        verify(source.activeFocus)
    }
}
