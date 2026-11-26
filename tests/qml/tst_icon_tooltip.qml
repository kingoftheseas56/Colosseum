import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml"

Item {
    id: host
    width: 300
    height: 200

    KeyboardAction {
        id: iconAction
        accessibleName: "Your Collections"
        x: 80; y: 100
        width: 40; height: 40
    }

    MouseArea {
        id: legacyInput
        x: 170; y: 100
        width: 40; height: 40
        hoverEnabled: true

        KeyboardAction {
            id: legacyAction
            accessibleName: "Play"
            anchors.fill: parent
            pointerEnabled: false
        }
    }

    TestCase {
        name: "IconActionWithoutTooltips"
        when: windowShown

        function init() {
            mouseMove(host, 15, 15)
        }

        function test_hover_has_no_tooltip_and_preserves_accessible_name() {
            mouseMove(iconAction, 20, 20)
            tryCompare(iconAction, "hovered", true)
            wait(450)
            compare(iconAction.ToolTip.visible, false)
            compare(iconAction.Accessible.name, "Your Collections")
        }

        function test_legacy_input_hover_has_no_tooltip() {
            mouseMove(legacyInput, 20, 20)
            tryCompare(legacyInput, "containsMouse", true)
            wait(450)
            compare(legacyAction.ToolTip.visible, false)
            compare(legacyAction.Accessible.name, "Play")
        }
    }
}
