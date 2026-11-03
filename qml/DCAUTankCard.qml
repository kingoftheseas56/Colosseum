import QtQuick

FocusScope {
    id: root
    objectName: "dcauTankCard"
    property string caption: ""
    property url cover: ""
    signal activated()

    width: 180
    height: 270
    activeFocusOnTab: true

    Theme { id: theme }

    RoundedPosterImage {
        anchors.fill: parent
        radius: 14
        hovered: pointer.containsMouse
        sources: root.cover.toString().length ? [root.cover] : []
    }

    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 9
        text: root.caption
        color: theme.ink
        font.family: theme.ui
        font.pixelSize: 14
        font.weight: Font.DemiBold
        lineHeightMode: Text.FixedHeight
        lineHeight: 16
        wrapMode: Text.WordWrap
        style: Text.Outline
        styleColor: Qt.rgba(0, 0, 0, 0.85)
    }

    Rectangle {
        anchors.fill: parent
        radius: 14
        visible: root.activeFocus
        color: "transparent"
        border.width: 2
        border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.55)
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: 16
            color: "transparent"
            border.width: 3
            border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.18)
        }
    }
    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.forceActiveFocus()
            root.activated()
        }
    }

    Keys.onReturnPressed: root.activated()
    Keys.onEnterPressed: root.activated()
}
