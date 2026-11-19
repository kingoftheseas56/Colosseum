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
    property real bottomInset: 24
    readonly property bool wide: width >= 950
    readonly property var filteredEntries: {
        const needle = search.text.trim().toLowerCase()
        return page.entries.map((entry, index) => ({ entry: entry, sourceIndex: index }))
            .filter(match => {
                const manifest = match.entry.manifest || {}
                const label = page.instanceLabelFor(match.sourceIndex)
                return (String(manifest.name || "") + " " + label).toLowerCase().includes(needle)
            })
    }
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
        if (index >= list.count) {
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
        search.clear()
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
        label: qsTr("Extensions")
        onTriggered: page.backRequested()
        Keys.onTabPressed: (event) => {
            search.forceActiveFocus(Qt.TabFocusReason)
            event.accepted = true
        }
    }
    Column {
        id: heading
        anchors.top: back.bottom; anchors.topMargin: 22
        anchors.left: parent.left; anchors.leftMargin: page.sideMargin
        width: page.wide ? 206 : parent.width - 2 * page.sideMargin
        spacing: 8
        Text {
            text: qsTr("Installed")
            color: theme.ink
            font.family: theme.display; font.pixelSize: page.width < 840 ? 28 : 40
        }
        Text {
            width: parent.width
            text: qsTr("%n installed", "", page.entries.length)
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
    TextField {
        id: search
        objectName: "extensionsInstalledSearch"
        anchors.left: page.wide ? heading.right : heading.left
        anchors.leftMargin: page.wide ? 30 : 0
        anchors.top: page.wide ? heading.top : heading.bottom
        anchors.topMargin: page.wide ? 0 : 16
        width: Math.min(340, page.width - x - page.sideMargin)
        height: 44
        placeholderText: qsTr("Search installed add-ons")
        color: theme.ink
        font.family: theme.ui
        font.pixelSize: 14
        background: Rectangle {
            color: "#17181e"
            radius: 10
            border.width: 1
            border.color: search.activeFocus ? theme.gold : theme.edge
        }
        Keys.onDownPressed: if (list.count) page.focusInstalledIndex(0)
    }

    ListView {
        id: list
        objectName: "extensionsManagementList"
        anchors.top: search.bottom; anchors.topMargin: 16
        anchors.bottom: parent.bottom; anchors.bottomMargin: page.bottomInset
        anchors.left: search.left
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
        model: page.visible ? page.filteredEntries : []
        ScrollBar.vertical: HouseScrollBar { flick: list; glassStyle: true }
        delegate: ExtensionsInstalledCard {
            id: managedCard
            required property var modelData
            required property int index
            property int focusRequestSerial: page.focusRequestSerial
            objectName: "extensionsManagedCard_" + index
            width: list.width
            entry: modelData.entry
            management: true
            capabilities: page.capabilitiesFor(entry.manifest)
            instanceLabel: page.instanceLabelFor(modelData.sourceIndex)
            configurable: page.configurableFor(entry)
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
                page.toggleRequested(entry)
                Qt.callLater(function() { page.focusInstalledIndex(index) })
            }
            onConfigureRequested: page.configureRequested(entry)
            onRemoveRequested: page.requestRemoval([modelData.sourceIndex])
        }
        Text {
            visible: list.count === 0
            width: list.width
            text: search.text.length ? qsTr("No matching add-ons") : qsTr("No add-ons installed")
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
                        removeDialog.close()
                        if (entry) page.removeRequested(entry)
                        Qt.callLater(function() {
                            if (!page.visible) return
                            if (list.count)
                                page.focusInstalledIndex(Math.min(list.currentIndex, list.count - 1))
                            else page.takeKeyboardFocus()
                        })
                    }
                }
            }
        }
    }
}
