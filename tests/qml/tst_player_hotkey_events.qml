import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml/PlayerHotkeys.js" as PlayerHotkeys

TestCase {
    id: testCase
    name: "PlayerHotkeyEvents"
    when: windowShown

    Window {
        id: testWindow
        width: 320
        height: 240
        visible: true
        Item {
            id: sink
            anchors.fill: parent
            focus: true
            property string lastAction: ""
            property int lastKey: 0
            Keys.onPressed: function(event) {
                sink.lastKey = event.key
                var action = PlayerHotkeys.actionForEvent(event)
                sink.lastAction = action ? action.id : ""
                event.accepted = !!action
            }
        }
    }
    function init() {
        testWindow.requestActivate()
        sink.forceActiveFocus(Qt.OtherFocusReason)
        sink.lastAction = ""
        sink.lastKey = 0
        wait(10)
    }

    function expectKey(key, modifiers, actionId) {
        sink.lastAction = ""
        keyClick(key, modifiers)
        compare(sink.lastAction, actionId)
    }

    function test_playback_and_seeking_bindings() {
        expectKey(Qt.Key_Space, Qt.NoModifier, "space")
        expectKey(Qt.Key_Escape, Qt.NoModifier, "escape")
        expectKey(Qt.Key_BracketLeft, Qt.NoModifier, "speedDown")
        expectKey(Qt.Key_BracketRight, Qt.NoModifier, "speedUp")
        expectKey(Qt.Key_Left, Qt.NoModifier, "seekBack")
        expectKey(Qt.Key_Right, Qt.NoModifier, "seekForward")
        expectKey(Qt.Key_Comma, Qt.NoModifier, "frameBack")
        expectKey(Qt.Key_Period, Qt.NoModifier, "frameForward")
        expectKey(Qt.Key_Home, Qt.NoModifier, "seekStart")
        expectKey(Qt.Key_End, Qt.NoModifier, "seekEnd")
    }

    function test_every_digit_routes_to_percent_seek() {
        var digits = [Qt.Key_0, Qt.Key_1, Qt.Key_2, Qt.Key_3, Qt.Key_4,
                      Qt.Key_5, Qt.Key_6, Qt.Key_7, Qt.Key_8, Qt.Key_9]
        for (var i = 0; i < digits.length; ++i)
            expectKey(digits[i], Qt.NoModifier, "seekPercent")
    }

    function test_sound_and_subtitle_bindings() {
        expectKey(Qt.Key_M, Qt.NoModifier, "mute")
        expectKey(Qt.Key_Up, Qt.NoModifier, "volumeUp")
        expectKey(Qt.Key_Up, Qt.ShiftModifier, "volumeUp")
        expectKey(Qt.Key_Down, Qt.NoModifier, "volumeDown")
        expectKey(Qt.Key_Down, Qt.ShiftModifier, "volumeDown")
        expectKey(Qt.Key_Z, Qt.NoModifier, "subtitleDelayDown")
        expectKey(Qt.Key_Z, Qt.ShiftModifier, "subtitleDelayDown")
        expectKey(Qt.Key_X, Qt.NoModifier, "subtitleDelayUp")
        expectKey(Qt.Key_X, Qt.ShiftModifier, "subtitleDelayUp")
        expectKey(Qt.Key_S, Qt.NoModifier, "cycleSubtitle")
        expectKey(Qt.Key_C, Qt.NoModifier, "cycleSubtitle")
    }

    function test_loop_tools_and_help_bindings() {
        expectKey(Qt.Key_I, Qt.NoModifier, "abLoopA")
        expectKey(Qt.Key_O, Qt.NoModifier, "abLoopB")
        expectKey(Qt.Key_L, Qt.NoModifier, "abLoopClear")
        expectKey(Qt.Key_D, Qt.NoModifier, "stats")
        expectKey(Qt.Key_E, Qt.NoModifier, "browser")
        expectKey(Qt.Key_Menu, Qt.NoModifier, "contextMenu")
        expectKey(Qt.Key_F10, Qt.ShiftModifier, "contextMenu")
        expectKey(Qt.Key_Slash, Qt.ShiftModifier, "shortcuts")
    }

    function test_unbound_keys_do_not_route() {
        sink.lastAction = "sentinel"
        keyClick(Qt.Key_F, Qt.NoModifier)
        compare(sink.lastAction, "")
        sink.lastAction = "sentinel"
        keyClick(Qt.Key_F10, Qt.NoModifier)
        compare(sink.lastAction, "")
    }
}
