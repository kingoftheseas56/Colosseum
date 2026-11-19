pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: page
    required property var entries
    required property var capabilitiesFor
    required property var instanceLabelFor
    required property var configurableFor
    property int initialIndex: 0
    property string actionError: ""
    property var removalEntry: null
    property var removalChoices: []
    property string removalInstanceLabel: ""
    property int removalIndex: 0
    property int focusRequestIndex: -1
    property int focusRequestSerial: 0
    signal backRequested()
    signal toggleRequested(var entry)
    signal configureRequested(var entry)
    signal removeRequested(var entry)
    Theme { id: theme }
    readonly property int sideMargin: width < 840 ? 16 : theme.margin
    function takeKeyboardFocus() { back.focusControl() }
    function focusInstalledIndex(index) {
        if (index >= page.entries.length) {
            page.takeKeyboardFocus()
            return
        }
        if (index < 0) return
        list.currentIndex = index
        list.positionViewAtIndex(index, ListView.Contain)
        page.focusRequestIndex = index
        page.focusRequestSerial++
    }
    function focusCurrentIndex() { page.focusInstalledIndex(list.currentIndex) }
    function dismissRemoval() {
        if (!removeDialog.opened) return false
        removeDialog.close()
        return true
    }
    function selectRemoval(index) {
        page.removalEntry = page.entries[index]
        page.removalInstanceLabel = page.instanceLabelFor(index)
        page.removalIndex = index
    }
    function requestRemoval(indices) {
        if (!indices.length) return
        page.removalChoices = indices.length > 1 ? indices : []
        page.removalEntry = null
        page.removalInstanceLabel = ""
        if (indices.length === 1) page.selectRemoval(indices[0])
        removeDialog.open()
    }
    function requestEscape() {
        if (page.dismissRemoval()) return true
        page.backRequested()
        return true
    }
    onEntriesChanged: {
        // Never retain a destructive target across native/profile changes.
        removeDialog.close()
        page.removalEntry = null
        page.removalChoices = []
        page.removalInstanceLabel = ""
        if (page.entries.length && page.initialIndex >= page.entries.length)
            page.initialIndex = page.entries.length - 1
    }
    onVisibleChanged: if (visible) {
        Qt.callLater(function() {
            if (page.entries.length) {
                list.currentIndex = Math.max(0, Math.min(page.initialIndex, page.entries.length - 1))
                list.positionViewAtIndex(list.currentIndex, ListView.Beginning)
            } else list.currentIndex = -1
            page.takeKeyboardFocus()
        })
    } else removeDialog.close()
    Keys.onEscapePressed: page.requestEscape()
    MouseArea { anchors.fill: parent }
    BackAction {
        id: back
        anchors.left: parent.left; anchors.leftMargin: page.sideMargin
        anchors.top: parent.top; anchors.topMargin: 44
        label: qsTr("Store")
        onTriggered: page.backRequested()
        Keys.onTabPressed: (event) => {
            if (page.entries.length) {
                page.focusInstalledIndex(Math.max(0, Math.min(page.initialIndex, page.entries.length - 1)))
                event.accepted = true
            }
        }
    }
    Column {
        id: heading
        anchors.top: back.bottom; anchors.topMargin: 22
        anchors.left: parent.left; anchors.leftMargin: page.sideMargin
        anchors.right: parent.right; anchors.rightMargin: page.sideMargin
        spacing: 8
        Text {
            text: qsTr("Your Add-ons")
            color: theme.ink
            font.family: theme.display; font.pixelSize: page.width < 840 ? 28 : 40
        }
        Text {
            width: parent.width
            text: qsTr("%1 installed · Includes disabled add-ons. Core add-ons stay on.").arg(page.entries.length)
            color: theme.inkDim
            wrapMode: Text.WordWrap
            font.family: theme.ui; font.pixelSize: 14
        }
        Text {
            width: parent.width
            visible: page.actionError.length > 0
            text: page.actionError
            color: "#ffb4ab"
            wrapMode: Text.WordWrap
            font.family: theme.ui; font.pixelSize: 13
        }
    }
    ListView {
        id: list
        objectName: "extensionsManagementList"
        anchors.top: heading.bottom; anchors.topMargin: 24
        anchors.bottom: parent.bottom; anchors.bottomMargin: 24
        anchors.left: parent.left; anchors.leftMargin: page.sideMargin
        anchors.right: parent.right; anchors.rightMargin: page.sideMargin
        spacing: 12
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        keyNavigationEnabled: true
        Accessible.name: qsTr("Installed add-ons")
        Accessible.role: Accessible.List
        Keys.onTabPressed: (event) => {
            page.focusCurrentIndex()
            event.accepted = true
        }
        Keys.onReturnPressed: page.focusCurrentIndex()
        Keys.onSpacePressed: page.focusCurrentIndex()
        model: page.visible ? page.entries : []
        ScrollBar.vertical: HouseScrollBar { flick: list; glassStyle: true }
        delegate: ExtensionsInstalledCard {
            id: managedCard
            required property var modelData
            required property int index
            property int focusRequestSerial: page.focusRequestSerial
            objectName: "extensionsManagedCard_" + index
            width: list.width
            entry: modelData
            management: true
            capabilities: page.capabilitiesFor(modelData.manifest)
            instanceLabel: page.instanceLabelFor(index)
            configurable: page.configurableFor(modelData)
            function applyFocusRequest() {
                if (page.focusRequestIndex === index && !managedCard.focusFirstAction())
                    Qt.callLater(function() { page.focusInstalledIndex(index + 1) })
            }
            onFocusRequestSerialChanged: managedCard.applyFocusRequest()
            Component.onCompleted: managedCard.applyFocusRequest()
            onKeyboardFocusReceived: {
                list.currentIndex = index
                list.positionViewAtIndex(index, ListView.Contain)
            }
            onTabForwardRequested: page.focusInstalledIndex(index + 1)
            onToggleRequested: {
                page.toggleRequested(modelData)
                Qt.callLater(function() { page.focusInstalledIndex(index) })
            }
            onConfigureRequested: page.configureRequested(modelData)
            onRemoveRequested: page.requestRemoval([index])
        }
        Text {
            visible: page.entries.length === 0
            width: list.width
            text: qsTr("No add-ons installed. Return to the Store to add one.")
            wrapMode: Text.WordWrap
            color: theme.inkDim
            font.family: theme.ui; font.pixelSize: 14
        }
    }
    Dialog {
        id: removeDialog
        objectName: "extensionsRemoveDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, page.width - 32)
        padding: 24
        modal: true
        popupType: Popup.Item
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { radius: 18; color: "#141820"; border.color: theme.edge }
        onOpened: cancelRemoval.forceActiveFocus()
        onClosed: if (page.visible) page.takeKeyboardFocus()
        contentItem: Column {
            spacing: 18
            Text {
                width: parent.width
                text: page.removalEntry ? qsTr("Uninstall %1?").arg(String(page.removalEntry.manifest.name))
                      : qsTr("Choose an installation")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: theme.ink
                font.family: theme.display; font.pixelSize: 24
            }
            ListView {
                id: removalChoice
                visible: page.removalChoices.length > 0
                width: parent.width
                height: Math.min(176, count * 44)
                clip: true
                model: page.removalChoices
                ScrollBar.vertical: HouseScrollBar { flick: removalChoice; glassStyle: true }
                delegate: KeyboardAction {
                    id: choice
                    required property int modelData
                    readonly property bool selected: page.removalEntry !== null
                                                     && page.removalIndex === modelData
                    width: removalChoice.width
                    height: 44
                    accessibleName: caption.text
                    onTriggered: page.selectRemoval(modelData)
                    onActiveFocusChanged: if (activeFocus) removalChoice.positionViewAtIndex(
                        page.removalChoices.indexOf(modelData), ListView.Contain)
                    Text {
                        id: caption
                        anchors.verticalCenter: parent.verticalCenter
                        leftPadding: 4
                        text: page.instanceLabelFor(choice.modelData)
                        color: choice.selected || choice.interactionActive ? theme.gold : theme.ink
                        font.family: theme.ui
                        font.pixelSize: 15
                    }
                }
            }
            Text {
                width: parent.width
                visible: !removalChoice.visible && page.removalInstanceLabel.length > 0
                text: page.removalInstanceLabel
                color: theme.gold
                font.family: theme.ui; font.pixelSize: 13
            }
            Text {
                width: parent.width
                text: qsTr("This removes only this installation and its saved configuration. You can add it again from the Store.")
                wrapMode: Text.WordWrap
                color: theme.inkDim
                font.family: theme.ui; font.pixelSize: 14
            }
            Row {
                spacing: 10
                ExtensionsActionButton {
                    id: cancelRemoval
                    text: qsTr("Cancel")
                    onClicked: removeDialog.close()
                }
                ExtensionsActionButton {
                    objectName: "extensionsConfirmRemoval"
                    text: qsTr("Uninstall")
                    enabled: page.removalEntry !== null
                    destructive: true
                    onClicked: {
                        var entry = page.removalEntry
                        var index = page.removalIndex
                        removeDialog.close()
                        if (entry) page.removeRequested(entry)
                        Qt.callLater(function() {
                            if (!page.visible) return
                            if (page.entries.length)
                                page.focusInstalledIndex(Math.min(index, page.entries.length - 1))
                            else page.takeKeyboardFocus()
                        })
                    }
                }
            }
        }
    }
}
