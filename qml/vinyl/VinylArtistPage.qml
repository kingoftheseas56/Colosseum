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
    signal backRequested()

    property real gutter: width < 720 ? 24 : width < 1100 ? 36 : 54
    readonly property real bodyWidth: width - 2 * gutter
    property string selectedCategory: ""
    property real sectionReturnY: 0

    property Item dialogReturnItem: null
    property bool systemMenuOpen: false

    readonly property alias heroItem: hero
    readonly property int albumGridColumns: bodyWidth >= 1340 ? 6 : bodyWidth >= 980 ? 5 : bodyWidth >= 620 ? 4 : 2
    readonly property real albumTileWidth: (bodyWidth - (albumGridColumns - 1) * 21) / albumGridColumns
    readonly property int mediaGridColumns: bodyWidth >= 980 ? 3 : bodyWidth >= 620 ? 2 : 1
    readonly property real mediaCardWidth: (bodyWidth - (mediaGridColumns - 1) * 23) / mediaGridColumns

    contentWidth: width
    contentHeight: (selectedCategory === "" ? mainFlow.height : (body.y + fullCategory.height)) + 120
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    Theme { id: theme }
    ScrollGlide { flick: root }
    // Scrolling is keyboard/wheel driven; the artist destination has no visible bars.

    function switchTab(tabKey, focusTabButton) {
        var valid = ["albums", "singles", "videos", "live"];
        if (valid.indexOf(tabKey) < 0)
            return;
        releasePicker.closePopup(false);
        artistData.activeTab = tabKey;
        Qt.callLater(function() {
            root.contentY = Math.max(0, Math.min(root.contentHeight - root.height, hero.height - artistTitleBar.height));
        });
        if (focusTabButton) {
            var idx = valid.indexOf(tabKey);
            var btn = tabRepeater.itemAt(idx);
            if (btn)
                btn.forceActiveFocus(Qt.TabFocusReason);
        }
    }

    function focusAlbumTileAt(targetIndex) {
        if (targetIndex < 0 || targetIndex >= discographyRepeater.count)
            return false;
        var tile = discographyRepeater.itemAt(targetIndex);
        if (tile) {
            tile.forceActiveFocus(Qt.TabFocusReason);
            reveal(tile);
            return true;
        }
        return false;
    }

    function activateDiscographyCard(card, originItem) {
        if (!card)
            return;
        if (card.playable && card.album && card.album.tracks && card.album.tracks.length > 0) {
            root.albumRequested(card.album);
            return;
        }
        dialogReturnItem = originItem || null;
        releaseDetailDialog.selection = card;
        releaseDetailDialog.open();
    }

    function openSearchDialog(originItem) {
        releasePicker.closePopup(false);
        dialogReturnItem = originItem || searchButton;
        searchInput.text = "";
        searchDialog.open();
    }

    function openBackdropsDialog(originItem) {
        releasePicker.closePopup(false);
        dialogReturnItem = originItem || backdropsButton;
        backdropDialog.open();
    }

    function restoreDialogFocus() {
        if (dialogReturnItem) {
            var target = dialogReturnItem;
            dialogReturnItem = null;
            Qt.callLater(function() {
                if (target)
                    target.forceActiveFocus(Qt.PopupFocusReason);
            });
        }
    }

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
        releasePicker.closePopup(false);
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
        if (!item) return;
        var y = item.mapToItem(root.contentItem, 0, 0).y;
        var topInset = artistTitleBar.height + (selectedCategory === "" ? stickyTabBar.height : 0) + 12;
        var bottomInset = hudPlayer.visible ? hudPlayer.height + 30 : 12;
        if (y < contentY + topInset) contentY = Math.max(0, y - topInset);
        else if (y + item.height > contentY + height - bottomInset)
            contentY = Math.min(Math.max(0, contentHeight - height), y + item.height - height + bottomInset);
    }

    function showMedia(record, originItem) {
        dialogReturnItem = originItem || null;
        mediaDialog.selection = record;
        mediaDialog.open();
    }

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

    Keys.onDownPressed: contentY = Math.min(Math.max(0, contentHeight - height), contentY + 72)
    Keys.onUpPressed: contentY = Math.max(0, contentY - 72)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
            if (releasePicker.open) {
                releasePicker.closePopup(true);
                event.accepted = true;
                return;
            }
            if (root.systemMenuOpen) {
                root.systemMenuOpen = false;
                moreButton.forceActiveFocus(Qt.PopupFocusReason);
                event.accepted = true;
                return;
            }
            root.backRequested();
            event.accepted = true;
            return;
        }
        if (event.key === Qt.Key_Slash) {
            root.openSearchDialog(searchButton);
            event.accepted = true;
            return;
        }
        if (event.key === Qt.Key_Home) {
            contentY = 0;
            event.accepted = true;
            return;
        }
        if (event.key === Qt.Key_End) {
            contentY = Math.max(0, contentHeight - height);
            event.accepted = true;
            return;
        }
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
            releasePicker.closePopup(false);
            root.systemMenuOpen = false;
            mediaDialog.close();
            playlistDialog.close();
            createDialog.close();
            searchDialog.close();
            backdropDialog.close();
            releaseDetailDialog.close();
        }
    }

    // Viewport-locked synchronized full-page blurred wallpaper skin (.page-skin parity).
    // Stays fixed to the viewport while content scrolls above it and crossfades in lockstep
    // with the sharp hero layers.
    VinylBackdrop {
        id: internalArtistSkin
        objectName: "vinylArtistWallpaper"
        x: 0
        y: root.contentY
        width: root.width
        height: root.height
        z: -2
        artistSkinActive: true
        artistLayerASource: hero.layerASource
        artistLayerBSource: hero.layerBSource
        artistActiveLayer: hero.activeVisualLayer
        artistLayerAFocalX: hero.layerAFocalX
        artistLayerAFocalY: hero.layerAFocalY
        artistLayerBFocalX: hero.layerBFocalX
        artistLayerBFocalY: hero.layerBFocalY
        reducedMotion: root.artistData.reducedMotion
    }

    // Outside click catcher for the page-specific system menu
    MouseArea {
        anchors.fill: parent
        z: 28
        visible: root.systemMenuOpen
        onClicked: {
            if (root.systemMenuOpen)
                root.systemMenuOpen = false;
        }
    }

    // Main scrollable artist room flow (Hero -> Sticky Tab Bar spacer -> Tab Panels -> Overview shelves)
    Column {
        id: mainFlow
        width: root.width
        visible: root.selectedCategory === ""
        spacing: 0

        VinylArtistHero {
            id: hero
            objectName: "vinylArtistHero"
            width: root.width
            viewportHeight: root.height
            horizontalGutter: root.gutter
            fullBleed: true
            skinLayerAReady: internalArtistSkin.artistLayerAReady
            skinLayerBReady: internalArtistSkin.artistLayerBReady
            artistData: root.artistData
            onCreatePlaylistRequested: { playlistName.text = ""; createDialog.open(); }
            onBackdropsRequested: root.openBackdropsDialog(null)
            onItemFocused: item => root.reveal(item)
        }

        // Flow spacer for the 58px sticky tab bar
        Item {
            id: tabBarSlot
            width: root.width
            height: 58
        }

        // Active Tab Panel container (.content parity)
        Column {
            id: tabPanelsContainer
            objectName: "vinylArtistTabPanels"
            x: root.gutter
            width: root.bodyWidth
            topPadding: 48
            bottomPadding: 44
            spacing: 32

            // TAB 1: ALBUMS (27-release discography grid + shared All releases picker + Sort toggle)
            Column {
                id: albumsPanel
                objectName: "vinylArtistPanel_albums"
                width: parent.width
                visible: root.artistData.activeTab === "albums"
                spacing: 24

                // Section header + Controls bar (.section-head + .controls parity)
                Item {
                    width: parent.width
                    height: 156

                    Column {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        spacing: 4
                        Text {
                            text: "THE COLLECTION / 01"
                            color: theme.gold
                            font.family: theme.ui
                            font.pixelSize: 11
                            font.weight: Font.Bold
                            font.letterSpacing: 2.2
                        }
                        Row {
                            spacing: 14
                            Text {
                                text: "Discography"
                                color: theme.ink
                                font.family: theme.display
                                font.pixelSize: Math.max(38, Math.min(55, root.width * 0.0315))
                                font.weight: Font.Normal
                            }
                            Text {
                                visible: false
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 5
                                text: String(root.artistData.discographyCards.length)
                                    + (root.artistData.discographyCards.length === 1 ? " release" : " releases")
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 13
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        y: parent.height - 63
                        color: theme.edge
                    }

                    // Controls: shared All releases picker + ONE Sort toggle
                    Row {
                        id: discographyControls
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        spacing: 10
                        z: 40

                        DiscoverPicker {
                            id: releasePicker
                            objectName: "vinylArtistReleasePicker"
                            automationName: "vinylArtistReleasePicker"
                            width: implicitWidth
                            height: 39
                            label: "All releases"
                            clearable: false
                            wrapNavigation: true
                            openOnDown: true
                            options: {
                                var categories = root.artistData.releaseCategories;
                                var rows = [];
                                for (var i = 0; i < categories.length; ++i) {
                                    var category = categories[i];
                                    rows.push({key: category.id, text: category.label,
                                        sub: category.count !== undefined ? String(category.count) : ""});
                                }
                                return rows;
                            }
                            currentKey: root.artistData.categoryFilter
                            onPicked: key => root.artistData.selectReleaseCategory(key)
                        }

                        // ONE Sort button (.control-btn parity)
                        KeyboardAction {
                            id: sort
                            objectName: "vinylArtistSort"
                            width: 148
                            height: 39
                            focusRadius: 9
                            spaceActivates: true
                            accessibleName: root.artistData.newestFirst ? "Sort oldest first" : "Sort newest first"
                            onTriggered: root.artistData.newestFirst = !root.artistData.newestFirst
                            Rectangle {
                                anchors.fill: parent
                                radius: 9
                                color: sort.interactionActive
                                    ? Qt.rgba(0.11, 0.12, 0.16, 0.88)
                                    : Qt.rgba(0.063, 0.067, 0.09, 0.78)
                                border.width: 1
                                border.color: sort.interactionActive ? theme.gold : theme.edge
                            }
                            Text {
                                anchors.centerIn: parent
                                text: root.artistData.newestFirst ? "Newest first  ↓" : "Oldest first  ↑"
                                color: sort.interactionActive ? theme.gold : theme.ink
                                font.family: theme.ui
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }

                // Dense 6/5/4/2-column responsive album tile grid (.album-grid parity)
                Grid {
                    id: discographyGrid
                    objectName: "vinylArtistDiscographyGrid"
                    width: parent.width
                    columns: root.albumGridColumns
                    columnSpacing: 21
                    rowSpacing: 34

                    Repeater {
                        id: discographyRepeater
                        model: root.artistData.discographyCards.length
                        delegate: VinylArtistCard {
                            id: discogTile
                            readonly property var modelData: root.artistData.discographyCards[index]
                            required property int index

                            objectName: "vinylArtistDiscographyCard_" + index
                            artObjectName: "vinylArtistDiscographyArt_" + index
                            width: root.albumTileWidth
                            height: implicitHeight
                            record: modelData
                            artistData: root.artistData
                            shape: "square"
                            darkCaptionSurface: true
                            onCardRequested: record => root.activateDiscographyCard(record, discogTile)
                            onActiveFocusChanged: if (activeFocus) root.reveal(discogTile)

                            Keys.onLeftPressed: event => {
                                if (root.focusAlbumTileAt(discogTile.index - 1))
                                    event.accepted = true;
                            }
                            Keys.onRightPressed: event => {
                                if (root.focusAlbumTileAt(discogTile.index + 1))
                                    event.accepted = true;
                            }
                            Keys.onUpPressed: event => {
                                if (root.focusAlbumTileAt(discogTile.index - root.albumGridColumns))
                                    event.accepted = true;
                            }
                            Keys.onDownPressed: event => {
                                if (root.focusAlbumTileAt(discogTile.index + root.albumGridColumns))
                                    event.accepted = true;
                            }
                        }
                    }
                }
            }

            // TAB 2: SINGLES (Compact 1-column 62px numbered, cover-backed vertical track list)
            Column {
                id: singlesPanel
                objectName: "vinylArtistPanel_singles"
                width: parent.width
                visible: root.artistData.activeTab === "singles"
                spacing: 22

                Column {
                    width: parent.width
                    spacing: 4
                    Text {
                        text: "THE SONGBOOK / 02"
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        font.letterSpacing: 2.2
                    }
                    Text {
                        text: "Singles & Tracks"
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: Math.max(38, Math.min(55, root.width * 0.0315))
                        font.weight: Font.Normal
                    }
                    Text {
                        width: parent.width
                        text: String(root.artistData.singlesRows.length)
                            + " selected tracks in a compact vertical list, with sleeve artwork and release context."
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                }

                VinylArtistSongs {
                    id: compactSinglesList
                    objectName: "vinylArtistCompactSingles"
                    width: parent.width
                    compactListMode: true
                    artistData: root.artistData
                    onSongRequested: song => root.songRequested(song)
                    onAlbumRequested: album => root.albumRequested(album)
                    onItemFocused: item => root.reveal(item)
                }
            }

            // TAB 3: VIDEOS (Stationary 16:9 widescreen 3/2/1-column grid)
            Column {
                id: videosPanel
                objectName: "vinylArtistPanel_videos"
                width: parent.width
                visible: root.artistData.activeTab === "videos"
                spacing: 22

                Column {
                    width: parent.width
                    spacing: 4
                    Text {
                        text: "MOVING PICTURE / 03"
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        font.letterSpacing: 2.2
                    }
                    Text {
                        text: "Official Videos"
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: Math.max(38, Math.min(55, root.width * 0.0315))
                        font.weight: Font.Normal
                    }
                }

                Grid {
                    id: videoTabGrid
                    objectName: "vinylArtistVideosGrid"
                    width: parent.width
                    visible: root.artistData.media.videos.length > 0
                    columns: root.mediaGridColumns
                    columnSpacing: 23
                    rowSpacing: 32

                    Repeater {
                        model: root.artistData.media.videos.length
                        delegate: VinylArtistCard {
                            id: videoTabCard
                            readonly property var modelData: root.artistData.media.videos[index]
                            required property int index
                            objectName: "vinylArtistVideoCard_" + index
                            width: root.mediaCardWidth
                            height: implicitHeight
                            record: modelData
                            artistData: root.artistData
                            shape: "wide"
                            badge: "VIDEO"
                            onCardRequested: record => root.showMedia(record, videoTabCard)
                            onActiveFocusChanged: if (activeFocus) root.reveal(videoTabCard)
                        }
                    }
                }

                Text {
                    visible: root.artistData.media.videos.length === 0
                    text: "No provider-qualified music videos in this artist snapshot."
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 14
                }
            }

            // TAB 4: LIVE (Stationary 16:9 widescreen 3/2/1-column grid)
            Column {
                id: livePanel
                objectName: "vinylArtistPanel_live"
                width: parent.width
                visible: root.artistData.activeTab === "live"
                spacing: 22

                Column {
                    width: parent.width
                    spacing: 4
                    Text {
                        text: "STAGE ARCHIVE / 04"
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        font.letterSpacing: 2.2
                    }
                    Text {
                        text: "Live Performances"
                        color: theme.ink
                        font.family: theme.display
                        font.pixelSize: Math.max(38, Math.min(55, root.width * 0.0315))
                        font.weight: Font.Normal
                    }
                }

                Grid {
                    id: liveTabGrid
                    objectName: "vinylArtistLiveGrid"
                    width: parent.width
                    visible: root.artistData.liveCards.length > 0
                    columns: root.mediaGridColumns
                    columnSpacing: 23
                    rowSpacing: 32

                    Repeater {
                        model: root.artistData.liveCards.length
                        delegate: VinylArtistCard {
                            id: liveTabCard
                            readonly property var modelData: root.artistData.liveCards[index]
                            required property int index
                            objectName: "vinylArtistLiveCard_" + index
                            width: root.mediaCardWidth
                            height: implicitHeight
                            record: modelData
                            artistData: root.artistData
                            shape: "wide"
                            badge: "LIVE" + (modelData.year ? " · " + String(modelData.year) : "")
                            onCardRequested: record => root.showMedia(record, liveTabCard)
                            onActiveFocusChanged: if (activeFocus) root.reveal(liveTabCard)
                        }
                    }
                }

                Text {
                    visible: root.artistData.liveCards.length === 0
                    text: "No live performance recordings in this artist snapshot."
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 14
                }
            }
        }

        // Native Colosseum shelf/sidebar overview & See-all compatibility surface
        Column {
            id: body
            visible: false // Superseded by the single tabbed catalogue; keep detail-route data intact.
            x: root.gutter
            width: root.bodyWidth
            spacing: 56

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
                                model: root.artistData.filters.length
                                delegate: KeyboardAction {
                                    readonly property var modelData: root.artistData.filters[index]
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
                    onActivated: record => root.showMedia(record, null)
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
        }
    }

    // Dedicated See-all full category view
    VinylArtistFullCategory {
        id: fullCategory
        objectName: "vinylArtistFullCategory"
        x: root.gutter
        y: 88
        width: root.bodyWidth
        category: root.selectedCategory
        artistData: root.artistData
        onBackRequested: root.closeCategory()
        onAlbumRequested: album => root.albumRequested(album)
        onSongRequested: song => root.songRequested(song)
        onArtistRequested: name => root.artistRequested(name)
        onMediaRequested: record => root.showMedia(record, null)
        onItemFocused: item => root.reveal(item)
    }

    // Pinned Sticky Artist Tab Bar (.tabbar parity: 58px height, sticks below 76px header)
    Rectangle {
        id: stickyTabBar
        objectName: "vinylArtistTabBar"
        visible: root.selectedCategory === ""
        x: 0
        y: Math.max(hero.height, root.contentY + artistTitleBar.height)
        width: root.width
        height: 58
        z: 30
        color: Qt.rgba(0.031, 0.035, 0.055, 0.88)

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: theme.edge
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: theme.edge
        }

        // Left: Albums / Singles / Videos / Live tabs
        Row {
            id: tabListRow
            x: root.gutter
            height: parent.height
            spacing: 22

            Repeater {
                id: tabRepeater
                readonly property var tabs: [
                    {key: "albums", label: "Albums", count: root.artistData.allDiscographyCards.length},
                    {key: "singles", label: "Singles", count: root.artistData.singlesRows.length},
                    {key: "videos", label: "Videos", count: root.artistData.media.videos.length},
                    {key: "live", label: "Live", count: root.artistData.liveCards.length}
                ]
                model: tabs.length
                delegate: KeyboardAction {
                    id: tabBtn
                    readonly property var modelData: tabRepeater.tabs[index]
                    required property int index

                    readonly property bool isCurrentTab: root.artistData.activeTab === modelData.key
                    objectName: "vinylArtistTab_" + modelData.key
                    width: tabRowInner.implicitWidth + 8
                    height: 58
                    focusRadius: 8
                    focusInset: 6
                    spaceActivates: true
                    accessibleName: modelData.label + " tab"
                    onTriggered: root.switchTab(modelData.key, false)

                    Keys.onLeftPressed: event => {
                        var keys = ["albums", "singles", "videos", "live"];
                        var nextKey = keys[(tabBtn.index + keys.length - 1) % keys.length];
                        root.switchTab(nextKey, true);
                        event.accepted = true;
                    }
                    Keys.onRightPressed: event => {
                        var keys = ["albums", "singles", "videos", "live"];
                        var nextKey = keys[(tabBtn.index + 1) % keys.length];
                        root.switchTab(nextKey, true);
                        event.accepted = true;
                    }
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Home) {
                            root.switchTab("albums", true);
                            event.accepted = true;
                        } else if (event.key === Qt.Key_End) {
                            root.switchTab("live", true);
                            event.accepted = true;
                        }
                    }

                    Row {
                        id: tabRowInner
                        anchors.centerIn: parent
                        spacing: 7
                        Text {
                            text: tabBtn.modelData.label
                            color: tabBtn.isCurrentTab
                                ? theme.ink
                                : (tabBtn.interactionActive ? Qt.rgba(0.969, 0.969, 0.961, 0.92) : theme.inkDimmer)
                            font.family: theme.ui
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: Number(tabBtn.modelData.count) > 0
                            text: String(tabBtn.modelData.count)
                            color: tabBtn.isCurrentTab ? theme.gold : theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 11
                            font.weight: Font.Bold
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    // Active 3px gold bottom bar (.tab.active::after parity)
                    Rectangle {
                        visible: tabBtn.isCurrentTab
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 3
                        radius: 2
                        color: theme.gold
                    }
                }
            }
        }

        // Right: Artist-scoped Search pill (.search-btn parity)
        KeyboardAction {
            id: searchButton
            objectName: "vinylArtistSearchButton"
            anchors.right: parent.right
            anchors.rightMargin: root.gutter
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(240, searchBtnRow.implicitWidth + 28)
            height: 40
            focusRadius: 20
            spaceActivates: true
            accessibleName: "Search " + root.artistData.record.name + " catalogue"
            onTriggered: root.openSearchDialog(searchButton)

            Rectangle {
                anchors.fill: parent
                radius: 20
                color: searchButton.interactionActive
                    ? Qt.rgba(1, 1, 1, 0.12)
                    : Qt.rgba(0.063, 0.067, 0.09, 0.68)
                border.width: 1
                border.color: searchButton.interactionActive ? theme.gold : theme.edge
            }

            Row {
                id: searchBtnRow
                anchors.centerIn: parent
                spacing: 8
                Text {
                    text: "⌕"
                    color: theme.gold
                    font.family: theme.ui
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "Search " + root.artistData.record.name
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    // Second-layer Destination Header (.titlebar / TitlePageBar parity: 76px height, no world pills)
    Item {
        id: artistTitleBar
        objectName: "vinylArtistTitleBar"
        x: 0
        y: root.contentY
        width: root.width
        height: 76
        z: 35

        readonly property bool solid: root.contentY > 70

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0.024, 0.027, 0.043, 0.985)
            opacity: artistTitleBar.solid ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: theme.edge
            }
        }
        Rectangle {
            anchors.fill: parent
            opacity: artistTitleBar.solid ? 0.0 : 1.0
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.55) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
            }
        }

        // Match TitlePageBar's destination chrome and protect covered page controls.
        MouseArea { anchors.fill: parent }

        Row {
            x: root.gutter
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12

            BackAction {
                id: headerBackButton
                objectName: "vinylArtistHeaderBack"
                label: "Vinyl"
                labelSize: 15
                tip: "Back to Vinyl"
                idleColor: theme.ink
                hoverColor: theme.gold
                onTriggered: {
                    if (root.selectedCategory.length > 0) {
                        root.closeCategory();
                        return;
                    }
                    root.backRequested();
                }
            }

            Text {
                visible: root.width >= 600
                text: "/  ARTIST"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 2.2
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Right: Backdrops selector + System menu button (.rightbar parity)
        Row {
            anchors.right: parent.right
            anchors.rightMargin: root.gutter
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            KeyboardAction {
                id: backdropsButton
                objectName: "vinylArtistBackdropsButton"
                width: backdropBtnRow.implicitWidth + 28
                height: 40
                focusRadius: 20
                spaceActivates: true
                accessibleName: "Artist backdrops"
                onTriggered: root.openBackdropsDialog(backdropsButton)

                Rectangle {
                    anchors.fill: parent
                    radius: 20
                    color: backdropsButton.interactionActive
                        ? Qt.rgba(1, 1, 1, 0.12)
                        : Qt.rgba(0.063, 0.067, 0.09, 0.56)
                    border.width: 1
                    border.color: backdropsButton.interactionActive ? theme.gold : theme.edge
                }

                Row {
                    id: backdropBtnRow
                    anchors.centerIn: parent
                    spacing: 8
                    Text {
                        text: "Backdrops"
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        visible: root.artistData.heroPhotos.length > 0
                        text: (root.artistData.heroPhotos.length < 10 ? "0" : "") + String(root.artistData.heroPhotos.length)
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            KeyboardAction {
                id: moreButton
                objectName: "vinylArtistMoreButton"
                width: 40
                height: 40
                focusRadius: 20
                spaceActivates: true
                accessibleName: "Page options"
                onTriggered: root.systemMenuOpen = !root.systemMenuOpen

                Rectangle {
                    anchors.fill: parent
                    radius: 20
                    color: (moreButton.interactionActive || root.systemMenuOpen)
                        ? Qt.rgba(1, 1, 1, 0.12)
                        : Qt.rgba(0.063, 0.067, 0.09, 0.56)
                    border.width: 1
                    border.color: root.systemMenuOpen ? theme.gold : theme.edge
                }

                Text {
                    anchors.centerIn: parent
                    text: "⋯"
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 16
                    font.weight: Font.Bold
                }
            }
        }

        // System popover (.sys-pop parity)
        Rectangle {
            id: sysPopup
            objectName: "vinylArtistSystemMenu"
            visible: root.systemMenuOpen
            anchors.right: parent.right
            anchors.rightMargin: root.gutter
            y: parent.height + 6
            width: 210
            height: sysColumn.implicitHeight + 12
            radius: 13
            color: Qt.rgba(0.039, 0.047, 0.071, 0.98)
            border.width: 1
            border.color: theme.edge
            z: 60

            Column {
                id: sysColumn
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2

                KeyboardAction {
                    id: createPlaylistMenuAction
                    objectName: "vinylArtistCreatePlaylistMenu"
                    width: parent.width
                    height: 36
                    visible: root.artistData.songs.length > 0
                    focusRadius: 8
                    spaceActivates: true
                    accessibleName: "Create playlist"
                    onTriggered: {
                        root.systemMenuOpen = false;
                        root.dialogReturnItem = moreButton;
                        playlistName.text = "";
                        createDialog.open();
                    }
                    Text {
                        x: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Create playlist"
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                    }
                }

                KeyboardAction {
                    id: topOfPageAction
                    objectName: "vinylArtistTopOfPageAction"
                    width: parent.width
                    height: 36
                    focusRadius: 8
                    spaceActivates: true
                    accessibleName: "Top of page"
                    onTriggered: {
                        root.systemMenuOpen = false;
                        root.contentY = 0;
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: 8
                        color: topOfPageAction.interactionActive ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    }
                    Text {
                        x: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Top of page"
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                    }
                }
            }
        }
    }

    // Floating 72px HUD preview bar when a demo track is queued (.hud parity)
    Rectangle {
        id: hudPlayer
        objectName: "vinylArtistHudPlayer"
        visible: root.artistData.queuedSongIndex >= 0 && root.artistData.queuedSong !== null
        x: root.gutter
        y: root.contentY + root.height - height - 18
        width: root.bodyWidth
        height: 72
        radius: 18
        color: Qt.rgba(0.047, 0.055, 0.082, 0.92)
        border.width: 1
        border.color: theme.edge
        z: 32

        Row {
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            width: parent.width * 0.36

            Rectangle {
                width: 48
                height: 48
                radius: 8
                color: "#1a1d28"
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.12)
                Text {
                    anchors.centerIn: parent
                    text: "♫"
                    color: theme.gold
                    font.family: theme.ui
                    font.pixelSize: 16
                }
                RoundedPosterImage {
                    anchors.fill: parent
                    radius: 8
                    sources: root.artistData.queuedSong && root.artistData.artwork(root.artistData.queuedSong.cover)
                        ? [root.artistData.artwork(root.artistData.queuedSong.cover)] : []
                }
            }

            Column {
                width: Math.max(80, parent.width - 66)
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    objectName: "vinylArtistHudTitle"
                    width: parent.width
                    text: root.artistData.queuedSong ? root.artistData.queuedSong.title : ""
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Text {
                    objectName: "vinylArtistHudSubtitle"
                    width: parent.width
                    text: root.artistData.queuedSong
                        ? (root.artistData.record.name + " · " + root.artistData.queuedSong.albumTitle) : ""
                    color: theme.inkDim
                    font.family: theme.ui
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }

        Column {
            anchors.centerIn: parent
            width: Math.min(320, parent.width * 0.32)
            spacing: 6
            visible: root.width >= 760
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.artistData.demoPlaying ? "PLAYING / VISUAL DEMO" : "PAUSED / VISUAL DEMO"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 1.6
            }
            Rectangle {
                width: parent.width
                height: 4
                radius: 2
                color: Qt.rgba(1, 1, 1, 0.14)
                Rectangle {
                    width: root.artistData.demoPlaying ? parent.width * 0.26 : 0
                    height: parent.height
                    radius: 2
                    color: theme.gold
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            KeyboardAction {
                objectName: "vinylArtistHudPrev"
                width: 38; height: 38; focusRadius: 19; spaceActivates: true
                accessibleName: "Previous track"
                onTriggered: root.artistData.stepDemoSong(-1)
                Rectangle {
                    anchors.fill: parent; radius: 19
                    color: parent.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                }
                Image { anchors.centerIn: parent; width: 16; height: 16; source: "../../assets/icons/lucide/skip-back.svg" }
            }
            KeyboardAction {
                objectName: "vinylArtistHudPlay"
                width: 42; height: 42; focusRadius: 21; focusPrimary: true; spaceActivates: true
                accessibleName: root.artistData.demoPlaying ? "Pause visual demo" : "Play visual demo"
                onTriggered: root.artistData.toggleDemoPlayback()
                Rectangle {
                    anchors.fill: parent; radius: 21
                    color: theme.gold
                }
                Image {
                    anchors.centerIn: parent
                    width: 16; height: 16
                    source: "data:image/svg+xml," + encodeURIComponent(
                        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#17181c">'
                        + (root.artistData.demoPlaying ? '<path d="M6 4h4v16H6zM14 4h4v16h-4z"/>' : '<path d="M7 4l14 8-14 8z"/>') + '</svg>')
                }
            }
            KeyboardAction {
                objectName: "vinylArtistHudNext"
                width: 38; height: 38; focusRadius: 19; spaceActivates: true
                accessibleName: "Next track"
                onTriggered: root.artistData.stepDemoSong(1)
                Rectangle {
                    anchors.fill: parent; radius: 19
                    color: parent.interactionActive ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
                }
                Image { anchors.centerIn: parent; width: 16; height: 16; source: "../../assets/icons/lucide/skip-forward.svg" }
            }
        }
    }

    // The production Colosseum row index: pinned after the hero, collapsible,
    // and driven by the actual visible category sections.
    RowIndexSidebar {
        id: sidebar
        visible: false // Artist destination uses its single sticky media tab bar.
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

    // Artist-scoped Search Dialog (openSearch parity)
    Dialog {
        id: searchDialog
        objectName: "vinylArtistSearchDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(620, parent.width - 48)
        height: Math.min(540, parent.height - 72)
        title: "Search " + root.artistData.record.name
        modal: true
        focus: true
        palette.window: "#0e1017"
        palette.windowText: theme.ink
        palette.text: theme.ink
        palette.base: "#171a24"
        palette.button: "#252834"
        palette.buttonText: theme.ink
        standardButtons: Dialog.Close

        readonly property var matches: root.artistData.search(searchInput.text)

        onOpened: searchInput.forceActiveFocus(Qt.PopupFocusReason)
        onClosed: root.restoreDialogFocus()

        contentItem: Column {
            spacing: 12
            TextField {
                id: searchInput
                objectName: "vinylArtistSearchInput"
                width: parent.width
                placeholderText: "Albums, songs, videos, live…"
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 14
                Keys.onDownPressed: event => {
                    if (searchResultsList.count > 0) {
                        var first = searchResultsList.itemAtIndex(0);
                        if (first) first.forceActiveFocus(Qt.TabFocusReason);
                        event.accepted = true;
                    }
                }
            }

            ListView {
                id: searchResultsList
                objectName: "vinylArtistSearchResultsList"
                width: parent.width
                height: Math.max(180, searchDialog.height - 170)
                clip: true
                spacing: 4
                model: searchDialog.matches.length

                delegate: KeyboardAction {
                    id: matchRow
                    readonly property var modelData: searchDialog.matches[index]
                    required property int index

                    objectName: "vinylArtistSearchResult_" + index
                    width: searchResultsList.width
                    height: 48
                    focusRadius: 9
                    focusInset: -2
                    spaceActivates: true
                    accessibleName: modelData.label + ", " + modelData.type
                    onTriggered: {
                        var item = modelData;
                        searchDialog.close();
                        root.switchTab(item.tab, false);
                        if (item.type === "Album") {
                            root.activateDiscographyCard(item.record, null);
                        } else if (item.type === "Track") {
                            root.artistData.queueDemoSong(item.rowIndex);
                            if (item.record && item.record.playable && item.record.song)
                                root.songRequested(item.record.song);
                        } else if (item.type === "Video" || item.type === "Live") {
                            root.showMedia(item.record, null);
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: 9
                        color: matchRow.interactionActive ? Qt.rgba(0.94, 0.77, 0.29, 0.14) : Qt.rgba(1, 1, 1, 0.04)
                        border.width: matchRow.interactionActive ? 1 : 0
                        border.color: theme.gold
                    }

                    Column {
                        x: 12
                        width: parent.width - 104
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text {
                            width: parent.width
                            text: matchRow.modelData.label
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: matchRow.modelData.extra
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: String(matchRow.modelData.type).toUpperCase()
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        font.letterSpacing: 1.4
                    }
                }
            }

            Text {
                width: parent.width
                text: "Search applies to this artist's release entries, tracks, and video/performance picks."
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }

    // Artist Backdrops Dialog (openBackdrops parity)
    Dialog {
        id: backdropDialog
        objectName: "vinylArtistBackdropsDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(640, parent.width - 48)
        title: "Artist backdrops"
        modal: true
        focus: true
        palette.window: "#0e1017"
        palette.windowText: theme.ink
        palette.text: theme.ink
        palette.button: "#252834"
        palette.buttonText: theme.ink
        standardButtons: Dialog.Close

        onOpened: autoWallOption.forceActiveFocus(Qt.PopupFocusReason)
        onClosed: root.restoreDialogFocus()

        contentItem: Column {
            spacing: 14

            Text {
                width: parent.width
                text: "Auto rotate is the default when multiple photographs exist. The selected photograph drives both the sharp hero and the synchronized full-page blurred skin."
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            Flow {
                width: parent.width
                spacing: 10

                KeyboardAction {
                    id: autoWallOption
                    objectName: "vinylArtistBackdrop_auto"
                    width: 180
                    height: 64
                    focusRadius: 10
                    spaceActivates: true
                    accessibleName: "Auto rotate backdrops"
                    onTriggered: {
                        hero.setBackdrop("auto");
                        backdropDialog.close();
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: 10
                        color: root.artistData.selectedWallpaper === "auto"
                            ? Qt.rgba(0.94, 0.77, 0.29, 0.16)
                            : Qt.rgba(1, 1, 1, 0.05)
                        border.width: root.artistData.selectedWallpaper === "auto" ? 2 : 1
                        border.color: root.artistData.selectedWallpaper === "auto" ? theme.gold : theme.edge
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: 3
                        Text {
                            text: "Auto rotate"
                            color: root.artistData.selectedWallpaper === "auto" ? theme.gold : theme.ink
                            font.family: theme.ui
                            font.pixelSize: 13
                            font.weight: Font.Bold
                        }
                        Text {
                            text: String(root.artistData.heroPhotos.length) + " eligible photos"
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 11
                        }
                    }
                }

                Repeater {
                    model: root.artistData.backdropOptions.length
                    delegate: KeyboardAction {
                        id: wallOption
                        readonly property var modelData: root.artistData.backdropOptions[index]
                        required property int index

                        readonly property bool isSelectedWall: root.artistData.selectedWallpaper === modelData.id
                        objectName: "vinylArtistBackdrop_" + modelData.id
                        width: 180
                        height: 64
                        focusRadius: 10
                        spaceActivates: true
                        accessibleName: modelData.name + ", " + modelData.era
                        onTriggered: {
                            hero.setBackdrop(modelData.id);
                            backdropDialog.close();
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: 10
                            color: wallOption.isSelectedWall
                                ? Qt.rgba(0.94, 0.77, 0.29, 0.16)
                                : Qt.rgba(1, 1, 1, 0.05)
                            border.width: wallOption.isSelectedWall ? 2 : 1
                            border.color: wallOption.isSelectedWall ? theme.gold : theme.edge
                        }
                        Column {
                            x: 12
                            width: parent.width - 24
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            Text {
                                width: parent.width
                                text: wallOption.modelData.name
                                color: wallOption.isSelectedWall ? theme.gold : theme.ink
                                font.family: theme.ui
                                font.pixelSize: 13
                                font.weight: Font.Bold
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: wallOption.modelData.era
                                color: theme.inkDim
                                font.family: theme.ui
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            Row {
                width: parent.width
                spacing: 8
                TextField {
                    id: customWallField
                    objectName: "vinylArtistBackdropCustomInput"
                    width: parent.width - 110
                    placeholderText: "Local image URL or path (file:///...)"
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 13
                }
                KeyboardAction {
                    id: applyCustomWallBtn
                    objectName: "vinylArtistBackdropCustomApply"
                    width: 100
                    height: 38
                    focusRadius: 8
                    spaceActivates: true
                    accessibleName: "Apply custom banner"
                    onTriggered: {
                        if (customWallField.text.trim().length > 0) {
                            root.artistData.customBackdropUrl = customWallField.text.trim();
                            hero.setBackdrop("custom");
                            backdropDialog.close();
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: 8
                        color: Qt.rgba(0.94, 0.77, 0.29, 0.16)
                        border.width: 1
                        border.color: theme.gold
                    }
                    Text {
                        anchors.centerIn: parent
                        text: "Use local"
                        color: theme.gold
                        font.family: theme.ui
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                }
            }

            Text {
                width: parent.width
                text: "Source note: Reference artist imagery from TheAudioDB does not establish commercial redistribution rights."
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }

    // Release Detail Dialog for reference-only discography entries without local tracklists (openAlbum parity)
    Dialog {
        id: releaseDetailDialog
        objectName: "vinylArtistReleaseDetailDialog"
        property var selection: ({title:"", year:"", category:"", sourceUrl:"", cover:""})
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(540, parent.width - 48)
        title: selection.title
        modal: true
        focus: true
        palette.window: "#17181d"
        palette.windowText: theme.ink
        palette.text: theme.ink
        palette.button: "#292a30"
        palette.buttonText: theme.ink
        standardButtons: Dialog.Close

        onClosed: root.restoreDialogFocus()

        contentItem: Column {
            spacing: 14
            Text {
                text: String(releaseDetailDialog.selection.category || "Album").toUpperCase()
                    + (releaseDetailDialog.selection.year ? " / " + String(releaseDetailDialog.selection.year) : "")
                color: theme.gold
                font.family: theme.ui
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 1.8
            }
            Label {
                width: parent.width
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: theme.inkDim
                text: "This is a release in the artist collection. The album and song detail players belong to their own Vinyl rooms; this prototype does not invent an album track list."
            }
            KeyboardAction {
                objectName: "vinylArtistReleaseReferenceLink"
                visible: !!(releaseDetailDialog.selection.sourceUrl && String(releaseDetailDialog.selection.sourceUrl).length > 0)
                width: parent.width
                height: 40
                accessibleName: "Open release reference"
                accessibleDescription: String(releaseDetailDialog.selection.sourceUrl || "")
                onTriggered: Qt.openUrlExternally(releaseDetailDialog.selection.sourceUrl)
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Open release reference ↗"
                    color: theme.gold
                    font.family: theme.ui
                    font.pixelSize: 14
                }
            }
        }
    }

    // Existing Media Source Dialog
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
        onClosed: root.restoreDialogFocus()
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
            clip: true; model: playlistDialog.selection.songs.length; spacing: 4
            delegate: ItemDelegate {
                readonly property var modelData: playlistDialog.selection.songs[index]
                required property int index
                objectName: "vinylArtistPlaylistSong_" + index
                width: ListView.view.width
                text: modelData.track.title
                onClicked: { root.songRequested(modelData); playlistDialog.close(); }
            }
        }
    }
}
