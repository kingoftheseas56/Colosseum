// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import QtQuick.Controls
import ".."

Control {
    id: control

    property string title: ""
    property string detail: ""
    property url iconSource: ""
    property url secondIconSource: ""
    signal chosen()

    Accessible.role: Accessible.Button
    Accessible.name: control.title
    Accessible.description: control.detail
    Accessible.onPressAction: if (control.enabled) control.chosen()

    implicitHeight: 76
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Theme { id: theme }

    contentItem: Item {
        Row {
            id: icons
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            visible: choiceIcon.status === Image.Ready || secondIcon.status === Image.Ready

            Image {
                id: choiceIcon
                width: secondIcon.status === Image.Ready ? 22 : 34
                height: width
                fillMode: Image.PreserveAspectFit
                source: control.iconSource
                visible: status === Image.Ready
            }

            Image {
                id: secondIcon
                width: 22
                height: width
                fillMode: Image.PreserveAspectFit
                source: control.secondIconSource
                visible: status === Image.Ready
            }
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: icons.visible ? 18 + icons.implicitWidth + 12 : 18
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
