pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root
    property bool shown: false
    property var entry: null
    property var actions: null
    property var syncState: null
    signal closeRequested()
    visible: shown
    enabled: visible

    function remove(alsoStremio) {
        if (!entry || !entry.id || !actions)
            return
        if (actions.removeTheatreItem(String(entry.id), String(entry.type || "movie"), alsoStremio))
            closeRequested()
    }

    Rectangle { anchors.fill: parent; color: Qt.rgba(0.01, 0.015, 0.025, 0.78) }
    MouseArea { anchors.fill: parent }
    Keys.onEscapePressed: root.closeRequested()

    Theme { id: theme }
    Rectangle {
        id: card
        objectName: "theatreRemovalDialog"
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 48)
        height: 278
        radius: 20
        color: "#171a22"
        border.width: 1
        border.color: theme.edge
        focus: root.visible

        Column {
            anchors.fill: parent
            anchors.margins: 30
            spacing: 16
            Text {
                width: parent.width
                text: qsTr("Remove from your library?")
                color: theme.ink
                font.family: theme.display
                font.pixelSize: 28
            }
            Text {
                width: parent.width
                text: root.entry && root.entry.title ? String(root.entry.title) : qsTr("This title")
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 14
                elide: Text.ElideRight
            }
            Action {
                objectName: "removeFromColosseumAction"
                label: qsTr("Remove from Colosseum")
                onTriggered: root.remove(false)
            }
            Action {
                objectName: "removeFromColosseumAndStremioAction"
                label: qsTr("Remove from Colosseum & Stremio")
                visible: root.syncState && root.syncState.linkedAccount
                onTriggered: root.remove(true)
            }
            Action {
                objectName: "cancelTheatreRemovalAction"
                label: qsTr("Cancel")
                quiet: true
                onTriggered: root.closeRequested()
            }
        }
    }

    component Action: Rectangle {
        id: action
        property string label: ""
        property bool quiet: false
        signal triggered()
        width: parent ? parent.width : 0
        height: 42
        radius: 10
        color: input.interactionActive ? Qt.rgba(1, 1, 1, 0.12)
                                       : (quiet ? "transparent" : Qt.rgba(1, 1, 1, 0.06))
        border.width: quiet ? 0 : 1
        border.color: theme.edge
        Text {
            anchors.centerIn: parent
            text: action.label
            color: action.quiet ? theme.inkDim : theme.ink
            font.family: theme.ui
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        KeyboardAction {
            id: input
            anchors.fill: parent
            accessibleName: action.label
            focusRadius: action.radius
            onTriggered: action.triggered()
        }
    }
}
