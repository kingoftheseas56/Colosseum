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
    function focusAddress() { address.forceActiveFocus(); address.selectAll() }
    function cancelAddressEditing() {
        if (!address.activeFocus && !addressMenu.visible) return false
        addressMenu.close()
        address.text = root.currentUrl
        root.editingCancelled()
        return true
    }
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
        TextField {
            id: address
            objectName: "feriaBrowserAddress"
            Layout.fillWidth: true
            Layout.minimumWidth: 120
            Layout.preferredHeight: 36
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
