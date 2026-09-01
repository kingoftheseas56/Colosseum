import QtQuick

FocusScope {
    id: root
    property string glyph: "›"
    signal triggered()

    width: 44
    height: 44
    activeFocusOnTab: true

    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
        radius: 22
        color: Qt.rgba(8 / 255, 10 / 255, 14 / 255, 0.76)
        border.width: 1
        border.color: hover.hovered || root.activeFocus
                      ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.62)
                      : Qt.rgba(1, 1, 1, 0.14)

        Text {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -1
            text: root.glyph
            color: hover.hovered || root.activeFocus ? theme.gold : "#d9d9de"
            font.family: theme.ui
            font.pixelSize: 25
        }
    }

    HoverHandler { id: hover }
    TapHandler {
        onTapped: {
            root.forceActiveFocus()
            root.triggered()
        }
    }

    Keys.onReturnPressed: root.triggered()
    Keys.onEnterPressed: root.triggered()
    Keys.onSpacePressed: root.triggered()
}
