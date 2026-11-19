import QtQuick
import QtQuick.Controls
import QtQuick.Window
import ".."

Popup {
    id: root
    objectName: "vinylPlaylistPicker"
    required property var libraryData
    required property Item backdrop
    property var pendingRows: []
    property Item returnFocus: null

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.max(0, Math.min(520, parent ? parent.width - 32 : 520))
    height: Math.max(0, Math.min(560, parent ? parent.height - 48 : 560))
    padding: width < 480 ? 16 : 22
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    readonly property var playlists: libraryData.playlists || []
    readonly property string dataError: String(libraryData.error || "")

    function openFor(rows) {
        pendingRows = rows || [];
        open();
    }

    onAboutToShow: {
        var window = root.backdrop ? root.backdrop.Window.window : null;
        returnFocus = window ? window.activeFocusItem : null;
    }
    onOpened: {
        if (playlistList.count > 0) {
            playlistList.currentIndex = 0;
            playlistList.forceActiveFocus(Qt.PopupFocusReason);
        } else nameField.forceActiveFocus(Qt.PopupFocusReason);
    }
    onClosed: {
        pendingRows = [];
        if (root.libraryData && typeof root.libraryData.closeAddPicker === "function")
            root.libraryData.closeAddPicker();
        if (returnFocus && returnFocus.visible && returnFocus.enabled)
            returnFocus.forceActiveFocus(Qt.PopupFocusReason);
    }

    Theme { id: theme }

    background: Glass { backdrop: root.backdrop; radius: 18; tint: 0.1; scrim: 0.94 }
    Overlay.modal: Rectangle { color: Qt.rgba(0,0,0,0.52) }

    Column {
        anchors.fill: parent
        spacing: 12

        Item {
            width: parent.width
            height: 46
            Column {
                spacing: 3
                Text { text: "Add to playlist"; color: theme.ink; font.family: theme.display; font.pixelSize: 24 }
                Text { text: root.pendingRows.length + (root.pendingRows.length === 1 ? " song" : " songs"); color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11 }
            }
            KeyboardAction {
                objectName: "vinylPlaylistPickerClose"
                anchors.right: parent.right
                width: 36; height: 36
                focusOnPointer: false
                accessibleName: "Close playlist picker"
                onTriggered: root.close()
                Rectangle { anchors.fill: parent; radius: 9; color: parent.interactionActive ? theme.glassTint : "transparent" }
                VinylControlIcon { anchors.centerIn: parent; symbol: "cancel"; color: theme.inkDim }
            }
        }

        Row {
            width: parent.width
            height: 40
            spacing: 8
            TextField {
                id: nameField
                objectName: "vinylPlaylistPickerName"
                width: Math.max(0, parent.width - 48)
                height: parent.height
                maximumLength: 80
                placeholderText: "Create a new playlist"
                color: theme.ink
                placeholderTextColor: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 13
                padding: 12
                selectByMouse: true
                onAccepted: createPlaylistAction.triggered()
                background: Glass { backdrop: root.backdrop; radius: 12; tint: 0.06; scrim: 0.18 }
            }
            KeyboardAction {
                id: createPlaylistAction
                objectName: "vinylPlaylistPickerCreate"
                width: 40; height: 40
                focusOnPointer: false
                accessibleName: "Create playlist and add songs"
                onTriggered: {
                    var name = String(nameField.text || "").trim();
                    if (!name) return;
                    var id = root.libraryData.createPlaylist(name, root.pendingRows);
                    if (id) root.close();
                }
                Rectangle { anchors.fill: parent; radius: 9; color: parent.interactionActive ? theme.glassTint : "transparent" }
                VinylControlIcon { anchors.centerIn: parent; symbol: "plus"; color: theme.ink }
            }
        }

        Text {
            id: errorMessage
            visible: root.dataError.length > 0
            width: parent.width
            text: root.dataError
            textFormat: Text.PlainText
            color: theme.gold
            font.family: theme.ui
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Text {
            id: noPlaylistsMessage
            visible: root.playlists.length === 0
            width: parent.width
            text: "No playlists yet. Create one above to add these songs."
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        ListView {
            id: playlistList
            objectName: "vinylPlaylistPickerList"
            width: parent.width
            height: root.playlists.length ? Math.max(0, parent.height - 46 - 40 - 24 - (errorMessage.visible ? errorMessage.implicitHeight + 12 : 0)) : 0
            clip: true
            spacing: 3
            model: root.playlists
            currentIndex: 0
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: KeyboardAction {
                required property var modelData
                required property int index
                objectName: "vinylPlaylistPickerPlaylist_" + index
                width: playlistList.width
                height: 54
                focusOnPointer: false
                showFocusFrame: false
                accessibleName: "Add songs to " + String(modelData.name || modelData.title || "playlist")
                onTriggered: {
                    var picker = root;
                    var id = modelData.id || modelData.playlistId;
                    if (!id || typeof picker.libraryData.addToPlaylist !== "function") return;
                    var result = picker.libraryData.addToPlaylist(id, picker.pendingRows);
                    if (result !== false && !picker.libraryData.error) picker.close();
                }

                Rectangle { anchors.fill: parent; radius: 8; color: parent.interactionActive ? theme.glassTint : "transparent" }
                Text {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 54
                    text: String(modelData.name || modelData.title || "Playlist")
                    textFormat: Text.PlainText
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 14
                    elide: Text.ElideRight
                }
                VinylControlIcon {
                    x: parent.width - 34
                    anchors.verticalCenter: parent.verticalCenter
                    symbol: "plus"
                    color: theme.inkDim
                }
            }
        }
    }

    Connections {
        target: root.libraryData
        function onPickerRequested() {
            root.pendingRows = root.libraryData.pendingRows || [];
            root.open();
        }
    }
}
