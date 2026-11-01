import QtQuick
import QtQuick.Controls
import ".."

Flickable {
    id: root
    objectName: "vinylArtistPage"
    required property var artistData
    required property var worldData
    required property Item backdrop
    signal albumRequested(var album)
    signal songRequested(var song)
    signal artistRequested(string name)

    property real gutter: width < 720 ? 24 : width < 1100 ? 36 : 54
    readonly property real bodyWidth: width - 2 * gutter
    property string selectedCategory: ""
    property real sectionReturnY: 0
    contentWidth: width
    contentHeight: body.height + 32
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    Theme { id: theme }
    ScrollGlide { flick: root }
    // Preserve wheel/key scrolling without a visible artist-page scrollbar.

    function categoryAvailable(category) {
        switch (category) {
        case "songs": return artistData.songOrder.length > 0;
        case "albums": return artistData.albumCards.length > 0;
        case "singles": return artistData.singleCards.length > 0;
        case "videos": return artistData.media.videos.length > 0;
        case "related": return artistData.media.relatedArtists.length > 0;
        }
        return false;
    }
    function openCategory(category) {
        if (!categoryAvailable(category)) return;
        sectionReturnY = contentY;
        selectedCategory = category;
        contentY = 0;
    }
    function closeCategory() {
        selectedCategory = "";
        Qt.callLater(function() {
            root.contentY = Math.min(root.sectionReturnY, Math.max(0, root.contentHeight - root.height));
        });
    }
    function reveal(item) {
        var y = item.mapToItem(root.contentItem, 0, 0).y;
        if (y < contentY) contentY = Math.max(0, y - 12);
        else if (y + item.height > contentY + height)
            contentY = Math.min(Math.max(0, contentHeight - height), y + item.height - height + 12);
    }
    function showMedia(record) { mediaDialog.selection = record; mediaDialog.open(); }
    function sidebarIcon(key) {
        var paths = {
            songs: '<path d="M8 17V6l12-3v11"/><ellipse cx="5" cy="18" rx="3" ry="2"/><ellipse cx="17" cy="15" rx="3" ry="2"/>',
            albums: '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="3"/><path d="M12 3v3"/>',
            singles: '<rect x="4" y="5" width="16" height="14" rx="2"/><path d="M8 10h8M8 14h5"/>',
            videos: '<rect x="3" y="5" width="18" height="14" rx="3"/><path d="m10 9 5 3-5 3z"/>',
            related: '<circle cx="9" cy="8" r="3"/><path d="M3 20v-2a6 6 0 0 1 12 0v2M16 5a3 3 0 0 1 0 6M18 14a5 5 0 0 1 3 4v2"/>'
        };
        return "data:image/svg+xml," + encodeURIComponent(
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="'
            + theme.inkDim + '" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">'
            + (paths[key] || paths.songs) + '</svg>');
    }

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
        function onNameChanged() {
            root.selectedCategory = "";
            root.sectionReturnY = 0;
            root.contentY = 0;
            mediaDialog.close();
            playlistDialog.close();
            createDialog.close();
        }
    }
    Column {
        id: body
        x: root.gutter
        width: root.bodyWidth
        spacing: 56

        VinylArtistHero {
            objectName: "vinylArtistHero"
            width: parent.width
            visible: root.selectedCategory === ""
            artistData: root.artistData
            onCreatePlaylistRequested: { playlistName.text = ""; createDialog.open(); }
            onItemFocused: item => root.reveal(item)
        }

        Column {
            id: sections
            objectName: "vinylArtistSections"
            visible: root.selectedCategory === ""
            x: sidebar.contentLeft
            width: Math.max(0, parent.width - x)
            spacing: 50

            VinylArtistSongs {
                id: songsSection
                objectName: "vinylArtistSongsSection"
                width: parent.width
                visible: root.artistData.popularSongs.length > 0
                artistData: root.artistData
                onSongRequested: song => root.songRequested(song)
                onAlbumRequested: album => root.albumRequested(album)
                onSeeAllRequested: root.openCategory("songs")
                onItemFocused: item => root.reveal(item)
            }
            VinylArtistShelf {
                id: unrankedSongsSection
                objectName: "vinylArtistUnrankedSongsSection"
                width: parent.width
                visible: root.artistData.popularSongs.length === 0 && records.length > 0
                title: "Songs"
                category: "songs"
                records: root.artistData.songCards
                artistData: root.artistData
                prefix: "vinylArtistUnrankedSong"
                onActivated: record => root.songRequested(record.song)
                onSeeAllRequested: root.openCategory("songs")
                onItemFocused: item => root.reveal(item)
            }
            VinylArtistShelf {
                id: albumsSection
                objectName: "vinylArtistAlbumsSection"
                width: parent.width
                title: "Albums"
                category: "albums"
                records: root.artistData.albumCards
                artistData: root.artistData
                prefix: "vinylArtistAlbum"
                onActivated: record => root.albumRequested(record.album)
                onSeeAllRequested: root.openCategory("albums")
                onItemFocused: item => root.reveal(item)
                toolbar: Component {
                    Flow {
                        width: parent ? parent.width : 0
                        spacing: 6
                        Repeater {
                            model: root.artistData.filters
                            delegate: KeyboardAction {
                                required property var modelData
                                objectName: "vinylArtistFilter_" + modelData.key
                                width: filterText.implicitWidth + 24; height: 34
                                accessibleName: modelData.label
                                onTriggered: root.artistData.filter = modelData.key
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 17
                                    color: root.artistData.filter === modelData.key ? theme.glassHi : "transparent"
                                    border.color: root.artistData.filter === modelData.key ? theme.edge : "transparent"
                                }
                                Text {
                                    id: filterText; anchors.centerIn: parent
                                    text: modelData.label
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
                            Text {
                                anchors.centerIn: parent
                                text: root.artistData.newestFirst ? "Newest first ↓" : "Oldest first ↑"
                                color: sort.interactionActive ? theme.gold : theme.inkDim
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
                category: "singles"
                records: root.artistData.singleCards
                artistData: root.artistData
                prefix: "vinylArtistSingle"
                onActivated: record => root.albumRequested(record.album)
                onSeeAllRequested: root.openCategory("singles")
                onItemFocused: item => root.reveal(item)
            }
            VinylArtistShelf {
                id: videosSection
                objectName: "vinylArtistVideosSection"
                width: parent.width; title: "Videos"
                category: "videos"
                records: root.artistData.media.videos
                artistData: root.artistData
                prefix: "vinylArtistVideo"
                shape: "wide"; badge: "VIDEO"
                onActivated: record => root.showMedia(record)
                onSeeAllRequested: root.openCategory("videos")
                onItemFocused: item => root.reveal(item)
            }
            VinylArtistShelf {
                id: relatedSection
                objectName: "vinylArtistRelatedSection"
                width: parent.width; title: "Related artists"
                category: "related"
                records: root.artistData.media.relatedArtists
                artistData: root.artistData
                prefix: "vinylArtistRelated"
                shape: "round"
                onActivated: record => root.artistRequested(record.title)
                onSeeAllRequested: root.openCategory("related")
                onItemFocused: item => root.reveal(item)
            }
        }

        VinylArtistFullCategory {
            id: fullCategory
            objectName: "vinylArtistFullCategory"
            width: parent.width
            category: root.selectedCategory
            artistData: root.artistData
            onBackRequested: root.closeCategory()
            onAlbumRequested: album => root.albumRequested(album)
            onSongRequested: song => root.songRequested(song)
            onArtistRequested: name => root.artistRequested(name)
            onMediaRequested: record => root.showMedia(record)
            onItemFocused: item => root.reveal(item)
        }
    }

    // The production Colosseum row index: pinned after the hero, collapsible,
    // and driven by the actual visible category sections.
    RowIndexSidebar {
        id: sidebar
        parent: root.contentItem
        x: root.gutter - 5
        z: 5
        pageFlick: root
        flowHost: sections
        backdrop: root.backdrop
        worldName: "Vinyl"
        contextLabel: "ARTIST"
        automationPrefix: "vinylArtistRowIndex"
        currentOffset: 20
        rows: [
            {key:"songs",title:root.artistData.popularSongs.length ? "Popular songs" : "Songs",
                target:root.artistData.popularSongs.length ? songsSection : unrankedSongsSection,
                iconSource:root.sidebarIcon("songs")},
            {key:"albums",title:"Albums",target:albumsSection,iconSource:root.sidebarIcon("albums")},
            {key:"singles",title:"Singles & EPs",target:singlesSection,iconSource:root.sidebarIcon("singles")},
            {key:"videos",title:"Videos",target:videosSection,iconSource:root.sidebarIcon("videos")},
            {key:"related",title:"Related artists",target:relatedSection,iconSource:root.sidebarIcon("related")}
        ]
        onRowRequested: target => {
            root.cancelFlick();
            root.contentY = Math.max(0, Math.min(root.contentHeight - root.height,
                target.mapToItem(root.contentItem, 0, 0).y - sidebar.pinLine));
        }
    }

    Dialog {
        id: mediaDialog
        property var selection: ({title:"",cover:"",provider:"",sourceUrl:"",kind:"",snapshotAt:""})
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 48)
        title: selection.title
        modal: true; focus: true
        palette.window: "#17181d"; palette.windowText: theme.ink
        palette.text: theme.ink; palette.button: "#292a30"
        palette.buttonText: theme.ink
        standardButtons: Dialog.Close
        contentItem: Column {
            spacing: 16
            RoundedPosterImage {
                width: parent.width
                height: Math.min(220, width * 9 / 16)
                radius: 8
                visible: mediaDialog.selection.cover.length > 0
                sources: [root.artistData.artwork(mediaDialog.selection.cover)]
            }
            Label {
                width: parent.width; textFormat: Text.PlainText; wrapMode: Text.WordWrap
                text: mediaDialog.selection.provider + (mediaDialog.selection.snapshotAt
                    ? " · Snapshot " + mediaDialog.selection.snapshotAt : "")
            }
            Label {
                width: parent.width; textFormat: Text.PlainText; wrapMode: Text.WordWrap
                color: theme.inkDim
                text: "Catalogue reference. Playback for this media is not connected in the artist preview. Opening the source leaves Colosseum."
            }
            KeyboardAction {
                objectName: "vinylArtistMediaSource"
                width: parent.width; height: 40
                accessibleName: "Open catalogue source in browser"
                accessibleDescription: mediaDialog.selection.sourceUrl
                onTriggered: Qt.openUrlExternally(mediaDialog.selection.sourceUrl)
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Open catalogue source ↗"
                    color: theme.gold; font.family: theme.ui; font.pixelSize: 14
                }
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
        palette.window: "#17181d"; palette.windowText: theme.ink
        palette.text: theme.ink; palette.base: "#22232a"
        palette.button: "#292a30"; palette.buttonText: theme.ink
        palette.highlight: theme.gold; palette.highlightedText: "#06070b"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            if (root.artistData.createPlaylist(playlistName.text)) {
                var list = root.artistData.playlists[root.artistData.playlists.length - 1];
                Qt.callLater(function() { playlistDialog.selection = list; playlistDialog.open(); });
            }
        }
        Column {
            width: parent.width; spacing: 14
            Label {
                width: parent.width
                text: "Start with the displayed songs. This prototype keeps playlists for this session."
                wrapMode: Text.WordWrap
            }
            TextField {
                id: playlistName
                objectName: "vinylArtistPlaylistName"
                width: parent.width
                placeholderText: "Playlist name"; maximumLength: 80
            }
        }
        onOpened: playlistName.forceActiveFocus()
    }
    Dialog {
        id: playlistDialog
        property var selection: ({name:"",songs:[]})
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 48)
        height: Math.min(500, parent.height - 80)
        title: selection.name
        modal: true
        palette.window: "#17181d"; palette.windowText: theme.ink
        palette.text: theme.ink; palette.base: "#22232a"
        palette.button: "#292a30"; palette.buttonText: theme.ink
        palette.highlight: theme.gold; palette.highlightedText: "#06070b"
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
