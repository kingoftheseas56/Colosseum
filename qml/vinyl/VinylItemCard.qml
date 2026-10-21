import QtQuick
import ".."
KeyboardAction {
    focusOnPointer: false
    id: root
    required property var worldData
    required property var item
    enabled: root.item.unavailable !== true
    readonly property string reason: item.reason || ""
    implicitHeight: art.height + 14 + copy.height + 8
    accessibleName: item.title + (item.unavailable ? ", unavailable" : "") + (reason ? ". " + reason : "")
    onTriggered: if(worldData) worldData.activate(item)
    Theme { id: theme }
    RoundedPosterImage {
        id: art
        objectName: root.objectName + "_cover"
        width: root.width
        height: width / (root.item.aspect || 1)
        radius: root.item.round ? width / 2 : 13
        sources: root.worldData && root.item && root.worldData.artwork(root.item.cover) ? [root.worldData.artwork(root.item.cover)] : []
        hovered: root.interactionActive
    }
    Column {
        id: copy
        y: art.height + 14
        width: parent.width
        spacing: 6
        Text {
            width: parent.width
            text: root.item.title
            textFormat: Text.PlainText
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: 15
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: root.item.unavailable ? "Unavailable" : root.reason || root.item.subtitle || ""
            textFormat: Text.PlainText
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
            lineHeight: 1.25
        }
    }
}
