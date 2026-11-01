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
            property int discoveryVersion: 1
            property var requestCalls: []
            signal completed(string lane, int requestId, var data, string error)
            function request(lane, operation, value) {
                requested=value;
                requestCalls=requestCalls.concat([{lane:lane,operation:operation,value:value}]);
                return 1;
            }
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
            property var homeSnapshot: ({exposureSnapshotId:"visit-0"})
            property string homeStatus: "empty"
            property string homeError: ""
            property int homeRevision: 0
            property int profileGeneration: 0
            property var feedbackCalls: []
            property var exposureCalls: []
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
            function setFeedback(ref, action, expectedGeneration) {
                feedbackCalls = feedbackCalls.concat([{ref:ref,action:action,generation:expectedGeneration}]);
                return true;
            }
            function recordVisibleItems(shelfId, snapshotId, refs, expectedGeneration) {
                exposureCalls = exposureCalls.concat([{shelfId:shelfId,snapshotId:snapshotId,
                    refs:refs,generation:expectedGeneration}]);
                return true;
            }
            function setHome(sections, status, error, suppressedItems) {
                homeSections = sections || [];
                homeStatus = status || "ready";
                homeError = error || "";
                homeRevision++;
                homeSnapshot = {exposureSnapshotId:"visit-" + homeRevision,
                    suppressedItems:suppressedItems || []};
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
        var entityKind = kind === "artists" ? "artist" : kind === "songs" ? "recording" : "release_group";
        return Object.assign({id:id,title:title,cover:"",kind:kind,destination:destination,
            key:"recommendation:"+id,ref:{identity:"provider",kind:entityKind,
                providerId:"youtube-music",itemId:id},artistRefs:[],reason:""}, fields || {});
    }

    function section(items, fields) {
        return Object.assign({id:"for-you",key:"for-you",title:"Made for you",
            kind:"recommendations",items:items,status:"ready",error:"",canRetry:false}, fields || {});
    }

    function revealCardVertically(cardItem) {
        var flick=world.worldView.pageFlick;
        var contentTop=cardItem.mapToItem(flick.contentItem,0,0).y;
        flick.contentY=Math.max(0,contentTop-80);
        wait(180);
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
        var play=findChild(world,"vinylBarPlay");
        verify(play !== null && !play.enabled);
        verify(!play.activate(Qt.MouseFocusReason));
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
        verify(status !== null && status.visible,
            "A populated factual Home may keep its compact global recommendation status.");
        verify(retry !== null && !retry.visible);

        recommendations.setHome([],"error","The request failed.");
        tryCompare(retry,"visible",true);
        var statusCopy=findChild(world,"vinylHomeRecommendationsStatusText");
        verify(statusCopy!==null);
        compare(statusCopy.text,"Some recommendations could not be loaded.");
        verify(retry.activate(Qt.MouseFocusReason));
        compare(recommendations.homeRefreshes,1);
        compare(recommendations.lastRefreshForced,true);
    }

    function test_homeShelfStatusesStayLocalAndPreserveCachedCards() {
        var cached = section([card("stale-release","Cached release","albums",
            {kind:"album",provider:"local",providerId:"release",id:"release",playable:false})],
            {id:"stale-lane",key:"stale-lane",title:"From your artists",status:"stale",
                reason:"Showing cached results after a provider timeout."});
        var loading = section([], {id:"loading-lane",key:"loading-lane",title:"Related discoveries",
            status:"loading",loading:true,reason:"Loading related discoveries…"});
        var failed = section([], {id:"failed-lane",key:"failed-lane",title:"New releases",
            status:"error",error:"This shelf could not be refreshed.",canRetry:true});
        var empty = section([], {id:"empty-lane",key:"empty-lane",title:"Empty lane",status:"empty"});
        recommendations.setHome([cached,loading,failed,empty],"partial","One recommendation lane failed.");

        var rows=world.worldData.homeRows;
        var cachedRow=rows.find(function(row){return row.key==="personalized:stale-lane";});
        var loadingRow=rows.find(function(row){return row.key==="personalized:loading-lane";});
        var failedRow=rows.find(function(row){return row.key==="personalized:failed-lane";});
        verify(cachedRow!==undefined,"A stale recommendation shelf must keep its cached cards.");
        compare(cachedRow.items[0].id,"stale-release");
        compare(cachedRow.status,"stale");
        verify(loadingRow!==undefined,"A known empty loading lane must remain visible.");
        compare(loadingRow.status,"loading");
        verify(failedRow!==undefined,"A known empty failed lane must remain visible for retry.");
        compare(failedRow.status,"error");
        verify(!rows.some(function(row){return row.key==="personalized:empty-lane";}),
            "An empty lane without a pending or failed state must stay hidden.");
        verify(rows.some(function(row){return row.key==="saved-albums"; }),
            "Factual shelves must remain available during a partial recommendation failure.");
        var factual=rows.find(function(row){return row.key==="saved-albums";});
        verify(factual.status===undefined,"Factual shelves must not inherit recommendation status.");

        var staleStatus=findChild(world,"vinylShelfStatus_personalized:stale-lane");
        var loadingStatus=findChild(world,"vinylShelfStatus_personalized:loading-lane");
        var failedStatus=findChild(world,"vinylShelfStatus_personalized:failed-lane");
        verify(staleStatus!==null && staleStatus.visible);
        verify(staleStatus.text.toLowerCase().indexOf("cached")>=0);
        verify(loadingStatus!==null && loadingStatus.visible);
        verify(loadingStatus.text.toLowerCase().indexOf("loading")>=0);
        verify(failedStatus!==null && failedStatus.visible);
        compare(failedStatus.text,"Could not load this shelf.");
        verify(!findChild(world,"vinylHomeSeeAll_personalized:loading-lane").visible,
            "A pending empty shelf must not offer an empty See-all destination");
        verify(!findChild(world,"vinylHomeSeeAll_personalized:failed-lane").visible);
        var globalStatus=findChild(world,"vinylHomeRecommendationsStatus");
        verify(globalStatus!==null && !globalStatus.visible,
            "A row-local failure must not be repeated as a page-wide status.");

        var retry=findChild(world,"vinylShelfRetry_personalized:failed-lane");
        verify(retry!==null && retry.visible);
        verify(!retry.focusOnPointer,"Shelf retry must not focus from pointer entry.");
        var previousRefreshes=recommendations.homeRefreshes;
        verify(retry.activate(Qt.MouseFocusReason));
        verify(!retry.activeFocus,"Pointer activation must leave keyboard focus unchanged.");
        compare(recommendations.homeRefreshes,previousRefreshes+1);
        compare(recommendations.lastRefreshForced,true);
    }

    function test_emptySkippedHomeOffersLoadingAndAvailableDestinations() {
        var source=recommendationAlbumSource.createObject(test);
        world.session.source=source;
        collection.seed([]);
        world.session.refreshLibrary();
        world.personalLibrary.refreshModels();
        recommendations.selectedArtists=[];
        world.worldData.recentContinue=[];
        recommendations.setHome([],"loading","");
        world.session.discoverySections={};
        world.session.discoveryRequests={"new-releases":1};

        var page=findChild(world,"vinylHomePage");
        var message=findChild(world,"vinylHomeEmptyMessage");
        var discover=findChild(world,"vinylHomeDiscover");
        var library=findChild(world,"vinylHomeLibrary");
        verify(page!==null && !page.hasHomeItems);
        verify(!world.worldData.homeRows.some(function(row){return row.recommendation===true;}),
            "Cold public discovery must not be mixed into personalized recommendation rows.");
        compare(world.worldData.homeStatus,"loading");
        verify(message!==null && message.visible);
        verify(message.text.indexOf("Loading")>=0);
        var loadingShelf=findChild(world,"vinylShelfStatus_home-discover-new-releases");
        verify(loadingShelf===null,
            "A no-cache Home should not render empty per-shelf placeholders.");
        var homeStatus=findChild(world,"vinylHomeRecommendationsStatus");
        verify(homeStatus!==null && !homeStatus.visible,
            "The central empty state owns loading copy when no shelf has cards.");
        verify(discover!==null && discover.visible);
        verify(library!==null && library.visible);
        var retry=findChild(world,"vinylHomeRetry");
        verify(retry!==null && !retry.visible);
        verify(discover.activate(Qt.MouseFocusReason));
        compare(world.worldView.currentTab,"discover");

        world.showHome();
        world.session.discoveryRequests={};
        world.session.discoverySections={"new-releases":{title:"New releases",items:[],
            error:"The catalogue could not be reached. Retry when it is available."}};
        message=findChild(world,"vinylHomeEmptyMessage");
        verify(message!==null && message.visible);
        compare(world.worldData.homeStatus,"error");
        compare(message.text,"Catalogue unavailable.");
        var failedShelf=findChild(world,"vinylShelfStatus_home-discover-new-releases");
        verify(failedShelf===null,
            "An empty failed public shelf should be represented by the central empty state.");
        discover=findChild(world,"vinylHomeDiscover");
        library=findChild(world,"vinylHomeLibrary");
        verify(discover!==null && discover.visible && library!==null && library.visible);
        retry=findChild(world,"vinylHomeRetry");
        verify(retry!==null && retry.visible,
            "An empty failed Home should offer one central retry action.");
        verify(!homeStatus.visible,"The central error state must not be repeated in a banner.");
        var visibleRetryCount=0;
        ["vinylHomeRetry","vinylHomeRecommendationsRetry",
            "vinylShelfRetry_home-discover-new-releases"].forEach(function(name) {
            var candidate=findChild(world,name);
            if(candidate!==null && candidate.visible)visibleRetryCount++;
        });
        compare(visibleRetryCount,1);
        var callsBeforeRetry=source.requestCalls.length;
        verify(retry.activate(Qt.MouseFocusReason));
        verify(source.requestCalls.slice(callsBeforeRetry).some(function(call){return call.operation==="discover"; }),
            "The central retry should refresh the failed public discovery catalogue.");
        verify(library.activate(Qt.MouseFocusReason));
        compare(world.worldView.currentTab,"library");
        compare(world.session.queue.length,0);
        world.session.source=NativeSource;
        source.destroy();
    }

    function test_homeCardBoundsAndPointerEntryRemainStable() {
        recommendations.setHome([section([
            card("bound-a","Release A","albums",{kind:"album",provider:"local",providerId:"a",id:"a"}),
            card("bound-b","Release B","albums",{kind:"album",provider:"local",providerId:"b",id:"b"})
        ])]);
        var page=findChild(world,"vinylHomePage");
        verify(page!==null);
        compare(page.cardColumns,5);
        verify(page.cardSize>=120 && page.cardSize<=page.width);
        var first=findChild(world,"vinylCard_personalized:for-you_0");
        verify(first!==null);
        compare(first.width,page.cardSize);
        verify(!first.focusOnPointer,"Home cards must not acquire a focus frame on pointer entry.");

        world.width=460;
        tryVerify(function(){return page.width>300 && page.width<world.width;});
        tryVerify(function(){return page.cardColumns>=1 && page.cardColumns<=2;});
        verify(page.cardSize>=120 && page.cardSize<=page.width);
        compare(first.width,page.cardSize);
    }

    function test_recommendationFeedbackAndRefreshPreservePlayback() {
        recommendations.setHome([section([card("playing-release","Suggested release","albums",
            {kind:"album",provider:"local",providerId:"release",id:"release",playable:false})])]);
        world.session.playAlbum(rawAlbum,0);
        tryVerify(function(){return world.session.playingIndex===0 && world.session.queue.length===2;});
        var playerBar=findChild(world,"vinylPlayerBar");
        verify(playerBar!==null && playerBar.visible);
        verify(playerBar.y-(world.worldScroll.y+world.worldScroll.height)>=50,
            "The scrollable Home viewport must leave room above the active playback bar.");
        var playingId=world.session.queue[world.session.playingIndex].trackId;
        var queueIds=world.session.queue.map(function(row){return row.trackId;});
        var generation=world.session.profileGeneration;
        verify(world.worldData.setRecommendationFeedback(
            world.worldData.homeRows.find(function(row){return row.recommendation;}).items[0].ref,
            "like",generation));
        recommendations.setHome([section([card("replacement-release","Replacement release","albums",
            {kind:"album",provider:"local",providerId:"other",id:"other",playable:false})])]);
        compare(world.session.queue.map(function(row){return row.trackId;}),queueIds);
        compare(world.session.queue[world.session.playingIndex].trackId,playingId);
        compare(world.session.playingIndex,0);
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

    function test_personalizedCardFeedbackUsesTypedRefAndDoesNotNavigateOrPlay() {
        var creditedArtist = {provider:"youtube-music",providerId:"UCabcdefghijklmnopqrstuv",name:"Credited Artist"};
        recommendations.setHome([section([card("feedback-target","Suggested release","albums",
            {kind:"album",provider:"youtube-music",providerId:"MPRE_feedback",id:"ytm:album:MPRE_feedback"},
            {artistRefs:[creditedArtist]})])]);
        var cardItem = findChild(world,"vinylCard_personalized:for-you_0");
        verify(cardItem !== null);
        revealCardVertically(cardItem);
        var actions = findChild(cardItem,"vinylRecommendationActions_feedback-target");
        verify(actions !== null && actions.accessibleName.indexOf("Suggested release") >= 0);
        verify(actions.contextEnabled);
        host.contentItem.forceActiveFocus(Qt.OtherFocusReason);
        verify(cardItem.openFeedbackMenu());
        var availableActions=cardItem.feedbackOptions.map(function(option){return option.action;});
        verify(["like","dislike","neutral","hide","unhide","snooze-7d","unsnooze",
            "exclude-artist","allow-artist"].every(function(action){return availableActions.indexOf(action)>=0;}));
        var like = cardItem.feedbackActionFor("like");
        verify(like !== null && like.accessibleName === "Like this recommendation");
        wait(50);
        verify(!like.activeFocus,"Pointer entry must not give the first feedback action a focus frame");
        verify(like.activate(Qt.MouseFocusReason));

        compare(recommendations.feedbackCalls.length,1);
        compare(recommendations.feedbackCalls[0].action,"like");
        compare(JSON.stringify(recommendations.feedbackCalls[0].ref),JSON.stringify(cardItem.item.ref));
        compare(recommendations.feedbackCalls[0].generation,world.session.profileGeneration);
        tryCompare(cardItem,"feedbackMenuOpen",false);
        verify(actions.activate(Qt.MouseFocusReason));
        tryCompare(cardItem,"feedbackMenuOpen",true);
        tryVerify(function(){return cardItem.feedbackActionFor("exclude-artist-0")!==null;});
        var fewerFromArtist=cardItem.feedbackActionFor("exclude-artist-0");
        verify(fewerFromArtist !== null && fewerFromArtist.activate(Qt.MouseFocusReason));
        compare(recommendations.feedbackCalls.length,2);
        compare(recommendations.feedbackCalls[1].action,"exclude-artist");
        compare(recommendations.feedbackCalls[1].ref.identity,"provider");
        compare(recommendations.feedbackCalls[1].ref.kind,"artist");
        compare(recommendations.feedbackCalls[1].ref.providerId,"youtube-music");
        compare(recommendations.feedbackCalls[1].ref.itemId,creditedArtist.providerId);
        compare(world.page,"home");
        compare(world.session.playingIndex,-1);
        compare(world.session.queue.length,0);
    }

    function test_tasteArtistCanRestoreRecommendationsWithoutAVisibleRecommendationCard() {
        recommendations.setHome([]);
        var artistCard=findChild(world,"vinylCard_taste-artists_0");
        verify(artistCard!==null);
        revealCardVertically(artistCard);
        compare(recommendations.homeSections.length,0);
        verify(artistCard.feedbackAvailable);
        compare(artistCard.item.ref.identity,"provider");
        compare(artistCard.item.ref.kind,"artist");
        compare(artistCard.item.ref.providerId,"local");
        compare(artistCard.item.ref.itemId,"native-artist");
        verify(artistCard.openFeedbackMenu());

        var exclude=artistCard.feedbackActionFor("exclude-current-artist");
        var allow=artistCard.feedbackActionFor("allow-current-artist");
        verify(exclude!==null && allow!==null);
        compare(allow.accessibleName,"Allow Native Artist again");
        verify(allow.activate(Qt.MouseFocusReason));

        compare(recommendations.feedbackCalls.length,1);
        compare(recommendations.feedbackCalls[0].action,"allow-artist");
        compare(JSON.stringify(recommendations.feedbackCalls[0].ref),JSON.stringify(artistCard.item.ref));
        compare(recommendations.feedbackCalls[0].generation,world.session.profileGeneration);
        compare(world.page,"home");
        compare(world.session.playingIndex,-1);
        compare(world.session.queue.length,0);
    }

    function test_tasteArtistRecoversSuppressedItemsAfterCardsLeaveHome() {
        var matchingArtist={provider:"local",providerId:"native-artist",localId:"native-artist",name:"Native Artist"};
        var hiddenRef={identity:"provider",kind:"release_group",providerId:"youtube-music",itemId:"hidden-release"};
        var snoozedRef={identity:"provider",kind:"release_group",providerId:"youtube-music",itemId:"snoozed-release"};
        var excludedArtistRef={identity:"provider",kind:"artist",providerId:"youtube-music",
            itemId:"UCcollaboratorabcdefghijkl"};
        recommendations.setHome([section([card("departed-release","Departed release","albums",
            {kind:"album",provider:"youtube-music",providerId:"MPRE_departed",id:"ytm:album:MPRE_departed"})])]);
        var recommendationCard=findChild(world,"vinylCard_personalized:for-you_0");
        verify(recommendationCard!==null);

        recommendations.setHome([],"ready","",[
            {ref:hiddenRef,title:"Hidden release",artistRefs:[matchingArtist],seedArtistRef:null,
                hidden:true,snoozed:false,excluded:false},
            {ref:snoozedRef,title:"Snoozed release",artistRefs:[],seedArtistRef:matchingArtist,
                hidden:false,snoozed:true,excluded:false},
            {ref:{identity:"provider",kind:"release_group",providerId:"youtube-music",itemId:"excluded-release"},
                title:"Release with excluded collaborator",artistRefs:[matchingArtist,
                    {provider:"youtube-music",providerId:excludedArtistRef.itemId,name:"Collaborator B"}],
                seedArtistRef:matchingArtist,excludedArtistRefs:[{ref:excludedArtistRef,name:"Collaborator B"}],
                hidden:false,snoozed:false,excluded:true},
            {ref:{identity:"provider",kind:"artist",providerId:"local",itemId:"native-artist"},
                title:"Excluded Native Artist",artistRefs:[],seedArtistRef:null,excluded:true,
                hidden:false,snoozed:false},
            {ref:{identity:"provider",kind:"release_group",providerId:"youtube-music",itemId:"other"},
                title:"Unrelated release",artistRefs:[{provider:"local",providerId:"other-artist"}],
                seedArtistRef:null,hidden:true,snoozed:false,excluded:false}
        ]);
        compare(findChild(world,"vinylCard_personalized:for-you_0"),null);
        var artistCard=findChild(world,"vinylCard_taste-artists_0");
        verify(artistCard!==null);
        revealCardVertically(artistCard);

        var suppressed=world.worldData.suppressedRecommendationsForArtist(artistCard.item.ref);
        compare(suppressed.length,4);
        verify(suppressed.some(function(entry){return entry.title==="Hidden release";}));
        verify(suppressed.some(function(entry){return entry.title==="Snoozed release";}));
        verify(suppressed.some(function(entry){return entry.title==="Release with excluded collaborator";}));
        verify(suppressed.some(function(entry){return entry.title==="Excluded Native Artist";}));
        verify(artistCard.openFeedbackMenu());
        var unhide=artistCard.feedbackOptions.find(function(option){return option.action==="unhide";});
        var unsnooze=artistCard.feedbackOptions.find(function(option){return option.action==="unsnooze";});
        var allowExcluded=artistCard.feedbackOptions.find(function(option){
            return option.action==="allow-artist" && option.label.indexOf("Collaborator B")>=0
                && option.label.indexOf("Release with excluded collaborator")>=0;
        });
        verify(unhide!==undefined && unhide.label.indexOf("Hidden release")>=0);
        verify(unsnooze!==undefined && unsnooze.label.indexOf("Snoozed release")>=0);
        verify(allowExcluded!==undefined);
        var unhideAction=artistCard.feedbackActionFor(unhide.key);
        verify(unhideAction!==null && unhideAction.activate(Qt.MouseFocusReason));
        compare(recommendations.feedbackCalls[0].action,"unhide");
        compare(recommendations.feedbackCalls[0].ref.identity,hiddenRef.identity);
        compare(recommendations.feedbackCalls[0].ref.kind,hiddenRef.kind);
        compare(recommendations.feedbackCalls[0].ref.providerId,hiddenRef.providerId);
        compare(recommendations.feedbackCalls[0].ref.itemId,hiddenRef.itemId);

        verify(artistCard.openFeedbackMenu());
        var unsnoozeAction=artistCard.feedbackActionFor(unsnooze.key);
        verify(unsnoozeAction!==null && unsnoozeAction.activate(Qt.MouseFocusReason));
        compare(recommendations.feedbackCalls[1].action,"unsnooze");
        compare(recommendations.feedbackCalls[1].ref.identity,snoozedRef.identity);
        compare(recommendations.feedbackCalls[1].ref.kind,snoozedRef.kind);
        compare(recommendations.feedbackCalls[1].ref.providerId,snoozedRef.providerId);
        compare(recommendations.feedbackCalls[1].ref.itemId,snoozedRef.itemId);

        verify(artistCard.openFeedbackMenu());
        var allowAction=artistCard.feedbackActionFor(allowExcluded.key);
        verify(allowAction!==null && allowAction.activate(Qt.MouseFocusReason));
        compare(recommendations.feedbackCalls[2].action,"allow-artist");
        compare(recommendations.feedbackCalls[2].ref.identity,excludedArtistRef.identity);
        compare(recommendations.feedbackCalls[2].ref.kind,excludedArtistRef.kind);
        compare(recommendations.feedbackCalls[2].ref.providerId,excludedArtistRef.providerId);
        compare(recommendations.feedbackCalls[2].ref.itemId,excludedArtistRef.itemId);
        verify(recommendations.feedbackCalls.every(function(call){
            return call.generation===world.session.profileGeneration;
        }));
        compare(world.page,"home");
        compare(world.session.playingIndex,-1);
        compare(world.session.queue.length,0);
    }

    function test_savedArtistRecoversHiddenRecommendationWithoutTasteSelection() {
        recommendations.selectedArtists=[];
        var hiddenRef={identity:"provider",kind:"release_group",providerId:"youtube-music",
            itemId:"saved-artist-hidden-release"};
        recommendations.setHome([section([card("saved-artist-departed","Departed recommendation","albums",
            {kind:"album",provider:"youtube-music",providerId:"MPRE_saved_artist_departed",
                id:"ytm:album:MPRE_saved_artist_departed"})])]);
        verify(world.worldData.homeRows.some(function(row){return row.key==="saved-artists";}));
        verify(!world.worldData.homeRows.some(function(row){return row.key==="taste-artists";}));

        recommendations.setHome([],"ready","",[
            {ref:hiddenRef,title:"Hidden local artist release",
                artistRefs:[{provider:"local",providerId:"native-artist",name:"Native Artist"}],
                seedArtistRef:null,hidden:true,snoozed:false,excluded:false}
        ]);
        compare(findChild(world,"vinylCard_personalized:for-you_0"),null);
        var savedArtistCard=findChild(world,"vinylCard_saved-artists_0");
        verify(savedArtistCard!==null);
        revealCardVertically(savedArtistCard);
        verify(savedArtistCard.feedbackAvailable);
        compare(savedArtistCard.item.ref.identity,"provider");
        compare(savedArtistCard.item.ref.kind,"artist");
        compare(savedArtistCard.item.ref.providerId,"local");
        compare(savedArtistCard.item.ref.itemId,"native-artist");
        verify(savedArtistCard.openFeedbackMenu());
        var restoreOption=savedArtistCard.feedbackOptions.find(function(option){
            return option.action==="unhide" && option.label.indexOf("Hidden local artist release")>=0;
        });
        verify(restoreOption!==undefined);
        var restoreAction=savedArtistCard.feedbackActionFor(restoreOption.key);
        verify(restoreAction!==null && restoreAction.activate(Qt.MouseFocusReason));
        compare(recommendations.feedbackCalls.length,1);
        compare(recommendations.feedbackCalls[0].action,"unhide");
        compare(recommendations.feedbackCalls[0].ref.identity,"provider");
        compare(recommendations.feedbackCalls[0].ref.kind,hiddenRef.kind);
        compare(recommendations.feedbackCalls[0].ref.providerId,hiddenRef.providerId);
        compare(recommendations.feedbackCalls[0].ref.itemId,hiddenRef.itemId);
        compare(recommendations.feedbackCalls[0].generation,world.session.profileGeneration);
        var providerRef=world.worldData.recommendationArtistRef({id:"ytm:artist:UCsavedArtist",name:"Provider Artist"});
        compare(providerRef.providerId,"youtube-music");
        compare(providerRef.itemId,"UCsavedArtist");
        compare(world.worldData.recommendationArtistRef({name:"Name without identity"}),null);
        compare(world.page,"home");
        compare(world.session.playingIndex,-1);
        compare(world.session.queue.length,0);
    }

    function test_staleFeedbackGenerationIsRejectedBeforeServiceCall() {
        recommendations.setHome([section([card("stale-feedback","Suggested release","albums",
            {kind:"album",provider:"youtube-music",providerId:"MPRE_stale",id:"ytm:album:MPRE_stale"})])]);
        var cardItem = findChild(world,"vinylCard_personalized:for-you_0");
        verify(cardItem !== null);
        revealCardVertically(cardItem);
        verify(cardItem !== null && cardItem.openFeedbackMenu());
        var capturedGeneration = cardItem.feedbackGeneration;
        var exposureCount = recommendations.exposureCalls.length;
        world.session.profileGeneration++;

        verify(!world.worldData.setRecommendationFeedback(cardItem.item.ref,"dislike",capturedGeneration));
        verify(!world.worldData.recordVisibleRecommendation(cardItem.recommendationRow,
            cardItem.item,capturedGeneration,true));
        compare(recommendations.feedbackCalls.length,0);
        compare(recommendations.exposureCalls.length,exposureCount);
    }

    function test_visibleExposureSendsOnlyVisibleCardsWithSnapshotAndGeneration() {
        var cards = [];
        for (var i=0;i<12;i++) {
            cards.push(card("visible-"+i,"Suggested "+i,"albums",
                {kind:"album",provider:"youtube-music",providerId:"MPRE_visible_"+i,
                    id:"ytm:album:MPRE_visible_"+i}));
        }
        var sections=[section(cards)];
        for (var row=1;row<6;row++) {
            var itemId=row===5 ? "below-fold" : "below-row-"+row;
            sections.push(section([card(itemId,"Recommendation "+row,"albums",
                {kind:"album",provider:"youtube-music",providerId:"MPRE_below_"+row,
                    id:"ytm:album:MPRE_below_"+row})],
                {id:row===5 ? "below-fold-section" : "below-row-section-"+row,
                    key:row===5 ? "below-fold-section" : "below-row-section-"+row,
                    title:row===5 ? "Below fold" : "Recommendation row "+row}));
        }
        recommendations.setHome(sections);
        var firstCard=findChild(world,"vinylCard_personalized:for-you_0");
        verify(firstCard !== null);
        revealCardVertically(firstCard);
        tryVerify(function(){return recommendations.exposureCalls.length>0;});

        verify(recommendations.exposureCalls.every(function(call) {
            return call.shelfId.length>0 && call.snapshotId === recommendations.homeSnapshot.exposureSnapshotId
                && call.refs.length === 1 && call.generation === world.session.profileGeneration;
        }));
        verify(!recommendations.exposureCalls.some(function(call) {
            return call.refs[0].itemId === "below-fold";
        }));
        verify(recommendations.exposureCalls.length < cards.length);
    }

    function test_hiddenHomeDoesNotRecordCardExposure() {
        recommendations.setHome([section([card("hidden-card","Suggested release","albums",
            {kind:"album",provider:"youtube-music",providerId:"MPRE_hidden",id:"ytm:album:MPRE_hidden"})])]);
        var firstCard=findChild(world,"vinylCard_personalized:for-you_0");
        verify(firstCard !== null);
        revealCardVertically(firstCard);
        tryVerify(function(){return recommendations.exposureCalls.length>0;});
        recommendations.exposureCalls = [];
        world.showBrowseSong(rawAlbum,0);
        wait(180);
        compare(recommendations.exposureCalls.length,0);
    }
}
