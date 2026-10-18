import QtQuick
import QtQuick.Controls
import ".."
import "../../tests/fixtures/vinyl-world/Catalogue.js" as WorldSnapshot
import "../../tests/fixtures/vinyl-world/ArtistMedia.js" as ArtistMedia

Window {
    id: root
    objectName: "vinylPrototypeWindow"
    title: "Colosseum · Vinyl world and pages prototype"
    width: 1280
    height: 720
    visible: true
    property string page: "album"
    property bool ready: false
    property var contentStore: typeof Content !== "undefined" ? Content : null
    readonly property string fallbackSource: {
        if (root.contentStore) {
            const remote = (root.contentStore.generation,
                            root.contentStore.assetUrl("assets/wallpaper/cold-ripple.jpg"))
            if (remote) return remote
        }
        return Qt.resolvedUrl("../../assets/wallpaper/cold-ripple.jpg")
    }
    property bool developerStrip: true
    property alias pageData: pageData
    property alias worldData: worldData
    property alias worldScroll: worldScroll
    property alias artistData: artistData
    property alias artistScroll: artistPage
    property var artistTrail: []
    property var artistChoices: []
    property string songReturnPage: "album"
    property string albumReturnPage: "world"
    property real worldReturnY: 0
    function openArtist(name) {
        if (!name) return;
        var names = name.split(" · ").map(function(n) { return n.trim(); }).filter(Boolean);
        if (names.length > 1) {
            artistChoices = names;
            artistChooser.open();
            return;
        }
        var trail = artistTrail.slice();
        trail.push({page: root.page, name: artistData.name, filter: artistData.filter,
            categoryFilter: artistData.categoryFilter, activeTab: artistData.activeTab,
            newestFirst: artistData.newestFirst, allSongs: artistData.allSongs,
            category: artistPage.selectedCategory, sectionReturn: artistPage.sectionReturnY,
            y: artistPage.contentY, snapshot: pageData.snapshotEdition,
            edition: pageData.shownEditionId, disc: pageData.shownDisc,
            albumReturn: albumReturnPage, songReturn: songReturnPage,
            worldY: worldScroll.contentY, worldReturn: worldReturnY});
        artistTrail = trail;
        artistData.open(name);
        artistPage.contentY = 0;
        root.page = "artist";
    }
    function artistBack() {
        var trail = artistTrail.slice(), previous = trail.pop();
        artistTrail = trail;
        if (!previous) { root.page = "world"; return; }
        artistData.name = previous.name;
        artistData.filter = previous.filter;
        artistData.categoryFilter = previous.categoryFilter || "All";
        artistData.activeTab = previous.activeTab || "albums";
        artistData.newestFirst = previous.newestFirst;
        artistData.allSongs = previous.allSongs;
        artistPage.selectedCategory = previous.category || "";
        artistPage.sectionReturnY = previous.sectionReturn || 0;
        pageData.snapshotEdition = previous.snapshot;
        pageData.shownEditionId = previous.edition;
        pageData.shownDisc = previous.disc;
        albumReturnPage = previous.albumReturn;
        songReturnPage = previous.songReturn;
        worldReturnY = previous.worldReturn;
        root.page = previous.page;
        Qt.callLater(function() {
            artistPage.contentY = previous.y;
            worldScroll.contentY = previous.worldY;
        });
    }
    function openArtistAlbum(record) {
        if (!pageData.loadAlbum(record, artistData.artwork(record.cover))) return;
        albumReturnPage = "artist";
        root.page = "album";
    }
    function openSong() { if(root.page!=="song") songReturnPage=root.page;root.page="song"; }
    function openWorldItem(item) {
        if (item.kind==="artists") { openArtist(item.title); return; }
        var album=worldData.catalogue.albums.find(function(a){return a.id===item.albumId});
        if (!album) return;
        if (item.kind==="mixes" || item.kind==="stations") {
            var records=(item.sequence||[item.albumId]).map(function(id) {
                return worldData.catalogue.albums.find(function(a){return a.id===id});
            }).filter(Boolean).map(function(a){return {album:a,cover:worldData.artwork(a.cover)}});
            pageData.startContinuous(records);return;
        }
        pageData.loadAlbum(album,worldData.artwork(album.cover));
        if (item.kind==="songs") {pageData.playTrack(item.trackIndex);return;}
        worldReturnY=worldScroll.contentY;albumReturnPage=root.page;root.page="album";
    }
    VinylWorldData {
        id: worldData
        catalogue: WorldSnapshot.data
        artworkRoot: Qt.resolvedUrl("../../tests/fixtures/vinyl-world")
        resumeSlide: ({
            title:pageData.playingTrack.title,
            art:pageData.artwork(pageData.playingEdition.cover),
            artKind:"poster",
            blurb:pageData.playingEdition.artist+" · "+pageData.playingEdition.name+
                "\nUp next: "+(pageData.playingEdition.discs[pageData.playingDisc][
                    (pageData.playingIndex+1)%pageData.playingEdition.discs[pageData.playingDisc].length].title)
        })
        onItemRequested: item => root.openWorldItem(item)
        onResumeRequested: root.openSong()
    }
    // Lanista resolves QQuickItems rather than Window/QObject roots.
    Item {
        objectName: "vinylPrototype"
        anchors.fill: parent
        property alias ready: root.ready
        property alias page: root.page
    }
    Theme {
        id: theme
    }
    FontLoader {
        source: "../../assets/fonts/Fraunces-Regular.ttf"
    }
    VinylPageData {
        id: pageData
        artworkRoot: Qt.resolvedUrl("../../tests/fixtures/vinyl-pages")
        onArtistRequested: name => root.openArtist(name)
    }
    VinylArtistData {
        id: artistData
        catalogue: worldData.catalogue
        artworkRoot: worldData.artworkRoot
        mediaCatalogue: ArtistMedia.data
        libraryRecord: worldData.profileRecord
    }
    VinylBackdrop {
        id: backdrop
        anchors.fill: parent
        artistSkinActive: root.page === "artist"
        artistLayerASource: artistPage.heroItem ? artistPage.heroItem.layerASource : ""
        artistLayerBSource: artistPage.heroItem ? artistPage.heroItem.layerBSource : ""
        artistActiveLayer: artistPage.heroItem ? artistPage.heroItem.activeVisualLayer : 0
        reducedMotion: artistData.reducedMotion
        source: root.page === "artist" ? (artistPage.heroItem && artistPage.heroItem.currentSynchronizedSource ? artistPage.heroItem.currentSynchronizedSource : (artistData.record.releases.length ? artistData.artwork(artistData.record.releases[0].album.cover) : root.fallbackSource)) : root.page === "world" ? root.fallbackSource : pageData.artwork(root.page === "album" ? pageData.shownEdition.cover : (pageData.playingEdition.backdrop || pageData.playingEdition.cover))
        wordsShown: root.page === "song" && pageData.lyricsShown
        tallFixture: root.page === "song" && pageData.playingEdition.id === "mounamela"
        Rectangle {
            anchors.fill: parent
            visible: root.page === "world"
            color: theme.biblioWashBottom
            opacity: 0.68
        }
    }
    Item {
        id: header
        x: theme.margin
        y: 30
        width: root.width - theme.margin * 2
        height: 55
        z: 40
        Text {
            x: 68
            visible: root.page !== "artist"
            text: Qt.formatTime(new Date(), "hh:mm")
            color: theme.ink
            font.family: theme.display
            font.pixelSize: 32
        }
        Text {
            x: 68
            y: 38
            text: Qt.formatDate(new Date(), "dddd, d MMMM")
            visible: root.width >= 1150 && root.page !== "artist"
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 12
        }
        KeyboardAction {
            width: 55
            height: 45
            visible: root.page === "album" || root.page === "artist"
            objectName: "vinylAlbumBack"
            accessibleName: "Back"
            onTriggered: {
                if (root.page === "artist") {
                    if (artistPage.selectedCategory.length > 0) {
                        artistPage.closeCategory();
                        return;
                    }
                    root.artistBack();
                    return;
                }
                root.page=root.albumReturnPage;
                Qt.callLater(function(){worldScroll.contentY=root.worldReturnY});
            }
            Text {
                anchors.centerIn: parent
                visible: root.page === "album"
                text: "‹ Back"
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 13
            }
        }
        // First-layer world pills belong to TopBar / World view, NEVER inside the second-layer Artist destination.
        Glass {
            objectName: "vinylWorldPillsBar"
            visible: root.page !== "artist"
            anchors.horizontalCenter: parent.horizontalCenter
            width: 520
            height: 46
            y: 5
            radius: 24
            backdrop: backdrop
            Row {
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: ["Tankoban", "Biblio", "Theatre", "Vinyl", "Universes", "Feria"]
                    delegate: Rectangle {
                        required property string modelData
                        width: 80
                        height: 36
                        radius: 18
                        color: modelData === "Vinyl" ? theme.gold : Qt.rgba(0, 0, 0, 0)
                        Text {
                            anchors.centerIn: parent
                            text: parent.modelData
                            color: parent.modelData === "Vinyl" ? theme.biblioWashBottom : theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 13
                            font.bold: true
                        }
                    }
                }
            }
        }
    }
    Flickable {
        id: worldScroll
        objectName: "vinylWorldScroll"
        x: theme.margin
        y: 110
        width: root.width-2*theme.margin
        height: bar.y-y-20
        contentWidth: width
        contentHeight: world.height
        visible: root.page==="world"
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollGlide {flick:worldScroll}
        ScrollBar.vertical: HouseScrollBar {flick:worldScroll}
        VinylWorld {
            id: world
            width: worldScroll.width
            height: implicitHeight
            worldData: worldData
            backdrop: backdrop
            pageFlick: worldScroll
        }
    }
    VinylArtistPage {
        id: artistPage
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            topMargin: 0
            bottom: parent.bottom
            bottomMargin: 0
        }
        artistData: artistData
        worldData: worldData
        backdrop: backdrop
        visible: root.page === "artist"
        onBackRequested: {
            if (artistPage.selectedCategory.length > 0) {
                artistPage.closeCategory();
                return;
            }
            root.artistBack();
        }
        onArtistRequested: name => root.openArtist(name)
        onAlbumRequested: record => root.openArtistAlbum(record)
        onSongRequested: song => {
            if (pageData.loadAlbum(song.album, artistData.artwork(song.album.cover)))
                pageData.playTrack(song.index);
        }
    }
    VinylAlbumPage {
        id: album
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            topMargin: 110
            bottom: bar.top
            bottomMargin: 20
        }
        pageData: pageData
        backdrop: backdrop
        visible: root.page === "album"
        onVideoRequested: track => {
            pageData.playTrack(track);
            pageData.videoShown = true;
            root.songReturnPage="album";
            root.page = "song";
        }
    }
    VinylSongPage {
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            topMargin: 108
            bottom: bar.top
            bottomMargin: 42
        }
        visible: root.page === "song"
        pageData: pageData
        artistNavigationEnabled: true
        onArtistRequested: name => root.openArtist(name)
        onBackRequested: {
            root.page = root.songReturnPage;
            if(root.page==="album") album.forceActiveFocus();
        }
        onAlbumRequested: {
            pageData.showPlayingAlbum();
            if(root.songReturnPage!=="album") root.albumReturnPage=root.songReturnPage;
            root.page = "album";
        }
    }
    Popup {
        id: artistChooser
        objectName: "vinylArtistChooser"
        parent: root.contentItem
        anchors.centerIn: parent
        width: Math.min(420, root.width - 80)
        height: Math.min(360, artistChoicesColumn.implicitHeight + 40)
        padding: 20
        modal: true
        focus: true
        background: Glass { backdrop: backdrop; radius: 18 }
        contentItem: Flickable {
            contentHeight: artistChoicesColumn.implicitHeight
            clip: true
            Column {
                id: artistChoicesColumn
                width: parent.width
                spacing: 8
                Text { text: "Artists"; color: theme.ink; font.family: theme.display; font.pixelSize: 26 }
                Repeater {
                    model: root.artistChoices
                    delegate: KeyboardAction {
                        required property string modelData
                        required property int index
                        objectName: "vinylArtistChoice_" + index
                        width: parent.width
                        height: 44
                        accessibleName: modelData
                        onTriggered: { artistChooser.close(); root.openArtist(modelData); }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            text: modelData
                            elide: Text.ElideRight
                            color: parent.interactionActive ? theme.gold : theme.ink
                            font.family: theme.ui
                            font.pixelSize: 15
                        }
                    }
                }
            }
        }
    }
    VinylCreditsPopup {
        id: creditsPopup
        parent: root.contentItem
        x: root.width - width - theme.margin
        y: bar.y - height - 12
        pageData: pageData
        backdrop: backdrop
        onClosed: bar.forceActiveFocus()
    }
    VinylPlayerBar {
        id: bar
        visible: root.page !== "artist"
        x: theme.margin
        y: root.height - height - 18
        width: root.width - theme.margin * 2
        pageData: pageData
        backdrop: backdrop
        page: root.page
        onSongRequested: root.openSong()
        onCreditsRequested: creditsPopup.opened ? creditsPopup.close() : creditsPopup.open()
    }
    Flow {
        id: dev
        visible: root.developerStrip
        x: 8
        y: 2
        width: root.width - 16
        spacing: 3
        z: 100
        Repeater {
            model: [["PageWorld","World"], ["ProfileNew","New"], ["ProfileSkipped","Skipped"], ["ProfileSeasoned","Seasoned"], ["Catalogue","Catalogue"], ["ResetPicker","Reset picker"], ["PageAlbum", "Album"], ["PageSong", "Song"], ["FixtureThriller", "Thriller"], ["FixtureWithoutMe", "Without Me"], ["FixtureMounamela", "Mounamela"], ["SourcePlaying", "Playing"], ["SourceFinding", "Finding"], ["SourceRetrying", "Retrying"], ["Freeze", "Freeze"], ["Size720", "1280×720"], ["Size900", "1600×900"], ["Clean", "Hide controls"]]
            delegate: Button {
                required property var modelData
                objectName: "vinylDev" + modelData[0]
                text: modelData[1]
                height: 22
                font.pixelSize: 10
                onClicked: {
                    var action = modelData[0];
                    if (action === "PageWorld") root.page="world";
                    else if (action === "ProfileNew") worldData.setProfile("new");
                    else if (action === "ProfileSkipped") worldData.setProfile("skipped");
                    else if (action === "ProfileSeasoned") worldData.setProfile("seasoned");
                    else if (action === "Catalogue") worldData.setCatalogueReachable(!worldData.catalogueReachable);
                    else if (action === "ResetPicker") worldData.resetPicker();
                    else if (action === "PageAlbum")
                        root.page = "album";
                    else if (action === "PageSong")
                        root.page = "song";
                    else if (action === "FixtureThriller")
                        pageData.setFixture("thriller");
                    else if (action === "FixtureWithoutMe")
                        pageData.setFixture("without-me");
                    else if (action === "FixtureMounamela")
                        pageData.setFixture("mounamela");
                    else if (action === "SourcePlaying")
                        pageData.setSourceState("playing");
                    else if (action === "SourceFinding")
                        pageData.setSourceState("finding");
                    else if (action === "SourceRetrying")
                        pageData.setSourceState("retrying");
                    else if (action === "Freeze")
                        pageData.frozen = !pageData.frozen;
                    else if (action === "Size720") {
                        root.width = 1280;
                        root.height = 720;
                    } else if (action === "Size900") {
                        root.width = 1600;
                        root.height = 900;
                    } else
                        root.developerStrip = false;
                }
            }
        }
    }
    Shortcut {
        sequence: "Ctrl+D"
        onActivated: root.developerStrip = !root.developerStrip
    }
    onFrameSwapped: ready = true
}
