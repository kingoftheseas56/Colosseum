import QtQuick
import QtQuick.Controls
ListView {
    id: root
    property var rows: []
    property bool hiddenMode: false
    property bool compact: false
    signal matchRequested(var row)
    signal restoreRequested(var row)
    signal extrasRequested(var row)
    Theme { id: theme }
    implicitHeight: compact ? contentHeight : 0
    interactive: !compact
    clip: !compact
    boundsBehavior: Flickable.StopAtBounds
    spacing: 10
    // Artwork and progress replace the rows array. Keep the visible delegates
    // and their focus instead of resetting the model for every such update.
    model: compact ? Math.min(rows.length, 3) : rows.length
    function hint(row) {
        if (row.subtitle) return row.subtitle
        var count = row.items ? row.items.length : 0
        if (!count) return "Choose its title once to add it to your shelves"
        var noun = row.nativeKind === "video" ? "video" : row.nativeKind === "book" ? "book" : row.nativeKind === "comic" ? "comic" : "file"
        return count + " " + noun + (count === 1 ? "" : "s") + (row.runtimeText ? " · " + row.runtimeText : "")
    }
    delegate: Rectangle {
        required property int index
        readonly property var row: root.rows[index] || ({})
        width: root.width - (root.compact ? 0 : 12); height: Math.max(80, copy.implicitHeight + 24); radius: 12; color: theme.glassTint
        Image {
            id: art
            x: 12; anchors.verticalCenter: parent.verticalCenter; width: 70; height: 54
            source: row.cover || row.poster || ""; fillMode: Image.PreserveAspectFit
            sourceSize.width: width * 2; sourceSize.height: height * 2
            asynchronous: true
        }
        Rectangle { anchors.fill: art; visible: art.status !== Image.Ready; radius: 8; color: theme.vaultSurfaceHi; Text { anchors.centerIn: parent; text: root.hiddenMode ? "◉" : "?"; color: theme.gold; font.family: theme.display; font.pixelSize: 24 } }
        Column {
            id: copy; anchors.left: art.right; anchors.leftMargin: 12; anchors.right: action.left; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; spacing: 5
            Text { width: parent.width; text: root.hiddenMode ? row.title || "" : row.path || row.seriesKey || "Unidentified file"; color: theme.ink; font.family: root.hiddenMode ? theme.ui : "monospace"; font.pixelSize: theme.vaultCaption; elide: Text.ElideMiddle }
            Text { width: parent.width; text: root.hint(row); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: theme.vaultCaption; elide: Text.ElideRight }
        }
        Row {
            id: action; anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; spacing: 8
            VaultLibraryButton { visible: !root.hiddenMode && row.nativeKind === "video"; objectName: "vaultFileExtras"; text: "File as extras"; onClicked: root.extrasRequested(row) }
            VaultLibraryButton { objectName: root.hiddenMode ? "vaultRestoreTitle" : "vaultMatchTitle"; text: root.hiddenMode ? "Restore" : "Match"; onClicked: root.hiddenMode ? root.restoreRequested(row) : root.matchRequested(row) }
        }
    }
}
