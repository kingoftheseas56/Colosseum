pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

Item {
    id: panel
    objectName: "collectionsPanel"
    required property var nativeStore
    property var publicationController: null
    property bool sharingActive: false
    property var openedGiftIds: ({})
    property bool showing: false
    property int bottomInset: 96
    property string collectionId: ""
    property bool pickerActive: false
    property bool deleteActive: false
    property bool pickerCreateOnly: false
    property var pickerReference: ({})
    property var pickerOpener: null
    property var deleteOpener: null
    property string pendingDestination: ""
    property string message: ""
    property int profileGeneration: 0
    property bool initialized: false
    readonly property bool pendingChanges: !!(detailPage() && detailPage().editing)
    signal closed()
    signal navigationCanceled()
    signal openReferenceRequested(var reference)
    visible: showing || pickerActive || deleteActive || sharingActive || message.length > 0
    focus: showing
    Theme { id: theme }
    CollectionsAdapter { id: adapter; nativeStore: panel.nativeStore; activeCollectionId: panel.collectionId }

    function detailPage() {
        return pageLoader.status === Loader.Ready && pageLoader.item
            && pageLoader.item.objectName === "collectionPage" ? pageLoader.item : null
    }
    function showOverview() {
        if (panel.pendingChanges) {
            panel.pendingDestination = "overview"
            panel.detailPage().requestBack()
            return
        }
        panel.collectionId = ""
        panel.showing = true
        panel.forceActiveFocus(Qt.OtherFocusReason)
    }
    function finishClose() {
        panel.pickerOpener = null
        panel.deleteOpener = null
        if (pickerLoader.status === Loader.Ready) pickerLoader.item.close()
        if (deleteLoader.status === Loader.Ready) deleteLoader.item.close()
        panel.pendingDestination = ""
        panel.showing = false
        panel.collectionId = ""
        panel.closed()
    }
    function requestClose() {
        if (!panel.showing && !panel.pickerActive && !panel.deleteActive) return true
        if (panel.pendingChanges) {
            panel.pendingDestination = "close"
            panel.detailPage().requestBack()
            return false
        }
        panel.finishClose()
        return true
    }
    function requestEscape() {
        if (sharingLoader.status === Loader.Ready) { sharingLoader.item.close(); return true }
        if (pickerLoader.status === Loader.Ready) { pickerLoader.item.close(); return true }
        if (deleteLoader.status === Loader.Ready) { deleteLoader.item.close(); return true }
        if (!panel.showing) return false
        const detail = panel.detailPage()
        if (detail && detail.leaving) {
            detail.leaving = false
            detail.reloading = false
            panel.pendingDestination = ""
            panel.navigationCanceled()
        } else if (detail) detail.requestBack()
        else panel.finishClose()
        return true
    }
    function openPicker(reference, opener) {
        if (!adapter.healthy) { panel.showMessage(adapter.error); return false }
        if (!reference || !reference.key || panel.pickerActive) return false
        panel.pickerReference = reference
        adapter.transientReference = reference
        panel.pickerCreateOnly = false
        panel.pickerOpener = opener || null
        panel.pickerActive = true
        return true
    }
    function createCollection(opener) {
        if (!adapter.healthy || panel.pickerActive) return
        panel.pickerReference = ({})
        adapter.transientReference = ({})
        panel.pickerCreateOnly = true
        panel.pickerOpener = opener
        panel.pickerActive = true
    }
    function restorePickerFocus() {
        const opener = panel.pickerOpener
        const generation = panel.profileGeneration
        panel.pickerActive = false
        panel.pickerOpener = null
        adapter.transientReference = ({})
        Qt.callLater(function() {
            if (generation === panel.profileGeneration && opener && opener.visible && opener.enabled)
                opener.forceActiveFocus(Qt.OtherFocusReason)
        })
    }
    function showMessage(value) {
        messageTimer.stop()
        panel.message = String(value || "")
    }
    function notify(value) {
        panel.message = value
        messageTimer.restart()
    }
    function profileChanged() {
        panel.profileGeneration++
        panel.openedGiftIds = ({})
        panel.sharingActive = false
        const detail = panel.detailPage()
        if (detail) detail.abortEdit()
        panel.pickerOpener = null
        panel.deleteOpener = null
        panel.pickerActive = false
        panel.deleteActive = false
        panel.pickerReference = ({})
        adapter.transientReference = ({})
        panel.pendingDestination = ""
        panel.collectionId = ""
        panel.showing = false
        panel.message = ""
        panel.navigationCanceled()
    }
    onNativeStoreChanged: if (initialized) profileChanged()
    Component.onCompleted: initialized = true
    Keys.priority: Keys.AfterItem
    Keys.onEscapePressed: panel.requestEscape()
    Timer { id: messageTimer; interval: 4200; onTriggered: panel.message = "" }

    Rectangle {
        anchors.fill: parent
        visible: panel.showing
        gradient: Gradient {
            GradientStop { position: 0; color: theme.biblioWashTop }
            GradientStop { position: 1; color: theme.biblioWashBottom }
        }
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: function(wheel) { wheel.accepted = true } }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 56
        anchors.bottomMargin: panel.bottomInset
        spacing: 10
        visible: panel.showing
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            visible: !adapter.healthy
            implicitHeight: errorBody.implicitHeight + 24
            radius: 10
            color: "#241e1a"
            border.color: "#66563d"
            RowLayout {
                id: errorBody
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12
                Text {
                    Layout.fillWidth: true
                    text: adapter.error || qsTr("Your collections could not be opened.")
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 14
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    textFormat: Text.PlainText
                    Accessible.role: Accessible.AlertMessage
                }
                CollectionActionButton { text: qsTr("Retry"); onClicked: adapter.reload() }
            }
        }
        Loader {
            id: pageLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: panel.showing
            sourceComponent: panel.collectionId ? detailComponent : overviewComponent
        }
    }
    CollectionActionButton {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 16
        visible: panel.showing
        objectName: "closeCollectionsButton"
        iconName: "close"
        compact: true
        emphasis: "ghost"
        Accessible.name: qsTr("Close Collections")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Close Collections")
        onClicked: panel.requestClose()
    }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(12, panel.height - panel.bottomInset - height - 8)
        width: Math.min(panel.width - 32, 640)
        height: messageRow.implicitHeight + 20
        visible: panel.message.length > 0
        radius: 12
        color: "#f01b202c"
        border.color: theme.edge
        RowLayout {
            id: messageRow
            anchors.fill: parent
            anchors.margins: 10
            Text {
                Layout.fillWidth: true
                text: panel.message
                textFormat: Text.PlainText
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 14
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                Accessible.role: Accessible.AlertMessage
            }
            CollectionActionButton { iconName: "close"; compact: true; emphasis: "ghost"; Accessible.name: qsTr("Dismiss message"); onClicked: panel.message = "" }
        }
    }
    Component {
        id: overviewComponent
        YourCollectionsPage {
            store: adapter
            sharingAvailable: !!panel.publicationController
            onSharingRequested: panel.sharingActive = true
            onCreateRequested: function(opener) { panel.createCollection(opener) }
            onCollectionRequested: function(id) { panel.collectionId = id }
        }
    }
    Component {
        id: detailComponent
        CollectionPage {
            store: adapter
            collectionId: panel.collectionId
            sharingAvailable: !!panel.publicationController
            onSharingRequested: panel.sharingActive = true
            onCopyGiftRequested: function(name) { panel.publicationController.makeOwnVersion(panel.collectionId, name) }
            Component.onCompleted: if (panel.openedGiftIds[panel.collectionId]) giftOpened = true
            onGiftOpenedChanged: {
                if (giftOpened && panel.collectionId) {
                    const opened = Object.assign({}, panel.openedGiftIds)
                    opened[panel.collectionId] = true
                    panel.openedGiftIds = opened
                }
            }
            onBackRequested: {
                if (panel.pendingDestination === "close") panel.finishClose()
                else { panel.pendingDestination = ""; panel.collectionId = "" }
            }
            onNavigationCanceled: { panel.pendingDestination = ""; panel.navigationCanceled() }
            onMessageRequested: function(message) { panel.notify(message) }
            onTitleRequested: function(key) {
                const reference = adapter.reference(key)
                if (reference) panel.openReferenceRequested(reference)
                else panel.showMessage(qsTr("This title reference is unavailable. You can keep it here and try again later."))
            }
            onDeleteRequested: function(opener) { panel.deleteOpener = opener; panel.deleteActive = true }
        }
    }
    Connections {
        target: panel.publicationController
        function onChanged() {
            if (!panel.sharingActive && panel.publicationController && panel.publicationController.error)
                panel.showMessage(panel.publicationController.error)
        }
        function onCopied(id) { panel.collectionId = id; panel.showing = true; panel.notify(qsTr("Your own editable version is ready.")) }
    }
    Loader {
        id: sharingLoader
        active: panel.sharingActive
        sourceComponent: Component {
            CollectionsSharingPopup {
                store: adapter
                controller: panel.publicationController
                collectionId: panel.collectionId
                onDismissed: panel.sharingActive = false
                onCollectionOpened: function(id) { panel.collectionId = id; panel.showing = true }
            }
        }
        onLoaded: if (item) item.open()
    }
    Loader {
        id: pickerLoader
        active: panel.pickerActive
        sourceComponent: Component {
            AddToCollectionsPopup {
                store: adapter
                reference: panel.pickerReference
                createOnly: panel.pickerCreateOnly
                onCompleted: function(message) { panel.notify(message) }
                onDismissed: panel.restorePickerFocus()
            }
        }
        onLoaded: if (item) item.open()
    }
    Loader {
        id: deleteLoader
        active: panel.deleteActive
        sourceComponent: Component {
            DeleteCollectionPopup {
                store: adapter
                collectionId: panel.collectionId
                collectionName: adapter.getCollection(panel.collectionId) ? adapter.getCollection(panel.collectionId).name : ""
                onDeleted: { panel.collectionId = ""; panel.notify(qsTr("Collection deleted")) }
                onDismissed: {
                    const opener = panel.deleteOpener
                    panel.deleteActive = false
                    panel.deleteOpener = null
                    Qt.callLater(function() { if (opener && opener.visible && opener.enabled) opener.forceActiveFocus(Qt.OtherFocusReason) })
                }
            }
        }
        onLoaded: if (item) item.open()
    }
}
