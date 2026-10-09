pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Button {
    id: root
    property string fontFamily: "Georgia"
    property bool reducedMotion: false
    property bool mapArtwork: false
    implicitWidth: Math.max(44, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: 44
    leftPadding: 12
    rightPadding: 12
    topPadding: 8
    bottomPadding: 8
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.family: fontFamily
    font.pixelSize: 12
    Accessible.name: text

    Keys.onReturnPressed: function(event) {
        if (root.enabled) root.clicked()
        event.accepted = true
    }
    Keys.onEnterPressed: function(event) {
        if (root.enabled) root.clicked()
        event.accepted = true
    }

    background: Rectangle {
        radius: 2
        color: root.mapArtwork ? "transparent" : !root.enabled ? "#ddcca6" : root.down ? "#cfb37e"
               : root.activeFocus ? "#f5e4ba" : root.hovered ? "#eddbaf" : "#e2cc9b"
        border.color: root.activeFocus ? "#50381f" : "#a18454"
        border.width: root.activeFocus ? 2 : root.mapArtwork ? (root.hovered || root.down ? 1 : 0) : 1
        Behavior on color {
            enabled: !root.reducedMotion
            ColorAnimation { duration: 90 }
        }
        Rectangle {
            visible: !root.mapArtwork
            anchors.fill: parent
            anchors.margins: 4
            color: "transparent"
            border.color: "#26fff5d7"
            border.width: 1
        }
    }
    contentItem: Text {
        visible: !root.mapArtwork
        text: root.text
        font.family: root.fontFamily
        font.pixelSize: root.font.pixelSize
        font.bold: root.font.bold
        font.italic: root.font.italic
        font.letterSpacing: root.font.letterSpacing
        color: root.enabled ? "#5c4229" : "#9f8c66"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
