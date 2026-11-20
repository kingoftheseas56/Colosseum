// AccountAuthoritativeSwitch.qml
// A non-optimistic settings switch: visual state belongs to the host/backend.

import QtQuick
import QtQuick.Controls
import ".."

Button {
    id: control

    property bool authoritativeChecked: true
    property bool busy: false
    property string accessibleName: "Setting"

    signal changeRequested(bool enabled)

    implicitWidth: 46
    implicitHeight: 26
    width: 46
    height: 26
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    enabled: !busy

    Theme { id: theme }

    Accessible.name: control.accessibleName
        + (control.authoritativeChecked ? qsTr(", on") : qsTr(", off"))

    onClicked: control.changeRequested(!control.authoritativeChecked)

    contentItem: Item {
        Rectangle {
            width: 18
            height: 18
            radius: 9
            y: 4
            x: control.authoritativeChecked ? 24 : 4
            color: control.authoritativeChecked
                ? theme.gold
                : theme.inkDimmer
            opacity: control.enabled ? 1.0 : 0.55

            Behavior on x {
                NumberAnimation { duration: 150 }
            }
        }
    }

    background: Rectangle {
        radius: 13
        color: control.authoritativeChecked
            ? Qt.rgba(theme.gold.r, theme.gold.g, theme.gold.b, 0.12)
            : (control.hovered ? theme.glassTint : "transparent")
        border.width: 1
        border.color: control.authoritativeChecked
            ? theme.gold
            : theme.edge
        opacity: control.enabled ? 1.0 : 0.60
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        visible: control.activeFocus
        radius: 16
        color: "transparent"
        border.width: 2
        border.color: theme.gold
    }
}
