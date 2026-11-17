pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "DiscoverApi.js" as DiscoverApi
import "VaultLibraryData.js" as VaultData

Item {
    id: view
    property string kind: "movie"
    property string title: "Movies"
    property string subtitle: ""
    property var rows: []
    property string sortMode: "Title"
    property string filterMode: "All"
    property string genreMode: "All"
    readonly property bool videoKind: kind === "movie" || kind === "show"
    readonly property var genreOptions: {
        if (!videoKind) return []
        const owned = {}
        rows.forEach(row => VaultData.videoGenres(row.genres).forEach(genre => owned[genre] = true))
        const theatre = kind === "movie" ? DiscoverApi.CINEMETA_MOVIE_GENRES : DiscoverApi.CINEMETA_SERIES_GENRES
        return [{key:"All",text:qsTr("All")}].concat(theatre.filter(genre => owned[genre]).map(genre => ({key:genre,text:genre})))
    }
    // A disappearing option or a kind switch must never leave a hidden active filter.
    onGenreOptionsChanged: {
        if (!genreOptions.some(option => option.key === genreMode)) genreMode = "All"
    }
    signal activated(var row)
    signal hideRequested(var row)
    readonly property var displayedRows: {
        let result = rows.filter(row => filterMode === "All" || (filterMode === "In progress" ? Number(row.progressFraction || 0) > 0 && !row.completed : row.completed === true))
        if (videoKind && genreMode !== "All") result = result.filter(row => VaultData.videoGenres(row.genres).indexOf(genreMode) >= 0)
        return result.slice().sort((a, b) => sortMode === "Year" ? Number(b.year || 0) - Number(a.year || 0) : String(a.title || "").localeCompare(String(b.title || "")))
    }
    Theme { id: theme }
    Text { id: heading; text: view.title; color: theme.ink; font.family: theme.display; font.pixelSize: 40 }
    Text { y: 53; width: parent.width; text: view.subtitle || (view.rows.length + (view.rows.length === 1 ? " title" : " titles") + (view.kind === "book" ? " · opens in Biblio’s reader" : view.kind === "comic" ? " · opens in Tankoban’s reader" : " · opens in Theatre")); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13; elide: Text.ElideRight }
    Row {
        y: 80; spacing: 12
        DiscoverPicker {
            id: filterPicker
            objectName: "vaultKindFilterPicker"
            automationName: "vaultKindFilter"
            width: 150
            label: qsTr("Filter titles")
            actionAccessibleName: qsTr("Filter titles")
            Accessible.name: qsTr("Filter titles")
            clearable: false
            openOnDown: true
            options: [{key: "All", text: qsTr("All")},
                      {key: "In progress", text: qsTr("In progress")},
                      {key: "Completed", text: qsTr("Completed")}]
            currentKey: view.filterMode
            onPicked: key => view.filterMode = key
        }
        DiscoverPicker {
            objectName: "vaultKindGenrePicker"
            automationName: "vaultKindGenre"
            width: 150
            visible: view.videoKind && view.genreOptions.length > 1
            label: qsTr("Genre")
            actionAccessibleName: qsTr("Genre")
            Accessible.name: qsTr("Genre")
            clearable: false
            openOnDown: true
            options: view.genreOptions
            currentKey: view.genreMode
            onPicked: key => view.genreMode = key
        }
        DiscoverPicker {
            id: sortPicker
            objectName: "vaultKindSortPicker"
            automationName: "vaultKindSort"
            width: 130
            label: qsTr("Sort titles")
            actionAccessibleName: qsTr("Sort titles")
            Accessible.name: qsTr("Sort titles")
            clearable: false
            openOnDown: true
            options: [{key: "Title", text: qsTr("Title")},
                      {key: "Year", text: qsTr("Year")}]
            currentKey: view.sortMode
            onPicked: key => view.sortMode = key
        }
    }
    CataloguePosterGrid {
        id: grid
        objectName: "vaultKindGrid"
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 132; bottom: parent.bottom }
        items: view.displayedRows
        visualProfile: "gallery"
        metadataExtraHeight: 16
            + ((view.kind === "book" || view.kind === "comic") && view.displayedRows.some(row => row.offline && row.author) ? 16 : 0)
        emptyMessage: "No titles in this view yet."
        onItemRequested: row => view.activated(row)
        delegate: CataloguePosterCard {
            required property int index
            objectName: "vaultKindPoster_" + index
            width: grid.cellWidth - 14
            height: grid.cellHeight - 14
            item: grid.items[index] || null
            readonly property var row: item || ({})
            visualProfile: grid.visualProfile
            showAuthorAtRest: view.kind === "book" || view.kind === "comic"
            revealOnFocus: showAuthorAtRest
            hoverSourceText: showAuthorAtRest ? "" : "IMDb"
            keyboardFocused: grid.activeFocus && grid.keyboardMode && grid.currentIndex === index
            subtitleText: row.offline ? (row.serverId ? "Server unavailable" : "Drive not connected")
                : showAuthorAtRest && authorText.length ? "" : String(row.subtitle || row.author || row.year || "")
            badgeText: row.isNew === true ? "NEW" : ""
            completed: row.completed === true
            progressFraction: Number(row.progressFraction || 0)
            dimmed: row.dimmed === true || row.offline === true || row.identified === false
            contextActionText: "Hide from Vault"
            onActivated: row => {
                grid.forceActiveFocus()
                grid.keyboardMode = false
                grid.currentIndex = index
                view.activated(row)
            }
            onContextRequested: row => {
                grid.forceActiveFocus()
                grid.keyboardMode = true
                grid.currentIndex = index
            }
            onContextActionTriggered: row => view.hideRequested(row)
        }
    }
}
