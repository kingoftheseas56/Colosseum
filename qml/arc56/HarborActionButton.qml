import QtQuick
import ".."

Rectangle {
    id: button
    property string label: ""
    property string iconKind: "play"
    property bool primary: false
    property bool compact: false
    property bool checked: false
    property bool flat: false
    property bool showLabel: !compact
    signal triggered()

    implicitHeight: 48
    implicitWidth: compact ? 48 : content.implicitWidth + (primary ? 54 : 44)
    radius: height / 2
    color: primary ? theme.ink
                   : flat ? (action.interactionActive ? Qt.rgba(1, 1, 1, 0.07) : "transparent")
                   : checked ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.14)
                             : Qt.rgba(17 / 255, 18 / 255, 19 / 255, 0.80)
    border.width: primary || flat ? 0 : 1
    border.color: checked ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.55)
                          : (action.interactionActive ? theme.inkDimmer : theme.edge)
    scale: action.pressed ? 0.98 : (action.hovered && primary ? 1.03 : 1.0)

    Behavior on scale { NumberAnimation { duration: 180 } }
    Behavior on color { ColorAnimation { duration: 150 } }
    Behavior on border.color { ColorAnimation { duration: 150 } }

    HarborTheme { id: theme }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 10

        HarborIcon {
            width: 19
            height: 19
            iconSize: 19
            kind: button.iconKind
            darkGlyph: button.primary
            filled: button.checked
            ink: button.primary ? "#17181c" : (button.checked ? theme.gold : theme.ink)
        }

        Text {
            visible: button.showLabel
            text: button.label
            color: button.primary ? "#17181c" : theme.ink
            font.family: theme.ui
            font.pixelSize: 15
            font.weight: button.primary ? Font.DemiBold : Font.Medium
        }
    }

    KeyboardAction {
        id: action
        anchors.fill: parent
        accessibleName: button.label
        focusRadius: button.radius
        onTriggered: button.triggered()
    }
}
