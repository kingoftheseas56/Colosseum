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
    property Item wallpaperItem: null
    property string initialView: collectionId ? "share" : "receive"
    property bool resumePreview: false
    property bool navigationPending: false
    property bool receivingSession: false
    property string closeReason: "dismiss"
    property string feedbackMessage: ""
    property bool publicationsReady: false
    property bool refreshingLinks: false
    property string pendingPublication: ""
    property bool mutationPending: false
    property bool publicationSignedIn: false
    readonly property bool outgoingSession: initialView !== "receive"
    property string settingsReturn: "share"
    readonly property bool working: navigationPending || (!!controller && controller.busy)
    readonly property var ordinaryLink: ownLinks.find(function(row) {
        return publicationsReady && row.kind === "collection" && !row.revoked
            && row.source_collection_id === root.collectionId
    }) || null
    readonly property string receivedUrl: controller && received.share_id ? controller.linkFor(received.share_id) : ""
    readonly property bool receivedSaved: (store.collections || []).some(function(row) {
        return row.savedShareId === root.received.share_id && row.savedRevision === root.received.revision
    })
    property string collectionId: ""
    property string incomingUrl: ""
    readonly property bool incomingReadOnly: receivingSession && !outgoingSession && tab === "receive" && opened
    property string tab: initialView
    property string coverData: ""
    property int draftRevision: 0
    property string localError: ""
    property string confirmation: ""
    property int confirmationRevision: 0
    readonly property var collection: store.getCollection(collectionId)
    readonly property var received: controller ? controller.received : ({})
    readonly property var receivedSnapshot: received.snapshot || ({})
    readonly property var ownLinks: controller && controller.signedIn ? controller.publications || [] : []
    signal dismissed(string reason)
    signal titleRequested(var reference)
    signal manualOpenAccepted()
    signal collectionOpened(string collectionId)
    parent: Overlay.overlay
    width: Math.min(500, parent ? parent.width - 24 : 500)
    implicitHeight: frame.implicitHeight + topPadding + bottomPadding
    height: Math.min(implicitHeight, parent ? parent.height - 32 : implicitHeight)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: width < 500 ? 16 : 24
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    Theme { id: theme }
    Overlay.modal: Rectangle { color: "#b4000209" }
    background: Glass { backdrop: root.wallpaperItem; radius: 16; tint: 0.08; scrim: 0.86; edge: theme.edge }

    function go(view) { root.tab = view; scroll.contentY = 0; closeButton.forceActiveFocus(Qt.OtherFocusReason) }
    function refreshLinks() {
        if (!root.controller || !root.controller.signedIn || root.working) return
        root.publicationsReady = false
        root.refreshingLinks = true
        if (!root.controller.refresh()) root.refreshingLinks = false
    }
    function openIncoming(url, reuse) {
        root.incomingUrl = String(url)
        root.receivingSession = true
        root.resumePreview = !!reuse
        root.go("receive")
        receiveLink.text = root.incomingUrl
        receivePassphrase.text = ""
        if (root.controller && !(reuse && root.receivedUrl === root.incomingUrl)) {
            root.controller.clearReceived()
            root.controller.openLink(root.incomingUrl, "")
        }
    }
    function openManual(value, phrase, editionFile) {
        const previousResume = root.resumePreview
        const previousOpened = receivedGift.opened
        // File imports can replace received content synchronously.
        root.resumePreview = false
        receivedGift.opened = false
        const accepted = editionFile ? root.controller.openEditionFile(value)
                                     : root.controller.openLink(value, phrase)
        if (!accepted) {
            root.resumePreview = previousResume
            receivedGift.opened = previousOpened
            return
        }
        root.receivingSession = true
        root.incomingUrl = ""
        root.manualOpenAccepted()
    }
    function createPublication(gift) {
        if (!root.controller || root.working) return
        if (!root.controller.signedIn) { root.signIn(gift); return }
        if (gift && !root.saveDraft()) return
        root.pendingPublication = gift ? "gift" : "collection"
        if (!root.controller.publish(root.collectionId, gift, passphrase.text)) root.pendingPublication = ""
    }
    function signIn(saveGift) {
        if (saveGift && !root.saveDraft()) return
        root.closeReason = "account"
        root.close()
    }
    function finishWithCollection(id) { root.closeReason = "saved"; root.collectionOpened(id); root.close() }
    function suspendForNavigation() { root.closeReason = "continue" }
    function beginUpdate(row) {
        if (!row || row.revoked || row.kind !== "collection" || row.source_collection_id !== root.collectionId) return
        root.confirmation = ""
        root.confirmation = "update:" + row.share_id
        root.confirmationRevision = row.revision
        root.go("links")
    }
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
        root.closeReason = "dismiss"
        root.publicationSignedIn = !!root.controller && root.controller.signedIn
        root.publicationsReady = false
        root.refreshingLinks = false
        root.pendingPublication = ""
        root.mutationPending = false
        root.go(root.initialView)
        root.localError = ""
        root.confirmation = ""
        if (root.tab === "receive") {
            root.receivingSession = true
            if (root.incomingUrl) root.openIncoming(root.incomingUrl, root.resumePreview)
        } else { root.beginDraft(); root.refreshLinks() }
    }
    onClosed: {
        passphrase.text = ""
        receivePassphrase.text = ""
        root.confirmation = ""
        const reason = root.closeReason
        root.dismissed(reason)
    }
    Connections {
        target: root.controller
        function onChanged() {
            if (!root.controller) return
            if (root.publicationSignedIn !== root.controller.signedIn) {
                root.publicationSignedIn = root.controller.signedIn
                root.publicationsReady = false
                root.refreshingLinks = false
                root.pendingPublication = ""
            }
            if (root.controller.busy) return
            if (root.mutationPending) {
                root.mutationPending = false
                if (!root.controller.error) root.confirmation = ""
            }
            if (root.refreshingLinks) {
                root.refreshingLinks = false
                root.publicationsReady = root.controller.signedIn && !root.controller.error
            }
            if (root.pendingPublication) {
                const kind = root.pendingPublication
                root.pendingPublication = ""
                if (!root.controller.error) {
                    root.publicationsReady = true
                    if (kind === "gift") root.go("links")
                    else root.go("share")
                }
            }
        }
        function onSaved(id, alreadySaved) { root.finishWithCollection(id) }
        function onKept(id) { root.finishWithCollection(id) }
        function onCopied(id) { root.finishWithCollection(id) }
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
        onAccepted: root.openManual(selectedFile, "", true)
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
        id: frame
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
                text: root.tab === "share" ? qsTr("Share") : root.tab === "publish" ? qsTr("Make a gift") : root.tab === "receive" ? qsTr("Open a collection") : root.tab === "settings" ? qsTr("Link settings") : qsTr("My shared links")
                textFormat: Text.PlainText
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
        CollectionActionButton {
            visible: root.tab !== root.initialView
            text: qsTr("Back")
            emphasis: "ghost"
            enabled: !root.working
            onClicked: root.go(root.tab === "settings" ? root.settingsReturn : root.initialView)
        }
        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.localError || (root.controller ? root.controller.error : "") || root.feedbackMessage
                || (root.controller ? root.controller.message : qsTr("Sharing is unavailable."))
            textFormat: Text.PlainText
            color: root.localError || (root.controller && root.controller.error) ? "#efb6a9" : theme.gold
            font.pixelSize: 13
            wrapMode: Text.Wrap
            Accessible.role: Accessible.AlertMessage
        }
        LabelText { visible: root.working; text: qsTr("Working…") }
        Flickable {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: body.implicitHeight
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
                    visible: root.tab === "settings"
                    Layout.fillWidth: true
                    spacing: 12
                    LabelText { text: qsTr("Optional link passphrase"); color: theme.ink }
                    Field { id: passphrase; objectName: "publishPassphrase"; placeholderText: qsTr("Leave empty for anyone with the link"); maximumLength: 128; echoMode: TextInput.Password; Accessible.name: qsTr("Publication passphrase") }
                    LabelText { text: qsTr("Used when creating a collection or gift link. Existing links change only through Publish changes.") }
                    CollectionActionButton { Layout.fillWidth: true; visible: !!root.ordinaryLink; text: qsTr("Change protection / publish changes"); enabled: !root.working; onClicked: root.beginUpdate(root.ordinaryLink) }
                    CollectionActionButton { objectName: "finishLinkSettings"; text: qsTr("Done"); onClicked: root.go(root.settingsReturn) }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.tab === "share" && !!root.collection && !root.collection.keptGift
                    spacing: 14
                    LabelText { text: root.collection ? root.collection.name : ""; color: theme.ink; font.pixelSize: 18 }
                    LabelText { text: qsTr("Your collection stays private until you create a link. Later edits need Publish changes.") }
                    Field { visible: !!root.ordinaryLink; readOnly: true; text: root.ordinaryLink ? root.controller.linkFor(root.ordinaryLink.share_id) : ""; Accessible.name: qsTr("Shareable collection link") }
                    CollectionActionButton {
                        objectName: "publishOrdinaryCollection"
                        Layout.fillWidth: true
                        text: !root.controller || !root.controller.signedIn ? qsTr("Sign in to create link") : root.ordinaryLink ? qsTr("Copy link") : qsTr("Create link")
                        emphasis: "primary"
                        enabled: !!root.controller && !root.working && (!root.controller.signedIn || root.publicationsReady)
                        onClicked: if (root.ordinaryLink) root.controller.copyLink(root.ordinaryLink.share_id); else root.createPublication(false)
                    }
                    CollectionActionButton { visible: !!root.controller && root.controller.signedIn && !root.publicationsReady; text: qsTr("Reload shared links"); enabled: !root.working; onClicked: root.refreshLinks() }
                    CollectionActionButton { visible: !!root.ordinaryLink; text: qsTr("Publish changes"); enabled: !root.working; onClicked: root.beginUpdate(root.ordinaryLink) }
                    Flow {
                        Layout.fillWidth: true; spacing: 8
                        CollectionActionButton { objectName: "collectionLinkSettings"; text: qsTr("Link settings"); onClicked: { root.settingsReturn = root.tab; root.go("settings") } }
                        CollectionActionButton { objectName: "makeCollectionGift"; text: qsTr("Make a gift"); onClicked: root.go("publish") }
                        CollectionActionButton { text: qsTr("My shared links"); onClicked: { root.go("links"); root.refreshLinks() } }
                        CollectionActionButton { objectName: "receiveCollectionEntry"; text: qsTr("Open shared collection"); onClicked: { root.receivingSession = true; root.go("receive") } }
                    }
                }
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
                        textFormat: TextEdit.PlainText
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
                    CollectionActionButton { objectName: "saveGiftDraft"; text: qsTr("Save gift draft"); enabled: !!root.controller && !root.working; onClicked: root.saveDraft() }
                    CollectionActionButton { objectName: "giftPassphraseSettings"; text: qsTr("Passphrase settings"); onClicked: { root.settingsReturn = root.tab; root.go("settings") } }
                    LabelText { visible: !root.controller || !root.controller.signedIn; text: qsTr("Sign in to your Colosseum account to publish. Drafting stays available offline.") }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !!root.controller && root.controller.retryResetAvailable
                        spacing: 10
                        LabelText { text: qsTr("The earlier request may already have created a link. Check Shared links first. Starting a new request can create another publication."); color: "#efc9a9" }
                        CollectionActionButton {
                            objectName: "requestNewPublication"
                            Layout.fillWidth: true
                            text: qsTr("Start a new publication request")
                            enabled: !root.working
                            onClicked: root.confirmation = "new-publication"
                        }
                        RowLayout {
                            visible: root.confirmation === "new-publication"
                            spacing: 8
                            CollectionActionButton { text: qsTr("Cancel"); onClicked: root.confirmation = "" }
                            CollectionActionButton {
                                objectName: "confirmNewPublication"
                                text: qsTr("Confirm new request")
                                enabled: !root.working
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
                            objectName: "publishGiftEdition"
                            text: root.controller && root.controller.signedIn ? qsTr("Publish gift edition") : qsTr("Save draft and sign in")
                            emphasis: "primary"
                            enabled: !!root.controller && !root.working
                            onClicked: root.createPublication(true)
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
                        CollectionActionButton { objectName: "openSharedCollection"; text: qsTr("Open link"); emphasis: "primary"; enabled: !!root.controller && root.controller.receivingReady && !root.working && receiveLink.text.trim().length > 0; onClicked: root.openManual(receiveLink.text, receivePassphrase.text, false) }
                        CollectionActionButton { text: qsTr("Open edition file"); enabled: !!root.controller && root.controller.receivingReady && !root.working; onClicked: editionPicker.open() }
                    }
                    LabelText { visible: !!root.received.share_id && root.received.kind !== "gift"; text: root.receivedSnapshot.name || ""; color: theme.ink; font.pixelSize: 22 }
                    CollectionGiftPresentation {
                        id: receivedGift
                        objectName: "receivedGiftPresentation"
                        Layout.fillWidth: true
                        visible: !!root.received.share_id && root.received.kind === "gift"
                        initiallyOpened: root.resumePreview
                        collectionName: root.receivedSnapshot.name || ""
                        presentation: root.receivedSnapshot.presentation || ({})
                        preview: false
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !!root.received.share_id && (root.received.kind !== "gift" || receivedGift.opened)
                        LabelText { text: root.receivedSnapshot.description || ""; visible: text.length > 0 }
                        Repeater {
                            model: root.receivedSnapshot.members || []
                            delegate: Rectangle {
                                id: receivedRow
                                required property var modelData
                                required property int index
                                readonly property var reference: root.controller ? root.controller.receivedReference(index) : ({})
                                readonly property var media: root.store.mediaForReference(reference)
                                Layout.fillWidth: true
                                implicitHeight: rowBody.implicitHeight + 24
                                color: "#0bffffff"
                                radius: 8
                                ColumnLayout {
                                    id: rowBody
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 6
                                    CollectionActionButton { Layout.fillWidth: true; text: String(receivedRow.index + 1) + ". " + receivedRow.modelData.title; enabled: receivedRow.media.available && !root.working; onClicked: root.titleRequested(receivedRow.reference) }
                                    LabelText { visible: !receivedRow.media.available; text: qsTr("This exact provider or edition is unavailable here."); font.pixelSize: 12 }
                                    LabelText { text: [receivedRow.modelData.kind, receivedRow.modelData.creator, receivedRow.modelData.year].filter(Boolean).join(" · "); font.pixelSize: 12 }
                                    LabelText { visible: !!receivedRow.modelData.note; text: receivedRow.modelData.note || "" }
                                }
                            }
                        }
                        LabelText { text: qsTr("Titles remain references. Keeping or copying does not add to Libraries, download media or change progress."); font.pixelSize: 12 }
                        CollectionActionButton { objectName: "saveReceivedCollection"; visible: root.received.kind === "collection"; text: root.receivedSaved ? qsTr("Open saved collection") : qsTr("Save"); emphasis: "primary"; enabled: !!root.controller && root.controller.receivingReady && !root.working; onClicked: root.controller.saveReceivedCollection() }
                        CollectionActionButton { objectName: "keepReceivedGift"; visible: root.received.kind === "gift"; text: qsTr("Keep Gift"); emphasis: "primary"; enabled: !!root.controller && root.controller.receivingReady && !root.working; onClicked: root.controller.keepReceivedGift() }
                    }
                }
                ColumnLayout {
                    visible: root.tab === "links"
                    Layout.fillWidth: true
                    spacing: 14
                    LabelText { text: root.controller && root.controller.signedIn ? qsTr("Revoking a link stops future access. Copies already kept cannot be recalled.") : qsTr("Sign in to manage your shared links.") }
                    CollectionActionButton { visible: !root.controller || !root.controller.signedIn; text: qsTr("Sign in"); onClicked: root.signIn(false) }
                    CollectionActionButton { text: qsTr("Refresh shared links"); enabled: !!root.controller && root.controller.signedIn && !root.working; onClicked: root.refreshLinks() }
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
                                Field { readOnly: true; text: root.controller.linkFor(linkRow.modelData.share_id); Accessible.name: qsTr("Shareable link") }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    CollectionActionButton { text: qsTr("Copy link"); enabled: !root.working && !linkRow.modelData.revoked; onClicked: root.controller.copyLink(linkRow.modelData.share_id) }
                                    CollectionActionButton {
                                        text: qsTr("Publish changes")
                                        visible: !!root.collection && !root.collection.keptGift && linkRow.modelData.kind === "collection" && linkRow.modelData.source_collection_id === root.collectionId && !linkRow.modelData.revoked
                                        enabled: !root.working
                                        onClicked: root.beginUpdate(linkRow.modelData)
                                    }
                                    CollectionActionButton {
                                        text: qsTr("Revoke link")
                                        destructive: true
                                        visible: !linkRow.modelData.revoked
                                        enabled: !root.working
                                        onClicked: { root.confirmation = "revoke:" + linkRow.modelData.share_id; root.confirmationRevision = linkRow.modelData.revision }
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    visible: root.confirmation.endsWith(":" + linkRow.modelData.share_id)
                                    LabelText { text: root.confirmation.indexOf("update:") === 0 ? qsTr("Replace this link with the current collection? Enter the current or new passphrase, or explicitly remove protection.") : qsTr("Revoke this link? Existing kept copies will remain.") }
                                    Connections {
                                        target: root
                                        function onConfirmationChanged() { updatePassphrase.text = ""; removeProtection.checked = false }
                                    }
                                    Field { id: updatePassphrase; visible: root.confirmation.indexOf("update:") === 0; placeholderText: qsTr("Passphrase for this revision"); maximumLength: 128; echoMode: TextInput.Password; Accessible.name: qsTr("New or existing passphrase") }
                                    CheckBox { id: removeProtection; visible: root.confirmation.indexOf("update:") === 0 && !!linkRow.modelData.protected; text: qsTr("Remove protection"); enabled: !root.working }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        CollectionActionButton { text: qsTr("Cancel"); onClicked: root.confirmation = "" }
                                        CollectionActionButton {
                                            text: root.confirmation.indexOf("update:") === 0 ? qsTr("Publish changes") : qsTr("Revoke link")
                                            enabled: !root.working && !linkRow.modelData.revoked
                                                && (root.confirmation.indexOf("update:") !== 0
                                                    || (linkRow.modelData.kind === "collection" && linkRow.modelData.source_collection_id === root.collectionId
                                                        && (!linkRow.modelData.protected || updatePassphrase.text.length > 0 || removeProtection.checked)))
                                            onClicked: {
                                                root.mutationPending = true
                                                const accepted = root.confirmation.indexOf("update:") === 0
                                                    ? root.controller.publishChanges(linkRow.modelData.share_id, root.confirmationRevision, root.collectionId, removeProtection.checked ? "" : updatePassphrase.text)
                                                    : root.controller.revoke(linkRow.modelData.share_id, root.confirmationRevision)
                                                if (!accepted) root.mutationPending = false
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
