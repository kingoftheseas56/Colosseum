import QtQuick
import ".."

// Only the song section uses a YouTube Music-like track/album list.
// The catalogue's track objects still own selection and timing.
Column {
    id: root
    required property var artistData
    property bool fullView: false
    readonly property string title: artistData.popularSongs.length ? "Popular songs" : "Songs"
    readonly property var shownSongs: fullView ? artistData.songOrder
        : (artistData.popularSongs.length ? artistData.popularSongs : artistData.songOrder).slice(0, 5)
    signal songRequested(var song)
    signal albumRequested(var album)
    signal seeAllRequested()
    signal itemFocused(var item)
    spacing: 12
    visible: shownSongs.length > 0
    Theme { id: theme }

    WidgetHeader {
        width: parent.width
        visible: !root.fullView
        title: root.title
        sub: root.artistData.popularSongs.length ? root.artistData.popularSource : ""
        moreLabel: "See all"
        automationId: "vinylArtistSeeAll_songs"
        onMoreClicked: root.seeAllRequested()
    }
    Column {
        width: parent.width
        Repeater {
            model: root.shownSongs
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                width: parent.width
                height: 56
                KeyboardAction {
                    id: play
                    objectName: (root.fullView ? "vinylArtistFullSong_" : "vinylArtistSong_") + row.index
                    width: parent.width * 0.61
                    height: parent.height
                    accessibleName: "Play " + row.modelData.track.title
                    onTriggered: root.songRequested(row.modelData)
                    onActiveFocusChanged: if (activeFocus) root.itemFocused(play)
                    Rectangle {
                        anchors.fill: parent; radius: 6
                        color: theme.glassTint
                        visible: parent.interactionActive
                    }
                    RoundedPosterImage {
                        x: 4; y: 7; width: 42; height: 42; radius: 4
                        sources: [root.artistData.artwork(row.modelData.album.cover)]
                    }
                    Column {
                        x: 62; width: parent.width - 76
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 5
                        Text {
                            width: parent.width
                            text: row.modelData.track.title
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: theme.ink
                            font.family: theme.ui; font.pixelSize: 14
                        }
                        Text {
                            width: parent.width
                            text: (row.modelData.track.artists && row.modelData.track.artists.length
                                ? row.modelData.track.artists : row.modelData.album.artists || []).join(" · ")
                            textFormat: Text.PlainText; elide: Text.ElideRight
                            color: theme.inkDim
                            font.family: theme.ui; font.pixelSize: 11
                        }
                    }
                }
                KeyboardAction {
                    id: album
                    objectName: (root.fullView ? "vinylArtistFullSongAlbum_" : "vinylArtistSongAlbum_") + row.index
                    x: parent.width * 0.63
                    width: parent.width * 0.29
                    height: parent.height
                    accessibleName: "Open " + row.modelData.album.title
                    onTriggered: root.albumRequested(row.modelData.album)
                    onActiveFocusChanged: if (activeFocus) root.itemFocused(album)
                    Text {
                        width: parent.width; anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.album.title; textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: parent.interactionActive ? theme.gold : theme.inkDim
                        font.family: theme.ui; font.pixelSize: 12
                    }
                }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    readonly property real seconds: Number(row.modelData.track.seconds)
                    text: Number.isFinite(seconds) && seconds > 0
                        ? Math.floor(seconds / 60) + ":" + ("0" + Math.floor(seconds % 60)).slice(-2) : ""
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 12
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width; height: 1
                    color: theme.edge; opacity: 0.4
                }
            }
        }
    }
}
