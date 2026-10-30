pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

Item {
    id: page
    objectName: "collectionPage"
    required property var store
    property string collectionId: ""
    property string draftToken: ""
    property bool reloading: false
    property bool editing: false
    property bool leaving: false
    property string kind: "all"
    property string errorText: ""
    property var draftItems: []
    property var draftNotes: ({})
    property var removedItem: null
    readonly property bool narrow: width < 700
    readonly property int sideMargin: narrow ? 22 : 54
    readonly property var collection: { page.store.revision; return page.store.getCollection(page.collectionId) }
    readonly property var visibleItems: {
        const ids = page.editing ? page.draftItems : page.collection ? page.collection.items : []
        if (page.editing || page.kind === "all") return ids
        return ids.filter(function(id) { return page.store.media(id).kind === page.kind })
    }
    signal backRequested()
    signal titleRequested(string titleId)
    signal messageRequested(string message)
    signal navigationCanceled()
    signal deleteRequested(var opener)
    Theme { id: theme }

    function abortEdit() {
        if (page.draftToken && page.store) page.store.cancelEdit(page.draftToken)
        page.draftToken = ""
        page.editing = false
        page.leaving = false
        page.reloading = false
        page.removedItem = null
        page.draftItems = []
        page.draftNotes = ({})
    }
    function beginEdit() {
        if (!page.collection || !page.store.healthy) return
        const draft = page.store.beginEdit(page.collectionId)
        if (!draft.token) {
            page.errorText = page.store.error || qsTr("Could not start editing this collection.")
            return
        }
        page.draftToken = draft.token
        nameEditor.text = draft.name
        descriptionEditor.text = draft.description || ""
        page.draftItems = draft.items.slice()
        page.draftNotes = Object.assign({}, draft.notes || {})
        page.errorText = ""
        page.removedItem = null
        page.kind = "all"
        page.leaving = false
        page.reloading = false
        page.editing = true
        flick.contentY = 0
        nameEditor.forceActiveFocus(Qt.TabFocusReason)
    }
    function cancelEdit() {
        page.abortEdit()
        page.errorText = ""
        page.navigationCanceled()
        editButton.forceActiveFocus(Qt.TabFocusReason)
    }
    function saveEdit() {
        if (nameEditor.inputMethodComposing || descriptionEditor.inputMethodComposing) return false
        const result = page.store.saveEdit(page.draftToken, nameEditor.text,
            descriptionEditor.text, page.draftItems, page.draftNotes)
        if (!result.ok) {
            page.errorText = result.error || qsTr("Could not save this collection.")
            flick.contentY = 0
            nameEditor.forceActiveFocus(Qt.TabFocusReason)
            return false
        }
        page.draftToken = ""
        page.editing = false
        page.leaving = false
        page.errorText = ""
        page.removedItem = null
        page.navigationCanceled()
        page.messageRequested(qsTr("Collection updated"))
        editButton.forceActiveFocus(Qt.TabFocusReason)
        return true
    }
    function discardEdits() {
        const reload = page.reloading
        page.abortEdit()
        if (reload) {
            page.navigationCanceled()
            if (page.store.reload()) page.beginEdit()
            else page.errorText = page.store.error
        } else page.backRequested()
    }
    function requestBack() {
        if (!page.editing) { page.backRequested(); return }
        page.reloading = false
        page.leaving = true
        flick.contentY = 0
        Qt.callLater(function() { stayButton.forceActiveFocus(Qt.TabFocusReason) })
    }
    function setNote(id, note) {
        if (!page.editing) return
        const notes = Object.assign({}, page.draftNotes)
        notes[id] = note.slice(0, 500)
        page.draftNotes = notes
    }
    function moveItem(id, direction) {
        if (!page.editing) return
        const items = page.draftItems.slice()
        const from = items.indexOf(id)
        const to = from + direction
        if (from < 0 || to < 0 || to >= items.length) return
        items.splice(from, 1)
        items.splice(to, 0, id)
        page.draftItems = items
        Qt.callLater(function() {
            const card = cards.itemAt(to)
            if (card) card.focusReorder(direction)
        })
    }
    function removeItem(id) {
        if (!page.editing) return
        const items = page.draftItems.slice()
        const index = items.indexOf(id)
        if (index < 0) return
        page.removedItem = { id: id, index: index }
        items.splice(index, 1)
        page.draftItems = items
        Qt.callLater(function() { undoButton.forceActiveFocus(Qt.TabFocusReason) })
    }
    function undoRemove() {
        if (!page.removedItem) return
        const items = page.draftItems.slice()
        items.splice(Math.min(page.removedItem.index, items.length), 0, page.removedItem.id)
        page.draftItems = items
        page.removedItem = null
    }
    function revealItem(item) {
        const caret = item.cursorRectangle
        const useCaret = item.activeFocus && caret !== undefined
        const p = item.mapToItem(flick.contentItem, 0, useCaret ? caret.y : 0)
        const targetHeight = useCaret ? Math.max(1, caret.height) : item.height
        if (p.y < flick.contentY + 12) flick.contentY = Math.max(0, p.y - 12)
        else if (p.y + targetHeight > flick.contentY + flick.height - 12)
            flick.contentY = Math.min(Math.max(0, flick.contentHeight - flick.height),
                Math.max(0, p.y + targetHeight - flick.height + 12))
    }
    Component.onDestruction: page.abortEdit()
    onCollectionIdChanged: {
        page.abortEdit()
        page.kind = "all"
        if (flick) flick.contentY = 0
    }
    Keys.priority: Keys.AfterItem
    Keys.onEscapePressed: {
        if (page.leaving) { page.leaving = false; page.navigationCanceled() }
        else page.requestBack()
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_PageDown) {
            flick.contentY = Math.min(Math.max(0, flick.contentHeight - flick.height), flick.contentY + flick.height * 0.8)
            event.accepted = true
        } else if (event.key === Qt.Key_PageUp) {
            flick.contentY = Math.max(0, flick.contentY - flick.height * 0.8)
            event.accepted = true
        }
    }

    Flickable {
        id: flick
        objectName: "collectionScroller"
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ColumnLayout {
            id: content
            x: page.sideMargin
            y: 10
            width: flick.width - page.sideMargin * 2
            spacing: 24
            CollectionActionButton {
                objectName: "backToCollections"
                text: qsTr("Your Collections")
                iconName: "back"
                emphasis: "ghost"
                onClicked: page.requestBack()
            }
            GridLayout {
                Layout.fillWidth: true
                columns: page.narrow ? 1 : 2
                columnSpacing: 30
                rowSpacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 9
                    RowLayout {
                        visible: !page.editing
                        spacing: 6
                        CollectionIcon { name: "lock"; strokeColor: theme.inkDimmer; Layout.preferredWidth: 13; Layout.preferredHeight: 13 }
                        Text { text: qsTr("Private collection"); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; textFormat: Text.PlainText }
                    }
                    Text {
                        visible: !page.editing
                        Layout.fillWidth: true
                        text: page.collection ? page.collection.name : qsTr("Collection unavailable")
                        font.family: theme.display
                        font.pixelSize: page.narrow ? 32 : 42
                        color: theme.ink
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        textFormat: Text.PlainText
                    }
                    Text {
                        visible: !page.editing
                        Layout.fillWidth: true
                        text: page.collection ? page.collection.description : qsTr("Return to Your Collections to choose another one.")
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 14
                        lineHeight: 1.25
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        textFormat: Text.PlainText
                    }
                    TextField {
                        id: nameEditor
                        objectName: "collectionNameInput"
                        visible: page.editing
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        maximumLength: 100
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: 28
                        padding: 10
                        selectByMouse: true
                        Accessible.name: qsTr("Collection name")
                        background: Rectangle { color: "#0effffff"; radius: 7; border.width: 1; border.color: nameEditor.activeFocus ? theme.gold : theme.edge }
                    }
                    TextArea {
                        id: descriptionEditor
                        objectName: "collectionDescriptionInput"
                        visible: page.editing
                        Layout.fillWidth: true
                        implicitHeight: Math.max(76, contentHeight + topPadding + bottomPadding)
                        color: theme.inkDim
                        placeholderText: qsTr("Add a description")
                        placeholderTextColor: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 13
                        padding: 10
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        Accessible.name: qsTr("Collection description")
                        onTextChanged: if (text.length > 300) text = text.slice(0, 300)
                        onActiveFocusChanged: if (activeFocus) page.revealItem(descriptionEditor)
                        onCursorRectangleChanged: if (activeFocus) page.revealItem(descriptionEditor)
                        background: Rectangle { color: "#0effffff"; radius: 7; border.width: 1; border.color: descriptionEditor.activeFocus ? theme.gold : theme.edge }
                    }
                    Text {
                        visible: page.editing || page.errorText.length > 0
                        Layout.fillWidth: true
                        Layout.minimumHeight: 18
                        text: page.errorText
                        color: "#efb6a9"
                        font.family: theme.ui
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        Accessible.role: Accessible.AlertMessage
                    }
                    CollectionActionButton {
                        visible: page.editing && page.errorText.length > 0
                        text: qsTr("Reload collection")
                        iconName: "reset"
                        emphasis: "ghost"
                        onClicked: {
                            page.reloading = true
                            page.leaving = true
                            Qt.callLater(function() { stayButton.forceActiveFocus(Qt.TabFocusReason) })
                        }
                    }
                }
                RowLayout {
                    Layout.alignment: page.narrow ? Qt.AlignLeft : Qt.AlignRight | Qt.AlignTop
                    spacing: 8
                    CollectionActionButton {
                        id: editButton
                        objectName: "editCollectionButton"
                        visible: !page.editing && !!page.collection
                        text: qsTr("Edit collection")
                        iconName: "edit"
                        enabled: page.store.healthy
                        onClicked: page.beginEdit()
                    }
                    CollectionActionButton {
                        id: deleteButton
                        objectName: "deleteCollectionButton"
                        visible: !page.editing && !!page.collection
                        enabled: page.store.healthy
                        compact: true
                        iconName: "trash"
                        emphasis: "ghost"
                        Accessible.name: qsTr("Delete collection")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Delete collection")
                        onClicked: page.deleteRequested(deleteButton)
                    }
                    CollectionActionButton {
                        objectName: "cancelCollectionEdit"
                        visible: page.editing
                        text: qsTr("Cancel")
                        emphasis: "ghost"
                        onClicked: page.cancelEdit()
                    }
                    CollectionActionButton {
                        objectName: "saveCollectionButton"
                        visible: page.editing
                        text: qsTr("Save changes")
                        emphasis: "primary"
                        enabled: page.store.healthy
                        onClicked: page.saveEdit()
                    }
                }
            }
            Rectangle {
                visible: page.leaving
                Layout.fillWidth: true
                implicitHeight: leaveContent.implicitHeight + 24
                color: "#1af0c44a"
                border.color: "#55f0c44a"
                radius: 9
                ColumnLayout {
                    id: leaveContent
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10
                    Text { Layout.fillWidth: true; text: page.reloading ? qsTr("Reload this collection and discard your unsaved edits?") : qsTr("Discard your unsaved edits?"); color: theme.ink; font.family: theme.ui; font.pixelSize: 14; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                    RowLayout {
                        spacing: 8
                        CollectionActionButton {
                            id: stayButton
                            objectName: "stayEditingButton"
                            text: qsTr("Keep editing")
                            onClicked: { page.leaving = false; page.navigationCanceled() }
                        }
                        CollectionActionButton {
                            objectName: "discardCollectionEdits"
                            text: page.reloading ? qsTr("Reload collection") : qsTr("Discard edits")
                            emphasis: "ghost"
                            onClicked: page.discardEdits()
                        }
                    }
                }
            }
            Flow {
                visible: !page.editing && !!page.collection
                Layout.fillWidth: true
                spacing: 5
                Repeater {
                    model: page.store.filterOptions(page.collection ? page.collection.items : [])
                    CollectionActionButton {
                        required property var modelData
                        objectName: "filter_" + modelData.key
                        text: modelData.label
                        selected: page.kind === modelData.key
                        emphasis: "ghost"
                        onClicked: page.kind = modelData.key
                    }
                }
            }
            RowLayout {
                visible: page.editing
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: qsTr("Your order · Notes are optional"); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                CollectionActionButton {
                    id: undoButton
                    objectName: "undoRemoveTitle"
                    visible: !!page.removedItem
                    text: qsTr("Undo remove")
                    emphasis: "ghost"
                    onClicked: page.undoRemove()
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#18ffffff" }
            Grid {
                id: mediaGrid
                Layout.fillWidth: true
                columns: width >= 1150 ? 5 : width >= 850 ? 4 : width >= 620 ? 3 : width >= 300 ? 2 : 1
                columnSpacing: page.narrow ? 16 : 28
                rowSpacing: 34
                readonly property real cardWidth: Math.min(230,
                    (width - (columns - 1) * columnSpacing) / columns)
                Repeater {
                    id: cards
                    model: page.visibleItems
                    CollectionMediaCard {
                        id: mediaCard
                        required property string modelData
                        required property int index
                        width: mediaGrid.cardWidth
                        height: implicitHeight
                        media: page.store.media(modelData)
                        noteText: (page.editing ? page.draftNotes : page.collection ? page.collection.notes : {})[modelData] || ""
                        editing: page.editing
                        position: index
                        itemCount: page.visibleItems.length
                        onTitleRequested: page.titleRequested(modelData)
                        onNoteEdited: (note) => page.setNote(modelData, note)
                        onMoveRequested: (direction) => page.moveItem(modelData, direction)
                        onRemoveRequested: page.removeItem(modelData)
                        onEnsureVisibleRequested: function(target) { page.revealItem(target) }
                    }
                }
            }
            ColumnLayout {
                visible: page.visibleItems.length === 0
                Layout.fillWidth: true
                Layout.topMargin: 48
                Layout.bottomMargin: 60
                spacing: 14
                CollectionIcon { name: "collections"; strokeColor: theme.inkDimmer; Layout.preferredWidth: 36; Layout.preferredHeight: 36; Layout.alignment: Qt.AlignHCenter }
                Text { Layout.fillWidth: true; text: page.kind === "all" ? qsTr("Room for your next favourite") : qsTr("No titles of this kind yet"); color: theme.ink; font.family: theme.display; font.pixelSize: 26; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; textFormat: Text.PlainText }
                Text { Layout.fillWidth: true; text: qsTr("Open a title and choose Add to Collections."); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; textFormat: Text.PlainText }
                CollectionActionButton {
                    objectName: "browseTitleButton"
                    Layout.alignment: Qt.AlignHCenter
                    visible: !page.editing && page.kind !== "all"
                    text: qsTr("Show all titles")
                    iconName: "grid"
                    onClicked: page.kind = "all"
                }
            }
        }
    }
}
