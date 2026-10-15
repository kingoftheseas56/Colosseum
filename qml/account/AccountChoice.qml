// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import QtQuick.Controls
import ".."

Control {
    id: control

    property string title: ""
    property string detail: ""
    signal chosen()

    implicitHeight: 76
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Theme { id: theme }

    contentItem: Item {
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 5

            Text {
                width: parent.width
                text: control.title
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: control.detail
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }

    background: Rectangle {
        radius: 14
        color: control.hovered
            ? Qt.rgba(1, 1, 1, 0.09)
            : Qt.rgba(1, 1, 1, 0.045)
        border.width: 1
        border.color: control.activeFocus ? theme.gold : theme.edge
    }

    TapHandler {
        enabled: control.enabled
        onTapped: control.chosen()
    }

    Keys.onReturnPressed: chosen()
    Keys.onEnterPressed: chosen()
    Keys.onSpacePressed: chosen()
}
