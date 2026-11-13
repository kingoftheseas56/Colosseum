import QtQuick
import QtQuick.Controls
Column {
    id: root
    property var rows: []
    property bool hiddenMode: false
    property bool compact: false
    signal matchRequested(var row)
    signal restoreRequested(var row)
    Theme { id: theme }
    function hint(row) {
        if (row.subtitle) return row.subtitle
        var count = row.items ? row.items.length : 0
        if (!count) return "Choose its title once to add it to your shelves"
        var noun = row.nativeKind === "video" ? "video" : row.nativeKind === "book" ? "book" : row.nativeKind === "comic" ? "comic" : "file"
        return count + " " + noun + (count === 1 ? "" : "s") + (row.runtimeText ? " · " + row.runtimeText : "")
    }
    spacing: 10
    Text { visible: root.rows.length === 0; width: parent.width; text: root.hiddenMode ? "Nothing hidden. Restored titles return to their shelves." : "Everything has a match."; color: theme.inkDimmer; wrapMode: Text.Wrap; font.family: theme.ui; font.pixelSize: theme.vaultBody }
    Repeater {
        model: root.compact ? root.rows.slice(0, 3) : root.rows
        delegate: Rectangle {
            required property var modelData
            width: root.width; height: Math.max(80, copy.implicitHeight + 24); radius: 12; color: theme.glassTint
            Image { id: art; x: 12; anchors.verticalCenter: parent.verticalCenter; width: 70; height: 54; source: modelData.cover || modelData.poster || ""; fillMode: Image.PreserveAspectFit }
            Rectangle { anchors.fill: art; visible: art.status !== Image.Ready; radius: 8; color: theme.vaultSurfaceHi; Text { anchors.centerIn: parent; text: root.hiddenMode ? "◉" : "?"; color: theme.gold; font.family: theme.display; font.pixelSize: 24 } }
            Column {
                id: copy; anchors.left: art.right; anchors.leftMargin: 12; anchors.right: action.left; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; spacing: 5
                Text { width: parent.width; text: root.hiddenMode ? modelData.title : modelData.path || modelData.seriesKey || "Unidentified file"; color: theme.ink; font.family: root.hiddenMode ? theme.ui : "monospace"; font.pixelSize: theme.vaultCaption; elide: Text.ElideMiddle }
                Text { width: parent.width; text: root.hint(modelData); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultCaption; elide: Text.ElideRight }
            }
            VaultLibraryButton { id: action; objectName: root.hiddenMode ? "vaultRestoreTitle" : "vaultMatchTitle"; anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; text: root.hiddenMode ? "Restore" : "Match"; onClicked: root.hiddenMode ? root.restoreRequested(modelData) : root.matchRequested(modelData) }
        }
    }
}
