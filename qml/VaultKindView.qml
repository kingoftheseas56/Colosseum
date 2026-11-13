pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Item {
    id: view
    property string kind: "movie"
    property string title: "Movies"
    property string subtitle: ""
    property var rows: []
    property string sortMode: "Title"
    property string filterMode: "All"
    signal activated(var row)
    signal hideRequested(var row)
    readonly property var displayedRows: {
        let result = rows.filter(row => filterMode === "All" || (filterMode === "In progress" ? Number(row.progressFraction || 0) > 0 && !row.completed : row.completed === true))
        return result.slice().sort((a, b) => sortMode === "Year" ? Number(b.year || 0) - Number(a.year || 0) : String(a.title || "").localeCompare(String(b.title || "")))
    }
    Theme { id: theme }
    Text { id: heading; text: view.title; color: theme.ink; font.family: theme.display; font.pixelSize: 40 }
    Text { y: 53; width: parent.width; text: view.subtitle || (view.rows.length + (view.rows.length === 1 ? " title" : " titles") + (view.kind === "book" ? " · opens in Biblio’s reader" : view.kind === "comic" ? " · opens in Tankoban’s reader" : " · opens in Theatre")); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13; elide: Text.ElideRight }
    Row {
        y: 80; spacing: 12
        VaultLibraryComboBox { width: 150; model: ["All", "In progress", "Completed"]; onActivated: view.filterMode = currentText; Accessible.name: "Filter titles" }
        VaultLibraryComboBox { width: 130; model: ["Title", "Year"]; onActivated: view.sortMode = currentText; Accessible.name: "Sort titles" }
    }
    // CataloguePosterGrid geometry and key contract, with the Vault state-aware delegate.
    GridView {
        id: grid; anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 132; bottom: parent.bottom }
        model: view.displayedRows; clip: true; keyNavigationEnabled: true; focus: true
        readonly property int columns: Math.max(1, Math.floor(width / (theme.vaultPosterWidth + theme.vaultGap)))
        cellWidth: width / columns; cellHeight: theme.vaultPosterWidth * 1.5 + 80
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: grid }
        delegate: VaultLibraryPosterCard {
            required property var modelData
            required property int index
            row: modelData; keyboardFocused: grid.activeFocus && grid.currentIndex === index || activeFocus
            onActivated: row => { grid.currentIndex = index; view.activated(row) }
            onHideRequested: row => view.hideRequested(row)
        }
        Keys.onReturnPressed: if (currentIndex >= 0 && currentIndex < view.displayedRows.length) view.activated(view.displayedRows[currentIndex])
        Keys.onEnterPressed: if (currentIndex >= 0 && currentIndex < view.displayedRows.length) view.activated(view.displayedRows[currentIndex])
        Text { anchors.centerIn: parent; visible: view.displayedRows.length === 0; text: "No titles in this view yet."; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 14 }
    }
}
