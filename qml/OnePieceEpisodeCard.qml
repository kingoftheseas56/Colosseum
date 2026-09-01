pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    required property var entry
    property string sourceLabel: ""
    signal activated()

    width: 278
    height: 208

    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
        radius: 14
        color: "#101419"
        border.width: root.activeFocus || hover.hovered ? 1 : 0
        border.color: theme.gold
        clip: true

        Image {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 150
            source: root.entry.thumbnail || ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 76
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0.03, 0.04, 0.05, 0.35) }
                GradientStop { position: 1.0; color: Qt.rgba(0.03, 0.04, 0.05, 0.98) }
            }
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 12
            spacing: 3
            Text {
                text: root.sourceLabel + "  ·  " + (root.entry.episode || "")
                color: theme.gold
                font.family: theme.ui
                font.pixelSize: 9
                font.bold: true
                font.letterSpacing: 1.2
            }
            Text {
                text: root.entry.title || ("Episode " + root.entry.episode)
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 12
                font.bold: true
                maximumLineCount: 2
                elide: Text.ElideRight
                wrapMode: Text.WordWrap
                width: parent.width
            }
        }
    }

    HoverHandler { id: hover }
    TapHandler { onTapped: root.activated() }
    activeFocusOnTab: true
    Keys.onReturnPressed: root.activated()
    Keys.onEnterPressed: root.activated()
}
