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
    implicitWidth: nativePlaybackAvailable ? 184 : 146
    implicitHeight: 36
    readonly property Item overlay: Overlay.overlay
    property string currentUrl: ""
    property bool canGoBack: false
    property bool canGoForward: false
    property bool loading: false
    property bool browserReady: false
    property bool nativePlaybackAvailable: false
    signal nativePlaybackRequested(string address)
    signal backRequested()
    signal forwardRequested()
    signal reloadRequested()
    signal addressAccepted(string address)
    signal editingCancelled()
    signal addressEditingStarted()
    signal findRequested()
    function focusAddress() {
        addressEditingStarted()
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

    RowLayout {
        anchors.fill: parent
        spacing: 6
        FeriaPageToolButton {
            objectName: "feriaBrowserBack"
            iconSource: "../../assets/icons/lucide/arrow-left.svg"
            helpText: "Back (Alt+Left)"
            enabled: root.canGoBack
            onClicked: root.backRequested()
        }
        FeriaPageToolButton {
            objectName: "feriaBrowserForward"
            iconSource: "../../assets/icons/lucide/arrow-left.svg"; iconRotation: 180
            helpText: "Forward (Alt+Right)"
            enabled: root.canGoForward
            onClicked: root.forwardRequested()
        }
        FeriaPageToolButton {
            objectName: "feriaBrowserReload"
            iconSource: root.loading ? "../../assets/icons/reader2/close.svg" : "../../assets/icons/lucide/rotate-cw.svg"
            helpText: root.loading ? "Stop loading" : "Reload (Ctrl+R / F5)"
            enabled: root.browserReady
            onClicked: root.reloadRequested()
        }
        FeriaPageToolButton {
            objectName: "feriaBrowserAddressToggle"
            iconSource: "../../assets/icons/link.svg"
            helpText: "Web address (Ctrl+L)"
            variant: addressPanel.visible ? "secondary" : "link"
            Accessible.description: "Show or hide the web address"
            onClicked: {
                if (addressPanel.visible) root.cancelAddressEditing()
                else root.focusAddress()
            }
        }
        FeriaPageToolButton {
            objectName: "feriaPlayInFeria"
            visible: root.nativePlaybackAvailable
            enabled: root.browserReady
            iconSource: "../../assets/icons/lucide/play.svg"
            helpText: "Play video in Feria"
            onClicked: root.nativePlaybackRequested("")
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
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_F && (event.modifiers & Qt.ControlModifier)) {
                    root.findRequested()
                    event.accepted = true
                }
            }
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
                MenuSeparator { visible: root.nativePlaybackAvailable }
                MenuItem {
                    text: "Play URL in Feria"
                    visible: root.nativePlaybackAvailable
                    enabled: address.text.length > 0
                    onTriggered: { root.nativePlaybackRequested(address.text); root.hideAddress() }
                }
            }
        }
    }
}
