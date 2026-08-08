pragma ComponentBehavior: Bound
import QtQuick
import "../"   // Theme (the shared token kit lives in qml/)

Item {
    id: card
    property var highlight: ({})
    property int cardIndex: 0
    implicitHeight: bodyCol.implicitHeight + 40

    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
        radius: 18
        color: Qt.rgba(0.04, 0.045, 0.065, 0.62)
        border.width: 1
        border.color: theme.edge
    }

    Column {
        id: bodyCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 22
        spacing: 9

        Text {
            text: String(card.highlight.section || "UPDATE")
            color: theme.gold
            font.family: theme.ui
            font.pixelSize: 10
            font.letterSpacing: 2.2
            font.weight: Font.DemiBold
        }
        Text {
            text: String(card.highlight.title || "")
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 27
            wrapMode: Text.WordWrap
            width: parent.width
        }
        Text {
            text: String(card.highlight.body || "")
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 13
            lineHeight: 1.22
            wrapMode: Text.WordWrap
            width: parent.width
        }

        Row {
            visible: String(card.highlight.value || "").length > 0
            spacing: 8
            Text {
                text: String(card.highlight.value || "")
                color: theme.gold
                font.family: theme.display
                font.pixelSize: 21
                font.weight: Font.DemiBold
            }
            Text {
                text: String(card.highlight.context || "")
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Row {
            visible: String(card.highlight.beforeCaption || "").length > 0
                     || String(card.highlight.afterCaption || "").length > 0
            spacing: 10
            Text {
                text: String(card.highlight.beforeCaption || "")
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
                elide: Text.ElideRight
                width: (parent.width - 10) / 2
            }
            Text {
                text: String(card.highlight.afterCaption || "")
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 11
                elide: Text.ElideRight
                width: (parent.width - 10) / 2
            }
        }

        // Artwork URLs are already verified local file URLs from UpdateService. If an
        // optional image is absent, the editorial card remains complete and readable.
        Image {
            visible: card.highlight.artwork && card.highlight.artwork.length > 0
            source: visible ? card.highlight.artwork[0] : ""
            width: parent.width
            height: visible ? 96 : 0
            fillMode: Image.PreserveAspectCrop
            clip: true
            asynchronous: true
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 180 } }
        }
    }
}
