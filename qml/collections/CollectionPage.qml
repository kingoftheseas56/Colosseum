pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."
import "CollectionText.js" as CollectionText

Item {
    id: page
    objectName: "collectionPage"
    required property var store
    property bool sharingAvailable: false
    property bool giftOpened: false
    signal sharingRequested()
    signal copyGiftRequested(string name)
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
    // Local names may be disambiguated on import; the received edition keeps
    // the sender's exact title, description and presentation.
    readonly property var displayedEdition: collection && collection.keptGift ? collection.giftSnapshot || ({}) : ({})
    readonly property string displayedName: collection ? (collection.keptGift && displayedEdition.name ? displayedEdition.name : collection.name) : ""
    readonly property string displayedDescription: collection ? (collection.keptGift && displayedEdition.description !== undefined ? displayedEdition.description : collection.description) : ""
    readonly property var displayedPresentation: collection ? (collection.keptGift && displayedEdition.presentation ? displayedEdition.presentation : collection.presentation || ({})) : ({})
    readonly property var visibleItems: {
        if (page.collection && page.collection.keptGift && !page.giftOpened) return []
        const ids = page.editing ? page.draftItems : page.collection ? page.collection.items : []
        if (page.editing || page.kind === "all") return ids
        return ids.filter(function(id) { return page.store.media(id).kind === page.kind })
    }
    // Row-chunked member model for the virtualized grid viewport. Only rows
    // near the viewport are instantiated, so member cards never scale with
    // the full collection size.
    readonly property real gridWidth: width - sideMargin * 2
    readonly property int memberColumns: gridWidth >= 1150 ? 5 : gridWidth >= 850 ? 4 : gridWidth >= 620 ? 3 : gridWidth >= 300 ? 2 : 1
    readonly property real cardSpacing: narrow ? 16 : 28
    readonly property real cardWidth: Math.min(230, (gridWidth - (memberColumns - 1) * cardSpacing) / memberColumns)
    readonly property var memberRows: {
        const ids = page.visibleItems
        const columns = page.memberColumns
        const rows = []
        for (let i = 0; i < ids.length; i += columns) rows.push(ids.slice(i, i + columns))
        return rows
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
        if (!page.collection || page.collection.keptGift || !page.store.healthy) return
        const draft = page.store.beginEdit(page.collectionId)
        if (!draft.token) {
            page.errorText = page.store.error || qsTr("Could not start editing this collection.")
            return
        }
        const header = memberList.headerItem
        page.draftToken = draft.token
        header.nameField.text = draft.name
        header.descriptionField.text = draft.description || ""
        page.draftItems = draft.items.slice()
        page.draftNotes = Object.assign({}, draft.notes || {})
        page.errorText = ""
        page.removedItem = null
        page.kind = "all"
        page.leaving = false
        page.reloading = false
        page.editing = true
        page.focusHeaderControl(header.nameField)
    }
    function cancelEdit() {
        page.abortEdit()
        page.errorText = ""
        page.navigationCanceled()
        page.focusHeaderControl(memberList.headerItem.primaryAction)
    }
    function saveEdit() {
        const header = memberList.headerItem
        if (header.nameField.inputMethodComposing || header.descriptionField.inputMethodComposing) return false
        const result = page.store.saveEdit(page.draftToken, header.nameField.text,
            header.descriptionField.text, page.draftItems, page.draftNotes)
        if (!result.ok) {
            page.errorText = result.error || qsTr("Could not save this collection.")
            page.focusHeaderControl(header.nameField)
            return false
        }
        page.draftToken = ""
        page.editing = false
        page.leaving = false
        page.errorText = ""
        page.removedItem = null
        page.navigationCanceled()
        page.messageRequested(qsTr("Collection updated"))
        page.focusHeaderControl(header.primaryAction)
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
        page.focusHeaderControl(memberList.headerItem.stayAction)
    }
    function scrollToBeginning() {
        if (!memberList) return
        memberList.forceLayout()
        memberList.positionViewAtBeginning()
    }
    function focusHeaderControl(control) {
        Qt.callLater(function() {
            if (!control || !control.visible) return
            page.scrollToBeginning()
            control.forceActiveFocus(Qt.TabFocusReason)
            page.revealItem(control)
        })
    }
    function boundedScrollPosition(position) {
        // Inline headers and model changes can give ListView a negative
        // origin. Zero is not necessarily the beginning or even in bounds.
        const minimum = memberList.originY - memberList.topMargin
        const maximum = Math.max(minimum, memberList.originY + memberList.contentHeight
            - memberList.height + memberList.bottomMargin)
        return Math.max(minimum, Math.min(maximum, position))
    }
    function setNote(id, note) {
        if (!page.editing) return
        const notes = Object.assign({}, page.draftNotes)
        notes[id] = CollectionText.clamp(note, 500)
        page.draftNotes = notes
    }
    function memberCard(id) {
        const name = "memberCard_" + id
        function scan(item) {
            if (!item) return null
            if (item.objectName === name) return item
            const children = item.children
            for (let i = 0; i < children.length; ++i) {
                const found = scan(children[i])
                if (found) return found
            }
            return null
        }
        return scan(memberList.contentItem)
    }
    function focusMemberCard(id, direction, attempt) {
        memberList.forceLayout()
        const card = page.memberCard(id)
        if (card) { card.focusReorder(direction); return }
        const index = page.visibleItems.indexOf(id)
        if (index >= 0)
            memberList.positionViewAtIndex(Math.floor(index / page.memberColumns), ListView.Contain)
        if (attempt < 8) Qt.callLater(function() { page.focusMemberCard(id, direction, attempt + 1) })
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
        Qt.callLater(function() { page.focusMemberCard(id, direction, 0) })
    }
    function removeItem(id) {
        if (!page.editing) return
        const items = page.draftItems.slice()
        const index = items.indexOf(id)
        if (index < 0) return
        page.removedItem = { id: id, index: index }
        items.splice(index, 1)
        page.draftItems = items
        page.focusHeaderControl(memberList.headerItem.undoAction)
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
        const p = item.mapToItem(memberList.contentItem, 0, useCaret ? caret.y : 0)
        const targetHeight = useCaret ? Math.max(1, caret.height) : item.height
        if (p.y < memberList.contentY + 12) memberList.contentY = page.boundedScrollPosition(p.y - 12)
        else if (p.y + targetHeight > memberList.contentY + memberList.height - 12)
            memberList.contentY = page.boundedScrollPosition(p.y + targetHeight - memberList.height + 12)
    }
    Component.onDestruction: page.abortEdit()
    onCollectionIdChanged: {
        page.giftOpened = false
        page.abortEdit()
        page.kind = "all"
        Qt.callLater(page.scrollToBeginning)
    }
    Keys.priority: Keys.AfterItem
    Keys.onEscapePressed: {
        if (page.leaving) { page.leaving = false; page.navigationCanceled() }
        else page.requestBack()
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_PageDown) {
            memberList.contentY = page.boundedScrollPosition(memberList.contentY + memberList.height * 0.8)
            event.accepted = true
        } else if (event.key === Qt.Key_PageUp) {
            memberList.contentY = page.boundedScrollPosition(memberList.contentY - memberList.height * 0.8)
            event.accepted = true
        }
    }

    ListView {
        id: memberList
        objectName: "collectionScroller"
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        topMargin: 10
        bottomMargin: 40
        cacheBuffer: 320
        spacing: 0
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }
        model: page.memberRows
        // The header is its own component scope, so the controls the page
        // functions drive are exposed through these properties.
        header: ColumnLayout {
            id: content
            property Item nameField: nameEditor
            property Item descriptionField: descriptionEditor
            property Item primaryAction: editButton
            property Item undoAction: undoButton
            property Item stayAction: stayButton
            x: page.sideMargin
            width: memberList.width - page.sideMargin * 2
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
                        Text { text: page.collection && page.collection.keptGift ? qsTr("Kept gift · offline edition") : qsTr("Private collection"); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; textFormat: Text.PlainText }
                    }
                    Text {
                        visible: !page.editing
                        Layout.fillWidth: true
                        objectName: "collectionDisplayName"
                        text: page.collection ? page.displayedName : qsTr("Collection unavailable")
                        font.family: theme.display
                        font.pixelSize: page.narrow ? 32 : 42
                        color: theme.ink
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        textFormat: Text.PlainText
                    }
                    Text {
                        visible: !page.editing
                        Layout.fillWidth: true
                        text: page.collection ? page.displayedDescription : qsTr("Return to Your Collections to choose another one.")
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
                        // Rewriting the text during IME composition would
                        // corrupt the preedit; clamp once it ends.
                        function applyLimit() {
                            if (descriptionEditor.inputMethodComposing) return
                            const clamped = CollectionText.clamp(text, 300)
                            if (clamped !== text) text = clamped
                        }
                        onTextChanged: applyLimit()
                        onInputMethodComposingChanged: applyLimit()
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
                            page.focusHeaderControl(stayButton)
                        }
                    }
                }
                RowLayout {
                    Layout.alignment: page.narrow ? Qt.AlignLeft : Qt.AlignRight | Qt.AlignTop
                    spacing: 8
                    CollectionActionButton {
                        id: editButton
                        objectName: "editCollectionButton"
                        visible: !page.editing && !!page.collection && !page.collection.keptGift
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
            Flow {
                Layout.fillWidth: true
                visible: !page.editing && !!page.collection
                spacing: 8
                CollectionActionButton {
                    objectName: "shareCollectionButton"
                    visible: page.sharingAvailable && !(page.collection && page.collection.keptGift)
                    text: qsTr("Share / make a gift")
                    onClicked: page.sharingRequested()
                    onActiveFocusChanged: if (activeFocus) page.revealItem(this)
                }
                CollectionActionButton {
                    objectName: "makeOwnCollectionVersion"
                    visible: page.sharingAvailable && !!page.collection && !!page.collection.keptGift
                    text: qsTr("Make My Own Version")
                    emphasis: "primary"
                    onClicked: page.copyGiftRequested(CollectionText.clamp(page.displayedName, Math.max(0, 100 - qsTr(" (my version)").length)) + qsTr(" (my version)"))
                    onActiveFocusChanged: if (activeFocus) page.revealItem(this)
                }
            }
            CollectionGiftPresentation {
                id: keptGiftPresentation
                objectName: "keptGiftPresentation"
                initiallyOpened: page.giftOpened
                onOpenedChanged: page.giftOpened = opened
                Layout.fillWidth: true
                visible: !!page.collection && !!page.collection.keptGift
                collectionName: page.displayedName
                presentation: page.displayedPresentation
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
            Item { Layout.fillWidth: true; Layout.preferredHeight: 24 }
        }
        delegate: Item {
            id: memberRow
            required property var modelData
            required property int index
            width: memberList.width
            height: memberRowCards.height + (memberRow.index < page.memberRows.length - 1 ? 34 : 0)
            Row {
                id: memberRowCards
                x: page.sideMargin
                spacing: page.cardSpacing
                Repeater {
                    model: memberRow.modelData
                    CollectionMediaCard {
                        id: memberCardItem
                        required property string modelData
                        required property int index
                        objectName: "memberCard_" + modelData
                        width: page.cardWidth
                        height: implicitHeight
                        media: page.store.media(modelData)
                        noteText: (page.editing ? page.draftNotes : page.collection ? page.collection.notes : {})[modelData] || ""
                        editing: page.editing
                        position: memberRow.index * page.memberColumns + index
                        itemCount: page.visibleItems.length
                        onTitleRequested: page.titleRequested(modelData)
                        onNoteEdited: (note) => page.setNote(modelData, note)
                        onMoveRequested: (direction) => page.moveItem(modelData, direction)
                        onRemoveRequested: page.removeItem(modelData)
                        onEnsureVisibleRequested: (target) => page.revealItem(target)
                    }
                }
            }
        }
        footer: Item {
            width: memberList.width
            visible: page.visibleItems.length === 0 && !(page.collection && page.collection.keptGift && !page.giftOpened)
            implicitHeight: visible ? emptyState.implicitHeight + 108 : 0
            height: implicitHeight
            ColumnLayout {
                id: emptyState
                x: page.sideMargin
                y: 48
                width: parent.width - page.sideMargin * 2
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
