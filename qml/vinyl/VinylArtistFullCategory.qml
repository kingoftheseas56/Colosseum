import QtQuick
import ".."

// Dedicated See-all surface. The overview's scroll and sidebar remain owned
// by VinylArtistPage; this reads the same catalogue rather than making a store.
Column {
    id: root
    required property var artistData
    property string category: ""
    readonly property string title: ({
        songs: "All songs",
        albums: "Albums",
        singles: "Singles & EPs",
        videos: "Videos",
        related: "Related artists"
    })[category] || ""
    readonly property var records: category === "songs" ? artistData.songOrder.map(function(song) {
            return {title:song.track.title, subtitle:song.album.title,
                cover:song.album.cover, song:song};
        }) : category === "albums" ? artistData.albumCards
        : category === "singles" ? artistData.singleCards
        : category === "videos" ? artistData.media.videos
        : category === "related" ? artistData.media.relatedArtists : []
    readonly property string shape: category === "videos" ? "wide"
        : category === "related" ? "round" : "square"
    readonly property real targetWidth: shape === "wide" ? 270 : 174
    readonly property int columns: Math.max(1, Math.min(6, Math.floor((width + 24) / (targetWidth + 24))))
    readonly property real cardWidth: (width - (columns - 1) * 24) / columns
    signal backRequested()
    signal albumRequested(var album)
    signal songRequested(var song)
    signal artistRequested(var artist)
    signal mediaRequested(var record)
    signal itemFocused(var item)
    spacing: 28
    visible: category.length > 0
    Theme { id: theme }

    Item {
        width: parent.width
        height: 56
        KeyboardAction {
            id: back
            objectName: "vinylArtistFullBack"
            width: 94; height: 40
            accessibleName: "Back to artist overview"
            onTriggered: root.backRequested()
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "‹ Back"
                color: back.interactionActive ? theme.gold : theme.inkDim
                font.family: theme.ui; font.pixelSize: 14
            }
        }
        Text {
            id: titleText
            objectName: "vinylArtistDetailTitle"
            x: back.width + 16
            width: parent.width - x
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            textFormat: Text.PlainText
            font.family: theme.display; font.pixelSize: 32
            color: theme.ink
            elide: Text.ElideRight
        }
    }
    Grid {
        id: fullGrid
        width: parent.width
        visible: root.category.length > 0
        columns: root.columns
        columnSpacing: 24
        rowSpacing: 32
        Repeater {
            model: root.records.length
            delegate: VinylArtistCard {
                id: tile
                readonly property var modelData: root.records[index]
                required property int index
                objectName: "vinylArtistFullCategory_" + index
                artObjectName: "vinylArtistFullArt_" + index
                width: root.cardWidth
                height: implicitHeight
                record: modelData
                artistData: root.artistData
                shape: root.shape
                badge: root.category === "videos" ? "VIDEO" : ""
                onCardRequested: record => {
                    if (root.category === "songs")
                        root.songRequested(record.song)
                    else if (root.category === "albums" || root.category === "singles")
                        root.albumRequested(record.album)
                    else if (root.category === "videos")
                        root.mediaRequested(record)
                    else if (root.category === "related")
                        root.artistRequested(record.artistRef || record.title)
                }
                onActiveFocusChanged: if (activeFocus) root.itemFocused(tile)
            }
        }
    }
}
