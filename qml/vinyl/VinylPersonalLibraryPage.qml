import QtQuick
import QtQuick.Controls
import QtQuick.Window
import ".."

Item {
    id: root
    objectName: "vinylPersonalLibraryPage"
    required property var libraryData
    required property Item backdrop
    property Flickable pageFlick: null
    property int currentIndex: 0
    property string selectedId: ""
    property bool addingSongs: false

    readonly property string section: String(libraryData.section || "albums")
    readonly property string selectedPlaylistId: String(libraryData.selectedPlaylistId || "")
    readonly property bool hasDetail: selectedPlaylistId.length > 0
    readonly property var albums: libraryData.albums || []
    readonly property var artists: libraryData.artists || []
    readonly property var savedSongs: libraryData.savedSongs || []
    readonly property var playlists: libraryData.playlists || []
    readonly property var availableSongs: libraryData.availableSongs || []
    readonly property var selectedPlaylist: libraryData.selectedPlaylist || ({id:"",name:"",title:"",items:[]})
    readonly property string dataError: String(libraryData.error || "")
    readonly property real sideInset: Math.max(0, Math.min(52, (width - 1120) / 2))
    readonly property real contentWidth: Math.max(0, width - sideInset * 2)

    implicitHeight: pageError.y + (pageError.visible ? pageError.height : 0) + 30
    Theme { id: theme }

    function displayTitle(item) { return String((item && (item.title || item.name)) || ""); }
    function displayArtist(item) { return String((item && (item.artist || item.subtitle)) || ""); }
    function playlistName(item) { return String((item && (item.name || item.title)) || ""); }
    function artwork(value) {
        if (!value) return "";
        if (libraryData && typeof libraryData.artwork === "function") {
            var resolved = libraryData.artwork(value);
            if (resolved) return resolved;
        }
        return String(value);
    }
    function setSection(value) {
        if (libraryData && typeof libraryData.setSection === "function") libraryData.setSection(value);
        currentIndex = 0; selectedId = ""; addingSongs = false;
    }
    function showPlaylistList() {
        if (libraryData && typeof libraryData.restoreViewState === "function")
            libraryData.restoreViewState({section:"playlists", selectedPlaylistId:""});
        else if (libraryData && typeof libraryData.setSection === "function")
            libraryData.setSection("playlists");
        selectedId = "";
        addingSongs = false;
    }
    function openPlaylist(id) {
        if (!id || typeof libraryData.openPlaylist !== "function") return;
        currentIndex = Math.max(0, playlists.findIndex(function(p) { return String(p.id || p.playlistId || "") === String(id); }));
        selectedId = String(id); addingSongs = false; libraryData.openPlaylist(id);
    }
    function openSongPicker(item) {
        if (item && item.row && typeof libraryData.openAddPicker === "function")
            libraryData.openAddPicker([item.row]);
    }
    function addAvailableSong(song) {
        var id = selectedPlaylistId;
        if (id && song && song.row && typeof libraryData.addToPlaylist === "function")
            libraryData.addToPlaylist(id, [song.row]);
    }
    function commitPlaylistDialog() {
        var name = String(nameField.text || "").trim();
        if (!name) return false;
        if (dialogMode === "rename") {
            var playlistId = selectedPlaylist.id || selectedPlaylistId;
            if (!libraryData.renamePlaylist(playlistId, name)) return false;
            playlistDialog.close();
            return true;
        }
        var createdId = libraryData.createPlaylist(name, []);
        if (!createdId) return false;
        libraryData.setSection("playlists");
        libraryData.openPlaylist(createdId);
        playlistDialog.close();
        return true;
    }
    function focusCurrent() {
        if (hasDetail) {
            if (playlistPlay.enabled) playlistPlay.forceActiveFocus(Qt.TabFocusReason);
            else if (playlistItemRows.count > 0) {
                var first = playlistItemRows.itemAt(0);
                if (first) first.forceActiveFocus(Qt.TabFocusReason);
            } else playlistAdd.forceActiveFocus(Qt.TabFocusReason);
            return;
        }
        if (section === "songs" && savedSongRows.count > 0) {
            var song = savedSongRows.itemAt(Math.max(0, Math.min(currentIndex, savedSongRows.count - 1)));
            if (song) song.playAction.forceActiveFocus(Qt.TabFocusReason); else sectionTabs.forceActiveFocus(Qt.TabFocusReason);
        } else if (section === "playlists" && playlistRows.count > 0) {
            var pl = playlistRows.itemAt(Math.max(0, Math.min(currentIndex, playlistRows.count - 1)));
            if (pl) pl.forceActiveFocus(Qt.TabFocusReason); else createPlaylist.forceActiveFocus(Qt.TabFocusReason);
        } else if (section === "albums" && albumCards.count > 0) {
            var album = albumCards.itemAt(Math.max(0, Math.min(currentIndex, albumCards.count - 1)));
            if (album) album.forceActiveFocus(Qt.TabFocusReason); else sectionTabs.forceActiveFocus(Qt.TabFocusReason);
        } else if (section === "artists" && artistCards.count > 0) {
            var artist = artistCards.itemAt(Math.max(0, Math.min(currentIndex, artistCards.count - 1)));
            if (artist) artist.forceActiveFocus(Qt.TabFocusReason); else sectionTabs.forceActiveFocus(Qt.TabFocusReason);
        } else sectionTabs.forceActiveFocus(Qt.TabFocusReason);
    }
    function captureViewState() {
        var dataState = libraryData && typeof libraryData.captureViewState === "function"
            ? libraryData.captureViewState() : {section:section, selectedPlaylistId:selectedPlaylistId};
        return {section:dataState.section, selectedPlaylistId:dataState.selectedPlaylistId,
            currentIndex:currentIndex, selectedId:selectedId};
    }
    function restoreViewState(state) {
        if (!state) return;
        if (libraryData && typeof libraryData.restoreViewState === "function")
            libraryData.restoreViewState({section:state.section || section,
                selectedPlaylistId:String(state.selectedPlaylistId || "")});
        currentIndex = Math.max(0, Number(state.currentIndex) || 0);
        selectedId = String(state.selectedId || "");
        addingSongs = false;
    }
    onSectionChanged: if (!hasDetail) { currentIndex = Math.max(0, currentIndex); addingSongs = false; }

    WorldTabBar {
        id: sectionTabs
        objectName: "vinylLibrarySections"
        x: root.sideInset; y: 0; width: root.contentWidth
        backdrop: root.backdrop
        tabPrefix: "vinylLibrarySection"
        focusOnPointer: false
        currentTab: root.hasDetail ? "playlists" : root.section
        tabModel: [{key:"albums",label:"Albums"},{key:"artists",label:"Artists"},{key:"songs",label:"Songs"},{key:"playlists",label:"Playlists"}]
        onTabRequested: tab => {
            if (tab === "playlists" && root.hasDetail) root.showPlaylistList();
            else root.setSection(tab);
        }
    }

    Item {
        id: content
        x: root.sideInset
        y: sectionTabs.y + sectionTabs.height + 24
        width: root.contentWidth
        height: root.hasDetail ? detailContent.height
            : root.section === "albums" ? albumsContent.height
            : root.section === "artists" ? artistsContent.height
            : root.section === "songs" ? songsContent.height : playlistsContent.height

        Item {
            id: albumsContent
            width: parent.width
            height: albumHeading.height + 20 + Math.max(albumGrid.height, root.albums.length ? 0 : 48)
            visible: !root.hasDetail && root.section === "albums"
            Text { id: albumHeading; width: parent.width; text: "Albums"; color: theme.ink; font.family: theme.display; font.pixelSize: 29 }
            Text { y: albumHeading.height + 18; width: parent.width; visible: root.albums.length === 0; text: "Albums from your library will appear here."; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; wrapMode: Text.WordWrap }
            Grid {
                id: albumGrid
                y: albumHeading.height + 20; width: parent.width; height: implicitHeight
                columns: Math.max(1, Math.min(6, Math.floor((width + 24) / 188)))
                columnSpacing: 24; rowSpacing: 28
                Repeater {
                    id: albumCards
                    model: root.albums
                    delegate: KeyboardAction {
                        id: albumCard
                        required property var modelData
                        required property int index
                        objectName: "vinylLibraryAlbum_" + index
                        focusOnPointer: false; showFocusFrame: false
                        width: (albumGrid.width - (albumGrid.columns - 1) * albumGrid.columnSpacing) / albumGrid.columns
                        height: art.height + copy.implicitHeight + 12
                        accessibleName: root.displayTitle(modelData) + (root.displayArtist(modelData) ? ", " + root.displayArtist(modelData) : "")
                        onActiveFocusChanged: if (activeFocus) { root.currentIndex=index; root.selectedId=String(modelData.albumId || modelData.id || ""); }
                        onTriggered: root.libraryData.activateAlbum(modelData)
                        RoundedPosterImage { id: art; width: albumCard.width; height: width; radius: 12; hovered: albumCard.interactionActive; sources: root.artwork(albumCard.modelData.cover) ? [root.artwork(albumCard.modelData.cover)] : [] }
                        Column {
                            id: copy; y: art.height + 11; width: parent.width; spacing: 4
                            Text { width: parent.width; text: root.displayTitle(albumCard.modelData); textFormat: Text.PlainText; color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold; maximumLineCount: 2; wrapMode: Text.WordWrap; elide: Text.ElideRight }
                            Text { width: parent.width; text: root.displayArtist(albumCard.modelData); textFormat: Text.PlainText; visible: text.length > 0; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11; maximumLineCount: 1; elide: Text.ElideRight }
                        }
                    }
                }
            }
        }

        Item {
            id: artistsContent
            width: parent.width
            height: artistHeading.height + 20 + Math.max(artistGrid.height, root.artists.length ? 0 : 48)
            visible: !root.hasDetail && root.section === "artists"
            Text { id: artistHeading; width: parent.width; text: "Artists"; color: theme.ink; font.family: theme.display; font.pixelSize: 29 }
            Text { y: artistHeading.height + 18; width: parent.width; visible: root.artists.length === 0; text: "Artists from your library will appear here."; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; wrapMode: Text.WordWrap }
            Grid {
                id: artistGrid
                y: artistHeading.height + 20; width: parent.width; height: implicitHeight
                columns: Math.max(1, Math.min(6, Math.floor((width + 24) / 188)))
                columnSpacing: 24; rowSpacing: 28
                Repeater {
                    id: artistCards
                    model: root.artists
                    delegate: KeyboardAction {
                        id: artistCard
                        required property var modelData
                        required property int index
                        objectName: "vinylLibraryArtist_" + index
                        focusOnPointer: false; showFocusFrame: false
                        width: (artistGrid.width - (artistGrid.columns - 1) * artistGrid.columnSpacing) / artistGrid.columns
                        height: art.height + copy.implicitHeight + 12
                        accessibleName: root.displayTitle(modelData)
                        onActiveFocusChanged: if (activeFocus) { root.currentIndex=index; root.selectedId=String(modelData.artistId || modelData.id || ""); }
                        onTriggered: root.libraryData.activateArtist(modelData)
                        RoundedPosterImage { id: art; width: artistCard.width; height: width; radius: width / 2; hovered: artistCard.interactionActive; sources: root.artwork(artistCard.modelData.cover) ? [root.artwork(artistCard.modelData.cover)] : [] }
                        Text { id: copy; y: art.height + 11; width: parent.width; text: root.displayTitle(artistCard.modelData); textFormat: Text.PlainText; color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; maximumLineCount: 2; wrapMode: Text.WordWrap; elide: Text.ElideRight }
                    }
                }
            }
        }


        Item {
            id: songsContent
            width: parent.width
            height: songHeading.height + 20 + Math.max(savedSongList.height, root.savedSongs.length ? 0 : 48)
            visible: !root.hasDetail && root.section === "songs"
            Text { id: songHeading; width: parent.width - 94; text: "Saved songs"; color: theme.ink; font.family: theme.display; font.pixelSize: 29 }
            VinylWorldButton {
                objectName: "vinylLibraryPlaySavedSongs"
                x: parent.width - 82; y: -2; width: 38; height: 38
                backdrop: root.backdrop; label: "Play saved songs"; symbol: "play"
                enabled: root.savedSongs.length > 0
                onTriggered: root.libraryData.playSavedSongs(false)
            }
            VinylWorldButton {
                objectName: "vinylLibraryShuffleSavedSongs"
                x: parent.width - 40; y: -2; width: 38; height: 38
                backdrop: root.backdrop; label: "Shuffle saved songs"; symbol: "shuffle"
                enabled: root.savedSongs.length > 0
                onTriggered: root.libraryData.playSavedSongs(true)
            }
            Text { y: songHeading.height + 18; width: parent.width; visible: root.savedSongs.length === 0; text: "Songs you save will appear here."; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; wrapMode: Text.WordWrap }
            Column {
                id: savedSongList
                y: songHeading.height + 20; width: parent.width
                height: savedSongRows.count * 68
                spacing: 2
                Repeater {
                    id: savedSongRows
                    model: root.savedSongs
                    delegate: Item {
                        id: savedRow
                        required property var modelData
                        required property int index
                        objectName: "vinylLibrarySavedSong_" + index
                        width: savedSongList.width; height: 66
                        readonly property bool unavailable: modelData.unavailable === true
                        property alias playAction: rowPlay
                        Rectangle { anchors.fill: parent; radius: 7; color: rowPlay.interactionActive ? theme.glassTint : "transparent" }
                        KeyboardAction {
                            objectName: "vinylLibrarySavedSongCover_" + savedRow.index
                            x:8; anchors.verticalCenter:parent.verticalCenter; width:46; height:46
                            focusOnPointer:false; enabled:!savedRow.unavailable
                            accessibleName:"Open " + root.displayTitle(savedRow.modelData)
                            onTriggered:root.libraryData.openSong(savedRow.modelData)
                            RoundedPosterImage {anchors.fill:parent;radius:6;sources:root.artwork(savedRow.modelData.cover) ? [root.artwork(savedRow.modelData.cover)] : []}
                        }
                        KeyboardAction {
                            id: rowPlay
                            objectName: "vinylLibrarySavedSongPlay_" + savedRow.index
                            x:66; width: Math.max(0, parent.width - 160); height: parent.height
                            focusOnPointer: false; showFocusFrame: false
                            enabled: !savedRow.unavailable
                            accessibleName: "Play " + root.displayTitle(savedRow.modelData) + (savedRow.unavailable ? ", unavailable" : "")
                            onActiveFocusChanged: if (activeFocus) { root.currentIndex=savedRow.index; root.selectedId=String(savedRow.modelData.itemId || savedRow.modelData.id || ""); }
                            onTriggered: root.libraryData.playSavedSong(savedRow.modelData.id)
                            Column {
                                width: Math.max(0, parent.width - 12); anchors.verticalCenter: parent.verticalCenter; spacing: 4
                                Text { width: parent.width; text: root.displayTitle(savedRow.modelData); textFormat: Text.PlainText; color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                Text { width: parent.width; text: root.displayArtist(savedRow.modelData) + (savedRow.unavailable ? " · Unavailable" : ""); textFormat: Text.PlainText; color: savedRow.unavailable ? theme.inkDimmer : theme.inkDim; font.family: theme.ui; font.pixelSize: 11; elide: Text.ElideRight }
                            }
                        }
                        KeyboardAction {
                            objectName: "vinylLibrarySavedSongAdd_" + savedRow.index
                            x: parent.width - 42; anchors.verticalCenter: parent.verticalCenter; width: 36; height: 36
                            focusOnPointer: false
                            accessibleName: "Add " + root.displayTitle(savedRow.modelData) + " to a playlist"
                            onTriggered: root.openSongPicker(savedRow.modelData)
                            Rectangle { anchors.fill: parent; radius: 9; color: parent.interactionActive ? theme.glassTint : "transparent" }
                            VinylControlIcon { anchors.centerIn: parent; symbol: "plus"; color: theme.inkDim }
                        }
                        KeyboardAction {
                            objectName: "vinylLibrarySavedSongRemove_" + savedRow.index
                            x: parent.width - 84; anchors.verticalCenter: parent.verticalCenter; width: 36; height: 36
                            focusOnPointer: false
                            accessibleName: "Remove " + root.displayTitle(savedRow.modelData) + " from saved songs"
                            onTriggered: root.libraryData.toggleSong(savedRow.modelData)
                            Rectangle { anchors.fill: parent; radius: 9; color: parent.interactionActive ? theme.glassTint : "transparent" }
                            VinylControlIcon { anchors.centerIn: parent; symbol: "saved"; color: theme.inkDim }
                        }
                    }
                }
            }
        }

        Item {
            id: playlistsContent
            width: parent.width
            height: playlistHeading.height + 20 + Math.max(playlistList.height, root.playlists.length ? 0 : 48)
            visible: !root.hasDetail && root.section === "playlists"
            Text { id: playlistHeading; width: parent.width - 52; text: "Your playlists"; color: theme.ink; font.family: theme.display; font.pixelSize: 29; elide: Text.ElideRight }
            VinylWorldButton {
                id: createPlaylist
                objectName: "vinylLibraryCreatePlaylist"
                x: parent.width - 40; y: -2; width: 38; height: 38
                backdrop: root.backdrop
                label: "Create playlist"
                symbol: "plus"
                onTriggered: {
                    nameField.text = ""; dialogMode = "create";
                    playlistDialog.title = "Create playlist"; playlistDialog.open();
                }
            }
            Text { y: playlistHeading.height + 18; width: parent.width; visible: root.playlists.length === 0; text: "Create a playlist to keep songs together for later."; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; wrapMode: Text.WordWrap }
            Column {
                id: playlistList
                y: playlistHeading.height + 20; width: parent.width
                height: playlistRows.count * 66
                spacing: 4
                Repeater {
                    id: playlistRows
                    model: root.playlists
                    delegate: KeyboardAction {
                        id: playlistRow
                        required property var modelData
                        required property int index
                        objectName: "vinylLibraryPlaylist_" + index
                        width: playlistList.width; height: 62
                        focusOnPointer: false; showFocusFrame: false
                        accessibleName: "Open playlist " + root.playlistName(modelData)
                        onActiveFocusChanged: if (activeFocus) { root.currentIndex=index; root.selectedId=String(modelData.id || modelData.playlistId || ""); }
                        onTriggered: root.openPlaylist(modelData.id || modelData.playlistId)
                        Rectangle { anchors.fill: parent; radius: 8; color: playlistRow.interactionActive ? theme.glassTint : "transparent" }
                        RoundedPosterImage { x: 8; anchors.verticalCenter: parent.verticalCenter; width: 44; height: 44; radius: 6; sources: root.artwork(playlistRow.modelData.cover) ? [root.artwork(playlistRow.modelData.cover)] : [] }
                        Column {
                            x: 66; width: Math.max(0, parent.width - x - 30); anchors.verticalCenter: parent.verticalCenter; spacing: 4
                            Text { width: parent.width; text: root.playlistName(playlistRow.modelData); textFormat: Text.PlainText; color: theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight }
                            Text { width: parent.width; text: String(playlistRow.modelData.count !== undefined ? playlistRow.modelData.count : ((playlistRow.modelData.items || []).length)) + " songs"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 11; elide: Text.ElideRight }
                        }
                        Text { anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter; text: "›"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 23 }
                    }
                }
            }
        }


        Item {
            id: detailContent
            width: parent.width
            height: detailHeading.height + actionRow.height + 16
                + (detailError.visible ? detailError.height + 12 : 0)
                + (addSongsPanel.visible ? addSongsPanel.height + 18 : 0)
                + Math.max(playlistItems.height, (root.selectedPlaylist.items || []).length ? 0 : emptyPlaylist.height + 16)
            visible: root.hasDetail

            Text {
                id: detailHeading
                x: 46; width: parent.width - x
                text: root.playlistName(root.selectedPlaylist)
                textFormat: Text.PlainText
                color: theme.ink
                font.family: theme.display
                font.pixelSize: Math.min(32, root.width < 600 ? 25 : 32)
                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
            }
            KeyboardAction {
                objectName: "vinylPlaylistBack"
                width: 38; height: 38
                focusOnPointer: false
                accessibleName: "Back to playlists"
                onTriggered: root.showPlaylistList()
                Rectangle { anchors.fill: parent; radius: 10; color: parent.interactionActive ? theme.glassTint : "transparent" }
                VinylControlIcon { anchors.centerIn: parent; symbol: "arrowLeft"; color: theme.inkDim }
            }
            Flow {
                id: actionRow
                y: detailHeading.height + 16
                width: parent.width
                spacing: 8
                KeyboardAction {
                    id: playlistPlay
                    objectName: "vinylPlaylistPlay"
                    width: 38; height: 36
                    focusOnPointer: false
                    accessibleName: "Play playlist"
                    enabled: (root.selectedPlaylist.items || []).length > 0
                    onTriggered: root.libraryData.playPlaylist(root.selectedPlaylist.id, false)
                    Rectangle { anchors.fill: parent; radius: 9; color: parent.interactionActive ? theme.glassTint : "transparent" }
                    VinylControlIcon { anchors.centerIn: parent; symbol: "play"; color: theme.ink }
                }
                KeyboardAction {
                    objectName: "vinylPlaylistShuffle"
                    width: 38; height: 36
                    focusOnPointer: false
                    accessibleName: "Shuffle playlist"
                    enabled: (root.selectedPlaylist.items || []).length > 0
                    onTriggered: root.libraryData.playPlaylist(root.selectedPlaylist.id, true)
                    Rectangle { anchors.fill: parent; radius: 9; color: parent.interactionActive ? theme.glassTint : "transparent" }
                    VinylControlIcon { anchors.centerIn: parent; symbol: "shuffle"; color: theme.ink }
                }
                KeyboardAction {
                    objectName: "vinylPlaylistQueue"
                    width: 38; height: 36
                    focusOnPointer: false
                    accessibleName: "Queue playlist"
                    enabled: (root.selectedPlaylist.items || []).length > 0
                    onTriggered: root.libraryData.enqueuePlaylist(root.selectedPlaylist.id, false)
                    Rectangle { anchors.fill: parent; radius: 9; color: parent.interactionActive ? theme.glassTint : "transparent" }
                    VinylControlIcon { anchors.centerIn: parent; symbol: "queue"; color: theme.ink }
                }
                KeyboardAction {
                    id: playlistAdd
                    objectName: "vinylPlaylistAdd"
                    width: 38; height: 36
                    focusOnPointer: false
                    accessibleName: root.addingSongs ? "Close add songs" : "Add songs to playlist"
                    onTriggered: root.addingSongs = !root.addingSongs
                    Rectangle { anchors.fill: parent; radius: 9; color: parent.interactionActive ? theme.glassTint : "transparent" }
                    VinylControlIcon { anchors.centerIn: parent; symbol: root.addingSongs ? "cancel" : "plus"; color: theme.ink }
                }
                KeyboardAction {
                    objectName: "vinylPlaylistRename"
                    width: 38; height: 36
                    focusOnPointer: false
                    accessibleName: "Rename playlist"
                    onTriggered: {
                        nameField.text = root.playlistName(root.selectedPlaylist);
                        dialogMode = "rename"; playlistDialog.title = "Rename playlist"; playlistDialog.open();
                    }
                    Rectangle { anchors.fill: parent; radius: 9; color: parent.interactionActive ? theme.glassTint : "transparent" }
                    VinylControlIcon { anchors.centerIn: parent; symbol: "edit"; color: theme.inkDim }
                }
                KeyboardAction {
                    objectName: "vinylPlaylistDelete"
                    width: 38; height: 36
                    focusOnPointer: false
                    accessibleName: "Delete playlist"
                    onTriggered: {
                        var id = root.selectedPlaylist.id || root.selectedPlaylistId;
                        if (root.libraryData.deletePlaylist(id)) root.showPlaylistList();
                    }
                    Rectangle { anchors.fill: parent; radius: 9; color: parent.interactionActive ? theme.glassTint : "transparent" }
                    VinylControlIcon { anchors.centerIn: parent; symbol: "trash"; color: theme.inkDim }
                }
            }
            Text {
                id: detailError
                y: actionRow.y + actionRow.height + 10
                width: parent.width
                visible: root.dataError.length > 0
                text: root.dataError
                textFormat: Text.PlainText
                color: theme.gold
                font.family: theme.ui
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Column {
                id: addSongsPanel
                y: actionRow.y + actionRow.height + (detailError.visible ? detailError.height + 18 : 12)
                width: parent.width
                height: implicitHeight
                visible: root.addingSongs
                spacing: 8
                Text { width: parent.width; text: "Add songs"; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                Text { visible: root.availableSongs.length === 0; width: parent.width; text: "No other saved songs are available."; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                ListView {
                    id: availableSongList
                    objectName: "vinylPlaylistAvailableSongs"
                    width: parent.width; height: Math.min(260, contentHeight)
                    visible: root.availableSongs.length > 0
                    clip: true; spacing: 2; model: root.availableSongs
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: KeyboardAction {
                        id: availableSongRow
                        required property var modelData
                        required property int index
                        objectName: "vinylPlaylistAvailableSong_" + index
                        width: availableSongList.width; height: 48
                        focusOnPointer: false; showFocusFrame: false
                        accessibleName: "Add " + root.displayTitle(modelData) + " to playlist"
                        onTriggered: root.addAvailableSong(modelData)
                        Rectangle { anchors.fill: parent; radius: 7; color: parent.interactionActive ? theme.glassTint : "transparent" }
                        Text {
                            x: 10; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 20
                            text: root.displayTitle(availableSongRow.modelData) + (root.displayArtist(availableSongRow.modelData) ? " · " + root.displayArtist(availableSongRow.modelData) : "")
                            textFormat: Text.PlainText; color: theme.ink; font.family: theme.ui; font.pixelSize: 12; elide: Text.ElideRight
                        }
                    }
                }
            }
            Column {
                id: playlistItems
                y: Math.max(actionRow.y + actionRow.height + (detailError.visible ? detailError.height + 18 : 12),
                    addSongsPanel.visible ? addSongsPanel.y + addSongsPanel.height + 16 : 0)
                width: parent.width
                height: playlistItemRows.count * (root.contentWidth < 280 ? 103 : 65)
                spacing: 3
                Repeater {
                    id: playlistItemRows
                    model: root.selectedPlaylist.items || []
                    delegate: Item {
                        id: playlistItemRow
                        required property var modelData
                        required property int index
                        objectName: "vinylPlaylistItem_" + index
                        width: playlistItems.width; height: root.contentWidth < 280 ? 100 : 62
                        readonly property string itemId: String(modelData.itemId || modelData.id || "")
                        Rectangle { anchors.fill: parent; radius: 7; color: itemPlay.interactionActive ? theme.glassTint : "transparent" }
                        KeyboardAction {
                            id: itemPlay
                            objectName: "vinylPlaylistItemPlay_" + playlistItemRow.index
                            enabled: !playlistItemRow.modelData.unavailable
                            width: root.contentWidth < 280 ? parent.width : Math.max(0, parent.width - 138)
                            height: root.contentWidth < 280 ? 58 : parent.height
                            focusOnPointer: false; showFocusFrame: false
                            accessibleName: "Play " + root.displayTitle(playlistItemRow.modelData) + (playlistItemRow.modelData.unavailable ? ", unavailable" : "")
                            onActiveFocusChanged: if (activeFocus) { root.currentIndex=playlistItemRow.index; root.selectedId=playlistItemRow.itemId; }
                            onTriggered: {
                                if (typeof root.libraryData.playPlaylistItem === "function")
                                    root.libraryData.playPlaylistItem(root.selectedPlaylist.id || root.selectedPlaylistId, playlistItemRow.itemId);
                            }
                            Text { x: 8; anchors.verticalCenter: parent.verticalCenter; width: 24; text: String(playlistItemRow.index + 1).padStart(2, "0"); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11 }
                            RoundedPosterImage { x: 38; anchors.verticalCenter: parent.verticalCenter; width: 42; height: 42; radius: 6; sources: root.artwork(playlistItemRow.modelData.cover) ? [root.artwork(playlistItemRow.modelData.cover)] : [] }
                            Column {
                                x: 90; width: Math.max(0, parent.width - x - 8); anchors.verticalCenter: parent.verticalCenter; spacing: 4
                                Text { width: parent.width; text: root.displayTitle(playlistItemRow.modelData); textFormat: Text.PlainText; color: theme.ink; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                Text { width: parent.width; text: root.displayArtist(playlistItemRow.modelData) + (playlistItemRow.modelData.unavailable ? " · Unavailable" : ""); textFormat: Text.PlainText; color: theme.inkDim; font.family: theme.ui; font.pixelSize: 10; elide: Text.ElideRight }
                            }
                        }
                        KeyboardAction {
                            objectName: "vinylPlaylistMoveUp_" + playlistItemRow.index
                            x: root.contentWidth < 280 ? Math.max(4, (parent.width - 114) / 2) : parent.width - 132
                            y: root.contentWidth < 280 ? 62 : (parent.height - 34) / 2
                            width: 34; height: 34
                            focusOnPointer: false; accessibleName: "Move " + root.displayTitle(playlistItemRow.modelData) + " up" + (playlistItemRow.modelData.unavailable ? ", unavailable" : "")
                            enabled: playlistItemRow.index > 0
                            onTriggered: root.libraryData.movePlaylistItem(root.selectedPlaylist.id || root.selectedPlaylistId, playlistItemRow.itemId, playlistItemRow.index - 1)
                            Rectangle { anchors.fill: parent; radius: 8; color: parent.interactionActive ? theme.glassTint : "transparent" }
                            VinylControlIcon { anchors.centerIn: parent; symbol: "arrowUp"; color: theme.inkDim }
                        }
                        KeyboardAction {
                            objectName: "vinylPlaylistMoveDown_" + playlistItemRow.index
                            x: root.contentWidth < 280 ? Math.max(4, (parent.width - 114) / 2 + 40) : parent.width - 92
                            y: root.contentWidth < 280 ? 62 : (parent.height - 34) / 2
                            width: 34; height: 34
                            focusOnPointer: false; accessibleName: "Move " + root.displayTitle(playlistItemRow.modelData) + " down" + (playlistItemRow.modelData.unavailable ? ", unavailable" : "")
                            enabled: playlistItemRow.index < (root.selectedPlaylist.items || []).length - 1
                            onTriggered: root.libraryData.movePlaylistItem(root.selectedPlaylist.id || root.selectedPlaylistId, playlistItemRow.itemId, playlistItemRow.index + 1)
                            Rectangle { anchors.fill: parent; radius: 8; color: parent.interactionActive ? theme.glassTint : "transparent" }
                            VinylControlIcon { anchors.centerIn: parent; symbol: "arrowDown"; color: theme.inkDim }
                        }
                        KeyboardAction {
                            objectName: "vinylPlaylistRemove_" + playlistItemRow.index
                            x: root.contentWidth < 280 ? Math.max(4, (parent.width - 114) / 2 + 80) : parent.width - 48
                            y: root.contentWidth < 280 ? 62 : (parent.height - 34) / 2
                            width: 34; height: 34
                            focusOnPointer: false; accessibleName: "Remove " + root.displayTitle(playlistItemRow.modelData) + " from playlist" + (playlistItemRow.modelData.unavailable ? ", unavailable" : "")
                            onTriggered: root.libraryData.removePlaylistItem(root.selectedPlaylist.id || root.selectedPlaylistId, playlistItemRow.itemId)
                            Rectangle { anchors.fill: parent; radius: 8; color: parent.interactionActive ? theme.glassTint : "transparent" }
                            VinylControlIcon { anchors.centerIn: parent; symbol: "cancel"; color: theme.inkDim }
                        }
                    }
                }
            }
            Text {
                id: emptyPlaylist
                x: 0
                y: playlistItems.y + 10
                width: parent.width
                visible: (root.selectedPlaylist.items || []).length === 0
                text: "This playlist is empty. Add songs from your library."
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14; wrapMode: Text.WordWrap
            }
        }

    }

    Text {
        id: pageError
        x: root.sideInset; y: content.y + content.height + 4; width: root.contentWidth
        visible: root.dataError.length > 0 && !root.hasDetail
        text: root.dataError; textFormat: Text.PlainText
        color: theme.gold; font.family: theme.ui; font.pixelSize: 12; wrapMode: Text.WordWrap
    }

    property string dialogMode: "create"
    property Item dialogReturnFocus: null

    Dialog {
        id: playlistDialog
        objectName: "vinylLibraryPlaylistDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.max(0, Math.min(420, parent.width - 40))
        title: "Create playlist"
        modal: true
        standardButtons: Dialog.NoButton
        palette.window: "#17181d"
        palette.windowText: theme.ink
        palette.text: theme.ink
        palette.base: "#22232a"
        palette.button: "#292a30"
        palette.buttonText: theme.ink
        palette.highlight: theme.gold
        palette.highlightedText: "#06070b"

        contentItem: Column {
            width: parent.width
            spacing: 12
            Text {
                width: parent.width
                text: root.dialogMode === "create" ? "Create an empty playlist." : "Choose a new name for this playlist."
                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12; wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                visible: root.dataError.length > 0
                text: root.dataError; textFormat: Text.PlainText
                color: theme.gold; font.family: theme.ui; font.pixelSize: 11; wrapMode: Text.WordWrap
            }
            TextField {
                id: nameField
                objectName: "vinylLibraryPlaylistName"
                width: parent.width
                height: 42
                maximumLength: 80
                placeholderText: "Playlist name"
                color: theme.ink
                placeholderTextColor: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 14
                padding: 14
                selectByMouse: true
                onAccepted: root.commitPlaylistDialog()
                background: Glass { backdrop: root.backdrop; radius: 12; tint: 0.06; scrim: 0.18 }
            }
        }

        background: Glass { backdrop: root.backdrop; radius: 18; tint: 0.1; scrim: 0.94 }
        footer: Row {
            width: parent.width
            spacing: 8
            layoutDirection: Qt.RightToLeft
            VinylWorldButton {
                objectName: "vinylLibraryPlaylistConfirm"
                width: 38; height: 38
                backdrop: root.backdrop
                label: root.dialogMode === "create" ? "Create playlist" : "Save playlist name"
                symbol: "check"
                enabled: String(nameField.text || "").trim().length > 0
                onTriggered: root.commitPlaylistDialog()
            }
            VinylWorldButton {
                objectName: "vinylLibraryPlaylistCancel"
                width: 38; height: 38
                backdrop: root.backdrop
                label: "Cancel playlist edit"
                symbol: "cancel"
                onTriggered: playlistDialog.close()
            }
        }
        onAboutToShow: {var window=root.Window.window;root.dialogReturnFocus=window ? window.activeFocusItem : null;}
        onOpened: nameField.forceActiveFocus(Qt.PopupFocusReason)
        onClosed: Qt.callLater(function() {
            var target=root.dialogReturnFocus;
            if(target && target.visible && target.enabled)target.forceActiveFocus(Qt.PopupFocusReason);
            else root.forceActiveFocus(Qt.OtherFocusReason);
        })
    }

}
