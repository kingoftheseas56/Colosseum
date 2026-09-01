import QtQuick

FocusScope {
    id: root
    objectName: "dcauTheatreCard"
    property var item: null
    property url cover: ""
    property string sourceText: "IMDb"
    signal activated(var item)

    readonly property string titleText: root.item ? (root.item.title || "") : ""

    width: 200
    height: 352
    activeFocusOnTab: true

    Theme { id: theme }

    Item {
        id: frame
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 300

        transform: Translate {
            y: pointer.containsMouse ? -8 : 0
            Behavior on y { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
        }
        RoundedPosterImage {
            anchors.fill: parent
            radius: 16
            hovered: pointer.containsMouse
            sources: root.cover.toString().length ? [root.cover] : []
        }

        Rectangle {
            anchors.fill: parent
            radius: 16
            opacity: pointer.containsMouse ? 1 : 0
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.54; color: "transparent" }
                GradientStop { position: 0.64; color: Qt.rgba(4 / 255, 5 / 255, 8 / 255, 0.10) }
                GradientStop { position: 1.0; color: Qt.rgba(4 / 255, 5 / 255, 8 / 255, 0.92) }
            }
            Behavior on opacity { NumberAnimation { duration: 280 } }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 11
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                text: root.sourceText
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 11
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: 16
            visible: root.activeFocus
            color: "transparent"
            border.width: 2
            border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.55)
            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                radius: 18
                color: "transparent"
                border.width: 3
                border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.18)
            }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: frame.bottom
        anchors.topMargin: 12
        height: 40
        text: root.titleText
        color: theme.ink
        font.family: theme.ui
        font.pixelSize: 14
        font.weight: Font.DemiBold
        lineHeight: 1.18
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.forceActiveFocus()
            root.activated(root.item)
        }
    }

    Keys.onReturnPressed: root.activated(root.item)
    Keys.onEnterPressed: root.activated(root.item)
}
