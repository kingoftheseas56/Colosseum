// Storage management is separate from Theatre's playback SourcesSheet.
import QtQuick
import QtQuick.Effects
import QtQuick.Controls
import QtQuick.Layouts
FocusScope {
    id: root
    objectName: "vaultStorageSheet"
    required property Item backdrop
    required property var adapter
    property bool open: false
    property bool editingIgnore: false
    property Item returnFocus: null
    signal dismissed()
    signal addFolderRequested()
    signal connectServerRequested()
    Theme { id: theme }
    visible: open || slide.running
    function show(invoker) { returnFocus = invoker || null; editingIgnore = false; open = true; Qt.callLater(function() { closeButton.forceActiveFocus(Qt.PopupFocusReason) }) }
    function close() { if (forgetDialog.opened) { forgetDialog.close(); return } if (!open) return; open = false; dismissed(); var target = returnFocus; if (target) Qt.callLater(function() { if (target.visible) target.forceActiveFocus(Qt.PopupFocusReason) }) }
    onOpenChanged: { if (open) ignoreInput.text = adapter.scanIgnore().join("\n"); else forgetDialog.close() }
    Keys.onEscapePressed: event => { root.close(); event.accepted = true }
    MultiEffect {
        anchors.fill: parent; source: root.backdrop
        blurEnabled: true; blurMax: 24; blur: 1; autoPaddingEnabled: false
        opacity: root.open ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: theme.vaultMotionDuration; easing.type: Easing.OutCubic } }
    }
    Rectangle { anchors.fill: parent; color: theme.vaultScrim; opacity: root.open ? 1 : 0; Behavior on opacity { NumberAnimation { duration: theme.vaultMotionDuration } } MouseArea { anchors.fill: parent; onClicked: root.close() } }
    Glass {
        id: panel
        objectName: "vaultSourcesPanel"
        x: root.open ? root.width - width - 24 : root.width + 24
        y: theme.vaultSidebarTop; width: Math.min(theme.vaultSheetWidth, root.width - 48); height: Math.max(280, root.height - y - 24)
        backdrop: root.backdrop; track: x; radius: 20; blurMax: 26
        Behavior on x { NumberAnimation { id: slide; duration: theme.vaultMotionDuration; easing.type: Easing.OutCubic } }
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 20; spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: root.editingIgnore ? "Ignore patterns" : "Sources"; color: theme.ink; font.family: theme.display; font.pixelSize: theme.vaultSectionTitle }
                VaultLibraryButton { id: closeButton; text: "×"; accessibleName: "Close sources"; quiet: true; onClicked: root.close() }
            }
            Text { Layout.fillWidth: true; text: root.editingIgnore ? "One pattern per line. Matching files stay outside your library." : "Where Vault finds your media."; wrapMode: Text.Wrap; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13 }
            Text { visible: !root.editingIgnore; text: "FOLDERS ON THIS PC"; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultSmall; font.letterSpacing: 1 }
            ListView {
                id: folders
                objectName: "vaultSourcesFolderList"
                visible: !root.editingIgnore; Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; spacing: 6; model: root.adapter.roots
                ScrollBar.vertical: ScrollBar { width: 6; policy: ScrollBar.AsNeeded }
                delegate: Rectangle {
                    required property var modelData
                    width: folders.width - 12; height: info.implicitHeight + 24; radius: 12; color: theme.glassTint
                    Column {
                        id: info; x: 12; y: 12; width: parent.width - 24; spacing: 6
                        Text { width: parent.width; text: modelData.name || "Folder"; elide: Text.ElideMiddle; color: theme.ink; font.family: theme.ui; font.pixelSize: theme.vaultBody }
                        Text { width: parent.width; text: modelData.path || ""; elide: Text.ElideMiddle; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultSmall }
                        Text { width: parent.width; text: (modelData.subtitle || "") + " · " + (modelData.state || ""); elide: Text.ElideRight; color: modelData.offline ? theme.gold : theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultCaption }
                        Row {
                            spacing: 8
                            VaultLibraryButton { text: "Rescan"; enabled: !root.adapter.busy; onClicked: root.adapter.rescan(modelData.path) }
                            VaultLibraryButton { text: "Forget"; quiet: true; onClicked: { forgetDialog.path = modelData.path; forgetDialog.open() } }
                        }
                    }
                }
                Text { visible: folders.count === 0; width: parent.width; text: "No folders added yet."; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultBody }
            }
            ScrollView {
                visible: root.editingIgnore; Layout.fillWidth: true; Layout.fillHeight: true
                TextArea { id: ignoreInput; objectName: "vaultIgnorePatterns"; color: theme.ink; font.family: theme.ui; font.pixelSize: theme.vaultBody; wrapMode: TextEdit.Wrap; background: Rectangle { radius: theme.vaultControlRadius; color: theme.glassTint; border.color: ignoreInput.activeFocus ? theme.gold : theme.edge } }
            }
            RowLayout {
                visible: !root.editingIgnore; Layout.fillWidth: true; spacing: 8
                VaultLibraryButton { Layout.fillWidth: true; text: "+ Add folder"; onClicked: root.addFolderRequested() }
                VaultLibraryButton { Layout.fillWidth: true; text: "Connect a server ›"; onClicked: { root.close(); root.connectServerRequested() } }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.edge }
            VaultLibraryButton { objectName: "vaultEditIgnorePatterns"; Layout.fillWidth: true; text: root.editingIgnore ? "Save patterns" : "Ignore patterns ›"; quiet: !root.editingIgnore; primary: root.editingIgnore; onClicked: { if (root.editingIgnore) { root.adapter.setScanIgnore(ignoreInput.text.split(/\r?\n/).map(s => s.trim()).filter(s => s.length)); root.editingIgnore = false } else { ignoreInput.text = root.adapter.scanIgnore().join("\n"); root.editingIgnore = true } } }
            VaultLibraryButton { objectName: "vaultCancelIgnorePatterns"; visible: root.editingIgnore; text: "Cancel"; quiet: true; onClicked: { ignoreInput.text = root.adapter.scanIgnore().join("\n"); root.editingIgnore = false } }
        }
    }
    Dialog {
        id: forgetDialog; objectName: "vaultForgetSourceDialog"; width: Math.min(380, root.width - 48); property string path: ""; anchors.centerIn: parent; modal: true; title: "Forget this source?"; standardButtons: Dialog.Cancel | Dialog.Ok
        contentItem: Text { text: "Files stay on your drive. Vault stops watching this folder."; color: theme.ink; wrapMode: Text.Wrap; width: 320; font.family: theme.ui }
        background: Rectangle { color: theme.vaultSurface; radius: theme.vaultRadius; border.color: theme.edge }
        onAccepted: root.adapter.forgetRoot(path)
    }
}
