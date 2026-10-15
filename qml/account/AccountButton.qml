// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import QtQuick.Controls
import ".."

Button {
    id: control

    property string variant: "secondary"
    property color primaryInk: "#141207"

    implicitHeight: 46
    implicitWidth: Math.max(160, contentItem.implicitWidth + 34)
    padding: 0
    hoverEnabled: true

    Theme { id: theme }

    contentItem: Text {
        text: control.text
        color: control.variant === "primary"
            ? control.primaryInk
            : (control.enabled ? theme.ink : theme.inkDimmer)
        opacity: control.enabled ? 1.0 : 0.58
        font.family: theme.ui
        font.pixelSize: 13
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: control.variant === "link" ? 6 : 12
        color: {
            if (control.variant === "primary")
                return control.down ? Qt.darker(theme.gold, 1.12) : theme.gold
            if (control.variant === "link")
                return control.hovered ? Qt.rgba(1, 1, 1, 0.07) : "transparent"
            return control.down
                ? Qt.rgba(1, 1, 1, 0.16)
                : (control.hovered ? theme.glassHi : theme.glassTint)
        }
        border.width: control.variant === "link" ? 0 : 1
        border.color: control.variant === "primary"
            ? Qt.rgba(0.94, 0.77, 0.29, 0.50)
            : theme.edge
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        visible: control.activeFocus
        radius: 14
        color: "transparent"
        border.width: 2
        border.color: theme.gold
    }
}
