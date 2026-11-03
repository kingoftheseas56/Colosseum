import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ".."

Popup {
    id: root
    required property var store
    property string collectionId: ""
    property string collectionName: ""
    property string errorText: ""
    signal deleted()
    signal dismissed()
    parent: Overlay.overlay
    width: Math.min(480, parent ? parent.width - 32 : 480)
    height: implicitHeight
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? Math.max(16, (parent.height - height) / 2) : 0
    padding: 24
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    Theme { id: theme }
    Overlay.modal: Rectangle { color: "#b4000209" }
    background: Rectangle { radius: 16; color: "#171b26"; border.width: 1; border.color: theme.edge }
    onOpened: cancel.forceActiveFocus(Qt.OtherFocusReason)
    onClosed: root.dismissed()
    contentItem: ColumnLayout {
        spacing: 16
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                if (cancel.activeFocus && remove.enabled) remove.forceActiveFocus(Qt.TabFocusReason)
                else cancel.forceActiveFocus(Qt.TabFocusReason)
                event.accepted = true
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Delete this collection?")
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 27
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: root.collectionName
            textFormat: Text.PlainText
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: 16
            font.weight: Font.DemiBold
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Only this collection will be removed. Your Libraries, downloads, and progress stay as they are.")
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 14
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            visible: root.errorText.length > 0
            text: root.errorText
            textFormat: Text.PlainText
            color: "#efb6a9"
            font.family: theme.ui
            font.pixelSize: 13
            wrapMode: Text.Wrap
            Accessible.role: Accessible.AlertMessage
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            CollectionActionButton {
                id: cancel
                objectName: "cancelDeleteCollection"
                text: qsTr("Cancel")
                emphasis: "ghost"
                onClicked: root.close()
            }
            CollectionActionButton {
                id: remove
                objectName: "confirmDeleteCollection"
                text: qsTr("Delete collection")
                iconName: "trash"
                destructive: true
                enabled: root.store.healthy
                onClicked: {
                    const result = root.store.deleteCollection(root.collectionId)
                    if (!result.ok) { root.errorText = result.error; return }
                    root.deleted()
                    root.close()
                }
            }
        }
    }
}
