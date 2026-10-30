pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

Popup {
    id: root
    objectName: "addToCollectionsPopup"
    required property var store
    property var reference: ({})
    readonly property string titleId: reference.key || ""
    property bool committing: false
    property bool createOnly: false
    property bool creating: createOnly
    property var selectedIds: []
    property string validationError: ""
    signal completed(string message)
    signal dismissed()
    readonly property var titleData: root.store.media(root.titleId)
    Theme { id: theme }
    parent: Overlay.overlay
    width: Math.min(560, parent ? parent.width - 32 : 560)
    height: Math.min(implicitHeight, parent ? parent.height - 32 : 720)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: width < 480 ? 20 : 26
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#b4000209" }
    background: Rectangle {
        color: "#171b26"
        radius: 18
        border.color: "#38ffffff"
        border.width: 1
    }
    function toggle(id) {
        var next = selectedIds.slice()
        var index = next.indexOf(id)
        if (index < 0) next.push(id)
        else next.splice(index, 1)
        selectedIds = next
    }
    function beginCreate() {
        creating = true
        validationError = ""
        collectionName.text = search.text.trim()
        Qt.callLater(function() { collectionName.forceActiveFocus(Qt.OtherFocusReason) })
    }
    function cancelCreate() {
        if (createOnly) { close(); return }
        creating = false
        validationError = ""
        collectionName.text = ""
        Qt.callLater(function() { newButton.forceActiveFocus(Qt.OtherFocusReason) })
    }
    function createCollection() {
        if (collectionName.inputMethodComposing || root.committing) return
        if (!collectionName.text.trim()) {
            root.validationError = qsTr("Give your collection a name.")
            collectionName.forceActiveFocus(Qt.OtherFocusReason)
            return
        }
        root.committing = true
        const result = root.createOnly ? root.store.createEmpty(collectionName.text)
            : root.store.saveMemberships(root.reference, root.selectedIds, collectionName.text)
        root.committing = false
        if (!result.ok) {
            root.validationError = result.error
            collectionName.forceActiveFocus(Qt.OtherFocusReason)
            return
        }
        root.completed(root.createOnly ? qsTr("Collection created") : qsTr("Collection created and title added"))
        root.close()
    }
    function saveMemberships() {
        if (root.committing) return
        root.committing = true
        const result = root.store.saveMemberships(root.reference, root.selectedIds, "")
        root.committing = false
        if (!result.ok) { root.validationError = result.error; return }
        root.completed(qsTr("Collections updated"))
        root.close()
    }
    function focusControls() {
        var controls = [closeButton]
        if (creating) return controls.concat([collectionName, cancelCreateButton, createButton])
        controls.push(search)
        if (search.text.length) controls.push(search.clearControl)
        for (var i = 0; i < membershipList.count; i++) {
            var row = membershipList.itemAtIndex(i)
            if (row) controls.push(row)
        }
        controls.push(newButton, cancelButton, doneButton)
        return controls.filter(function(c) { return c.visible && c.enabled })
    }
    function advanceFocus(backwards) {
        var controls = focusControls()
        var current = -1
        for (var i = 0; i < controls.length; i++)
            if (controls[i].activeFocus) current = i
        var next = backwards ? (current <= 0 ? controls.length - 1 : current - 1) : (current + 1) % controls.length
        if (controls.length) controls[next].forceActiveFocus(Qt.TabFocusReason)
    }
    onOpened: {
        creating = createOnly
        validationError = ""
        search.text = ""
        collectionName.text = ""
        selectedIds = createOnly ? [] : store.memberships(titleId).filter(function(id) { const row = root.store.getCollection(id); return row && !row.keptGift })
        if (createOnly) collectionName.forceActiveFocus(Qt.OtherFocusReason)
        else search.forceActiveFocus(Qt.OtherFocusReason)
    }
    onClosed: root.dismissed()
    contentItem: ColumnLayout {
        spacing: 12
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                root.advanceFocus(event.key === Qt.Key_Backtab || !!(event.modifiers & Qt.ShiftModifier))
                event.accepted = true
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: root.createOnly ? qsTr("New collection") : qsTr("Add to Collections")
                font.family: theme.display
                font.pixelSize: root.width < 480 ? 25 : 29
                color: theme.ink
                wrapMode: Text.WordWrap
            }
            CollectionActionButton {
                id: closeButton
                objectName: "pickerClose"
                iconName: "close"
                emphasis: "ghost"
                compact: true
                Accessible.name: qsTr("Close")
                onClicked: root.close()
            }
        }
        Text {
            Layout.fillWidth: true
            text: root.creating ? qsTr("Give it a name. Make it yours over time.") : qsTr("Keep this title in one collection, or a few.")
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        Rectangle {
            visible: !root.createOnly
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            radius: 10
            color: "#08ffffff"
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 14
                CollectionCover { Layout.preferredWidth: 39; Layout.preferredHeight: 58; source: root.titleData.cover; title: root.titleData.title }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { Layout.fillWidth: true; text: root.titleData.title; textFormat: Text.PlainText; color: theme.ink; font.family: theme.ui; font.pixelSize: 16; font.weight: Font.DemiBold; elide: Text.ElideRight }
                    Text { Layout.fillWidth: true; text: root.titleData.year || root.titleData.creator ? qsTr("%1 · %2").arg(root.titleData.kindLabel).arg(root.titleData.year || root.titleData.creator) : root.titleData.kindLabel; textFormat: Text.PlainText; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; elide: Text.ElideRight }
                }
                CollectionIcon { name: root.titleData.kind; strokeColor: theme.inkDimmer; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
            }
        }
        CollectionSearchField {
            id: search
            objectName: "pickerSearch"
            visible: !root.creating
            Layout.fillWidth: true
            placeholderText: qsTr("Find a collection")
            onTextChanged: membershipList.positionViewAtBeginning()
        }
        ListView {
            id: membershipList
            objectName: "membershipList"
            visible: !root.creating
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(110, Math.min(300, (root.parent ? root.parent.height : 960) - 500))
            clip: true
            spacing: 4
            boundsBehavior: Flickable.StopAtBounds
            model: { root.store.revision; return root.store.filteredCollections(search.text).filter(function(row) { return !row.keptGift }) }
            delegate: CollectionMembershipRow {
                required property var modelData
                required property int index
                width: membershipList.width
                collection: modelData
                store: root.store
                included: root.selectedIds.indexOf(modelData.id) !== -1
                onClicked: root.toggle(modelData.id)
                onActiveFocusChanged: if (activeFocus) membershipList.positionViewAtIndex(index, ListView.Contain)
            }
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }
            Text {
                anchors.centerIn: parent
                width: parent.width - 30
                visible: membershipList.count === 0
                text: root.store.collections.length ? qsTr("No collections match that name.\nCreate one below, or clear your search.") : qsTr("Your first collection starts here.\nGive this story a place to stay.")
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                lineHeight: 1.5
            }
        }
        CollectionActionButton {
            id: newButton
            objectName: "pickerNewCollection"
            visible: !root.creating
            Layout.fillWidth: true
            text: qsTr("New collection")
            enabled: root.store.healthy
            iconName: "plus"
            emphasis: "ghost"
            onClicked: root.beginCreate()
        }
        ColumnLayout {
            visible: root.creating
            Layout.fillWidth: true
            spacing: 8
            Text { text: qsTr("Collection name"); color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
            TextField {
                id: collectionName
                objectName: "collectionNameField"
                Layout.fillWidth: true
                implicitHeight: 46
                maximumLength: 100
                placeholderText: qsTr("Something worth keeping")
                placeholderTextColor: theme.inkDimmer
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 15
                leftPadding: 13
                rightPadding: 13
                selectByMouse: true
                Accessible.name: qsTr("Collection name")
                Accessible.description: root.validationError
                background: Rectangle { radius: 9; color: "#080b12"; border.width: collectionName.activeFocus ? 2 : 1; border.color: root.validationError ? "#e5a4a4" : collectionName.activeFocus ? theme.gold : theme.edge }
                onTextEdited: root.validationError = ""
                onAccepted: root.createCollection()
            }
        }
        Text {
            objectName: "pickerValidationError"
            Layout.fillWidth: true
            Layout.minimumHeight: root.creating ? 34 : 0
            visible: root.creating || root.validationError.length > 0
            text: root.validationError
            textFormat: Text.PlainText
            color: "#e5a4a4"
            font.family: theme.ui
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#1cffffff" }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { visible: !root.creating && root.width >= 420; Layout.fillWidth: true; text: qsTr("Private, just for you"); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
            Item { visible: root.creating || root.width < 420; Layout.fillWidth: true }
            CollectionActionButton { id: cancelButton; objectName: "pickerCancel"; visible: !root.creating; text: qsTr("Cancel"); emphasis: "ghost"; onClicked: root.close() }
            CollectionActionButton { id: doneButton; objectName: "pickerDone"; visible: !root.creating; text: qsTr("Save"); enabled: root.store.healthy && !root.committing; emphasis: "primary"; onClicked: root.saveMemberships() }
            CollectionActionButton { id: cancelCreateButton; objectName: "pickerCancelCreate"; visible: root.creating; text: qsTr("Cancel"); emphasis: "ghost"; onClicked: root.cancelCreate() }
            CollectionActionButton { id: createButton; objectName: "createAndAddButton"; visible: root.creating; text: root.createOnly ? qsTr("Create collection") : qsTr("Create & add"); enabled: root.store.healthy && !root.committing; emphasis: "primary"; onClicked: root.createCollection() }
        }
    }
}
