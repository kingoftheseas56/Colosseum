pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import ".."
import "CollectionText.js" as CollectionText

Popup {
    id: root
    required property var controller
    required property var store
    property string collectionId: ""
    property string tab: collectionId ? "publish" : "receive"
    property string coverData: ""
    property int draftRevision: 0
    property string localError: ""
    property string confirmation: ""
    property int confirmationRevision: 0
    readonly property var collection: store.getCollection(collectionId)
    readonly property var received: controller ? controller.received : ({})
    readonly property var receivedSnapshot: received.snapshot || ({})
    readonly property var ownLinks: controller ? controller.publications || [] : []
    signal dismissed()
    signal collectionOpened(string collectionId)
    parent: Overlay.overlay
    width: Math.min(760, parent ? parent.width - 24 : 760)
    height: Math.min(760, parent ? parent.height - 32 : 760)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: width < 500 ? 16 : 24
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    Theme { id: theme }
    Overlay.modal: Rectangle { color: "#b4000209" }
    background: Rectangle { radius: 16; color: "#171b26"; border.color: theme.edge }

    function beginDraft() {
        root.draftRevision = root.store.revision
        const p = root.collection ? root.collection.presentation || {} : {}
        dedication.text = p.dedication || ""
        signature.text = p.signature || ""
        root.coverData = p.cover_data || ""
        presets.currentIndex = Math.max(0, ["midnight", "paper", "sea"].indexOf(p.preset || "midnight"))
    }
    function presentation() {
        return {preset:["midnight","paper","sea"][presets.currentIndex], dedication:dedication.text,
            signature:signature.text, cover_data:root.coverData}
    }
    function saveDraft() {
        root.localError = ""
        if (!root.controller || !root.collection) return false
        const ok = root.controller.savePresentation(root.collectionId, root.presentation(), root.draftRevision)
        if (ok) root.draftRevision = root.store.revision
        return !!ok
    }
    function reveal(item) {
        const point = item.mapToItem(body, 0, 0)
        if (point.y < scroll.contentY) scroll.contentY = point.y
        else if (point.y + item.height > scroll.contentY + scroll.height)
            scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.height, point.y + item.height - scroll.height))
    }
    onOpened: {
        root.beginDraft()
        root.localError = ""
        root.confirmation = ""
        closeButton.forceActiveFocus(Qt.OtherFocusReason)
        if (root.controller && root.controller.signedIn) root.controller.refresh()
    }
    onClosed: {
        passphrase.text = ""
        receivePassphrase.text = ""
        root.dismissed()
    }
    Connections {
        target: root.controller
        function onKept(id) { root.collectionOpened(id); root.close() }
        function onCopied(id) { root.collectionOpened(id); root.close() }
    }
    Connections {
        target: body.Window.window
        function onActiveFocusItemChanged() {
            if (!root.visible || !body.Window.window) return
            const focused = body.Window.window.activeFocusItem
            let item = focused
            while (item && item !== body) item = item.parent
            if (item === body && focused) root.reveal(focused)
        }
    }
    FileDialog {
        id: coverPicker
        title: qsTr("Choose your gift cover")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            const cover = root.controller.loadCover(selectedFile)
            if (cover) root.coverData = cover
        }
    }
    FileDialog {
        id: editionPicker
        title: qsTr("Open a saved collection edition")
        nameFilters: [qsTr("Collection edition (*.json)")]
        fileMode: FileDialog.OpenFile
        onAccepted: root.controller.openEditionFile(selectedFile)
    }

    component LabelText: Text {
        Layout.fillWidth: true
        color: theme.inkDim
        font.family: theme.ui
        font.pixelSize: 14
        textFormat: Text.PlainText
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    }
    component Field: TextField {
        id: field
        Layout.fillWidth: true
        color: theme.ink
        font.family: theme.ui
        font.pixelSize: 14
        placeholderTextColor: theme.inkDimmer
        selectByMouse: true
        padding: 12
        background: Rectangle { color: "#0cffffff"; radius: 8; border.color: field.activeFocus ? theme.gold : theme.edge }
        onActiveFocusChanged: if (activeFocus) root.reveal(field)
    }

    contentItem: ColumnLayout {
        spacing: 16
        Keys.priority: Keys.AfterItem
        Keys.onPressed: function(event) {
            const focused = body.Window.window ? body.Window.window.activeFocusItem : null
            if (focused && (focused instanceof TextInput || focused instanceof TextEdit)) return
            let delta = 0
            if (event.key === Qt.Key_Down) delta = 64
            else if (event.key === Qt.Key_Up) delta = -64
            else if (event.key === Qt.Key_PageDown) delta = scroll.height * 0.8
            else if (event.key === Qt.Key_PageUp) delta = -scroll.height * 0.8
            if (delta) {
                scroll.contentY = Math.max(0, Math.min(Math.max(0, scroll.contentHeight - scroll.height), scroll.contentY + delta))
                event.accepted = true
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("Share a collection")
                font.family: theme.display
                font.pixelSize: 26
                color: theme.ink
                wrapMode: Text.Wrap
            }
            CollectionActionButton {
                id: closeButton
                objectName: "closeCollectionsSharing"
                text: qsTr("Close")
                emphasis: "ghost"
                onClicked: root.close()
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            CollectionActionButton { visible: !!root.collection && !root.collection.keptGift; text: qsTr("Publish / gift"); selected: root.tab === "publish"; onClicked: { root.tab = "publish"; scroll.contentY = 0 } }
            CollectionActionButton { text: qsTr("Receive"); selected: root.tab === "receive"; onClicked: { root.tab = "receive"; scroll.contentY = 0 } }
            CollectionActionButton { text: qsTr("My shared links"); selected: root.tab === "links"; onClicked: { root.tab = "links"; scroll.contentY = 0; if (root.controller) root.controller.refresh() } }
        }
        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.localError || (root.controller ? root.controller.error || root.controller.message : qsTr("Sharing is unavailable."))
            textFormat: Text.PlainText
            color: root.localError || (root.controller && root.controller.error) ? "#efb6a9" : theme.gold
            font.pixelSize: 13
            wrapMode: Text.Wrap
            Accessible.role: Accessible.AlertMessage
        }
        LabelText { visible: !!root.controller && root.controller.busy; text: qsTr("Working…") }
        Flickable {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            ColumnLayout {
                id: body
                width: scroll.width
                spacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.tab === "publish" && !!root.collection && !root.collection.keptGift
                    spacing: 16
                    LabelText { text: root.collection ? root.collection.name : ""; font.pixelSize: 18; color: theme.ink }
                    LabelText { text: qsTr("Your collection stays private until you publish. Later edits stay private until you choose Publish changes. Gift links always keep their original edition.") }
                    LabelText { text: qsTr("Gift presentation · saved on this profile, even offline"); color: theme.gold }
                    Flow {
                        id: presets
                        objectName: "giftPreset"
                        property int currentIndex: 0
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: [qsTr("Midnight"), qsTr("Paper"), qsTr("Sea")]
                            CollectionActionButton {
                                required property string modelData
                                required property int index
                                text: modelData
                                selected: presets.currentIndex === index
                                onClicked: presets.currentIndex = index
                                onActiveFocusChanged: if (activeFocus) root.reveal(this)
                            }
                        }
                    }
                    TextArea {
                        id: dedication
                        objectName: "giftDedication"
                        Layout.fillWidth: true
                        implicitHeight: Math.max(100, contentHeight + 24)
                        placeholderText: qsTr("A personal dedication")
                        color: theme.ink
                        placeholderTextColor: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 14
                        padding: 12
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        Accessible.name: qsTr("Gift dedication")
                        function applyLimit() {
                            if (!inputMethodComposing && text.length > 2000) text = CollectionText.clamp(text, 2000)
                        }
                        onTextChanged: applyLimit()
                        onInputMethodComposingChanged: applyLimit()
                        onActiveFocusChanged: if (activeFocus) root.reveal(dedication)
                        background: Rectangle { color: "#0cffffff"; radius: 8; border.color: dedication.activeFocus ? theme.gold : theme.edge }
                    }
                    Field { id: signature; objectName: "giftSignature"; placeholderText: qsTr("Your signature (optional)"); maximumLength: 100; Accessible.name: qsTr("Gift signature") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        CollectionActionButton { text: qsTr("Choose cover image"); onClicked: coverPicker.open() }
                        CollectionActionButton { visible: !!root.coverData; text: qsTr("Remove cover"); emphasis: "ghost"; onClicked: root.coverData = "" }
                    }
                    LabelText { text: qsTr("PNG or JPEG · up to 256 KB and 2048 × 2048. Only upload an image you have permission to share."); font.pixelSize: 12 }
                    CollectionGiftPresentation { Layout.fillWidth: true; preview: true; collectionName: root.collection ? root.collection.name : ""; presentation: root.presentation() }
                    CollectionActionButton { objectName: "saveGiftDraft"; text: qsTr("Save gift draft"); enabled: !!root.controller && !root.controller.busy; onClicked: root.saveDraft() }
                    LabelText { text: qsTr("Optional link passphrase"); color: theme.ink }
                    Field { id: passphrase; objectName: "publishPassphrase"; placeholderText: qsTr("Leave empty for anyone with the link"); maximumLength: 128; echoMode: TextInput.Password; Accessible.name: qsTr("Publication passphrase") }
                    LabelText { visible: !root.controller || !root.controller.signedIn; text: qsTr("Sign in to your Colosseum account to publish. Drafting stays available offline.") }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !!root.controller && root.controller.retryResetAvailable
                        spacing: 10
                        LabelText { text: qsTr("The earlier request may already have created a link. Check Shared links first. Starting a new request can create another publication."); color: "#efc9a9" }
                        CollectionActionButton {
                            objectName: "requestNewPublication"
                            text: qsTr("Start a new publication request")
                            enabled: !root.controller.busy
                            onClicked: root.confirmation = "new-publication"
                        }
                        RowLayout {
                            visible: root.confirmation === "new-publication"
                            spacing: 8
                            CollectionActionButton { text: qsTr("Cancel"); onClicked: root.confirmation = "" }
                            CollectionActionButton {
                                objectName: "confirmNewPublication"
                                text: qsTr("Confirm new request")
                                enabled: !root.controller.busy
                                onClicked: {
                                    root.controller.startNewPublicationRequest()
                                    root.confirmation = ""
                                }
                            }
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        CollectionActionButton {
                            objectName: "publishOrdinaryCollection"
                            text: qsTr("Publish collection")
                            enabled: !!root.controller && root.controller.signedIn && !root.controller.busy
                            onClicked: root.controller.publish(root.collectionId, false, passphrase.text)
                        }
                        CollectionActionButton {
                            objectName: "publishGiftEdition"
                            text: qsTr("Publish gift edition")
                            emphasis: "primary"
                            enabled: !!root.controller && root.controller.signedIn && !root.controller.busy
                            onClicked: if (root.saveDraft()) root.controller.publish(root.collectionId, true, passphrase.text)
                        }
                    }
                }
                ColumnLayout {
                    visible: root.tab === "receive"
                    Layout.fillWidth: true
                    spacing: 14
                    LabelText { text: qsTr("Open a link or an edition saved from the browser. Keeping a gift stores its messages and cover on this profile for offline viewing.") }
                    Field { id: receiveLink; objectName: "receiveCollectionLink"; placeholderText: qsTr("Paste a collection link"); maximumLength: 2048; Accessible.name: qsTr("Collection link") }
                    Field { id: receivePassphrase; objectName: "receivePassphrase"; placeholderText: qsTr("Passphrase, if protected"); maximumLength: 128; echoMode: TextInput.Password; Accessible.name: qsTr("Link passphrase") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        CollectionActionButton { objectName: "openSharedCollection"; text: qsTr("Open link"); emphasis: "primary"; enabled: !!root.controller && !root.controller.busy && receiveLink.text.trim().length > 0; onClicked: root.controller.openLink(receiveLink.text, receivePassphrase.text) }
                        CollectionActionButton { text: qsTr("Open edition file"); enabled: !!root.controller && !root.controller.busy; onClicked: editionPicker.open() }
                    }
                    CollectionGiftPresentation {
                        id: receivedGift
                        objectName: "receivedGiftPresentation"
                        Layout.fillWidth: true
                        visible: !!root.received.share_id
                        collectionName: root.receivedSnapshot.name || ""
                        presentation: root.receivedSnapshot.presentation || ({})
                        preview: root.received.kind !== "gift"
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !!root.received.share_id && (receivedGift.opened || receivedGift.preview)
                        LabelText { text: root.receivedSnapshot.description || ""; visible: text.length > 0 }
                        Repeater {
                            model: root.receivedSnapshot.members || []
                            delegate: Rectangle {
                                id: receivedRow
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                implicitHeight: rowBody.implicitHeight + 24
                                color: "#0bffffff"
                                radius: 8
                                ColumnLayout {
                                    id: rowBody
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 6
                                    LabelText { text: String(receivedRow.index + 1) + ". " + receivedRow.modelData.title; color: theme.ink }
                                    LabelText { text: [receivedRow.modelData.kind, receivedRow.modelData.creator, receivedRow.modelData.year].filter(Boolean).join(" · "); font.pixelSize: 12 }
                                    LabelText { visible: !!receivedRow.modelData.note; text: receivedRow.modelData.note || "" }
                                }
                            }
                        }
                        LabelText { text: qsTr("Titles remain references. Keeping or copying does not add to Libraries, download media or change progress."); font.pixelSize: 12 }
                        CollectionActionButton { objectName: "keepReceivedGift"; visible: root.received.kind === "gift"; text: qsTr("Keep Gift"); emphasis: "primary"; enabled: !!root.controller && !root.controller.busy; onClicked: root.controller.keepReceivedGift() }
                    }
                }
                ColumnLayout {
                    visible: root.tab === "links"
                    Layout.fillWidth: true
                    spacing: 14
                    LabelText { text: root.controller && root.controller.signedIn ? qsTr("Revoking a link stops future access. Copies already kept cannot be recalled.") : qsTr("Sign in to manage your shared links.") }
                    CollectionActionButton { text: qsTr("Refresh shared links"); enabled: !!root.controller && root.controller.signedIn && !root.controller.busy; onClicked: root.controller.refresh() }
                    LabelText { visible: root.ownLinks.length === 0; text: qsTr("No shared links loaded.") }
                    Repeater {
                        model: root.ownLinks
                        delegate: Rectangle {
                            id: linkRow
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: linkBody.implicitHeight + 24
                            color: "#0bffffff"
                            radius: 10
                            ColumnLayout {
                                id: linkBody
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10
                                LabelText { text: linkRow.modelData.name || (linkRow.modelData.snapshot || {}).name || qsTr("Shared collection"); color: theme.ink; font.pixelSize: 16 }
                                LabelText { text: (linkRow.modelData.kind === "gift" ? qsTr("Fixed gift edition") : qsTr("Collection")) + " · " + qsTr("Revision %1").arg(linkRow.modelData.revision) + (linkRow.modelData.revoked ? " · " + qsTr("Revoked") : linkRow.modelData.protected ? " · " + qsTr("Passphrase protected") : ""); font.pixelSize: 12 }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    CollectionActionButton { text: qsTr("Copy link"); enabled: !linkRow.modelData.revoked; onClicked: root.controller.copyLink(linkRow.modelData.share_id) }
                                    CollectionActionButton {
                                        text: qsTr("Publish changes")
                                        visible: !!root.collection && !root.collection.keptGift && linkRow.modelData.kind === "collection" && linkRow.modelData.source_collection_id === root.collectionId && !linkRow.modelData.revoked
                                        enabled: !root.controller.busy
                                        onClicked: { root.confirmation = "update:" + linkRow.modelData.share_id; root.confirmationRevision = linkRow.modelData.revision }
                                    }
                                    CollectionActionButton {
                                        text: qsTr("Revoke link")
                                        destructive: true
                                        visible: !linkRow.modelData.revoked
                                        enabled: !root.controller.busy
                                        onClicked: { root.confirmation = "revoke:" + linkRow.modelData.share_id; root.confirmationRevision = linkRow.modelData.revision }
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    visible: root.confirmation.endsWith(":" + linkRow.modelData.share_id)
                                    LabelText { text: root.confirmation.indexOf("update:") === 0 ? qsTr("Replace this link with the current collection? Its passphrase becomes the value entered in Publish / gift.") : qsTr("Revoke this link? Existing kept copies will remain.") }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        CollectionActionButton { text: qsTr("Cancel"); onClicked: root.confirmation = "" }
                                        CollectionActionButton {
                                            text: qsTr("Confirm")
                                            enabled: !root.controller.busy
                                            onClicked: {
                                                if (root.confirmation.indexOf("update:") === 0) root.controller.publishChanges(linkRow.modelData.share_id, root.confirmationRevision, root.collectionId, passphrase.text)
                                                else root.controller.revoke(linkRow.modelData.share_id, root.confirmationRevision)
                                                root.confirmation = ""
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Item { Layout.preferredHeight: 8 }
            }
        }
    }
}
