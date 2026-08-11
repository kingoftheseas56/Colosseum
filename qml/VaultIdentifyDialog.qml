// VaultIdentifyDialog — the small explicit identity gesture. The native VaultIdentifier remains
// the certainty gate; this surface is deliberately honest when no single offline candidate exists.
import QtQuick
import QtQuick.Controls

Popup {
    id: dialog
    objectName: "vaultIdentifyDialog"
    modal: true
    focus: true
    width: 430
    height: 228
    property string groupKey: ""
    property string titleText: ""
    property string kind: ""
    property string feedback: ""
    signal confirmRequested(string groupKey)

    background: Rectangle {
        radius: 16
        color: Qt.rgba(0.055, 0.065, 0.09, 0.98)
        border.width: 1
        border.color: theme.edge
    }
    Theme { id: theme }

    contentItem: Column {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12
        Text {
            text: "Identify this Vault folder"
            color: theme.ink; font.family: theme.display; font.pixelSize: 24
        }
        Text {
            width: parent.width
            text: dialog.titleText || "Untitled folder"
            color: theme.gold; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: dialog.feedback || "Use the offline catalogue only when it has one certain match. Your files stay in place."
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12; wrapMode: Text.WordWrap
        }
        Item { width: 1; height: 2 }
        Row {
            spacing: 10
            anchors.right: parent.right
            Rectangle {
                objectName: "vaultIdentifyCancel"
                width: cancelText.implicitWidth + 28; height: 36; radius: 9
                color: Qt.rgba(1, 1, 1, 0.06); border.width: 1; border.color: theme.edge
                Text { id: cancelText; anchors.centerIn: parent; text: "Cancel"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13 }
                MouseArea { anchors.fill: parent; onClicked: dialog.close() }
            }
            Rectangle {
                objectName: "vaultIdentifyConfirm"
                width: identifyText.implicitWidth + 28; height: 36; radius: 9
                color: theme.gold
                Text { id: identifyText; anchors.centerIn: parent; text: "Identify"; color: "#151310"; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                MouseArea {
                    anchors.fill: parent
                    onClicked: dialog.confirmRequested(dialog.groupKey)
                }
            }
        }
    }
}
