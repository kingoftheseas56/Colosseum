import QtQuick
import ".."

Column {
    id: root
    required property string title
    required property var records
    required property var artistData
    property string subtitle: ""
    property string prefix: "vinylArtistItem"
    property string shape: "square"
    property string badge: ""
    property Component toolbar: null
    property bool expanded: false
    readonly property int columns: Math.max(1, Math.min(shape === "wide" ? 3 : 6,
        Math.floor((width + 22) / (shape === "wide" ? 300 : 190))))
    readonly property var shownRecords: expanded ? records : records.slice(0, columns)
    signal activated(var record)
    signal itemFocused(var item)
    spacing: 20
    visible: records.length > 0
    onRecordsChanged: expanded = false
    Theme { id: theme }

    Row {
        width: parent.width
        Column {
            width: parent.width - more.width
            spacing: 5
            Text {
                width: parent.width; text: root.title; textFormat: Text.PlainText
                color: theme.ink; font.family: theme.display; font.pixelSize: 28
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width; visible: text.length > 0; text: root.subtitle
                textFormat: Text.PlainText; color: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 11; wrapMode: Text.WordWrap
            }
        }
        KeyboardAction {
            id: more
            objectName: root.prefix + "More"
            visible: root.records.length > root.columns
            width: visible ? moreLabel.implicitWidth + 24 : 0; height: 34
            accessibleName: root.expanded ? "Show fewer " + root.title : "Show all " + root.records.length + " " + root.title
            onTriggered: root.expanded = !root.expanded
            onActiveFocusChanged: if (activeFocus) root.itemFocused(more)
            Rectangle { anchors.fill: parent; radius: 17; color: "transparent"; border.color: parent.interactionActive ? theme.gold : theme.edge }
            Text {
                id: moreLabel; anchors.centerIn: parent
                text: root.expanded ? "Show fewer" : "Show all (" + root.records.length + ")"
                color: parent.interactionActive ? theme.gold : theme.inkDim
                font.family: theme.ui; font.pixelSize: 12
            }
        }
    }
    Loader {
        width: parent.width
        sourceComponent: root.toolbar
        active: sourceComponent !== null
        visible: active
        height: item ? item.implicitHeight : 0
    }
    Grid {
        width: parent.width; columns: root.columns; columnSpacing: 22; rowSpacing: 24
        Repeater {
            model: root.shownRecords
            delegate: KeyboardAction {
                id: card
                required property var modelData
                required property int index
                readonly property real artHeight: width * (root.shape === "wide" ? 9 / 16 : 1)
                objectName: root.prefix + "_" + index
                width: (root.width - (root.columns - 1) * 22) / root.columns
                height: artHeight + 76
                accessibleName: modelData.title + (modelData.subtitle ? ", " + modelData.subtitle : "")
                onTriggered: root.activated(modelData)
                onActiveFocusChanged: if (activeFocus) root.itemFocused(card)
                RoundedPosterImage {
                    objectName: root.prefix + "Art_" + index
                    width: parent.width; height: card.artHeight
                    radius: root.shape === "round" ? width / 2 : 10
                    sources: [root.artistData.artwork(card.modelData.cover)]
                    hovered: card.interactionActive
                }
                Rectangle {
                    x: 10; y: card.artHeight - height - 10
                    width: badgeLabel.implicitWidth + 16; height: 24; radius: 6
                    color: "#d906070b"; visible: root.badge.length > 0
                    Text { id: badgeLabel; anchors.centerIn: parent; text: root.badge; color: theme.ink; font.family: theme.ui; font.pixelSize: 10 }
                }
                Text {
                    y: card.artHeight + 12; width: parent.width; height: 38
                    text: card.modelData.title; textFormat: Text.PlainText
                    color: card.interactionActive ? theme.gold : theme.ink
                    font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                    horizontalAlignment: root.shape === "round" ? Text.AlignHCenter : Text.AlignLeft
                }
                Text {
                    y: card.artHeight + 55; width: parent.width
                    text: card.modelData.subtitle || ""; textFormat: Text.PlainText
                    color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                    elide: Text.ElideRight
                    horizontalAlignment: root.shape === "round" ? Text.AlignHCenter : Text.AlignLeft
                }
            }
        }
    }
}
