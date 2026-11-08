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
            anchors.fill: parent
            pointerEnabled: false
        }
    }

    TestCase {
        name: "IconToolTip"
        when: windowShown

        function init() {
            mouseMove(host, 15, 15)
        }

        function test_label_appears_after_hover_and_dismisses_on_exit() {
            verify(iconAction.hasOwnProperty("tooltipText"), "Icon actions need an opt-in tooltip label")
            iconAction.tooltipText = "Your Collections"
            compare(iconAction.ToolTip.delay, 350)

            mouseMove(iconAction, 20, 20)
            tryCompare(iconAction, "hovered", true)
            verify(!iconAction.ToolTip.visible)
            tryVerify(function() { return iconAction.ToolTip.visible })
            compare(iconAction.ToolTip.toolTip.text, "Your Collections")

            mouseMove(host, 15, 15)
            tryVerify(function() { return !iconAction.ToolTip.visible })
            iconAction.tooltipText = ""
        }

        function test_opt_out_does_not_show_a_label() {
            mouseMove(iconAction, 20, 20)
            tryCompare(iconAction, "hovered", true)
            wait(400)
            verify(!iconAction.ToolTip.visible)
        }

        function test_uses_existing_hover_for_custom_pointer_input() {
            verify(legacyAction.hasOwnProperty("tooltipHovered"), "Custom pointer actions need a hover source")
            legacyAction.tooltipText = "Play"
            legacyAction.tooltipHovered = Qt.binding(function() { return legacyInput.containsMouse })

            mouseMove(legacyInput, 20, 20)
            tryCompare(legacyInput, "containsMouse", true)
            tryVerify(function() { return legacyAction.ToolTip.visible })
            compare(legacyAction.ToolTip.toolTip.text, "Play")

            mouseMove(host, 15, 15)
            tryVerify(function() { return !legacyAction.ToolTip.visible })
        }
    }
}
