// Shared identity ceremony surface for launch sessions and Vault rows. It is seedable so
// store decisions stay testable without booting the whole shell; the owning facade supplies
// the ceremony and records the choice in VaultIdentity.
import QtQuick
import QtQuick.Controls

Popup {
    id: dialog
    objectName: "vaultIdentityCeremonyDialog"
    modal: true
    focus: true
    // The owning shell/page must clear its pending identity state when this closes.
    // Escape therefore routes through the owner instead of Popup auto-close silently
    // leaving pendingIdentityRoute / identityCeremonyDismissed out of sync.
    closePolicy: Popup.NoAutoClose
    width: 520
    height: 250

    property var ceremony: ({})
    readonly property bool copyCeremony: String(ceremony.type || "") === "likely-copy"
    property alias sameMediaButton: sameMediaButtonRect
    property alias newMediaButton: newMediaButtonRect
    property alias useExistingStateButton: useExistingStateRect
    property alias separateCopyButton: separateCopyRect
    signal choiceMade(string relationship, string choice)
    signal cancelRequested()

    function openCeremony(value) {
        if (value !== undefined) dialog.ceremony = value || ({})
        dialog.open()
    }
    function choose(choice) {
        dialog.choiceMade(String(dialog.ceremony.relationship || ""), choice)
        dialog.close()
    }

    background: Rectangle {
        radius: 16
        color: Qt.rgba(0.055, 0.065, 0.09, 0.99)
        border.width: 1
        border.color: theme.edge
    }
    Theme { id: theme }

    contentItem: Column {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 10
        Text {
            text: dialog.copyCeremony ? "This looks like a copy" : "The media at this path changed"
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 23
        }
        Text {
            width: parent.width
            text: dialog.copyCeremony
                  ? "Choose whether the copy shares the existing progress state."
                  : "Is this still the same media, or should it start with new state?"
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            text: (dialog.ceremony.oldPath || "") + (dialog.ceremony.newPath && dialog.ceremony.newPath !== dialog.ceremony.oldPath
                  ? "  →  " + dialog.ceremony.newPath : "")
            color: theme.gold
            font.family: theme.ui
            font.pixelSize: 11
            elide: Text.ElideMiddle
        }
        Item { width: 1; height: 10 }
        Row {
            spacing: 10
            anchors.right: parent.right
            Rectangle {
                id: sameMediaButtonRect
                objectName: "vaultSameMedia"
                visible: !dialog.copyCeremony
                width: sameMediaText.implicitWidth + 24; height: 38; radius: 9
                color: theme.gold
                Text { id: sameMediaText; anchors.centerIn: parent; text: "Same Media"; color: "#151310"; font.pixelSize: 12; font.weight: Font.DemiBold }
                MouseArea { anchors.fill: parent; onClicked: dialog.choose("same-media") }
            }
            Rectangle {
                id: newMediaButtonRect
                objectName: "vaultNewMedia"
                visible: !dialog.copyCeremony
                width: newMediaText.implicitWidth + 24; height: 38; radius: 9
                color: Qt.rgba(1, 1, 1, 0.08); border.width: 1; border.color: theme.edge
                Text { id: newMediaText; anchors.centerIn: parent; text: "New Media"; color: theme.ink; font.pixelSize: 12 }
                MouseArea { anchors.fill: parent; onClicked: dialog.choose("new-media") }
            }
            Rectangle {
                id: useExistingStateRect
                objectName: "vaultUseExistingState"
                visible: dialog.copyCeremony
                width: existingText.implicitWidth + 24; height: 38; radius: 9
                color: theme.gold
                Text { id: existingText; anchors.centerIn: parent; text: "Use Existing State"; color: "#151310"; font.pixelSize: 12; font.weight: Font.DemiBold }
                MouseArea { anchors.fill: parent; onClicked: dialog.choose("use-existing-state") }
            }
            Rectangle {
                id: separateCopyRect
                objectName: "vaultSeparateCopy"
                visible: dialog.copyCeremony
                width: separateText.implicitWidth + 24; height: 38; radius: 9
                color: Qt.rgba(1, 1, 1, 0.08); border.width: 1; border.color: theme.edge
                Text { id: separateText; anchors.centerIn: parent; text: "Separate Copy"; color: theme.ink; font.pixelSize: 12 }
                MouseArea { anchors.fill: parent; onClicked: dialog.choose("separate-copy") }
            }
        }
    }
}
