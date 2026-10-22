import QtQuick
import QtTest

TestCase {
    id: test
    name: "VinylPersonalizedHomeNativeBridge"
    width: 960
    height: 720
    when: windowShown

    property var host: null
    property var world: null
    property var collection: null
    property var preferences: null
    property var recommendations: null
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
            signal changed()
            function items(worldName) { return entries; }
            function has(worldName, id) { return entries.some(function(entry) { return entry.id === id; }); }
            function replaceItems(worldName, removeIds, replacements) {
                var removed = removeIds || [], writes = replacements || [];
                entries = entries.filter(function(entry) {
                    return removed.indexOf(entry.id) < 0
                        && !writes.some(function(write) { return write.id === entry.id; });
                }).concat(writes);
                revision++;
                changed();
                return true;
            }
            function seed(values) { entries = values.slice(); revision++; changed(); }
        }
    }

    Component {
        id: recommendationAlbumSource
        QtObject {
            property string requested: ""
            signal completed(string lane, int requestId, var data, string error)
            function request(lane, operation, value) { requested=value; return 1; }
            function cancel(lane: string): void {}
            function setAlbums(values) {}
        }
    }

    Component {
        id: preferencesFactory
        QtObject {
            property bool vinylTasteAnswered: true
            property var vinylTasteArtistIds: ["local:native-artist"]
            property string vinylPersistenceError: ""
            signal vinylTasteChanged()
            function setVinylTasteSelections(values) { vinylTasteArtistIds = values; return true; }
        }
    }

    Component {
        id: recommendationsFactory
        QtObject {
            property string onboardingState: "answered"
            property var selectedArtists: [{key:"local:native-artist",provider:"local",
                providerId:"native-artist",localId:"native-artist",name:"Native Artist"}]
            property var draftArtists: []
            property var artistResults: ({query:"",items:[],hasMore:false,nextCursor:{},error:""})
            property var homeSections: []
            property string homeStatus: "empty"
            property string homeError: ""
            property int homeRevision: 0
            property string error: ""
            property int homeVisits: 0
            property int homeRefreshes: 0
            property bool lastRefreshForced: false
            function beginHomeVisit() { homeVisits++; return true; }
            function refreshHome(force) { homeRefreshes++; lastRefreshForced = !!force; return true; }
            function beginTasteEdit() { onboardingState = "editing"; return true; }
            function cancelTasteEdit() { onboardingState = "answered"; return true; }
            function commitTaste(skip) { onboardingState = "answered"; return true; }
            function toggleDraftArtist(artist) { return true; }
            function searchArtists(query, cursor) { return true; }
            function setHome(sections, status, error) {
                homeSections = sections || [];
                homeStatus = status || "ready";
                homeError = error || "";
                homeRevision++;
            }
        }
    }

    function albumFor(path) {
        return {id:"release",title:"Native Release",origin:"local",artists:["Native Artist"],
            artistIds:["native-artist"],cover:"",tracks:[
                {id:"native-track-one",title:"Native Track One",artists:["Native Artist"],
                    seconds:41,disc:1,number:1,available:true,localPath:path},
                {id:"native-track-two",title:"Native Track Two",artists:["Native Artist"],
                    seconds:53,disc:1,number:2,available:true,localPath:path}
            ]};
    }

    function albumEntry(album) {
        return {id:album.id,type:"album",title:album.title,cover:album.cover,
            artist:album.artists.join(" · "),browseId:album.id,
            payload:{version:1,album:album,saved:true}};
    }

    function artistEntry() {
        return {id:"native-artist",type:"artist",title:"Native Artist",cover:"",
            payload:{version:1,artistId:"native-artist",name:"Native Artist"}};
    }

    function card(id, title, kind, destination, fields) {
        return Object.assign({id:id,title:title,cover:"",kind:kind,destination:destination,
            key:"recommendation:"+id,artistRefs:[],reason:""}, fields || {});
    }

    function section(items, fields) {
        return Object.assign({id:"for-you",key:"for-you",title:"Made for you",
            kind:"recommendations",items:items,status:"ready",error:"",canRetry:false}, fields || {});
    }

    function init() {
        if (typeof BridgeFixtures === "undefined" || typeof NativeSource === "undefined") {
            skip("Requires the vinyl_native_session_bridge runner.");
            return;
        }
        host = windowFactory.createObject(test);
        collection = collectionFactory.createObject(test);
        preferences = preferencesFactory.createObject(test);
        recommendations = recommendationsFactory.createObject(test);
        audioPath = BridgeFixtures.createIdentityAudio();
        rawAlbum = albumFor(audioPath);
        collection.seed([albumEntry(rawAlbum),artistEntry()]);

        var component = Qt.createComponent("../../qml/vinyl/VinylLiveWorld.qml");
        compare(component.status,Component.Ready,component.errorString());
        world = component.createObject(host.contentItem,{width:960,height:720});
        verify(world !== null);
        world.session.source = NativeSource;
        world.session.collectionStore = collection;
        world.session.refreshLibrary();
        world.personalLibrary.refreshModels();
        world.worldData.preferencesStore = preferences;
        world.worldData.recommendationService = recommendations;
        world.worldData.loadTaste();
        world.showHome();
        wait(0);
        verify(world.session.personalEnabled,"The native session must accept the fake atomic Collection.");
    }

    function cleanup() {
        if (world) { world.destroy(); wait(0); world = null; }
        if (host) { host.close(); host.destroy(); wait(0); host = null; }
        if (collection) { collection.destroy(); wait(0); collection = null; }
        if (preferences) { preferences.destroy(); wait(0); preferences = null; }
        if (recommendations) { recommendations.destroy(); wait(0); recommendations = null; }
        rawAlbum = null;
    }

    function test_recommendationsFollowContinueAndPrecedeFactualRows() {
        recommendations.setHome([section([card("rec-album","Suggested release","albums",
            {kind:"album",provider:"local",providerId:"release",id:"release",playable:false})])]);
        world.worldData.recentContinue = [{id:"resume:one",kind:"resume",title:"Resume",cover:""}];
        var rows = world.worldData.homeRows;
        compare(rows.map(function(row) { return row.key; }),
            ["continue-listening","personalized:for-you","taste-artists","saved-albums","saved-artists"]);
        compare(rows[1].kind,"recommendations");
        compare(rows[1].items[0].destination.id,"release");
    }
    function test_nativeQVariantSectionsReachRenderedHome() {
        var nativeService=world.recommendationService;
        world.worldData.recommendationService=nativeService;
        nativeService.homeCollections={artists:[{kind:"artist",provider:"local",providerId:"native-artist",name:"Native Artist"}],
            albums:[{album:rawAlbum}]};
        nativeService.beginHomeVisit();
        tryVerify(function(){return nativeService.homeSections.length>0;});
        tryVerify(function(){return world.worldData.homeRows.some(function(row){return row.recommendation;});});
        var row=world.worldData.homeRows.find(function(row){return row.recommendation;});
        compare(row.items[0].title,"Native Release");
        tryVerify(function(){return findChild(world,"vinylHomeRow_"+row.key)!==null;});
    }

    function test_homeVisitBeginsOneNativeRefresh() {
        recommendations.homeVisits=0;
        world.worldView.setTab("discover");
        world.showHome();
        compare(recommendations.homeVisits,1);

        recommendations.homeVisits=0;
        world.showHome();
        compare(recommendations.homeVisits,1);
    }

    function test_providerAlbumDestinationUsesBrowseIdentity() {
        var source=recommendationAlbumSource.createObject(test);
        world.session.source=source;
        world.activateItem(card("ytm:album:MPRE_test","Release","albums",
            {kind:"album",provider:"youtube-music",providerId:"MPRE_test",id:"ytm:album:MPRE_test"}));
        compare(source.requested,"MPRE_test");
        var album=Object.assign({},rawAlbum,{id:"existing-stable-uuid",browseId:"MPRE_test",origin:"youtube-music"});
        source.completed("browse",1,album,"");
        tryCompare(world,"page","album");
        compare(world.session.shownEdition.sourceAlbum.id,"existing-stable-uuid");
        world.session.source=NativeSource;
        source.destroy();
    }

    function test_personalizedSeeAllKeepsItsSnapshotAcrossAlbumBack() {
        recommendations.setHome([section([card("rec-album","Suggested release","albums",
            {kind:"album",provider:"local",providerId:"release",id:"release",playable:false})])]);
        var key = "personalized:for-you";
        world.worldData.openList(key,true);
        compare(world.worldView.listKey,key);
        recommendations.setHome([section([card("new-album","Replacement","albums",
            {kind:"album",provider:"local",providerId:"other",id:"other",playable:false})])]);
        compare(world.worldData.row(key,true).items[0].id,"rec-album");

        world.worldData.activate(world.worldData.row(key,true).items[0]);
        tryCompare(world,"page","album",5000);
        compare(world.session.shownEdition.sourceAlbum.id,"release");
        verify(world.goBack());
        tryCompare(world,"page","home",2000);
        compare(world.worldView.listKey,key);
        compare(world.worldData.row(key,true).items[0].id,"rec-album");
        verify(world.goBack());
        compare(world.worldView.listKey,"");
        compare(world.worldView.currentTab,"home");
    }

    function test_songRecommendationOpensBrowseSongWithoutStartingPlayback() {
        var song = card("rec-song","Native Track One","songs",
            {kind:"song",provider:"local",providerId:"native-track-one",id:"native-track-one",playable:true},
            {record:rawAlbum,trackIndex:0});
        recommendations.setHome([section([song])]);
        world.worldData.activate(world.worldData.homeRows[0].items[0]);
        compare(world.page,"song");
        verify(world.browsingSong);
        var songPage=findChild(world,"vinylSongPage");
        verify(songPage !== null);
        compare(songPage.pageData.trackIndex,0);
        compare(world.session.playingIndex,-1);
        compare(world.session.queue.length,0);
    }

    function test_unplayableBrowseSongCannotStartPlayback() {
        var unavailableAlbum=Object.assign({},rawAlbum,{tracks:rawAlbum.tracks.map(function(track) {
            return Object.assign({},track,{available:false});
        })});
        world.showBrowseSong(unavailableAlbum,0);
        var songPage=findChild(world,"vinylSongPage");
        verify(songPage !== null);
        compare(songPage.pageData.playingTrack.available,false);
        var play=findChild(world,"vinylSongPlay");
        verify(play !== null && play.activate(Qt.MouseFocusReason));
        compare(world.session.playingIndex,-1);
        compare(world.session.queue.length,0);
    }

    function test_personalizedArtistUsesItsDestinationAndBack() {
        recommendations.setHome([section([card("rec-artist","Recommended artist","artists",
            {kind:"artist",provider:"local",providerId:"native-artist",id:"native-artist",playable:false})])]);
        world.worldData.activate(world.worldData.homeRows[0].items[0]);
        compare(world.page,"artist");
        compare(world.artistData.artistId,"native-artist");
        verify(world.goBack());
        compare(world.page,"home");
        compare(world.worldView.currentTab,"home");
    }

    function test_emptyLanesAndMissingDatesDoNotSuppressHomeRows() {
        var populated = section([card("dated-not-required","Undated release","albums",
            {kind:"album",provider:"local",providerId:"release",id:"release",playable:false})]);
        var empty = section([], {id:"empty-lane",key:"empty-lane",title:"Empty lane",status:"empty"});
        delete populated.fetchedAt;
        delete populated.evaluatedAtMs;
        recommendations.setHome([populated,empty]);
        var rows = world.worldData.homeRows;
        verify(rows.some(function(row) { return row.key === "personalized:for-you"; }));
        verify(!rows.some(function(row) { return row.key === "personalized:empty-lane"; }));
        verify(rows.some(function(row) { return row.key === "saved-albums"; }));
    }

    function test_unqualifiedSongCandidateIsHiddenButAlbumAndArtistRemain() {
        var unavailableAlbum=Object.assign({},rawAlbum,{tracks:rawAlbum.tracks.map(function(track) {
            return Object.assign({},track,{available:false});
        })});
        var unqualifiedSong=card("candidate-song","Unqualified recording","songs",
            {kind:"song",provider:"local",providerId:"native-track-one",id:"native-track-one",playable:false},
            {record:unavailableAlbum,trackIndex:0});
        recommendations.setHome([section([
            unqualifiedSong,
            card("qualified-album","Suggested release","albums",
                {kind:"album",provider:"local",providerId:"release",id:"release",playable:false}),
            card("related-artist","Related artist","artists",
                {kind:"artist",provider:"local",providerId:"native-artist",id:"native-artist",playable:false})
        ])]);
        var row=world.worldData.homeRows.find(function(candidate) { return candidate.key==="personalized:for-you"; });
        verify(row !== undefined);
        compare(row.items.map(function(item) { return item.id; }),["qualified-album","related-artist"]);
    }

    function test_partialRecommendationErrorKeepsFactualShelves() {
        recommendations.setHome([section([card("stale-album","Cached release","albums",
            {kind:"album",provider:"local",providerId:"release",id:"release",playable:false})])],
            "partial","One recommendation lane failed.");
        verify(world.worldData.homeError.indexOf("failed") >= 0);
        verify(world.worldData.homeRows.some(function(row) { return row.key === "personalized:for-you"; }));
        verify(world.worldData.homeRows.some(function(row) { return row.key === "taste-artists"; }));
        verify(world.worldData.homeRows.some(function(row) { return row.key === "saved-albums"; }));
    }

    function test_loadingAndRetryUseTheExistingCompactHomeControls() {
        recommendations.setHome([],"loading","");
        var status=findChild(world,"vinylHomeRecommendationsStatus");
        var retry=findChild(world,"vinylHomeRecommendationsRetry");
        verify(status !== null && status.visible);
        verify(retry !== null && !retry.visible);

        recommendations.setHome([],"error","The request failed.");
        tryCompare(retry,"visible",true);
        verify(retry.activate(Qt.MouseFocusReason));
        compare(recommendations.homeRefreshes,1);
        compare(recommendations.lastRefreshForced,true);
    }

    function test_profileClearDropsRecommendationListSnapshot() {
        recommendations.setHome([section([card("rec-album","Suggested release","albums",
            {kind:"album",provider:"local",providerId:"release",id:"release",playable:false})])]);
        var key = "personalized:for-you";
        world.worldData.openList(key,true);
        compare(world.worldData.row(key,true).items[0].id,"rec-album");
        recommendations.setHome([],"empty","");
        world.session.clearProfile();
        compare(world.worldData.row(key,true).items.length,0);
    }
}
