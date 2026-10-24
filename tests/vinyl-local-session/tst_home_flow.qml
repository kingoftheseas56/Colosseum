import QtQuick
import QtTest

TestCase {
    id: test
    name: "VinylHomeNativeBridge"
    width: 960
    height: 720
    when: windowShown

    property var host: null
    property var world: null
    property var collection: null
    property var progress: null
    property var rawAlbum: null
    property string audioPath: ""

    Component {
        id: windowFactory
        Window { width: 960; height: 720; visible: true }
    }

    Component {
        id: collectionFactory
        QtObject {
            property var entries: []
            property int revision: 0
            property bool healthy: true
            property string persistenceError: ""
            property bool failWrites: false
            signal changed()

            function items(worldName) { return entries; }
            function has(worldName, id) {
                return entries.some(function(entry) { return entry.id === id; });
            }
            function replaceItems(worldName, removeIds, replacements) {
                if (failWrites || !healthy) {
                    persistenceError = "Synthetic Collection write failure.";
                    return false;
                }
                var removed = removeIds || [];
                var writes = replacements || [];
                var next = entries.filter(function(entry) {
                    return removed.indexOf(entry.id) < 0
                        && !writes.some(function(write) { return write.id === entry.id; });
                });
                writes.forEach(function(write) { next.push(write); });
                entries = next;
                persistenceError = "";
                revision++;
                changed();
                return true;
            }
            function add(worldName, entry) {
                return replaceItems(worldName, [], [entry]);
            }
            function seed(values) {
                entries = values.slice();
                revision++;
                changed();
            }
        }
    }

    Component {
        id: progressFactory
        QtObject {
            property var entries: []
            property int revision: 0
            function recent(kind, limit) {
                var result = entries.filter(function(entry) { return entry.kind === kind; }).slice().reverse();
                return result.slice(0, Math.max(0, Number(limit) || result.length));
            }
            function get(kind, id) {
                return entries.find(function(entry) { return entry.kind === kind && entry.id === id; }) || null;
            }
            function recordSilent(entry) {
                var next = entries.filter(function(value) {
                    return value.kind !== entry.kind || value.id !== entry.id;
                });
                next.push(JSON.parse(JSON.stringify(entry)));
                entries = next;
                revision++;
            }
            function seed(values) {
                entries = values.slice();
                revision++;
            }
        }
    }

    function albumFor(path) {
        return {
            id: "release",
            title: "Native Release",
            origin: "local",
            artists: ["Native Artist"],
            artistIds: ["native-artist"],
            cover: "",
            tracks: [
                {id:"native-track-one", title:"Native Track One", artists:["Native Artist"],
                    seconds:41, disc:1, number:1, available:true, localPath:path},
                {id:"native-track-two", title:"Native Track Two", artists:["Native Artist"],
                    seconds:53, disc:1, number:2, available:true, localPath:path}
            ]
        };
    }

    function albumEntry(album) {
        return {id:album.id, type:"album", title:album.title, cover:album.cover,
            browseId:album.id, artist:album.artists.join(" · "),
            payload:{version:1, album:album, saved:true}};
    }

    function artistEntry() {
        return {id:"native-artist", type:"artist", title:"Native Artist", cover:"",
            payload:{version:1, artistId:"native-artist", name:"Native Artist"}};
    }

    function rowAt(index) {
        var edition = world.session.normalize(rawAlbum);
        return world.session.rowFor(edition, 0, index);
    }

    function homeRow(key) {
        return world.worldData.homeRows.find(function(row) { return row.key === key; }) || null;
    }

    function backendObject() {
        return findChild(world, "vinylBackendSession");
    }

    function init() {
        if (typeof BridgeFixtures === "undefined" || typeof NativeSource === "undefined") {
            skip("Requires the vinyl_native_session_bridge runner.");
            return;
        }
        host = windowFactory.createObject(test);
        collection = collectionFactory.createObject(test);
        progress = progressFactory.createObject(test);
        audioPath = BridgeFixtures.createIdentityAudio();
        rawAlbum = albumFor(audioPath);
        collection.seed([albumEntry(rawAlbum), artistEntry()]);

        var component = Qt.createComponent("../../qml/vinyl/VinylLiveWorld.qml");
        compare(component.status, Component.Ready, component.errorString());
        world = component.createObject(host.contentItem, {width:960, height:720});
        verify(world !== null);
        world.session.source = NativeSource;
        world.session.collectionStore = collection;
        world.session.progressStore = progress;
        world.session.refreshLibrary();
        world.personalLibrary.refreshModels();
        world.worldData.preferencesStore = null;
        world.worldData.loadTaste();
        world.showHome();
        wait(0);
        verify(world.session.personalEnabled, "The real native session must accept the fake atomic Collection.");
    }

    function cleanup() {
        if (world) {
            world.destroy();
            wait(0);
            world = null;
        }
        if (host) {
            host.close();
            host.destroy();
            wait(0);
            host = null;
        }
        if (collection) {
            collection.destroy();
            wait(0);
            collection = null;
        }
        if (progress) {
            progress.destroy();
            wait(0);
            progress = null;
        }
        rawAlbum = null;
    }

    function test_coldHomeNeedsNoTasteAnswerAndSearchOpensDiscover() {
        collection.seed([]);
        world.session.refreshLibrary();
        world.personalLibrary.refreshModels();
        world.worldData.refreshHome();
        world.showHome();
        wait(0);
        compare(world.worldData.pickerAnswered, false);
        compare(world.page, "home");
        compare(world.worldView.currentTab, "home");
        verify(findChild(world, "vinylHomeEmpty") !== null,
            "An empty local library must still present Home's empty state.");
        verify(findChild(world, "vinylHomeSearch") === null,
            "Home uses the shared top-bar search entry.");
        findChild(world, "vinylTopBar").searchClicked();
        compare(world.page, "discover");
        compare(world.worldView.currentTab, "discover");
        compare(world.worldData.browseStates.discover.onlineMode, true);
    }

    function test_coldHomeLibraryExposesFolderImport() {
        collection.seed([]);
        world.session.refreshLibrary();
        world.personalLibrary.refreshModels();
        world.showHome();
        wait(0);
        findChild(world, "vinylHomeLibrary").activate(Qt.MouseFocusReason);
        compare(world.page, "library");
        compare(world.personalLibrary.section, "albums");
        var importAction = findChild(world, "vinylLibraryImportFolder");
        verify(importAction !== null);
        verify(importAction.visible && importAction.enabled);
        verify(importAction.width >= 38 && importAction.height >= 38);
    }

    function test_homeCollectionsIgnoreLibraryQueryAndSort() {
        verify(world.personalLibrary.toggleSong({row:rowAt(0)}));
        var playlistId = world.personalLibrary.createPlaylist("Home mix", [rowAt(1)]);
        verify(playlistId.length > 0);

        world.worldView.setTab("library");
        world.worldData.setQuery("no matching local record");
        world.worldData.sortOrder = "year";
        compare(world.personalLibrary.albums.length, 0);
        world.worldView.setTab("home");
        world.worldData.refreshHome();

        var songs = homeRow("saved-songs");
        var albums = homeRow("saved-albums");
        var artists = homeRow("saved-artists");
        var playlists = homeRow("playlists");
        verify(songs && albums && artists && playlists);
        compare(songs.kind, "saved-song");
        compare(albums.kind, "albums");
        compare(artists.kind, "artists");
        compare(playlists.kind, "playlists");
        compare(songs.items[0].kind, "saved-song");
        compare(albums.items[0].kind, "albums");
        compare(artists.items[0].kind, "artists");
        compare(playlists.items[0].kind, "playlists");
        compare(playlists.items[0].id, playlistId);
        compare(albums.items[0].id, rawAlbum.id);
    }

    function test_savedSongCardBackRestoresHomeQueryAndScroll() {
        verify(world.personalLibrary.toggleSong({row:rowAt(0)}));
        var playlistId = world.personalLibrary.createPlaylist("Scroll mix", [rowAt(0)]);
        verify(playlistId.length > 0);
        world.showHome();
        world.worldData.setQuery("home-only query");
        world.worldData.refreshHome();
        wait(0);

        var maxScroll = Math.max(0, world.worldScroll.contentHeight - world.worldScroll.height);
        verify(maxScroll > 0, "Fixture Home shelves should be scrollable.");
        world.worldScroll.contentY = Math.min(180, maxScroll);
        wait(0);
        var savedScroll = world.worldScroll.contentY;
        var song = homeRow("saved-songs").items[0];
        world.worldData.activate(song);
        compare(world.page, "song");
        verify(world.goBack());
        tryCompare(world, "page", "home", 2000);
        tryCompare(world.worldView, "currentTab", "home", 2000);
        tryCompare(world.worldData, "query", "home-only query", 2000);
        tryCompare(world.worldScroll, "contentY", savedScroll, 2000);
    }

    function test_homePlaylistCardOpensTheMatchingLibraryPlaylist() {
        var playlistId = world.personalLibrary.createPlaylist("Home playlist", [rowAt(0)]);
        verify(playlistId.length > 0);
        world.showHome();
        world.worldData.refreshHome();
        var playlist = homeRow("playlists").items.find(function(item) { return item.id === playlistId; });
        verify(playlist !== undefined);

        world.worldData.activate(playlist);
        compare(world.page, "library");
        compare(world.worldView.currentTab, "library");
        compare(world.personalLibrary.selectedPlaylistId, playlistId);
        verify(world.goBack());
        compare(world.page, "home");
        compare(world.worldView.currentTab, "home");
    }

    function test_homeAlbumCardOpensTheNativeAlbumDestination() {
        world.showHome();
        world.worldData.refreshHome();
        var album = homeRow("saved-albums").items.find(function(item) { return item.id === rawAlbum.id; });
        verify(album !== undefined);
        world.worldData.activate(album);
        tryCompare(world, "page", "album", 5000);
        compare(world.session.shownEdition.sourceAlbum.id, rawAlbum.id);
        verify(world.goBack());
        tryCompare(world, "page", "home", 2000);
    }

    function test_continueCheckpointSummaryIsPureAndRestoreKeepsQueueIdentityAndPosition() {
        var rows = [rowAt(0), rowAt(1)];
        verify(world.session.playPersonalRows(rows, false, 1));
        var backend = backendObject();
        verify(backend !== null);
        var checkpoint = world.session.plainData(backend.checkpoint());
        checkpoint.position = 7.25;
        var queueIds = world.session.queue.map(function(row) { return row.queueId; });
        var activeIndex = world.session.queueIndex;
        var serial = world.session.queueSerial;
        var summary = world.session.checkpointSummary(checkpoint);
        verify(summary.row !== undefined);
        compare(summary.row.trackId, rows[1].trackId);
        compare(summary.position, 7.25);
        compare(JSON.stringify(world.session.queue.map(function(row) { return row.queueId; })), JSON.stringify(queueIds));
        compare(world.session.queueIndex, activeIndex);
        compare(world.session.queueSerial, serial);

        world.session.clearQueue();
        progress.seed([{id:"home-resume", kind:"music", title:"Native Track Two", resume:checkpoint}]);
        world.worldData.refreshHome();
        var continueRow = homeRow("continue-listening");
        verify(continueRow !== null);
        var saved = continueRow.items.find(function(item) { return !item.resumeCurrent; });
        verify(saved !== undefined);
        world.worldData.activate(saved);
        compare(world.page, "song");
        compare(JSON.stringify(world.session.queue.map(function(row) { return row.queueId; })), JSON.stringify(queueIds));
        compare(world.session.queueIndex, 1);
        compare(world.session.playingTrack.id, rows[1].trackId);
        compare(world.session.position, 7.25);
    }

    function test_otherProfileCheckpointIsNotShownOrRewritten() {
        var backend = backendObject();
        verify(backend !== null);
        var originalScope = backend.profileScope;
        backend.profileScope = "other-profile";
        verify(world.session.playPersonalRows([rowAt(0), rowAt(1)], false, 1));
        var foreign = world.session.plainData(backend.checkpoint());
        backend.profileScope = originalScope;
        progress.seed([{id:"foreign-resume", kind:"music", title:"Native Track Two", resume:foreign}]);
        var before = JSON.stringify(progress.entries);

        world.worldData.refreshHome();
        verify(homeRow("continue-listening") === null);
        compare(JSON.stringify(progress.entries), before);
    }

    function test_emptySessionCheckpointSuppressesOlderContinueEntry() {
        verify(world.session.playPersonalRows([rowAt(0)], false, 0));
        var backend = backendObject();
        var oldCheckpoint = world.session.plainData(backend.checkpoint());
        progress.seed([{id:"older-resume", kind:"music", title:"Native Track One", resume:oldCheckpoint}]);
        world.session.clearQueue();
        verify(progress.get("music-session", "vinyl") !== null);

        world.worldData.refreshHome();
        verify(homeRow("continue-listening") === null,
            "An explicit empty session checkpoint must prevent an older track from reappearing.");
    }

    function test_profileClearHidesHomePersonalRecordsWithoutDeletingCollection() {
        verify(world.personalLibrary.toggleSong({row:rowAt(0)}));
        var playlistId = world.personalLibrary.createPlaylist("Scoped home playlist", [rowAt(0)]);
        verify(playlistId.length > 0);
        verify(homeRow("saved-songs") !== null);
        verify(homeRow("playlists") !== null);

        world.session.clearProfile();
        wait(0);
        world.worldData.refreshHome();
        compare(world.personalLibrary.homeCollections.songs.length, 0);
        compare(world.personalLibrary.homeCollections.playlists.length, 0);
        compare(world.worldData.homeRows.length, 0);
        verify(collection.entries.some(function(entry) { return entry.type === "song"; }));
        verify(collection.entries.some(function(entry) { return entry.id === playlistId; }));
    }

    function test_failedPlaylistWriteDoesNotChangeHomeProjection() {
        world.worldData.refreshHome();
        var before = JSON.stringify(collection.entries);
        var beforeRows = JSON.stringify(world.worldData.homeRows);
        collection.failWrites = true;
        compare(world.personalLibrary.createPlaylist("Must not appear", [rowAt(0)]), "");
        compare(JSON.stringify(collection.entries), before);
        compare(JSON.stringify(world.worldData.homeRows), beforeRows);
        verify(world.personalLibrary.error.length > 0);
    }

    function test_rowForAndOpeningHomeDoNotAllocateQueueOccurrences() {
        var before = world.session.queueSerial;
        var row = rowAt(0);
        compare(world.session.queueSerial, before);
        verify(row.queueId === undefined);
        world.worldData.refreshHome();
        world.showHome();
        compare(world.session.queueSerial, before);
        compare(world.session.queue.length, 0);
    }

    function useHomeSections(sections) {
        var prefs=Qt.createQmlObject('import QtQuick; QtObject {property bool vinylTasteAnswered:true; signal vinylTasteChanged(); function setVinylTasteSelections(answered,artists){return true;}}',world);
        var service=Qt.createQmlObject('import QtQuick; QtObject {property string onboardingState:"answered"; property var selectedArtists:[]; property var homeSections:[]; property var homeSnapshot:({}); property int homeRevision:1; property string homeStatus:"ready"; property string homeError:""; function beginHomeVisit(){} function refreshHome(force){}}',world);
        service.homeSections=sections;
        world.worldData.preferencesStore=prefs;world.worldData.recommendationService=service;
        return service;
    }
    function test_historyRowsFollowSavedCollectionsAndRetainSeeAllSnapshot() {
        var item={id:"release",title:"Native Release",kind:"albums",ref:{identity:"provider",kind:"release",providerId:"local",itemId:"release"},
            destination:{kind:"album",provider:"local",providerId:"release",id:"release"}};
        var section=function(id){return {id:id,title:id,kind:"recommendations",items:[item],status:"ready"};};
        var service=useHomeSections([section("rediscover-library"),section("less-played"),section("from-your-artists")]);
        var keys=world.worldData.homeRows.map(row=>row.key);
        verify(keys.indexOf("personalized:from-your-artists")<keys.indexOf("saved-albums"));
        verify(keys.indexOf("saved-albums")<keys.indexOf("personalized:rediscover-library"));
        verify(keys.indexOf("personalized:rediscover-library")<keys.indexOf("personalized:less-played"));
        world.worldData.openList("personalized:rediscover-library",true);
        service.homeSections=[];
        compare(world.worldData.row("personalized:rediscover-library",true).items[0].id,"release");
        world.session.clearProfile();wait(0);
        compare(world.worldData.homeRows.length,0);
        verify(world.worldData.homeListSnapshot===null);
    }
    function test_coldSkippedHomeUsesExistingPublicDiscovery() {
        collection.seed([]);progress.seed([]);world.session.refreshLibrary();world.personalLibrary.refreshModels();
        useHomeSections([]);
        world.session.discoverySections={trending:{key:"trending",title:"Trending",items:[{id:"public-album",kind:"albums",title:"Public release"}],records:[],cached:true}};
        world.showHome();wait(0);
        verify(world.worldData.homePublicDiscovery===true);
        var publicRow=homeRow("home-discover-trending");verify(publicRow!==null);
        compare(publicRow.personal,false);compare(publicRow.recommendation,false);
        compare(publicRow.items[0].id,"public-album");
        world.session.discoverySections={"new-releases":{error:"Synthetic source failure"}};
        compare(homeRow("home-discover-new-releases").title,"Discover · New releases");
        collection.seed([albumEntry(rawAlbum)]);world.session.refreshLibrary();world.personalLibrary.refreshModels();wait(0);
        verify(!world.worldData.homePublicDiscovery);
        verify(homeRow("home-discover-trending")===null);
    }
    function test_privacyFenceInvalidatesPersonalSeeAllCopies() {
        var item={id:"release",title:"Native Release",kind:"albums",destination:{kind:"album",provider:"local",providerId:"release",id:"release"}};
        var service=useHomeSections([{id:"rediscover-library",title:"Rediscover your library",kind:"recommendations",items:[item],status:"ready"}]);
        service.homeSnapshot={historyEnabled:true,activityResetGeneration:0,historyResetGeneration:0};wait(0);
        world.worldData.openList("personalized:rediscover-library",true);
        verify(world.worldData.homeListSnapshot!==null);
        service.homeSections=[];service.homeSnapshot={historyEnabled:false,activityResetGeneration:0,historyResetGeneration:0};wait(0);
        verify(world.worldData.homeListSnapshot===null,"History-off must clear copied personal evidence");
        service.homeSections=[{id:"rediscover-library",title:"Rediscover your library",kind:"recommendations",items:[item],status:"ready"}];
        service.homeSnapshot={historyEnabled:true,activityResetGeneration:0,historyResetGeneration:0};wait(0);
        world.worldData.openList("personalized:rediscover-library",true);
        service.homeSections=[];service.homeSnapshot={historyEnabled:true,activityResetGeneration:1,historyResetGeneration:1};wait(0);
        verify(world.worldData.homeListSnapshot===null,"History clear must invalidate the copied list");
    }
    function test_publicSeeAllSnapshotSurvivesPlaybackAndHomeEligibilityChange() {
        collection.seed([]);progress.seed([]);world.session.refreshLibrary();world.personalLibrary.refreshModels();
        useHomeSections([]);
        world.session.discoverySections={trending:{key:"trending",title:"Trending",items:[{id:"public-album",kind:"albums",title:"Public release"}],records:[]}};
        world.showHome();wait(0);
        world.worldData.openList("home-discover-trending",true);
        verify(world.worldData.homeListSnapshot!==null,"Public See-all must have the same stable-route contract");
        world.session.discoverySections={};
        verify(world.session.playPersonalRows([rowAt(0)],false,0));
        compare(world.worldData.row("home-discover-trending",true).items[0].id,"public-album");
    }
}
