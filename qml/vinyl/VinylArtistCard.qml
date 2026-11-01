import QtQuick
import ".."

KeyboardAction {
    id: card
    required property var record
    required property var artistData
    property string shape: "square"
    property string badge: ""
    property string artObjectName: ""
    readonly property real artHeight: width * (shape === "wide" ? 9 / 16 : 1)
    implicitHeight: artHeight + 72
    accessibleName: String(record.title || "") + (record.subtitle ? ", " + record.subtitle : "")
    signal cardRequested(var record)
    onTriggered: cardRequested(card.record)
    Theme { id: theme }

    RoundedPosterImage {
        objectName: card.artObjectName
        width: parent.width
        height: card.artHeight
        radius: card.shape === "round" ? width / 2 : 12
        sources: card.artistData.artwork(card.record.cover) ? [card.artistData.artwork(card.record.cover)] : []
        hovered: card.interactionActive
    }
    Rectangle {
        x: 10
        y: card.artHeight - height - 10
        width: badgeText.implicitWidth + 16
        height: 24
        radius: 6
        color: "#d906070b"
        visible: card.badge.length > 0
        Text {
            id: badgeText
            anchors.centerIn: parent
            text: card.badge
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: 10
        }
    }
    Column {
        width: parent.width
        y: card.artHeight + 12
        spacing: 5
        Text {
            width: parent.width
            text: String(card.record.title || "")
            textFormat: Text.PlainText
            color: card.interactionActive ? theme.gold : theme.ink
            font.family: theme.ui
            font.pixelSize: 15
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            horizontalAlignment: card.shape === "round" ? Text.AlignHCenter : Text.AlignLeft
        }
        Text {
            width: parent.width
            text: String(card.record.subtitle || "")
            textFormat: Text.PlainText
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 12
            elide: Text.ElideRight
            horizontalAlignment: card.shape === "round" ? Text.AlignHCenter : Text.AlignLeft
        }
    }
}
