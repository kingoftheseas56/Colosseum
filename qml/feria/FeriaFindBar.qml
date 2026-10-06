// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: BSD-3-Clause
// Adapted from Qt Quick Nano Browser FindBar.qml at
// c8e817ac60146921c9d73de2e420e1445c9cf513. See NanoBrowser-LICENSE.txt.
// Colosseum owns its glass material, focus behavior and responsive presentation.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".." as Colosseum

Popup {
    id: panel
    objectName: "feriaFindPanel"
    property var tools: null
    required property Item backdrop
    readonly property alias input: findInput
    width: 382
    height: 50
    padding: 8
    modal: false
    focus: true
    closePolicy: Popup.NoAutoClose
    visible: tools && tools.active && tools.findOpen
    onClosed: if (tools) tools.finishDismissal()
    onOpened: { findInput.forceActiveFocus(); findInput.selectAll() }
    Shortcut {
        sequence: "Escape"; context: Qt.WindowShortcut
        enabled: panel.visible
        onActivated: if (panel.tools) panel.tools.closeFind(true)
    }
    Colosseum.Theme { id: theme }
    background: Colosseum.Glass {
        backdrop: panel.backdrop
        contentBackdrop: panel.tools ? panel.tools.browser : null
        contentTrack: panel.tools && panel.tools.browser ? panel.tools.browser.scrollPosition.y : 0
        track: panel.x + panel.y + panel.width + panel.height
        radius: 16; tint: 0.10; scrim: 0.70
    }
    Connections {
        target: panel.tools
        function onFocusFindRequested() {
            if (panel.visible) { findInput.forceActiveFocus(); findInput.selectAll() }
        }
    }
    contentItem: RowLayout {
        spacing: 5
        TextField {
            id: findInput
            objectName: "feriaFindInput"
            Layout.fillWidth: true
            Layout.minimumWidth: 75
            Layout.preferredHeight: 34
            text: panel.tools ? panel.tools.query : ""
            placeholderText: "Find in page"
            placeholderTextColor: theme.inkDimmer
            color: theme.ink; selectionColor: theme.gold; selectedTextColor: theme.biblioWashBottom
            font.family: theme.ui; font.pixelSize: 13
            leftPadding: 9; rightPadding: text.length ? 30 : 9
            selectByMouse: true
            Accessible.name: "Find in page"
            Accessible.description: "Enter or F3 for next match. Shift+Enter for previous. Escape closes."
            onTextEdited: if (!inputMethodComposing && panel.tools) panel.tools.search(text)
            onInputMethodComposingChanged: if (!inputMethodComposing && activeFocus && panel.tools) panel.tools.search(text)
            Keys.onPressed: function(event) {
                if (!panel.tools || inputMethodComposing) return
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    if (event.modifiers & Qt.ShiftModifier) panel.tools.findPrevious()
                    else panel.tools.findNext()
                    event.accepted = true
                } else if (event.key === Qt.Key_Escape) {
                    panel.tools.closeFind(true)
                    event.accepted = true
                }
            }
            background: Rectangle {
                radius: 8; color: theme.glassTint
                border.color: findInput.activeFocus ? theme.gold : theme.edge
            }
            FeriaPageToolButton {
                objectName: "feriaFindClear"
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                width: 28; height: 28
                visible: findInput.text.length > 0
                iconSource: "../../assets/icons/reader2/close.svg"
                helpText: "Clear search"
                onClicked: { panel.tools.search(""); findInput.forceActiveFocus() }
            }
        }
        Text {
            objectName: "feriaFindCount"
            Layout.preferredWidth: 74
            horizontalAlignment: Text.AlignHCenter
            text: !panel.tools || !panel.tools.query.length ? ""
                : panel.tools.pending ? "Searching…"
                : !panel.tools.hasResults ? ""
                : panel.tools.numberOfMatches === 0 ? "No matches"
                : panel.tools.activeMatch + " / " + panel.tools.numberOfMatches
            color: panel.tools && panel.tools.query.length && !panel.tools.pending && !panel.tools.numberOfMatches
                ? theme.gold : theme.inkDim
            font.family: theme.ui; font.pixelSize: 12
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        FeriaPageToolButton {
            objectName: "feriaFindPrevious"
            iconSource: "../../assets/icons/reader2/chevron-left.svg"; iconRotation: 90
            helpText: "Previous match (Shift+F3)"
            enabled: panel.tools && panel.tools.numberOfMatches > 0 && !panel.tools.pending
            onClicked: panel.tools.findPrevious()
        }
        FeriaPageToolButton {
            objectName: "feriaFindNext"
            iconSource: "../../assets/icons/reader2/chevron-right.svg"; iconRotation: 90
            helpText: "Next match (F3)"
            enabled: panel.tools && panel.tools.numberOfMatches > 0 && !panel.tools.pending
            onClicked: panel.tools.findNext()
        }
        FeriaPageToolButton {
            objectName: "feriaFindClose"
            iconSource: "../../assets/icons/reader2/close.svg"
            helpText: "Close Find (Escape)"
            onClicked: panel.tools.closeFind(true)
        }
    }
}
