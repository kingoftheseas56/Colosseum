// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: BSD-3-Clause
// Adapted from Qt Quick Nano Browser's WebToolBar.qml and BrowserWindow.qml,
// qt/qtwebengine c8e817ac60146921c9d73de2e420e1445c9cf513 (Qt 6.11).
// See NanoBrowser-LICENSE.txt. Feria supplies engine actions and Colosseum styling.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../account" as AccountUi
import ".." as Colosseum

Item {
    id: root
    implicitWidth: 162
    implicitHeight: 36
    readonly property Item overlay: Overlay.overlay
    property string currentUrl: ""
    property bool canGoBack: false
    property bool canGoForward: false
    property bool loading: false
    property bool browserReady: false
    signal backRequested()
    signal forwardRequested()
    signal reloadRequested()
    signal addressAccepted(string address)
    signal editingCancelled()
    function focusAddress() {
        addressPanel.open()
        address.forceActiveFocus()
        address.selectAll()
    }
    function hideAddress() { addressPanel.close() }
    function cancelAddressEditing() {
        if (!addressPanel.visible) return false
        addressMenu.close()
        address.text = root.currentUrl
        hideAddress()
        root.editingCancelled()
        return true
    }
    onVisibleChanged: if (!visible) hideAddress()
    Colosseum.Theme { id: theme }

    component NavigationButton: AccountUi.AccountButton {
        id: navigationControl
        variant: "link"
        implicitWidth: 36
        implicitHeight: 36
        Accessible.name: ToolTip.text
        ToolTip.visible: hovered
        ToolTip.delay: 700
        contentItem: Text {
            text: navigationControl.text
            color: navigationControl.enabled ? theme.ink : theme.inkDimmer
            opacity: navigationControl.enabled ? 1 : 0.58
            font.family: theme.ui
            font.pixelSize: 20
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
    RowLayout {
        anchors.fill: parent
        spacing: 6
        NavigationButton {
            objectName: "feriaBrowserBack"
            text: "←"
            ToolTip.text: "Back (Alt+Left)"
            enabled: root.canGoBack
            onClicked: root.backRequested()
        }
        NavigationButton {
            objectName: "feriaBrowserForward"
            text: "→"
            ToolTip.text: "Forward (Alt+Right)"
            enabled: root.canGoForward
            onClicked: root.forwardRequested()
        }
        NavigationButton {
            objectName: "feriaBrowserReload"
            text: root.loading ? "×" : "↻"
            ToolTip.text: root.loading ? "Stop loading" : "Reload (Ctrl+R / F5)"
            enabled: root.browserReady
            onClicked: root.reloadRequested()
        }
        NavigationButton {
            objectName: "feriaBrowserAddressToggle"
            ToolTip.text: "Web address (Ctrl+L)"
            variant: addressPanel.visible ? "secondary" : "link"
            Accessible.description: "Show or hide the web address"
            contentItem: Item {
                Image {
                    anchors.centerIn: parent
                    width: 20; height: 20
                    source: "../../assets/icons/link.svg"
                    sourceSize: Qt.size(20, 20)
                }
            }
            onClicked: {
                if (addressPanel.visible) root.cancelAddressEditing()
                else root.focusAddress()
            }
        }
    }
    Popup {
        id: addressPanel
        objectName: "feriaBrowserAddressPanel"
        parent: root
        x: root.overlay ? Math.min(0, root.overlay.width - root.mapToItem(root.overlay, 0, 0).x - width - 12) : 0
        y: root.height + 8
        width: root.overlay ? Math.min(520, Math.max(0, root.overlay.width - 24)) : 520
        height: 52
        padding: 8
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        onClosed: { addressMenu.close(); address.text = root.currentUrl }
        background: Rectangle {
            radius: 12
            color: theme.biblioWashTop
            border.color: theme.edge
        }
        contentItem: TextField {
            id: address
            objectName: "feriaBrowserAddress"
            focus: true
            leftPadding: 12
            rightPadding: 12
            color: theme.ink
            selectionColor: theme.gold
            selectedTextColor: "#141207"
            placeholderTextColor: theme.inkDimmer
            placeholderText: "Enter web address"
            font.family: theme.ui
            font.pixelSize: 13
            selectByMouse: true
            Accessible.name: "Web address"
            Accessible.description: "Ctrl+L to edit, Enter to open, Escape to cancel"
            Binding on text {
                when: !address.activeFocus && !addressMenu.visible
                value: root.currentUrl
                restoreMode: Binding.RestoreNone
            }
            onAccepted: root.addressAccepted(text)
            Keys.onEscapePressed: function(event) {
                root.cancelAddressEditing()
                event.accepted = true
            }
            background: Rectangle {
                radius: 10
                color: theme.glassTint
                border.width: 1
                border.color: address.activeFocus ? theme.gold : theme.edge
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onClicked: function(mouse) { addressMenu.popup(mouse.x, mouse.y) }
            }
            Menu {
                id: addressMenu
                objectName: "feriaAddressMenu"
                palette.window: theme.biblioWashTop
                palette.windowText: theme.ink
                palette.text: theme.ink
                palette.highlight: theme.glassHi
                palette.highlightedText: theme.ink
                font.family: theme.ui
                MenuItem { text: "Cut"; enabled: address.selectedText.length > 0; onTriggered: address.cut() }
                MenuItem { text: "Copy"; enabled: address.selectedText.length > 0; onTriggered: address.copy() }
                MenuItem { text: "Paste"; enabled: address.canPaste; onTriggered: address.paste() }
                MenuItem { objectName: "feriaAddressDelete"; text: "Delete"; enabled: address.selectedText.length > 0; onTriggered: address.remove(address.selectionStart, address.selectionEnd) }
                MenuSeparator {}
                MenuItem { text: "Select all"; enabled: address.text.length > 0; onTriggered: address.selectAll() }
            }
        }
    }
}
