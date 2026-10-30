pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

Item {
    id: card
    required property var media
    property string noteText: ""
    property bool editing: false
    property int position: 0
    property int itemCount: 0
    signal titleRequested()
    signal noteEdited(string note)
    signal moveRequested(int direction)
    signal removeRequested()
    signal ensureVisibleRequested(var target)
    implicitHeight: body.implicitHeight
    Theme { id: theme }

    function focusReorder(direction) {
        if (direction < 0 && earlier.enabled) earlier.forceActiveFocus(Qt.TabFocusReason)
        else if (later.enabled) later.forceActiveFocus(Qt.TabFocusReason)
        else remove.forceActiveFocus(Qt.TabFocusReason)
    }

    Column {
        id: body
        width: card.width
        spacing: 12
        Button {
            id: openTitle
            objectName: "mediaCard_" + card.media.id
            width: body.width
            height: artwork.height + titleBlock.height + 13
            padding: 0
            enabled: !card.editing
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.name: qsTr("Open %1 in %2").arg(card.media.title).arg(card.media.world)
            onClicked: card.titleRequested()
            Keys.onReturnPressed: card.titleRequested()
            Keys.onEnterPressed: card.titleRequested()
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Select) {
                    if (openTitle.enabled) card.titleRequested()
                    event.accepted = true
                }
            }
            onActiveFocusChanged: if (activeFocus) card.ensureVisibleRequested(openTitle)
            background: Rectangle {
                color: "transparent"
                radius: 9
                border.width: openTitle.activeFocus ? 2 : 0
                border.color: theme.gold
            }
            contentItem: Column {
                spacing: 13
                Item {
                    id: artwork
                    width: openTitle.width
                    height: width * 1.43
                    CollectionCover {
                        anchors.fill: parent
                        source: card.media.cover
                        title: card.media.title
                        radius: 7
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: 7
                        color: openTitle.hovered ? "#12ffffff" : "transparent"
                        border.color: openTitle.hovered ? theme.edge : "transparent"
                    }
                    Rectangle {
                        visible: !card.media.available
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 32
                        color: "#e60c0f18"
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Reopen unavailable")
                            font.family: theme.ui
                            font.pixelSize: 11
                            color: theme.inkDim
                            textFormat: Text.PlainText
                        }
                    }
                }
                Column {
                    id: titleBlock
                    width: openTitle.width
                    spacing: 4
                    Text {
                        width: parent.width
                        text: card.media.title
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        textFormat: Text.PlainText
                    }
                    Text {
                        width: parent.width
                        text: card.media.creator ? qsTr("%1 · %2").arg(card.media.kindLabel).arg(card.media.creator) : card.media.kindLabel
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 11
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        textFormat: Text.PlainText
                    }
                }
            }
            HoverHandler { cursorShape: card.editing ? Qt.ArrowCursor : Qt.PointingHandCursor }
        }
        Text {
            visible: !card.editing && card.noteText.length > 0
            width: body.width
            text: card.noteText
            color: theme.inkDim
            font.family: theme.display
            font.pixelSize: 14
            font.italic: true
            lineHeight: 1.25
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            textFormat: Text.PlainText
        }
        TextArea {
            id: noteEditor
            objectName: "noteInput_" + card.media.id
            visible: card.editing
            width: body.width
            implicitHeight: Math.max(88, contentHeight + topPadding + bottomPadding)
            text: card.noteText
            placeholderText: qsTr("Add a personal note")
            wrapMode: TextEdit.Wrap
            color: theme.ink
            placeholderTextColor: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 13
            padding: 10
            selectByMouse: true
            Accessible.name: qsTr("Your note for %1").arg(card.media.title)
            onTextChanged: {
                if (card.editing && text !== card.noteText) {
                    if (text.length > 500) text = text.slice(0, 500)
                    card.noteEdited(text)
                }
            }
            onActiveFocusChanged: if (activeFocus) card.ensureVisibleRequested(noteEditor)
            onCursorRectangleChanged: if (activeFocus) card.ensureVisibleRequested(noteEditor)
            background: Rectangle {
                color: "#0effffff"
                radius: 7
                border.width: 1
                border.color: noteEditor.activeFocus ? theme.gold : theme.edge
            }
        }
        RowLayout {
            visible: card.editing
            width: body.width
            spacing: 4
            Text {
                Layout.fillWidth: true
                text: qsTr("%1 of %2").arg(card.position + 1).arg(card.itemCount)
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
                textFormat: Text.PlainText
            }
            CollectionActionButton {
                id: earlier
                objectName: "moveEarlier_" + card.media.id
                iconName: "up"
                compact: true
                emphasis: "ghost"
                enabled: card.position > 0
                Accessible.name: qsTr("Move %1 earlier").arg(card.media.title)
                onClicked: card.moveRequested(-1)
                onActiveFocusChanged: if (activeFocus) card.ensureVisibleRequested(earlier)
            }
            CollectionActionButton {
                id: later
                objectName: "moveLater_" + card.media.id
                iconName: "down"
                compact: true
                emphasis: "ghost"
                enabled: card.position < card.itemCount - 1
                Accessible.name: qsTr("Move %1 later").arg(card.media.title)
                onClicked: card.moveRequested(1)
                onActiveFocusChanged: if (activeFocus) card.ensureVisibleRequested(later)
            }
            CollectionActionButton {
                id: remove
                objectName: "removeTitle_" + card.media.id
                iconName: "close"
                compact: true
                emphasis: "ghost"
                Accessible.name: qsTr("Remove %1 from this collection").arg(card.media.title)
                onClicked: card.removeRequested()
                onActiveFocusChanged: if (activeFocus) card.ensureVisibleRequested(remove)
            }
        }
    }
}
