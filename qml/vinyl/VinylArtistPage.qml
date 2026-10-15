import QtQuick
import QtQuick.Controls
import ".."

Flickable {
    id: root
    objectName: "vinylArtistPage"
    required property var artistData
    required property var worldData
    signal albumRequested(var album)
    signal songRequested(var song)
    signal artistRequested(string name)
    property real gutter: width < 720 ? 24 : width < 1100 ? 36 : 54
    readonly property real bodyWidth: width - gutter * 2
    function jumpTo(section) { contentY = Math.min(section.y, Math.max(0, contentHeight - height)); }
    function reveal(item) {
        var y = item.mapToItem(body, 0, 0).y;
        if (y < contentY) contentY = Math.max(0, y - 8);
        else if (y + item.height > contentY + height)
            contentY = Math.min(Math.max(0, contentHeight - height), y + item.height - height + 8);
    }
    function showMedia(record) { mediaDialog.selection = record; mediaDialog.open(); }
    Keys.onDownPressed: contentY = Math.min(Math.max(0, contentHeight - height), contentY + 64)
    Keys.onUpPressed: contentY = Math.max(0, contentY - 64)
    Keys.onPressed: event => {
        if (event.key !== Qt.Key_PageDown && event.key !== Qt.Key_PageUp) return;
        var delta = height * (event.key === Qt.Key_PageDown ? 0.85 : -0.85);
        contentY = Math.max(0, Math.min(Math.max(0, contentHeight - height), contentY + delta));
        event.accepted = true;
    }
    Connections {
        target: root.artistData
        function onNameChanged() { mediaDialog.close(); playlistDialog.close(); createDialog.close(); }
    }
    contentWidth: width
    contentHeight: body.height + 24
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    Theme { id: theme }
    ScrollGlide { flick: root }
    ScrollBar.vertical: HouseScrollBar { flick: root }

    Column {
        id: body
        x: root.gutter
        width: root.bodyWidth
        spacing: 32
        Flow {
            width: parent.width
            spacing: 22
            Repeater {
                model: [
                    {key:0,label:"Overview",section:body},
                    {key:1,label:"Albums",section:albumsSection},
                    {key:2,label:"Singles & EPs",section:singlesSection},
                    {key:5,label:"Videos",section:videosSection},
                    {key:6,label:"Live",section:liveSection},
                    {key:7,label:"Library",section:librarySection},
                    {key:8,label:"Featured on",section:featuredSection},
                    {key:9,label:"Artist playlists",section:artistPlaylistsSection},
                    {key:3,label:"Your playlists",section:playlistsSection},
                    {key:10,label:"Related artists",section:relatedSection},
                    {key:4,label:"About",section:about}
                ]
                delegate: KeyboardAction {
                    required property var modelData
                    objectName: "vinylArtistNav_" + modelData.key
                    width: navLabel.implicitWidth; height: 28
                    visible: modelData.key === 0 || modelData.section.visible
                    accessibleName: modelData.label
                    onTriggered: root.jumpTo(modelData.section)
                    Text { id: navLabel; text: modelData.label; color: parent.interactionActive ? theme.gold : theme.inkDim; font.family: theme.ui; font.pixelSize: 12 }
                }
            }
        }
        VinylArtistHero {
            objectName: "vinylArtistHero"
            width: parent.width
            artistData: root.artistData
            onCreatePlaylistRequested: { playlistName.text = ""; createDialog.open(); }
            onItemFocused: item => root.reveal(item)
        }
        VinylArtistSongs {
            objectName: "vinylArtistSongsSection"
            width: parent.width
            artistData: root.artistData
            onSongRequested: song => root.songRequested(song)
            onAlbumRequested: album => root.albumRequested(album)
            onItemFocused: item => root.reveal(item)
        }
        VinylArtistShelf {
            id: albumsSection
            objectName: "vinylArtistAlbumsSection"
            width: parent.width
            title: "Albums"
            subtitle: "Releases in this collection"
            records: root.artistData.albumCards
            artistData: root.artistData
            prefix: "vinylArtistAlbum"
            onActivated: record => root.albumRequested(record.album)
            onItemFocused: item => root.reveal(item)
            toolbar: Component {
                Flow {
                    spacing: 6
                    Repeater {
                        model: root.artistData.filters
                        delegate: KeyboardAction {
                            id: filter
                            required property var modelData
                            objectName: "vinylArtistFilter_" + modelData.key
                            width: filterText.implicitWidth + 24
                            height: 34
                            accessibleName: modelData.label
                            onTriggered: root.artistData.filter = modelData.key
                            onActiveFocusChanged: if (activeFocus) root.reveal(filter)
                            Rectangle {
                                anchors.fill: parent; radius: 17
                                color: root.artistData.filter === modelData.key ? theme.glassHi : "transparent"
                                border.color: root.artistData.filter === modelData.key ? theme.edge : "transparent"
                            }
                            Text {
                                id: filterText; anchors.centerIn: parent; text: modelData.label
                                color: root.artistData.filter === modelData.key ? theme.gold : theme.inkDim
                                font.family: theme.ui; font.pixelSize: 12
                            }
                        }
                    }
                    KeyboardAction {
                        id: sort
                        objectName: "vinylArtistSort"; width: 138; height: 34
                        accessibleName: root.artistData.newestFirst ? "Sort oldest first" : "Sort newest first"
                        onTriggered: root.artistData.newestFirst = !root.artistData.newestFirst
                        onActiveFocusChanged: if (activeFocus) root.reveal(sort)
                        Text {
                            anchors.centerIn: parent
                            text: root.artistData.newestFirst ? "Newest first ↓" : "Oldest first ↑"
                            color: parent.interactionActive ? theme.gold : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 12
                        }
                    }
                }
            }
        }
        VinylArtistShelf {
            id: singlesSection
            objectName: "vinylArtistSinglesSection"
            width: parent.width; title: "Singles & EPs"
            records: root.artistData.singleCards; artistData: root.artistData
            prefix: "vinylArtistSingle"
            onActivated: record => root.albumRequested(record.album)
            onItemFocused: item => root.reveal(item)
        }
        VinylArtistShelf {
            id: videosSection
            objectName: "vinylArtistVideosSection"
            width: parent.width; title: "Videos"; shape: "wide"; badge: "VIDEO"
            subtitle: root.artistData.media.provider + " catalogue"
            records: root.artistData.media.videos; artistData: root.artistData
            prefix: "vinylArtistVideo"
            onActivated: record => root.showMedia(record)
            onItemFocused: item => root.reveal(item)
        }
        VinylArtistShelf {
            id: liveSection
            objectName: "vinylArtistLiveSection"
            width: parent.width; title: "Live performances"; shape: "wide"; badge: "LIVE"
            records: root.artistData.media.livePerformances; artistData: root.artistData
            prefix: "vinylArtistLive"
            onActivated: record => root.showMedia(record)
            onItemFocused: item => root.reveal(item)
        }
        VinylArtistShelf {
            id: librarySection
            objectName: "vinylArtistLibrarySection"
            width: parent.width; title: "From your library"
            records: root.artistData.libraryCards; artistData: root.artistData
            prefix: "vinylArtistLibrary"
            onActivated: record => root.albumRequested(record.album)
            onItemFocused: item => root.reveal(item)
        }
        VinylArtistShelf {
            id: featuredSection
            objectName: "vinylArtistFeaturedSection"
            width: parent.width; title: "Featured on"; badge: "PLAYLIST"
            records: root.artistData.media.featuredPlaylists; artistData: root.artistData
            prefix: "vinylArtistFeatured"
            onActivated: record => root.showMedia(record)
            onItemFocused: item => root.reveal(item)
        }
        VinylArtistShelf {
            id: artistPlaylistsSection
            objectName: "vinylArtistArtistPlaylistsSection"
            width: parent.width; title: "Playlists by " + root.artistData.record.name; badge: "PLAYLIST"
            records: root.artistData.media.artistPlaylists; artistData: root.artistData
            prefix: "vinylArtistArtistPlaylist"
            onActivated: record => root.showMedia(record)
            onItemFocused: item => root.reveal(item)
        }
        VinylArtistShelf {
            id: playlistsSection
            width: parent.width; title: "Your playlists"; subtitle: "Created in this preview session"
            records: root.artistData.playlists.map(function(p) {
                return {title:p.name, subtitle:p.songs.length + " songs", playlist:p,
                    cover:p.songs.length ? p.songs[0].album.cover : ""};
            })
            artistData: root.artistData; prefix: "vinylArtistPlaylist"
            onActivated: record => { playlistDialog.selection = record.playlist; playlistDialog.open(); }
            onItemFocused: item => root.reveal(item)
        }
        VinylArtistShelf {
            id: relatedSection
            objectName: "vinylArtistRelatedSection"
            width: parent.width; title: "Related artists"; shape: "round"
            subtitle: root.artistData.media.provider + " catalogue associations"
            records: root.artistData.media.relatedArtists; artistData: root.artistData
            prefix: "vinylArtistRelated"
            onActivated: record => root.artistRequested(record.title)
            onItemFocused: item => root.reveal(item)
        }
        Column {
            id: about
            objectName: "vinylArtistAbout"
            width: parent.width; spacing: 20
            visible: root.artistData.context.history.length > 0 || root.artistData.context.facts.length > 0 || root.artistData.releaseHistory.length > 0
            Text { text: "About"; color: theme.ink; font.family: theme.display; font.pixelSize: 32 }
            Row {
                width: parent.width; spacing: 44
                visible: root.artistData.context.history.length > 0 || root.artistData.context.facts.length > 0
                Column {
                    width: parent.width * 0.6 - 22; spacing: 18
                    visible: root.artistData.context.history.length > 0
                    Text { width: parent.width; text: root.artistData.context.history; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 15; lineHeight: 1.45; wrapMode: Text.WordWrap }
                    Row {
                        spacing: 22
                        Repeater {
                            model: root.artistData.context.sources
                            delegate: KeyboardAction {
                                required property var modelData
                                width: sourceText.implicitWidth; height: 30; accessibleName: "Read " + modelData.title
                                onTriggered: Qt.openUrlExternally(modelData.url)
                                Text { id: sourceText; text: modelData.title + " ↗"; color: theme.gold; font.family: theme.ui; font.pixelSize: 12 }
                            }
                        }
                    }
                }
                Column {
                    width: parent.width * 0.4 - 22; spacing: 14
                    visible: root.artistData.context.facts.length > 0
                    Repeater {
                        model: root.artistData.context.facts
                        delegate: Row {
                            required property var modelData
                            width: parent.width; spacing: 14
                            Text { width: 104; text: modelData.label; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                            Text { width: parent.width - 118; text: modelData.value; wrapMode: Text.WordWrap; color: theme.ink; font.family: theme.ui; font.pixelSize: 13 }
                        }
                    }
                }
            }
            Text {
                visible: root.artistData.releaseHistory.length > 0
                text: "From this collection"
                color: theme.ink; font.family: theme.display; font.pixelSize: 24
            }
            Flow {
                width: parent.width; spacing: 24
                visible: root.artistData.releaseHistory.length > 0
                Repeater {
                    model: root.artistData.releaseHistory
                    delegate: KeyboardAction {
                        required property var modelData
                        required property int index
                        objectName: "vinylArtistHistory_" + index
                        width: (root.bodyWidth - (root.width < 1100 ? 24 : 72)) / (root.width < 1100 ? 2 : 4)
                        height: 124
                        accessibleName: "Open " + modelData.album.title
                        onTriggered: root.albumRequested(modelData.album)
                        Rectangle { width: parent.width; height: 1; color: parent.interactionActive ? theme.gold : theme.edge }
                        Text { y: 15; text: modelData.album.year; color: theme.gold; font.family: theme.display; font.pixelSize: 26 }
                        Text { y: 51; width: parent.width; text: modelData.album.title; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight; color: theme.ink; font.family: theme.ui; font.pixelSize: 14 }
                        Text { y: 102; text: modelData.credit === "album" ? "Album credit ↗" : modelData.credit === "single" ? modelData.album.releaseType + " ↗" : "Appears on ↗"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12 }
                    }
                }
            }
        }
        Column {
            width: parent.width
            spacing: 14
            visible: root.artistData.record.collaborators.length > 0
            Text {
                text: "Shared credits"
                color: theme.ink; font.family: theme.display; font.pixelSize: 28
            }
            Flow {
                width: parent.width; spacing: 10
                Repeater {
                    model: root.artistData.record.collaborators
                    delegate: KeyboardAction {
                        required property string modelData
                        objectName: "vinylArtistCredit_" + modelData
                        width: creditText.implicitWidth + 28; height: 38
                        accessibleName: modelData
                        onTriggered: root.artistRequested(modelData)
                        Rectangle { anchors.fill: parent; radius: 19; color: parent.interactionActive ? theme.glassHi : theme.glassTint }
                        Text {
                            id: creditText
                            anchors.centerIn: parent; text: modelData
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                        }
                    }
                }
            }
        }
    }
    Dialog {
        id: mediaDialog
        objectName: "vinylArtistMediaDialog"
        property var selection: ({title:"",cover:"",provider:"",sourceUrl:"",kind:"",snapshotAt:""})
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 48)
        modal: true
        focus: true
        title: selection.title
        palette.window: "#17181d"
        palette.windowText: theme.ink
        palette.text: theme.ink
        palette.button: "#292a30"
        palette.buttonText: theme.ink
        standardButtons: Dialog.Close
        contentItem: Column {
            spacing: 16
            RoundedPosterImage {
                width: parent.width; height: Math.min(220, width * 9 / 16); radius: 8
                visible: mediaDialog.selection.cover.length > 0
                sources: [root.artistData.artwork(mediaDialog.selection.cover)]
            }
            Label {
                width: parent.width; textFormat: Text.PlainText; wrapMode: Text.WordWrap
                text: mediaDialog.selection.provider + (mediaDialog.selection.snapshotAt ? " · Snapshot " + mediaDialog.selection.snapshotAt : "")
            }
            Label {
                width: parent.width; textFormat: Text.PlainText; wrapMode: Text.WordWrap; color: theme.inkDim
                text: "Catalogue reference. Playback for this media is not connected in the artist preview. Opening the source leaves Colosseum."
            }
            KeyboardAction {
                objectName: "vinylArtistMediaSource"; width: parent.width; height: 40
                accessibleName: "Open catalogue source in browser"
                accessibleDescription: mediaDialog.selection.sourceUrl
                onTriggered: Qt.openUrlExternally(mediaDialog.selection.sourceUrl)
                Text { anchors.verticalCenter: parent.verticalCenter; text: "Open catalogue source ↗"; color: theme.gold; font.family: theme.ui; font.pixelSize: 14 }
            }
        }
    }
    Dialog {
        id: createDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(420, parent.width - 48)
        title: "Create playlist"
        modal: true
        palette.window: "#17181d"
        palette.windowText: theme.ink
        palette.text: theme.ink
        palette.base: "#22232a"
        palette.button: "#292a30"
        palette.buttonText: theme.ink
        palette.highlight: theme.gold
        palette.highlightedText: "#06070b"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.artistData.createPlaylist(playlistName.text)
        Column {
            width: parent.width; spacing: 14
            Label { width: parent.width; text: "Start with the displayed songs. This prototype keeps playlists for this session."; wrapMode: Text.WordWrap }
            TextField { id: playlistName; objectName: "vinylArtistPlaylistName"; width: parent.width; placeholderText: "Playlist name"; maximumLength: 80 }
        }
        onOpened: playlistName.forceActiveFocus()
    }
    Dialog {
        id: playlistDialog
        property var selection: ({name:"",songs:[]})
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 48); height: Math.min(500, parent.height - 80)
        title: selection.name
        modal: true
        palette.window: "#17181d"
        palette.windowText: theme.ink
        palette.text: theme.ink
        palette.base: "#22232a"
        palette.button: "#292a30"
        palette.buttonText: theme.ink
        palette.highlight: theme.gold
        palette.highlightedText: "#06070b"
        standardButtons: Dialog.Close
        contentItem: ListView {
            clip: true; model: playlistDialog.selection.songs; spacing: 4
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                objectName: "vinylArtistPlaylistSong_" + index
                width: ListView.view.width
                text: modelData.track.title
                onClicked: { root.songRequested(modelData); playlistDialog.close(); }
            }
        }
    }
}
